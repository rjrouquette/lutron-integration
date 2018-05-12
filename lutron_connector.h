//
// Created by robert on 5/11/18.
//

#ifndef LUTRON_INTEGRATION_LUTRON_CONNECTOR_H
#define LUTRON_INTEGRATION_LUTRON_CONNECTOR_H


#include "libtelnet.h"

class LutronConnector {
private:
    // network configuration
    int port;
    char hostname[256];
    char username[64];
    char password[64];

    // connection state
    int sockfd;
    telnet_t *telnet;
    bool joinRX, doLogin, doPassword;
    pthread_t threadRX;
    pthread_mutex_t mutex;

    static void * doRX(void *context);
    static void telnet_event(telnet_t *telnet, telnet_event_t *event, void *context);
    void recv(const char *data, size_t len);
    void send(const char *data, size_t len);

public:
    LutronConnector(const char *hostname, int port, const char *username, const char *password);
    ~LutronConnector();

    bool connect();
    bool disconnect();

    // getters
    const char * getHostName() const { return hostname; }
    int          getPort()     const { return port;     }
    const char * getUserName() const { return username; }
    const char * getPassword() const { return password; }

    void sendCommand(const char *data, size_t len);
};


#endif //LUTRON_INTEGRATION_LUTRON_CONNECTOR_H
