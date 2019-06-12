//
// Created by robert on 5/12/18.
//

#ifndef LUTRON_INTEGRATION_DEVICE_H
#define LUTRON_INTEGRATION_DEVICE_H

#include <string>
#include <map>
#include <json-c/json_object.h>
#include <set>

class LutronConnector;
class room;

class device {
public:
    enum device_type {
        invalid_type,
        smart_bridge,
        wall_dimmer,
        plugin_dimmer,
        pico_remote,
        wall_switch,
        plugin_switch
    };

    enum button_t {
        invalid_button,
        unknown,
        on,
        favorite,
        off,
        up,
        down
    };

    class listener {
    public:
        virtual void buttonEvent(device *dev, button_t button, bool state) = 0;
    };

    LutronConnector *conn;

    const int id;
    const std::string name;
    const std::string description;
    const device_type type;
    room * const location;

    device(int id, const char *name, const char *desc, device_type type, room *loc);
    virtual ~device();

    static device * parse(json_object *object, std::map<std::string, room *> &rooms);

    virtual void requestRefresh() const;
    virtual void processMessage(const char *command, const char **fields, int fcnt);

    virtual void setOn();
    virtual void setOff();

    void addListener(listener *l);
    void removeListener(listener *l);

private:
    std::set<listener *> listeners;

};

class device_dimmer : public device {
private:
    float level;

public:
    device_dimmer(int id, const char *name, const char *desc, device_type type, room *loc);
    ~device_dimmer() override = default;

    void requestRefresh() const override;
    void processMessage(const char *command, const char **fields, int fcnt) override;

    void setOn() override;
    void setOff() override;

    float getLevel() const;
    void setLevel(float level, int fade);
};

class device_switch : public device {
private:
    bool state;

public:
    device_switch(int id, const char *name, const char *desc, device_type type, room *loc);
    ~device_switch() override = default;

    void requestRefresh() const override;
    void processMessage(const char *command, const char **fields, int fcnt) override;

    void setOn() override;
    void setOff() override;

    bool getState() const;
    void setState(bool state);
};

class device_remote : public device {
public:
    device_remote(int id, const char *name, const char *desc, device_type type, room *loc);
    ~device_remote() override = default;

    void requestRefresh() const override;
    void processMessage(const char *command, const char **fields, int fcnt) override;
};

#endif //LUTRON_INTEGRATION_DEVICE_H
