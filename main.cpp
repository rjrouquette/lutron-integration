#include <json-c/json.h>
#include <pthread.h>
#include <cstdlib>
#include <unistd.h>
#include <sysexits.h>
#include <map>
#include <cstring>
#include "lutron_connector.h"
#include "room.h"
#include "logging.h"

std::map<int, device *> devices;
std::map<std::string, device *> deviceNames;
std::map<std::string, room *> rooms;

LutronConnector *lutronBridge;

bool loadConfiguration(json_object *config);
bool loadConfigurationBridge(json_object *config);
bool loadConfigurationRooms(json_object *jrooms);
bool loadConfigurationDevices(json_object *jdevices);

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

    int devId = atoi(fields[1]);
    auto dev = devices.find(devId);
    if(dev == devices.end()) {
        log_error("received system message for unknown device: %s", msg);
        return;
    }

    dev->second->processMessage(fields[0], ((const char**)fields)+2, f-2);
}

int main(int argc, char **argv) {
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

    lutronBridge->disconnect();
    delete lutronBridge;
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

bool loadConfigurationRooms(json_object *jrooms) {
    json_object *jroom, *jtmp;
    const char *name, *desc;

    int len = json_object_array_length(jrooms);
    for(int i = 0; i < len; i++) {
        jroom = json_object_array_get_idx(jrooms, i);

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

bool loadConfigurationDevices(json_object *jdevices) {
    json_object *jdev;
    device *dev;

    int len = json_object_array_length(jdevices);
    for(int i = 0; i < len; i++) {
        jdev = json_object_array_get_idx(jdevices, i);
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
