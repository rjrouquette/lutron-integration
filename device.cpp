//
// Created by robert on 5/12/18.
//

#include <cstring>
#include <cmath>
#include "device.h"
#include "room.h"
#include "lutron_connector.h"
#include "logging.h"

static const char *str_dtype[7] = {
        "invalid",
        "smart_bridge",
        "wall_dimmer",
        "plugin_dimmer",
        "pico_remote",
        "wall_switch",
        "plugin_switch"
};

device::device(int i, const char *n, const char *d, device_type t, room *l) :
id(i), name(n), description(d), type(t), location(l)
{
    conn = nullptr;
    if(location) {
        location->devices.insert(this);
    }
}

device::~device() {
    if(location) {
        location->devices.erase(this);
    }
}

device* device::parse(json_object *object, std::map<std::string, room *> &rooms) {
    json_object *jtmp;
    int id;
    const char *name, *desc;
    device_type type;
    room *loc;

    if(json_object_object_get_ex(object, "id", &jtmp)) {
        id = json_object_get_int(jtmp);
    }
    else {
        log_error("device entry is missing `id`");
        return nullptr;
    }

    if(json_object_object_get_ex(object, "name", &jtmp)) {
        name = json_object_get_string(jtmp);
    }
    else {
        log_error("device entry is missing `name`");
        return nullptr;
    }

    if(json_object_object_get_ex(object, "description", &jtmp)) {
        desc = json_object_get_string(jtmp);
    }
    else {
        log_error("device entry is missing `description`");
        return nullptr;
    }

    if(json_object_object_get_ex(object, "type", &jtmp)) {
        const char *temp = json_object_get_string(jtmp);
        type = invalid;
        for(int i = 0; i < sizeof(str_dtype)/sizeof(const char *); i++) {
            if(strcmp(temp, str_dtype[i]) == 0) {
                type = (device_type)i;
                break;
            }
        }
        if(type == invalid) {
            log_error("device entry `type` is invalid: %s", temp);
            return nullptr;
        }
    }
    else {
        log_error("device entry is missing `type`");
        return nullptr;
    }

    if(json_object_object_get_ex(object, "room", &jtmp)) {
        if(jtmp == nullptr) {
            loc = nullptr;
        }
        else {
            const char *temp = json_object_get_string(jtmp);
            auto it = rooms.find(temp);
            if (it == rooms.end()) {
                log_error("device entry `room` is invalid: %s", temp);
                return nullptr;
            }
            loc = it->second;
        }
    }
    else {
        log_error("device entry is missing `room`");
        return nullptr;
    }

    // TODO implement device sub classes
    if(type == wall_dimmer || type == plugin_dimmer)
        return new device_dimmer(id, name, desc, type, loc);
    else if(type == wall_switch || type == plugin_switch)
        return new device_switch(id, name, desc, type, loc);
    else if(type == pico_remote)
        return new device_remote(id, name, desc, type, loc);
    else
        return new device(id, name, desc, type, loc);
}

void device::requestRefresh() const {
    // do nothing by default
}

void device::processMessage(const char *command, const char **fields, int fcnt) {
    // do nothing by default
}



device_dimmer::device_dimmer(int id, const char *name, const char *desc, device_type type, room *loc) :
device(id, name, desc, type, loc)
{
    level = 0;
}

void device_dimmer::requestRefresh() const {
    char temp[32];
    sprintf(temp, "?OUTPUT,%d,1", id);
    conn->sendCommand(temp);
}

void device_dimmer::processMessage(const char *command, const char **fields, int fcnt) {
    if(strcmp(command, "OUTPUT") == 0) {
        if(strcmp(fields[0], "1") == 0) {
            sscanf(fields[1], "%f", &level);
            log_notice("update `%s` set `level` = %0.02f", name.c_str(), level);
        }
    }
}

float device_dimmer::getLevel() const {
    return level;
}

void device_dimmer::setLevel(float l) {
    if(std::isnan(l)) l = 0;
    if(l < 0) l = 0;
    if(l > 100.0) l = 100.0;

    level = l;
    log_notice("update `%s` set `level` = %0.02f", name.c_str(), level);

    // TODO send command
}




device_switch::device_switch(int id, const char *name, const char *desc, device_type type, room *loc) :
device(id, name, desc, type, loc)
{
    state = false;
}

void device_switch::requestRefresh() const {
    char temp[32];
    sprintf(temp, "?OUTPUT,%d,1", id);
    conn->sendCommand(temp);
}

void device_switch::processMessage(const char *command, const char **fields, int fcnt) {
    if(strcmp(command, "OUTPUT") == 0) {
        if(strcmp(fields[0], "1") == 0) {
            float temp;
            sscanf(fields[1], "%f", &temp);
            state = temp >= 50;
            log_notice("update `%s` set `state` = %s", name.c_str(), state?"on":"off");
        }
    }
}

bool device_switch::getState() const {
    return state;
}

void device_switch::setState(bool s) {
    state = s;
    log_notice("update `%s` set `state` = %s", name.c_str(), state?"on":"off");

    // TODO send command
}




static const char *str_button[] = {
        "invalid",
        "unknown",
        "on",
        "favorite",
        "off",
        "up",
        "down"
};

device_remote::device_remote(int id, const char *name, const char *desc, device_type type, room *loc) :
device(id, name, desc, type, loc)
{

}

void device_remote::addListener(listener *l) {
    listeners.insert(l);
}

void device_remote::removeListener(listener *l) {
    listeners.erase(l);
}

void device_remote::requestRefresh() const {
    // do nothing
}

void device_remote::processMessage(const char *command, const char **fields, int fcnt) {
    if(strcmp(command, "DEVICE") == 0) {
        auto button = (button_t) atoi(fields[0]);
        int event = atoi(fields[1]);

        if(event == 3) {
            log_notice("event `%s` button `%s` pressed", name.c_str(), str_button[button]);
            for(auto l : listeners) {
                l->buttonEvent(this, button, true);
            }
        }
        else if(event == 4) {
            log_notice("event `%s` button `%s` released", name.c_str(), str_button[button]);
            for(auto l : listeners) {
                l->buttonEvent(this, button, false);
            }
        }
    }
}
