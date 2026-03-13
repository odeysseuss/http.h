#define HTTP_IMPLEMENTATION
#define TCP_IMPLEMENTATION
#define POOL_IMPLEMENTATION
#define STR_IMPLEMENTATION
#define HASHMAP_IMPLEMENTATION
#include "http.h"
#include "tcp.h"
#include "hashmap.h"
#include <stdio.h>

#define PORT "8000"

void readAndWrite(Conn *conn) {
    if (!conn || conn->fd < 0) {
        return;
    }

    char buf[1024];
    char str[INET6_ADDRSTRLEN];
    ssize_t bytes_recv;

    while (1) {
        bytes_recv = tcpRecv(conn->fd, buf, sizeof(buf) - 1);
        if (bytes_recv == -2) {
            break;
        } else if (bytes_recv == 0) {
            fprintf(stdout,
                    "[Disconnected] %s:%d (fd: %d)\n",
                    getIPAddr(&conn->addr, str, INET6_ADDRSTRLEN),
                    getPort(&conn->addr),
                    conn->fd);

            goto clean;
        } else if (bytes_recv == -1) {
            goto clean;
        }
        buf[bytes_recv] = '\0';

        printf("%s\n", buf);
        String str = strNewLen(buf, bytes_recv);

        HttpConn *ser = httpInit(conn);
        HttpRequest *req = httpParseReq(ser, str);
        printf("reqline: %s\n", req->req_line);
        hashmapPrint(req->headers);
        printf("body: [%s]\n", req->body);
        httpFree(ser);

        strFree(str);
    }

    return;

clean:
    tcpCloseConn(conn);
    return;
}

int main(void) {
    char buf[INET6_ADDRSTRLEN];

    Listener *listener = tcpListen(&(ListenerArgs){
        .port = PORT,
    });
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
            if (event->events[i].events & (EPOLLERR | EPOLLHUP)) {
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
