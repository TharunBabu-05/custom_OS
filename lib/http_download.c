#include "http_download.h"

static bool header_is(const char *text,size_t length,const char *lowercase)
{
    size_t i=0;
    for (;i<length && lowercase[i];++i) {
        char c=text[i];
        if (c>='A' && c<='Z') { c=(char)(c+'a'-'A'); }
        if (c!=lowercase[i]) { return false; }
    }
    return i==length && !lowercase[i];
}

static size_t line_end(const char *p,size_t at,size_t bytes)
{
    while (at+1<bytes && (p[at]!='\r' || p[at+1]!='\n')) { ++at; }
    return at;
}

static bool value_valid(const char *p,size_t start,size_t end)
{
    for (size_t i=start;i<end;++i) {
        unsigned char c=(unsigned char)p[i];
        if (c!='\t' && (c<32 || c>126)) { return false; }
    }
    return true;
}

static size_t body_size(const char *p,size_t start,size_t end,size_t limit)
{
    while (start<end && (p[start]==' ' || p[start]=='\t')) { ++start; }
    while (end>start && (p[end-1]==' ' || p[end-1]=='\t')) { --end; }
    size_t n=0;
    for (size_t i=start;i<end;++i) {
        if (p[i]<'0' || p[i]>'9') { return 0; }
        unsigned digit=(unsigned)(p[i]-'0');
        if (n>limit/10 || (n==limit/10 && digit>limit%10)) { return 0; }
        n=n*10+digit;
    }
    return n;
}

static bool response_headers(struct http_download *download)
{
    const char *p=download->header;
    size_t bytes=download->header_bytes;
    size_t end=line_end(p,0,bytes);
    if (end<13 || end+2>=bytes || !header_is(p,7,"http/1.") || (p[7]!='0' && p[7]!='1') ||
        p[8]!=' ' || p[9]!='2' || p[10]!='0' || p[11]!='0' || p[12]!=' ' ||
        !value_valid(p,13,end)) { return false; }
    for (size_t at=end+2;at+2<bytes;at=end+2) {
        end=line_end(p,at,bytes);
        size_t colon=at;
        while (colon<end && p[colon]!=':') {
            unsigned char c=(unsigned char)p[colon];
            if (!((c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') || c=='-')) { return false; }
            ++colon;
        }
        if (at==colon || colon==end || end+2>=bytes || !value_valid(p,colon+1,end)) { return false; }
        if (header_is(p+at,colon-at,"content-length")) {
            if (download->expected) { return false; }
            download->expected=body_size(p,colon+1,end,download->capacity);
            if (!download->expected) { return false; }
        } else if (header_is(p+at,colon-at,"transfer-encoding") || header_is(p+at,colon-at,"content-encoding")) {
            return false;
        }
    }
    return download->expected!=0;
}

static bool receive(struct http_download *download,const unsigned char *data,size_t bytes)
{
    if (!data) {
        download->complete=!bytes && download->headers_done && download->received==download->expected;
        return download->complete;
    }
    while (bytes && !download->headers_done) {
        size_t at=download->header_bytes;
        if (at>=sizeof(download->header)) { return false; }
        char *p=download->header;
        p[at]=(char)*data++;
        --bytes;
        ++download->header_bytes;
        if (at>=3 && p[at-3]=='\r' && p[at-2]=='\n' && p[at-1]=='\r' && p[at]=='\n') {
            if (!response_headers(download)) { return false; }
            download->headers_done=true;
        }
    }
    if (bytes>download->expected-download->received) { return false; }
    for (size_t i=0;i<bytes;++i) { download->body[download->received+i]=data[i]; }
    download->received+=bytes;
    return true;
}

bool http_download_receive(void *context,const unsigned char *data,size_t bytes)
{
    struct http_download *download=context;
    if (download->complete || download->rejected) { return false; }
    if (receive(download,data,bytes)) { return true; }
    download->rejected=true;
    return false;
}
