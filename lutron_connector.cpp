//
// Created by robert on 5/11/18.
//

#include <cstring>
#include <netinet/in.h>
#include <netdb.h>
#include <zconf.h>
#include <csignal>
#include "lutron_connector.h"
#include "logging.h"

static const char promptLogin[] = "login: ";
static const char promptPassword[] = "password: ";
static const char promptCommand[] = "GNET> ";

LutronConnector::LutronConnector(const char *host, int port, const char *user, const char *pass) :
    hostname{0}, username{0}, password{0}, threadRX{},
    mutex(PTHREAD_MUTEX_INITIALIZER),
    mutexSend(PTHREAD_MUTEX_INITIALIZER),
    condResponse(PTHREAD_COND_INITIALIZER),
    condSend(PTHREAD_COND_INITIALIZER)
{
    this->port = port;

    strncpy(hostname, host, sizeof(hostname));
    hostname[sizeof(hostname)-1] = 0;

    strncpy(username, user, sizeof(username));
    username[sizeof(username)-1] = 0;

    strncpy(password, pass, sizeof(password));
    password[sizeof(password)-1] = 0;

    callback = nullptr;
    sockfd = -1;
    telnet = nullptr;
    joinRX = false;
    doLogin = true;
    doPassword = true;
    ready = false;
    connected = false;
}

LutronConnector::~LutronConnector() {
    pthread_mutex_lock(&mutex);
    if(sockfd >= 0) {
        disconnect();
    }
    if(joinRX) {
        pthread_kill(threadRX, SIGINT);
        pthread_join(threadRX, nullptr);
    }
    pthread_mutex_unlock(&mutex);
}

void LutronConnector::setCallback(callback_t callback) {
    this->callback = callback;
}

bool LutronConnector::disconnect() {
    pthread_mutex_lock(&mutex);

    pthread_mutex_lock(&mutexSend);
    connected = false;
    pthread_cond_broadcast(&condSend);
    pthread_mutex_unlock(&mutexSend);

    telnet_free(telnet);
    close(sockfd);
    sockfd = -1;

    pthread_mutex_unlock(&mutex);
}

bool LutronConnector::connect() {
    pthread_mutex_lock(&mutex);
    ready = false;
    connected = false;

    if(joinRX) {
        pthread_join(threadRX, nullptr);
        joinRX = false;
    }

    // create network socket
    sockfd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if(sockfd < 0) {
        log_error("smart bridge connect() failed to create network socket");
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
        log_error("smart bridge connect() failed to resolve network address");
        close(sockfd);
        pthread_mutex_unlock(&mutex);
        return false;
    }

    // connect to remote host
    auto serv_addr = (const struct sockaddr *) &addr;
    if (::connect(sockfd, serv_addr, sizeof(struct sockaddr_in)) < 0) {
        log_error("smart bridge connect() failed to connect to smart bridge %s:%d", hostname, port);
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
    connected = true;
    pthread_create(&threadRX, nullptr, doRX, this);
    pthread_mutex_unlock(&mutex);

    pthread_mutex_lock(&mutexSend);
    while(connected && !ready) {
        pthread_cond_wait(&condSend, &mutexSend);
    }
    pthread_mutex_unlock(&mutexSend);
    return connected;
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
            if(errno != EINTR) {
                log_error("smart bridge recv() failed: %s", strerror(errno));
            }
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
            log_error("smart bridge telnet error: %s", event->error.msg);
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
            log_error("smart bridge send() failed: %s", strerror(errno));
            disconnect();
        } else if (rs == 0) {
            log_error("smart bridge send() unexpectedly returned zero");
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
            log_error("smart bridge login prompt not received");
            disconnect();
            return;
        }
        telnet_send(telnet, username, strlen(username));
        telnet_send(telnet, "\r\n", 2);
        doLogin = false;
        log_notice("smart bridge login sent");
        return;
    }
    if(doPassword) {
        if(len != sizeof(promptPassword)-1 || strncmp(data, promptPassword, len) != 0) {
            log_error("smart bridge password prompt not received");
            disconnect();
            return;
        }
        telnet_send(telnet, password, strlen(password));
        telnet_send(telnet, "\r\n", 2);
        doPassword = false;
        log_notice("smart bridge password sent");
        return;
    }

    std::string temp;
    size_t next = 0;
    for(;;) {
        while(next < len && data[next] != '\r' && data[next] != '\n') {
            next++;
        }

        temp.assign(data, next);
        if(temp == promptLogin) {
            log_error("smart bridge login rejected");
            disconnect();
        }
        else if(temp == promptCommand) {
            log_debug("smart bridge ready");

            // notify any blocked senders
            pthread_mutex_lock(&mutexSend);
            ready = true;
            pthread_cond_signal(&condSend);
            pthread_mutex_unlock(&mutexSend);
        }
        else {
            pthread_mutex_lock(&mutexSend);
            pthread_cond_signal(&condResponse);
            pthread_mutex_unlock(&mutexSend);
            log_debug("smart bridge recv %s", temp.c_str());
            if(callback) (*callback)(temp.c_str());
        }

        while(next < len && (data[next] == '\r' || data[next] == '\n')) {
            next++;
        }
        if(next >= len) break;
        data += next;
        len -= next;
        next = 0;
    }
}

bool LutronConnector::sendCommand(const char *cmd) {
    pthread_mutex_lock(&mutexSend);
    while(connected && !ready) {
        pthread_cond_wait(&condSend, &mutexSend);
    }
    ready = false;

    if(!connected) {
        pthread_mutex_unlock(&mutexSend);
        return false;
    }

    log_debug("smart bridge send %s", cmd);
    telnet_send(telnet, cmd, strlen(cmd));
    telnet_send(telnet, "\r\n", 2);
    pthread_cond_wait(&condResponse, &mutexSend);

    pthread_mutex_unlock(&mutexSend);
    return true;
}
