/*
* TCP abstraction layer with linux sockets api
* Uses non blocking sockets for concurrency with epoll api
*
* DEPENDENCIES: mem/pool.h
* USAGE:
*   #define TCP_IMPLEMENTATION
*   #define POOL_IMPLEMENTATION
*   #include "tcp.h"
*
* WARNING: Only accessible in Linux
* TODO:
*   - Windows and BSD support
*/
#ifndef TCP_H
#define TCP_H

#include "pool.h"
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <wait.h>
#include <signal.h>
#include <netdb.h>

#ifdef __cplusplus
extern "C" {
#endif

#define malloc_ malloc
#define calloc_ calloc
#define realloc_ realloc
#define free_ free

/// Batch size of ready events processed at once by epoll. A higher value means that
/// it will be more performant while consuming more memory.
#define MAX_EPOLL_EVENTS 256

/// The lifetime of `Event` is managed by `Listener`
typedef struct {
    struct epoll_event ev, events[MAX_EPOLL_EVENTS];
    int fd;
    int nfds;
} Event;

typedef struct {
    struct sockaddr_storage addr;
    PoolAlloc *pool;
    int fd;

    /// Server config
    char *port;
    uint16_t backlog;
    uint16_t pool_size;
} Listener;

typedef struct {
    struct sockaddr_storage addr;
    Listener *listener;
    int fd;
} Conn;

/// Helper for tcpListen
typedef struct {
    char *port;
    uint16_t backlog;
    uint16_t pool_size;
} ListenerArgs;

/// Initiates the server instance and adds the server socket for polling
/// Allocates `Listener` and `Event` on the same heap region [Listener + Event]
/// Free it with `tcpCloseListener`.
/// MUST provide a `port`. `backlog` and `pool_size` defaults to SOMAXCONN and 1024.
Listener *tcpListen(const ListenerArgs *args);
/// Poll the sockets for IO multiplexing.
/// Info:
/// After the function returns, use the `nfds` field to loop through the available events
/// and check for the specific event's `data.fd`. If fd is `listener.fd` then event occured
/// in server. For server event, run `tcpAccept` and for each client run `tcpHandler`.
/// `tcpHandler` can get the Conn pointer from `data.ptr`.
/// Warning:
/// The lifetime of `Event` is managed by `Listener` and thus is allocated and freed
/// with `Listener`. DO NOT explicitly call free on `Event`
Event *tcpPoll(Listener *listener);
/// Accepts a client connection and adds it for polling
/// Free it with `tcpCloseConn`
Conn *tcpAccept(Listener *listener);
/// Handler function for each clients.
/// On success, returns a 0. On error, returns -1.
int tcpHandler(Conn *conn, void (*handler)(Conn *conn));
/// Removes the client socket from epoll, closes the socket and frees Conn instance
/// Warning:
/// SHOULD be called inside the tcpHandler
void tcpCloseConn(Conn *conn);
/// Closes both epoll_fd and listener socket and frees Listener instance
void tcpCloseListener(Listener *listener);

/// Receive a message from the socket. Calls `recv` syscall
/// Returns:
/// On success, returns the total bytes received.
/// On error, returns 0 if `fd` gets closed,
/// -2 if `recv` call returns `EAGAIN/EWOULDBLOCK`
/// and -1 for general error
ssize_t tcpRecv(int fd, void *buf, size_t len);
/// Send a message on socket. Calls `send` in a loop to ensure all the data has been send
/// Returns:
/// On success, returns the total bytes send.
/// On error, returns 0 if `fd` gets closed,
/// -2 if `send` call returns `EAGAIN/EWOULDBLOCK`
/// and -1 for general error
ssize_t tcpSend(int fd, const void *buf, size_t len);

/// IP version agnostic `inet_ntop`
/// Warning:
/// Make sure, len >= INET6_ADDRSTRLEN
/// Returns:
/// On success, writes the IP address to buf and return the value at buf. On error, returns NULL.
char *getIPAddr(const struct sockaddr_storage *sa, char *buf, size_t len);
/// IP version agnostic `ntohs`
/// Warning:
/// The passed argument CAN NOT be NULL
uint16_t getPort(const struct sockaddr_storage *sa);

#ifdef TCP_IMPLEMENTATION

static int setSockOpt_(int fd) {
    int yes = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1) {
        perror("setsockopt");
        return -1;
    }

    if (setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes)) == -1) {
        perror("setsockopt");
        return -1;
    }

    return 0;
}

static int setNonBlockingSocket_(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl get");
        return -1;
    }

    flags |= O_NONBLOCK;
    if (fcntl(fd, F_SETFL, flags) == -1) {
        perror("fcntl set");
        return -1;
    }

    return 0;
}

static inline Event *getEventPtr_(const Listener *listener) {
    return (Event *)(listener + 1);
}

static inline void sigchldHandler_(int s) {
    (void)s;

    int saved_errno = errno;
    while (waitpid(-1, NULL, WNOHANG) > 0) {
    }
    errno = saved_errno;
}

static int reapDeadProcs_(void) {
    struct sigaction sig;
    sig.sa_handler = sigchldHandler_;
    sigemptyset(&sig.sa_mask);
    sig.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sig, NULL) == -1) {
        perror("sigaction");
        return -1;
    }

    return 0;
}

static int tcpEpollInit_(const Listener *listener) {
    int epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd == -1) {
        perror("epoll_create1");
        return -1;
    }

    Event *event = getEventPtr_(listener);
    event->fd = epoll_fd;
    event->ev.data.fd = listener->fd;
    event->ev.events = EPOLLIN | EPOLLET;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listener->fd, &event->ev) == -1) {
        perror("epoll_ctl listener");
        close(epoll_fd);
        return -1;
    }

    return 0;
}

static int addtoEpollList_(const Listener *listener, Conn *conn) {
    Event *event = getEventPtr_(listener);
    event->ev.data.ptr = conn;
    event->ev.events = EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLET;
    if (epoll_ctl(event->fd, EPOLL_CTL_ADD, conn->fd, &event->ev) == -1) {
        perror("epoll_ctl client");
        return -1;
    }

    return 0;
}

Listener *tcpListen(const ListenerArgs *args) {
    if (!args || !args->port) {
        return NULL;
    }

    // As the lifetime of Listener and Event are same we allocate both of them in a same region,
    // later we access them using getEventPtr_ which returns the Event chunk by moving the listener
    // pointer by sizeof Listener (listener + 1)
    Listener *listener = (Listener *)malloc_(sizeof(Listener) + sizeof(Event));
    listener->port = args->port;
    if (args->backlog) {
        listener->backlog = args->backlog;
    } else {
        listener->backlog = SOMAXCONN;
    }

    if (args->pool_size) {
        listener->pool_size = args->pool_size;
    } else {
        listener->pool_size = 1024;
    }

    struct addrinfo hints, *res, *p;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    int rv = getaddrinfo(NULL, listener->port, &hints, &res);
    if (rv != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return NULL;
    }

    int fd = -1;
    for (p = res; p != NULL; p = p->ai_next) {
        fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd == -1) {
            perror("socket");
            continue;
        }

        if (setSockOpt_(fd) == -1) {
            freeaddrinfo(res);
            goto clean;
        }

        if (setNonBlockingSocket_(fd) == -1) {
            freeaddrinfo(res);
            goto clean;
        }

        if (bind(fd, p->ai_addr, p->ai_addrlen) == -1) {
            perror("bind");
            close(fd);
            continue;
        }

        memcpy(&listener->addr, p->ai_addr, p->ai_addrlen);

        break;
    }

    freeaddrinfo(res);

    if (!p) {
        goto clean;
    }

    listener->fd = fd;

    if (listen(fd, listener->backlog) == -1) {
        perror("listen");
        goto clean;
    }

    if (reapDeadProcs_() == -1) {
        goto clean;
    }

    if (tcpEpollInit_(listener) == -1) {
        goto clean;
    }

    listener->pool = poolInit(sizeof(Conn), listener->pool_size);

    return listener;

clean:
    close(fd);
    free_(listener);
    return NULL;
}

Event *tcpPoll(Listener *listener) {
    if (!listener) {
        return NULL;
    }

    Event *event = getEventPtr_(listener);
    event->nfds = epoll_wait(event->fd, event->events, MAX_EPOLL_EVENTS, -1);
    if (event->nfds == -1) {
        perror("epoll_wait");
        tcpCloseListener(listener);
        return NULL;
    }

    return event;
}

Conn *tcpAccept(Listener *listener) {
    if (!listener) {
        return NULL;
    }

    struct sockaddr_storage addr;
    socklen_t size = sizeof(addr);
    int conn_fd = accept(listener->fd, (struct sockaddr *)&addr, &size);

    if (conn_fd == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return NULL;
        }
        perror("accept");
        return NULL;
    }

    Conn *conn = poolAlloc(listener->pool);
    conn->listener = listener;
    conn->fd = conn_fd;
    conn->addr = addr;

    if (setNonBlockingSocket_(conn_fd) == -1) {
        goto clean;
    }

    if (addtoEpollList_(listener, conn) == -1) {
        goto clean;
    }

    return conn;

clean:
    close(conn_fd);
    poolFree(listener->pool, conn);
    return NULL;
}

int tcpHandler(Conn *conn, void (*handler)(Conn *conn)) {
    if (!conn || !handler) {
        return -1;
    }

    handler(conn);
    return 0;
}

void tcpCloseConn(Conn *conn) {
    if (!conn) {
        return;
    }

    Event *event = getEventPtr_(conn->listener);
    if (epoll_ctl(event->fd, EPOLL_CTL_DEL, conn->fd, NULL) == -1) {
        perror("epoll_ctl del");
    }
    close(conn->fd);
    poolFree(conn->listener->pool, conn);
}

void tcpCloseListener(Listener *listener) {
    if (!listener) {
        return;
    }

    Event *event = getEventPtr_(listener);
    close(event->fd);
    close(listener->fd);
    poolDestroy(listener->pool);
    free_(listener);
}

ssize_t tcpRecv(int fd, void *buf, size_t len) {
    if (fd < 0 || !buf) {
        return -1;
    }

    ssize_t bytes_recv = recv(fd, buf, len, 0);
    if (bytes_recv == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return -2;
        }
        perror("recv");
        return -1;
    } else if (bytes_recv == 0) {
        return 0;
    }

    return bytes_recv;
}

ssize_t tcpSend(int fd, const void *buf, size_t len) {
    if (fd < 0 || !buf) {
        return -1;
    }

    size_t total = 0;
    size_t bytes_left = len;
    ssize_t bytes_send = 0;

    while (total < len) {
        bytes_send =
            send(fd, (const char *)buf + total, bytes_left, MSG_NOSIGNAL);
        if (bytes_send == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return -2;
            }
            perror("send");
            return -1;
        } else if (bytes_send == 0) {
            return 0;
        }

        total += bytes_send;
        bytes_left -= bytes_send;
    }

    return total;
}

char *getIPAddr(const struct sockaddr_storage *sa, char *buf, size_t len) {
    if (!sa || !buf) {
        return NULL;
    }

    if (sa->ss_family == AF_INET) {
        struct sockaddr_in *s = (struct sockaddr_in *)sa;
        inet_ntop(AF_INET, &s->sin_addr, buf, len);
    } else {
        struct sockaddr_in6 *s = (struct sockaddr_in6 *)sa;
        inet_ntop(AF_INET6, &s->sin6_addr, buf, len);
    }

    return buf;
}

uint16_t getPort(const struct sockaddr_storage *sa) {
    if (sa->ss_family == AF_INET) {
        struct sockaddr_in *s = (struct sockaddr_in *)sa;
        return ntohs(s->sin_port);
    } else {
        struct sockaddr_in6 *s = (struct sockaddr_in6 *)sa;
        return ntohs(s->sin6_port);
    }
}

#endif // TCP_IMPLEMENTATION

#undef malloc_
#undef calloc_
#undef realloc_
#undef free_

#ifdef __cplusplus
}
#endif

#endif
