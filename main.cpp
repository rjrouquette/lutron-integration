#include <libssh/libssh.h>
#include <libssh/server.h>
#include <json-c/json.h>
#include <pthread.h>
#include <cstdlib>
#include <iostream>
#include <sysexits.h>
#include "lutron_connector.h"

LutronConnector *lutronBridge;

bool loadConfiguration(json_object *config);
bool loadConfigurationBridge(json_object *config);

void lutronMessage(const char *msg) {
    std::cout << "RX: " << msg << std::endl;
}

int main(int argc, char **argv) {
    if(argc != 2) {
        std::cerr << "lutron-integration <config_json_path>" << std::endl;
        return EX_USAGE;
    }

    // open config file
    json_object *config = json_object_from_file(argv[1]);
    if(json_object_get_type(config) != json_type_object) {
        std::cerr << "failed to load configuration file" << std::endl;
        return EX_CONFIG;
    }

    // load configuration
    if(!loadConfiguration(config)) return false;

    std::cout << "finished loading configuration" << std::endl;
    std::cout << "smartBridge.hostname = " << lutronBridge->getHostName() << std::endl;
    std::cout << "smartBridge.port     = " << lutronBridge->getPort() << std::endl;
    std::cout << "smartBridge.username = " << lutronBridge->getUserName() << std::endl;
    std::cout << "smartBridge.password = " << lutronBridge->getPassword() << std::endl;

    lutronBridge->setCallback(lutronMessage);
    lutronBridge->connect();
    lutronBridge->sendCommand("?OUTPUT,3");
    lutronBridge->sendCommand("?OUTPUT,3,1");
    lutronBridge->sendCommand("#OUTPUT,3,1,50,00:00");
    lutronBridge->sendCommand("#OUTPUT,3,1,01,00:00");
    lutronBridge->sendCommand("?OUTPUT,3,1");

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
        std::cerr << "configuration file is missing `smartBridge` section" << std::endl;
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
        std::cerr << "configuration `smartBridge` section is missing `host` attribute" << std::endl;
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
