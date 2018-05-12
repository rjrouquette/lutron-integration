//
// Created by robert on 5/11/18.
//

#include <cstring>
#include <netinet/in.h>
#include <iostream>
#include <netdb.h>
#include <zconf.h>
#include "lutron_connector.h"

static const char promptLogin[] = "login: ";
static const char promptPassword[] = "password: ";

LutronConnector::LutronConnector(const char *host, int port, const char *user, const char *pass) {
    this->port = port;

    strncpy(hostname, host, sizeof(hostname));
    hostname[sizeof(hostname)-1] = 0;

    strncpy(username, user, sizeof(username));
    username[sizeof(username)-1] = 0;

    strncpy(password, pass, sizeof(password));
    password[sizeof(password)-1] = 0;

    sockfd = -1;
    telnet = nullptr;
    joinRX = false;
    mutex = PTHREAD_MUTEX_INITIALIZER;
}

LutronConnector::~LutronConnector() {
    pthread_mutex_lock(&mutex);
    if(sockfd >= 0) {
        disconnect();
    }
    if(joinRX) {
        pthread_join(threadRX, nullptr);
    }
    pthread_mutex_unlock(&mutex);
}

bool LutronConnector::disconnect() {
    pthread_mutex_lock(&mutex);
    telnet_free(telnet);
    close(sockfd);
    sockfd = -1;
    pthread_mutex_unlock(&mutex);
}

bool LutronConnector::connect() {
    pthread_mutex_lock(&mutex);
    if(joinRX) {
        pthread_join(threadRX, nullptr);
        joinRX = false;
    }

    // create network socket
    sockfd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if(sockfd < 0) {
        std::cerr << "[LutronConnector::connect()] failed to create network socket" << std::endl;
        pthread_mutex_unlock(&mutex);
        return false;
    }

    // resolve network address
    struct sockaddr_in addr = {};
    bzero((char *) &addr, sizeof(addr));
    addr.sin_family = AF_INET;
    struct hostent *server = gethostbyname(hostname);
    if (server) {
        memcpy(&addr.sin_addr.s_addr, server->h_addr, (size_t)server->h_length);
        addr.sin_port = htons((uint16_t)port);
    }
    else {
        std::cerr << "[LutronConnector::connect()] failed to resolve network address" << std::endl;
        close(sockfd);
        pthread_mutex_unlock(&mutex);
        return false;
    }

    // connect to remote host
    auto serv_addr = (const struct sockaddr *) &addr;
    if (::connect(sockfd, serv_addr, sizeof(struct sockaddr_in)) < 0) {
        std::cerr << "[LutronConnector::connect()] failed to connect to smart bridge " << hostname << ":" << port << std::endl;
        close(sockfd);
        pthread_mutex_unlock(&mutex);
        return false;
    }

    static const telnet_telopt_t telopts[] = {
            { TELNET_TELOPT_ECHO,		TELNET_WONT, TELNET_DONT },
            { TELNET_TELOPT_TTYPE,		TELNET_WONT, TELNET_DONT },
            { TELNET_TELOPT_COMPRESS2,	TELNET_WONT, TELNET_DONT },
            { TELNET_TELOPT_MSSP,		TELNET_WONT, TELNET_DONT },
            { -1, 0, 0 }
    };

    // telnet protocol processor
    telnet = telnet_init(telopts, telnet_event, 0, this);

    // start network RX thread
    doLogin = true;
    doPassword = true;
    joinRX = true;
    pthread_create(&threadRX, nullptr, doRX, this);
    pthread_mutex_unlock(&mutex);
    return true;
}

void* LutronConnector::doRX(void *context) {
    auto ctx = (LutronConnector *) context;
    ssize_t rs;
    char buffer[256];

    while(ctx->sockfd >= 0) {
        if ((rs = ::recv(ctx->sockfd, buffer, sizeof(buffer), 0)) > 0) {
            telnet_recv(ctx->telnet, buffer, (size_t)rs);
        } else if (rs == 0) {
            break;
        } else {
            std::cerr << "[LutronConnector] recv() failed: " << strerror(errno) << std::endl;
            break;
        }
    }

    return nullptr;
}

void LutronConnector::telnet_event(telnet_t *telnet, telnet_event_t *event, void *context) {
    auto ctx = (LutronConnector *) context;

    switch (event->type) {
        /* data received */
        case TELNET_EV_DATA:
            if (event->data.size) {
                ctx->recv(event->data.buffer, event->data.size);
            }
            break;

        /* data must be sent */
        case TELNET_EV_SEND:
            ctx->send(event->data.buffer, event->data.size);
            break;

        /* request to enable remote feature (or receipt) */
        case TELNET_EV_WILL:

        /* notification of disabling remote feature (or receipt) */
        case TELNET_EV_WONT:
            break;

        /* request to enable local feature (or receipt) */
        case TELNET_EV_DO:
            break;

        /* demand to disable local feature (or receipt) */
        case TELNET_EV_DONT:
            break;

        /* respond to TTYPE commands */
        case TELNET_EV_TTYPE:
            /* respond with our terminal type, if requested */
            if (event->ttype.cmd == TELNET_TTYPE_SEND) {
                telnet_ttype_is(telnet, getenv("TERM"));
            }
            break;

        /* respond to particular subnegotiations */
        case TELNET_EV_SUBNEGOTIATION:
            break;
            /* error */

        case TELNET_EV_ERROR:
            std::cerr << "[LutronConnector] telnet error: " << event->error.msg << std::endl;
            ctx->disconnect();

        default:
            /* ignore */
            break;
    }
}

void LutronConnector::send(const char *data, size_t len) {
    ssize_t rs;

    /* send data */
    while (len > 0) {
        if ((rs = ::send(sockfd, data, len, 0)) == -1) {
            std::cerr << "[LutronConnector] send() failed: " << strerror(errno) << std::endl;
            disconnect();
        } else if (rs == 0) {
            std::cerr << "[LutronConnector] send() unexpectedly returned zero" << std::endl;
            disconnect();
        }

        /* update pointer and size to see if we've got more to send */
        data += rs;
        len -= rs;
    }
}

void LutronConnector::recv(const char *data, size_t len) {
    if(doLogin) {
        if(len != sizeof(promptLogin)-1 || strncmp(data, promptLogin, len) != 0) {
            std::cerr << "[LutronConnector] login prompt not received" << std::endl;
            disconnect();
            return;
        }
        telnet_send(telnet, username, strlen(username));
        telnet_send(telnet, "\r\n", 2);
        doLogin = false;
        std::cerr << "[LutronConnector] login sent" << std::endl;
        return;
    }
    if(doPassword) {
        if(len != sizeof(promptPassword)-1 || strncmp(data, promptPassword, len) != 0) {
            std::cerr << "[LutronConnector] password prompt not received" << std::endl;
            disconnect();
            return;
        }
        telnet_send(telnet, password, strlen(password));
        telnet_send(telnet, "\r\n", 2);
        doPassword = false;
        std::cerr << "[LutronConnector] password sent" << std::endl;
        return;
    }

    std::cout.write(data, len);
    std::cout << std::endl;
}

void LutronConnector::sendCommand(const char *data, size_t len) {
    telnet_send(telnet, data, len);
    telnet_send(telnet, "\r\n", 2);
}
