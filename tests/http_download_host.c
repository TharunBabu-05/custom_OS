#include "http_download.h"
#include "host_assert.h"
#include <stdio.h>
#include <string.h>

static void reject(const char *response,size_t capacity)
{
    unsigned char body[16];
    struct http_download download={.body=body,.capacity=capacity};
    bool accepted=http_download_receive(&download,(const unsigned char *)response,strlen(response));
    if (accepted) { accepted=http_download_receive(&download,NULL,0); }
    assert(!accepted && download.rejected && !download.complete);
    assert(!http_download_receive(&download,NULL,0));
}

int main(void)
{
    static const unsigned char response[]="HTTP/1.1 200 OK\r\nCoNtEnT-LeNgTh:\t4 \r\nConnection: close\r\n\r\nA\0BC";
    size_t n=sizeof(response)-1;
    for (size_t offset=0;offset<=n;++offset) {
        unsigned char body[4];
        struct http_download download={.body=body,.capacity=sizeof(body)};
        assert(http_download_receive(&download,response,offset));
        assert(http_download_receive(&download,response+offset,n-offset));
        assert(download.received==4 && !download.complete);
        assert(http_download_receive(&download,NULL,0) && download.complete);
        assert(!memcmp(body,"A\0BC",4));
    }
    reject("HTTP/1.1 404 Not Found\r\nContent-Length: 1\r\n\r\nx",16);
    reject("HTTP/1.1 200 OK\r\nContent-Length: 4\r\nContent-Length: 4\r\n\r\nABCD",16);
    reject("HTTP/1.1 200 OK\r\nContent-Length: 4, 4\r\n\r\nABCD",16);
    reject("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nContent-Length: 4\r\n\r\nABCD",16);
    reject("HTTP/1.1 200 OK\r\nContent-Encoding: gzip\r\nContent-Length: 4\r\n\r\nABCD",16);
    reject("HTTP/1.1 200 OK\r\nContent-Length: 4\r\n\r\nABC",16);
    reject("HTTP/1.1 200 OK\r\nContent-Length: 4\r\n\r\nABCDE",16);
    reject("HTTP/1.1 200 OK\r\nContent-Length: 4\r\n\r\nABCD",3);
    reject("HTTP/1.1 200 OK\r\nContent-Length: 1844674407370955161600\r\n\r\n",16);
    reject("HTTP/1.1 200 OK\r\nConnection: close\r\n\r\nABCD",16);
    reject("HTTP/1.1 200 OK\r\nContent-Length: 4\r\n folded\r\n\r\nABCD",16);
    char overflow[1100];
    memset(overflow,'a',sizeof(overflow)-1);
    overflow[sizeof(overflow)-1]=0;
    reject(overflow,16);
    puts("PASS: fragmented binary HTTP body, strict bounded framing, malformed headers, truncation and excess data.");
    return 0;
}
