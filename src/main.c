#define TCP_IMPLEMENTATION
#include "tcp.h"

#define PORT "8000"

int readAndWrite(Conn *conn) {
    if (!conn || conn->fd < 0) {
        return -1;
    }

    char buf[1024];
    char str[INET6_ADDRSTRLEN];

    ssize_t bytes_recv = tcpRecv(conn->fd, buf, sizeof(buf) - 1);
    if (bytes_recv == 0) {
        fprintf(stdout,
                "[Disconnected] %s:%d (fd: %d)\n",
                getIPAddr(&conn->addr, str, INET6_ADDRSTRLEN),
                getPort(&conn->addr),
                conn->fd);

        return -1;
    } else if (bytes_recv <= -1) {
        return -1;
    }

    ssize_t bytes_send = tcpSend(conn->fd, buf, bytes_recv);
    if (bytes_send <= 0) {
        return -1;
    }

    return 0;
}

int main(void) {
    char buf[INET6_ADDRSTRLEN];

    Listener *listener = tcpListen(PORT);
    if (!listener) {
        return -1;
    }

    fprintf(stdout,
            "[Listening] %s:%d\n",
            getIPAddr(&listener->addr, buf, sizeof(buf)),
            getPort(&listener->addr));

    while (1) {
        Event *event = tcpPoll(listener);
        if (!event) {
            break;
        }

        int nfds = event->nfds;
        for (int i = 0; i < nfds; i++) {
            if (event->events[i].events & (EPOLLERR | EPOLLRDHUP | EPOLLHUP)) {
                continue;
            }

            if (event->events[i].data.fd == listener->fd) {
                while (1) {
                    Conn *conn = tcpAccept(listener);
                    if (!conn) {
                        break;
                    }

                    fprintf(stdout,
                            "[Connected] %s:%d (fd: %d)\n",
                            getIPAddr(&listener->addr, buf, INET6_ADDRSTRLEN),
                            getPort(&listener->addr),
                            conn->fd);
                }
            } else {
                Conn *conn = event->events[i].data.ptr;
                if (tcpHandler(conn, readAndWrite) == -1) {
                    fprintf(stderr, "Error in tcpHandler\n");
                }
            }
        }
    }

    tcpCloseListener(listener);

    return 0;
}
