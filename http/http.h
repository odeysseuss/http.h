/*
* HTTP abstraction layer with linux sockets api
* Uses non blocking sockets for concurrency with epoll api
*
* DEPENDENCIES:
*   - net/tcp.h
*   - mem/pool.h (dependency of tcp.h)
*   - structs/{str.h, hashmap/h}
* NOTE: To use this library define the following macro in EXACTLY
* ONE FILE BEFORE inlcuding http.h:
*   #define HTTP_IMPLEMENTATION
*   #define TCP_IMPLEMENTATION
*   #define POOL_IMPLEMENTATION
*   #define STR_IMPLEMENTATION
*   #define HASHMAP_IMPLEMENTATION
*   #include "http.h"
*
* REFERENCE: rfc 7231
* WARNING: Only accessible in Linux as net/tcp.h is Linux only
*/
#ifndef HTTP_H
#define HTTP_H

#include "tcp.h"
#include "str.h"
#include "hashmap.h"

#ifdef __cplusplus
extern "C" {
#endif

#define malloc_ malloc
#define calloc_ calloc
#define realloc_ realloc
#define free_ free

/// Http status codes
typedef enum {
    HTTP_OK = 200,
    HTTP_BAD_REQUEST = 400,
    HTTP_FORBIDDEN = 403,
    HTTP_NOT_FOUND = 404,
    HTTP_TOO_MANY_REQUESTS = 429,
    HTTP_INTERNAL_SERVER_ERROR = 500,
    HTTP_NOT_IMPLEMENTED = 501,
} HttpStatus;

typedef struct {
    /// method     path        version
    /// GET     /index.html  HTTP/1.1\r\n
    /// key: value
    /// { body }
    const Conn *conn;
    String req_line;
    HashMap *map;
    String body;
} HttpRequest;

typedef struct {
    /// version   status_code status_str
    /// HTTP/1.1     200         OK
    /// key: value
    /// { body }
    const Conn *conn;
    String res_line;
    HashMap *map;
    String body;
} HttpResponse;

typedef struct {
    HttpRequest *req;
    HttpResponse *res;
} HttpConn;

HttpConn *httpInit(const Conn *conn);
HttpRequest *httpParseReq(HttpConn *ser, const String buf);
HttpResponse *httpGet(const HttpRequest *req,
                      const char *route,
                      void (*getHadler)(HttpRequest *req));
HttpResponse *httpPost(const HttpRequest *req,
                       const char *route,
                       void (*postHadler)(HttpRequest *req));
HttpResponse *httpFileServe(const HttpRequest *req, const char *path);
int httpSendResponse(const HttpResponse *res);
void httpFree(HttpConn *conn);

#ifdef HTTP_IMPLEMENTATION

HttpConn *httpInit(const Conn *conn) {
    if (!conn) {
        return NULL;
    }

    size_t total_size =
        sizeof(HttpConn) + sizeof(HttpRequest) + sizeof(HttpResponse);
    HttpConn *server = malloc_(total_size);
    memset(server, 0, total_size);
    server->req = (HttpRequest *)(server + 1);
    server->res = (HttpResponse *)(server->req + 1);
    server->req->conn = conn;
    server->res->conn = conn;
    return server;
}

void httpFree(HttpConn *conn) {
    if (!conn) {
        return;
    }

    free_(conn);
}

#endif // HTTP_IMPLEMENTATION

#undef malloc_
#undef calloc_
#undef realloc_
#undef free_

#ifdef __cplusplus
}
#endif

#endif
