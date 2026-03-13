/*
* HTTP abstraction layer with linux sockets api
* Uses non blocking sockets for concurrency with epoll api
*
* DEPENDENCIES:
*   - net/tcp.h
*   - mem/pool.h (dependency of tcp.h)
*   - structs/{str.h, hashmap.h}
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
#include <stdio.h>
#include <string.h>

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
    HashMap *headers;
    String body;
} HttpRequest;

typedef struct {
    /// version   status_code status_str
    /// HTTP/1.1     200         OK
    /// key: value
    /// { body }
    const Conn *conn;
    String res_line;
    HashMap *headers;
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

#    define INITIAL_HASHMAP_CAPACITY 64

static size_t mapKeysize_(const void *key) {
    return strLen((String)key);
}

static void mapKeyValFree_(void *node) {
    strFree(node);
}

static int mapKeyCompare_(const void *a, const void *b) {
    return strCmp((String)a, (String)b);
}

static void mapPrint_(const void *key, const void *val) {
    printf("%s: %s\n", (char *)key, (char *)val);
}

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

HttpRequest *httpParseReq(HttpConn *ser, const String buf) {
    if (!ser || !buf) {
        return NULL;
    }

    HttpRequest *req = ser->req;

    size_t req_line_end = strFindLen(buf, "\r\n", 2);
    req->req_line = strNewLen(buf, req_line_end);

    size_t header_start = req_line_end + 2;
    size_t header_end = strFindLen(buf, "\r\n\r\n", 4);
    req->headers = hashmapNew(&(HashMapArgs){
        .capacity = 16,
        .keySize = mapKeysize_,
        .keyFree = mapKeyValFree_,
        .valFree = mapKeyValFree_,
        .keyCmp = mapKeyCompare_,
        .print = mapPrint_,
    });
    size_t curr_pos = header_start;
    while (curr_pos < header_end) {
        char *key_start = buf + curr_pos;
        char *colon = strchr(key_start, ':');
        size_t key_len = colon - key_start;

        char *header_line_end = strstr(colon, "\r\n");
        char *val_start = colon + 2;
        size_t val_len = header_line_end - val_start;

        String key = strNewLen(key_start, key_len);
        String val = strNewLen(val_start, val_len);

        hashmapSet(req->headers, key, val);

        curr_pos = (header_line_end - buf) + 2;
    }

    req->body = strSlice(buf, header_end + 4, strLen(buf));

    return req;
}

void httpFree(HttpConn *conn) {
    if (!conn) {
        return;
    }

    strFree(conn->req->req_line);
    hashmapFree(conn->req->headers);
    strFree(conn->req->body);

    strFree(conn->res->res_line);
    hashmapFree(conn->res->headers);
    strFree(conn->res->body);

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
