#ifndef OS_HTTP_DOWNLOAD_H
#define OS_HTTP_DOWNLOAD_H
#include <stdbool.h>
#include <stddef.h>

#define PACKAGE_TLS_NAME "demo.ouros.test"
#define PACKAGE_HTTP_REQUEST "GET /package.elf HTTP/1.1\r\nHost: " PACKAGE_TLS_NAME "\r\nConnection: close\r\n\r\n"
enum { PACKAGE_MAX_BYTES=32768 };
struct http_download {
    unsigned char *body;
    size_t capacity,received,expected,header_bytes;
    bool headers_done,complete,rejected;
    char header[1024];
};

/* One HTTP 200 response with exactly one Content-Length. A final NULL
 * span indicates authenticated EOF. The caller publishes only on success. */
bool http_download_receive(void *,const unsigned char *,size_t);
#endif
