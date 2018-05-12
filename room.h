//
// Created by robert on 5/12/18.
//

#ifndef LUTRON_INTEGRATION_ROOM_H
#define LUTRON_INTEGRATION_ROOM_H

#include "device.h"

#include <string>
#include <set>

class room {
public:
    const std::string name;
    const std::string description;
    std::set<device *> devices;

    room(const char *name, const char *description);
    ~room() = default;
};


#endif //LUTRON_INTEGRATION_ROOM_H
