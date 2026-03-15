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
*   #define ARENA_IMPLEMENTATION
*   #define POOL_IMPLEMENTATION
*   #define STR_IMPLEMENTATION
*   #define HASHMAP_IMPLEMENTATION
*   #define TCP_IMPLEMENTATION
*   #define HTTP_IMPLEMENTATION
*   #include "http.h"
*
* WARNING: Only accessible in Linux as net/tcp.h is Linux only
*/
#ifndef HTTP_H
#define HTTP_H

#include "arena.h"
#include "pool.h"
#include "tcp.h"
#include "str.h"
#include "hashmap.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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

/// Http methods
typedef enum {
    // ref: [mdn/httpmethods](https://developer.mozilla.org/en-US/docs/Web/HTTP/Reference/Methods)
    HTTP_GET = 0,
    HTTP_HEAD,
    HTTP_OPTIONS,
    HTTP_TRACE,
    HTTP_PUT,
    HTTP_DELETE,
    HTTP_POST,
    HTTP_PATCH,
    HTTP_CONNECT
} HttpMethod;

typedef struct HttpConn HttpConn;
typedef void (*Route_Handler)(HttpConn *conn);

typedef struct Router {
    String route;
    Route_Handler handler;
    HttpMethod method;
    struct Router *next;
} Router;

typedef struct {
    /// method     path        version
    /// GET     /index.html  HTTP/1.1\r\n
    /// key: value
    const TcpConn *conn;
    String route;
    HashMap *headers;
    String body;
    Router *routes;
    HttpMethod method;
} HttpRequest;

typedef struct {
    /// version   status_code status_str
    /// HTTP/1.1     200         OK
    /// key: value
    /// { body }
    const TcpConn *conn;
    String res_line;
    HashMap *headers;
    String body;
} HttpResponse;

/// represents an http connection
struct HttpConn {
    const TcpConn *conn;
    HttpRequest *req;
    HttpResponse *res;
};

typedef struct {
    TcpListener *listener;
    PoolAlloc *http_pool; /// Connection pool
    size_t pool_size;
    HashMap *mime_types;
} HttpServer;

typedef struct {
    char *port;
    void (*onAccept)(const TcpConn *conn);
    void (*onClose)(const TcpConn *conn);
    int (*tcpHandler)(const TcpConn *conn);
} HttpArgs;

HttpServer *httpInit(size_t pool_size);
int httpLoop(HttpServer *server, HttpArgs *args);
HttpConn *httpConnInit(const TcpConn *conn);
HttpRequest *httpParseReq(HttpConn *ser, const String buf);
void httpGet(HttpConn *conn, const char *route, Route_Handler getHandler);
void httpPost(HttpConn *conn, const char *route, Route_Handler postHandler);
void httpFileServ(HttpConn *conn, const char *path);
void httpConnFree(HttpConn *conn);
void httpFree(HttpServer *server);

// #ifdef HTTP_IMPLEMENTATION

// Private helpers
#define INITIAL_REQUEST_HEADERS_CAPACITY 32
#define INITIAL_RESPONSE_HEADERS_CAPACITY 8
#define INITIAL_MIME_CAPACITY 32

// Headers hashmap helpers
static size_t mapKeysize_(const void *key) {
    return strLen((String)key);
}

static int mapKeyCompare_(const void *a, const void *b) {
    return strCmp((String)a, (String)b);
}

static void mapKeyValFree_(void *node) {
    strFree(node);
}

static void mapPrint_(const void *key, const void *val) {
    printf("%s: %s\n", (char *)key, (char *)val);
}

// Mime hashmap helpers
static size_t mimeMapKeySize_(const void *key) {
    return strlen(key);
}

static int mimeMapKeyCmp_(const void *a, const void *b) {
    return strcmp(a, b);
}

static HashMap *httpMimeMapInit_(HashMap *map) {
    hashmapSet(map, ".txt", "text/plain");
    hashmapSet(map, ".html", "text/html");
    hashmapSet(map, ".htm", "text/html");
    hashmapSet(map, ".css", "text/css");
    hashmapSet(map, ".js", "application/javascript");
    hashmapSet(map, ".json", "application/json");
    hashmapSet(map, ".xml", "application/xml");
    hashmapSet(map, ".svg", "image/svg+xml");
    hashmapSet(map, ".png", "image/png");
    hashmapSet(map, ".jpg", "image/jpeg");
    hashmapSet(map, ".jpeg", "image/jpeg");
    hashmapSet(map, ".woff", "font/woff");
    hashmapSet(map, ".woff2", "font/woff2");
    hashmapSet(map, ".ttf", "font/ttf");

    return map;
}

// Parsing helpers
static HttpMethod getHttpMethod_(char *method) {
    switch (method[0]) {
    case 'G':
        return HTTP_GET;
    case 'H':
        return HTTP_HEAD;
    case 'O':
        return HTTP_OPTIONS;
    case 'T':
        return HTTP_TRACE;
    case 'P':
        switch (method[1]) {
        case 'U':
            return HTTP_PUT;
        case 'O':
            return HTTP_POST;
        case 'A':
            return HTTP_PATCH;
        }
    case 'D':
        return HTTP_DELETE;
    case 'C':
        return HTTP_CONNECT;
    default:
        return -1;
    }
}

// === Http API implementation ===
// Constructors
HttpServer *httpInit(size_t pool_size) {
    if (pool_size == 0) {
        return NULL;
    }

    HttpServer *server = malloc_(sizeof(HttpServer));
    server->http_pool = poolInit(sizeof(HttpConn), pool_size);
    server->pool_size = pool_size;
    server->mime_types = hashmapNew(&(HashMapArgs){
        .capacity = INITIAL_MIME_CAPACITY,
        .keySize = mimeMapKeySize_,
        .keyCmp = mimeMapKeyCmp_,
        .print = mapPrint_,
    });
    httpMimeMapInit_(server->mime_types);
    hashmapPrint(server->mime_types);

    return server;
}

HttpConn *httpConnInit(const TcpConn *conn) {
    if (!conn) {
        return NULL;
    }

    HttpConn *server = arenaAlloc(conn->arena, sizeof(HttpConn));
    server->req = arenaAlloc(conn->arena, sizeof(HttpRequest));
    server->res = arenaAlloc(conn->arena, sizeof(HttpResponse));
    server->conn = conn;
    server->req->conn = conn;
    server->res->conn = conn;
    return server;
}

// Server loop
int httpLoop(HttpServer *server, HttpArgs *args) {
    if (!server || !args || !args->port || !args->tcpHandler) {
        return -1;
    }

    TcpListener *listener = tcpListen(&(TcpListenerArgs){
        .port = args->port,
        .pool_size = server->pool_size,
        .arena_size =
            sizeof(HttpConn) + sizeof(HttpRequest) + sizeof(HttpResponse),
    });
    if (!listener) {
        return -1;
    }

    char buf[INET6_ADDRSTRLEN];
    fprintf(stdout,
            "[Listening] %s:%d\n",
            getIPAddr(&listener->addr, buf, sizeof(buf)),
            getPort(&listener->addr));

    while (1) {
        TcpEvent *event = tcpPoll(listener);
        if (!event) {
            break;
        }

        int nfds = event->nfds;
        for (int i = 0; i < nfds; i++) {
            if (event->events[i].data.fd == listener->fd) {
                while (1) {
                    TcpConn *conn = tcpAccept(listener);
                    if (!conn) {
                        break;
                    }
                    if (args->onAccept) {
                        args->onAccept(conn);
                    }
                }
            } else {
                TcpConn *conn = event->events[i].data.ptr;
                if (tcpHandler(conn, args->tcpHandler) == -1) {
                    fprintf(stderr,
                            "[Error] tcpHandler (%s:%d)\n",
                            getIPAddr(&conn->addr, buf, INET6_ADDRSTRLEN),
                            getPort(&conn->addr));
                }

                if (event->events[i].events &
                    (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) {
                    if (args->onClose) {
                        args->onClose(conn);
                    }
                }
            }
        }
    }

    return 0;
}

// Request Parser
HttpRequest *httpParseReq(HttpConn *ser, const String buf) {
    if (!ser || !buf) {
        return NULL;
    }

    HttpRequest *req = ser->req;

    // initiate method
    char *temp = buf;
    char method[16];
    int i = 0;
    while (*temp != ' ' && i < 15) {
        method[i] = *temp;
        temp++;
        i++;
    }
    req->method = getHttpMethod_(method);

    // initiate route
    char *route_end = strchr(temp + 1, ' ');
    req->route = strNewLen(temp, route_end - temp);
    // just gonna ignore the http version now

    // header buffer
    size_t req_line_end_i = strFindLen(buf, "\r\n", 2);
    size_t header_start_i = req_line_end_i + 2;
    size_t header_end_i = strFindLen(buf, "\r\n\r\n", 4);

    // initiate hashmap
    req->headers = hashmapNew(&(HashMapArgs){
        .capacity = INITIAL_REQUEST_HEADERS_CAPACITY,
        .keySize = mapKeysize_,
        .keyFree = mapKeyValFree_,
        .valFree = mapKeyValFree_,
        .keyCmp = mapKeyCompare_,
        .print = mapPrint_,
    });

    // add headers in hashmap
    size_t curr_pos = header_start_i;
    while (curr_pos < header_end_i) {
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

    // initiate body and routes
    req->body = strSlice(buf, header_end_i + 4, strLen(buf));
    req->routes = NULL;

    return req;
}

// Destructors
void httpConnFree(HttpConn *conn) {
    if (!conn) {
        return;
    }

    strFree(conn->req->route);
    hashmapFree(conn->req->headers);
    strFree(conn->req->body);

    strFree(conn->res->res_line);
    hashmapFree(conn->res->headers);
    strFree(conn->res->body);

    arenaFree(conn->conn->arena);
}

void httpFree(HttpServer *server) {
    if (!server) {
        return;
    }

    poolDestroy(server->http_pool);
    tcpCloseListener(server->listener);
    hashmapFree(server->mime_types);
    free_(server);
}

// #endif // HTTP_IMPLEMENTATION

#undef malloc_
#undef calloc_
#undef realloc_
#undef free_

#ifdef __cplusplus
}
#endif

#endif
