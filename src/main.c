#define POOL_IMPLEMENTATION
#define TCP_IMPLEMENTATION
#define HASHMAP_IMPLEMENTATION
#define STR_IMPLEMENTATION
#define HTTP_IMPLEMENTATION
#include "http.h"
#include "tcp.h"
#include "hashmap.h"
#include "str.h"

#define PORT "8000"

int readAndWrite(Conn *conn) {
    if (!conn || conn->fd < 0) {
        return -1;
    }

    char buf[1024];
    char str[INET6_ADDRSTRLEN];

    ssize_t bytes_recv = tcpRecv(conn->fd, buf, sizeof(buf) - 1);
    if (bytes_recv == -1) {
        return -1;
    } else if (bytes_recv == 0) {
        fprintf(stdout,
                "[Disconnected] %s:%d (fd: %d)\n",
                getIPAddr(&conn->addr, str, INET6_ADDRSTRLEN),
                getPort(&conn->addr),
                conn->fd);

        return -1;
    }

    String req_buf = strNewLen(buf, bytes_recv);
    HttpConn *ser = httpConnInit(conn);
    HttpRequest *req = httpParseReq(ser, req_buf);
    printf("req_line: %s\n", req->req_line);
    hashmapPrint(req->headers);
    printf("body: %s\n", req->body);

    return 0;
}

int main(void) {
    char buf[INET6_ADDRSTRLEN];

    HttpServer *http = httpInit(&(HttpArgs){
        .port = PORT,
    });

    hashmapPrint(http->mime_types);

    while (1) {
        Event *event = tcpPoll(http->listener);
        if (!event) {
            break;
        }

        int nfds = event->nfds;
        for (int i = 0; i < nfds; i++) {
            if (event->events[i].data.fd == http->listener->fd) {
                while (1) {
                    Conn *conn = tcpAccept(http->listener);
                    if (!conn) {
                        break;
                    }

                    fprintf(stdout,
                            "[Connected] %s:%d (fd: %d)\n",
                            getIPAddr(&conn->addr, buf, INET6_ADDRSTRLEN),
                            getPort(&conn->addr),
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

    httpFree(http);

    return 0;
}
