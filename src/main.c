#define ARENA_IMPLEMENTATION
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

void onAccept(const TcpConn *conn) {
    char buf[INET6_ADDRSTRLEN];
    fprintf(stdout,
            "[Connected] %s:%d (fd: %d)\n",
            getIPAddr(&conn->addr, buf, INET6_ADDRSTRLEN),
            getPort(&conn->addr),
            conn->fd);
}

void onClose(const TcpConn *conn) {
    char buf[INET6_ADDRSTRLEN];
    fprintf(stdout,
            "[Disconnected] %s:%d (fd: %d)\n",
            getIPAddr(&conn->addr, buf, INET6_ADDRSTRLEN),
            getPort(&conn->addr),
            conn->fd);
}

int httpHandler(const TcpConn *conn) {
    if (!conn || conn->fd < 0) {
        return -1;
    }

    char buf[1024];

    ssize_t bytes_recv = tcpRecv(conn->fd, buf, sizeof(buf) - 1);
    if (bytes_recv <= 0) {
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
    HttpServer *http = httpInit(&(HttpArgs){
        .port = PORT,
        .onAccept = onAccept,
        .onClose = onClose,
        .tcpHandler = httpHandler,
    });
    if (!http) {
        return -1;
    }

    httpFree(http);

    return 0;
}
