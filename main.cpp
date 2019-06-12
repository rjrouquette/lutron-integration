#include <json-c/json.h>
#include <pthread.h>
#include <cstdlib>
#include <unistd.h>
#include <sysexits.h>
#include <map>
#include <cstring>
#include <netdb.h>
#include <csignal>
#include "lutron_connector.h"
#include "room.h"
#include "logging.h"

#define UNUSED __attribute__((unused))

std::map<int, device *> devices;
std::map<std::string, device *> deviceNames;
std::map<std::string, room *> rooms;

LutronConnector *lutronBridge;

bool isRunning = true;
int socketUdp = -1;
pthread_t threadRx;

bool loadConfiguration(json_object *config);
bool loadConfigurationBridge(json_object *config);
bool loadConfigurationRooms(json_object *jRooms);
bool loadConfigurationDevices(json_object *jDevices);
bool loadConfigurationService(json_object *jService);

static void *doUdpRx(void *obj);
static json_object* processRequest(json_object *request);

static json_object* doStatus(json_object *request);

void lutronMessage(const char *msg) {
    char temp[256];
    strncpy(temp, msg, 255);
    temp[255] = 0;

    char *fields[16];
    if(msg[0] != '~') {
        log_error("ignoring non-system message: %s", msg);
        return;
    }

    int f, i = 1;
    fields[0] = temp + i;
    for(f = 0; f < 16 && temp[i] != 0; i++) {
        if(temp[i] == ',') {
            temp[i] = 0;
            fields[++f] = temp+i+1;
        }
    }

    if(f < 3) {
        log_error("corrupt system message: %s", msg);
        return;
    }

    int devId = (int)strtol(fields[1], nullptr, 10);
    auto dev = devices.find(devId);
    if(dev == devices.end()) {
        log_error("received system message for unknown device: %s", msg);
        return;
    }

    dev->second->processMessage(fields[0], ((const char**)fields)+2, f-2);
}

void sig_ignore(UNUSED int sig) {}

int main(int argc, char **argv) {
    struct sigaction act = {};
    memset(&act, 0, sizeof(act));
    act.sa_handler = sig_ignore;
    sigaction(SIGINT, &act, nullptr);
    sigaction(SIGTERM, &act, nullptr);

    if(argc != 2) {
        log_notice("Usage: lutron-integration <config_json_path>");
        return EX_USAGE;
    }

    // open config file
    json_object *config = json_object_from_file(argv[1]);
    if(json_object_get_type(config) != json_type_object) {
        log_error("failed to load configuration file: %s", argv[1]);
        return EX_CONFIG;
    }

    // load configuration
    if(!loadConfiguration(config)) {
        log_error("failed to load configuration file: %s", argv[1]);
        return EX_CONFIG;
    }

    log_notice("finished loading configuration");
    log_notice("smartBridge.hostname = %s", lutronBridge->getHostName());
    log_notice("smartBridge.port = %d", lutronBridge->getPort());
    log_debug("smartBridge.username = %s", lutronBridge->getUserName());
    log_debug("smartBridge.password = %s", lutronBridge->getPassword());
    log_notice("registered %ld rooms", rooms.size());
    log_notice("registered %ld devices", devices.size());

    lutronBridge->setCallback(lutronMessage);
    lutronBridge->connect();
    log_notice("smart bridge connection ready");

    log_notice("start udp rx thread");
    pthread_create(&threadRx, nullptr, doUdpRx, nullptr);

    // update all device states
    log_notice("requesting current device states");
    for(auto &dev : devices) {
        dev.second->requestRefresh();
    }

    //lutronBridge->sendCommand("?OUTPUT,3");
    //lutronBridge->sendCommand("?OUTPUT,3,1");
    //lutronBridge->sendCommand("#OUTPUT,3,1,50,00:00");
    //lutronBridge->sendCommand("#OUTPUT,3,1,01,00:00");
    //lutronBridge->sendCommand("?OUTPUT,3,1");

    pause();
    isRunning = false;
    log_notice("shutting down");

    log_notice("disconnect from smart bridge");
    lutronBridge->disconnect();
    delete lutronBridge;

    pthread_kill(threadRx, SIGINT);
    pthread_join(threadRx, nullptr);
    log_notice("udp rx thread stopped");

    close(socketUdp);
    return 0;
}

bool loadConfiguration(json_object *config) {
    json_object *jtmp;

    if(json_object_object_get_ex(config, "smartBridge", &jtmp)) {
        if(!loadConfigurationBridge(jtmp)) {
            return false;
        }
    }
    else {
        log_error("configuration file is missing `smartBridge` section");
        return false;
    }

    if(json_object_object_get_ex(config, "rooms", &jtmp)) {
        if(!loadConfigurationRooms(jtmp)) {
            return false;
        }
    }
    else {
        log_error("configuration file is missing `rooms` section");
        return false;
    }

    if(json_object_object_get_ex(config, "devices", &jtmp)) {
        if(!loadConfigurationDevices(jtmp)) {
            return false;
        }
    }
    else {
        log_error("configuration file is missing `devices` section");
        return false;
    }

    if(json_object_object_get_ex(config, "service", &jtmp)) {
        if(!loadConfigurationService(jtmp)) {
            return false;
        }
    }
    else {
        log_error("configuration file is missing `service` section");
        return false;
    }

    return true;
}

bool loadConfigurationBridge(json_object *config) {
    json_object *jtmp;
    int port = 23;
    const char *host = nullptr, *user = "lutron", *pass = "integration";

    if(json_object_object_get_ex(config, "host", &jtmp)) {
        host = json_object_get_string(jtmp);
    }
    else {
        log_error("configuration `smartBridge` section is missing `host` attribute");
        return false;
    }

    if(json_object_object_get_ex(config, "port", &jtmp)) {
        port = json_object_get_int(jtmp);
    }

    if(json_object_object_get_ex(config, "user", &jtmp)) {
        user = json_object_get_string(jtmp);
    }

    if(json_object_object_get_ex(config, "password", &jtmp)) {
        pass = json_object_get_string(jtmp);
    }

    lutronBridge = new LutronConnector(host, port, user, pass);
    return true;
}

bool loadConfigurationRooms(json_object *jRooms) {
    json_object *jroom, *jtmp;
    const char *name, *desc;

    int len = json_object_array_length(jRooms);
    for(int i = 0; i < len; i++) {
        jroom = json_object_array_get_idx(jRooms, i);

        if(json_object_object_get_ex(jroom, "name", &jtmp)) {
            name = json_object_get_string(jtmp);
        }
        else {
            log_error("room entry is missing `name`");
            return false;
        }

        if(json_object_object_get_ex(jroom, "description", &jtmp)) {
            desc = json_object_get_string(jtmp);
        }
        else {
            log_error("room entry is missing `description`");
            return false;
        }

        auto it = rooms.find(name);
        if(it == rooms.end()) {
            rooms[name] = new room(name, desc);
        }
        else {
            log_error("room entry is already defined: %s", name);
            return false;
        }
    }

    return true;
}

bool loadConfigurationDevices(json_object *jDevices) {
    json_object *jdev;
    device *dev;

    int len = json_object_array_length(jDevices);
    for(int i = 0; i < len; i++) {
        jdev = json_object_array_get_idx(jDevices, i);
        dev = device::parse(jdev, rooms);
        if(dev == nullptr) {
            return false;
        }
        if(devices.find(dev->id) != devices.end()) {
            log_error("device entry is already defined: %d", dev->id);
            delete dev;
            return false;
        }
        if(deviceNames.find(dev->name) != deviceNames.end()) {
            log_error("device entry is already defined: %s", dev->name.c_str());
            delete dev;
            return false;
        }
        devices[dev->id] = dev;
        deviceNames[dev->name] = dev;
        dev->conn = lutronBridge;
    }

    return true;
}

bool loadConfigurationService(json_object *jService) {
    json_object *jtmp;
    std::string bindAddress;
    int bindPort;

    if(json_object_object_get_ex(jService, "address", &jtmp)) {
        bindAddress = json_object_get_string(jtmp);
    }
    else {
        log_error("`service` section is missing `address`");
        return false;
    }

    if(json_object_object_get_ex(jService, "port", &jtmp)) {
        bindPort = json_object_get_int(jtmp);
    }
    else {
        log_error("`service` section is missing `port`");
        return false;
    }

    struct hostent *server = gethostbyname(bindAddress.c_str());
    if(!server) {
        log_error("could not resolve bind address: %s", bindAddress.c_str());
        return false;
    }

    sockaddr_in sockAddr = {};
    bzero(&sockAddr, sizeof(sockAddr));
    sockAddr.sin_family = AF_INET;
    memcpy(&sockAddr.sin_addr.s_addr, server->h_addr, (size_t)server->h_length);
    sockAddr.sin_port = htons(bindPort);

    socketUdp = socket(AF_INET, SOCK_DGRAM, 0);
    if(socketUdp < 0) {
        log_error("failed to create socket");
        return false;
    }

    if (bind(socketUdp, (struct sockaddr *) &sockAddr, sizeof(sockAddr)) < 0) {
        log_error("ERROR on socket binding! %s (%d)", strerror(errno), errno);
        close(socketUdp);
        return EX_CANTCREAT;
    }

    log_notice("listening on %s:%d", bindAddress.c_str(), bindPort);
    return true;
}

static void *doUdpRx(UNUSED void *obj) {
    auto tokener = json_tokener_new_ex(8);
    sockaddr_in remoteAddr = {};
    socklen_t addrLen;
    char buffer[65536];

    // strict parsing only
    json_tokener_set_flags(tokener, JSON_TOKENER_STRICT);

    while(isRunning && socketUdp >= 0) {
        bzero(&remoteAddr, sizeof(remoteAddr));
        addrLen = sizeof(remoteAddr);
        ssize_t r = recvfrom(socketUdp, buffer, sizeof(buffer), 0, (struct sockaddr *) &remoteAddr, &addrLen);
        if(r > 0) {
            auto request = json_tokener_parse_ex(tokener, buffer, (int)r);
            auto response = processRequest(request);
            json_object_put(request);

            auto responseStr = json_object_to_json_string_ext(response, JSON_C_TO_STRING_PLAIN);
            sendto(socketUdp, responseStr, strlen(responseStr), 0, (struct sockaddr *) &remoteAddr, sizeof(remoteAddr));
            json_object_put(request);
        }
    }

    json_tokener_free(tokener);
    return nullptr;
}

static json_object* processRequest(json_object *request) {
    json_object *jAction;
    if(!json_object_object_get_ex(request, "action", &jAction)) return nullptr;
    auto action = json_object_get_string(jAction);

    if(strcmp(action, "status") == 0){
        return doStatus(request);
    }

    auto response = json_object_new_object();
    json_object_object_add(response, "error", json_object_new_string("invalid action"));
    return response;
}

static json_object* doStatus(json_object *request) {
    json_object *jList, *jtmp;
    std::set<device *> statDevices;

    // check for list of rooms
    if(json_object_object_get_ex(request, "rooms", &jList)) {

    }

    // check for list of devices
    if(json_object_object_get_ex(request, "devices", &jList)) {
        int len = json_object_array_length(jList);
        for(int i = 0; i < len; i++) {
            device *target = nullptr;
            jtmp = json_object_array_get_idx(jList, i);
            if(json_object_get_type(jtmp) == json_type_int) {
                auto d = devices.find(json_object_get_int(jtmp));
                if(d != devices.end()) {
                    target = d->second;
                }
            }
            else {
                auto deviceName = json_object_get_string(jtmp);
                for (auto d : devices) {
                    if (d.second->name == deviceName) {
                        target = d.second;
                        break;
                    }
                }
            }

            if(target != nullptr) {
                statDevices.insert(target);
            }
        }
    }

    auto jDevices = json_object_new_array();
    for(auto d : statDevices) {
        auto jDevice = json_object_new_object();
        json_object_object_add(jDevice, "id", json_object_new_int(d->id));
        if(d->type == device::plugin_switch || d->type == device::wall_switch) {
            auto sw = (device_switch *)d;
            json_object_object_add(jDevice, "state", json_object_new_string(sw->getState() ? "on" : "off"));
        }
        else if(d->type == device::plugin_dimmer || d->type == device::wall_dimmer) {
            auto sw = (device_dimmer *)d;
            json_object_object_add(jDevice, "level", json_object_new_double(sw->getLevel()));
        }
    }

    auto response = json_object_new_object();
    json_object_object_add(response, "type", json_object_new_string("status"));
    json_object_object_add(response, "devices", jDevices);
    return response;
}
