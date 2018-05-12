//
// Created by robert on 5/12/18.
//

#include "room.h"

room::room(const char *n, const char *d) :
name(n), description(d)
{
    devices.clear();
}
