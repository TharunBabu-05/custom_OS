#include "file_input.h"
#include <stdbool.h>

enum { LINE_MAX_BYTES=4096 };

static bool contains(const unsigned char *line,size_t bytes,const char *pattern,size_t length)
{
    if (length>bytes) { return false; }
    for (size_t i=0;i<=bytes-length;++i) {
        size_t n=0;
        while (n<length && line[i+n]==(unsigned char)pattern[n]) { ++n; }
        if (n==length) { return true; }
    }
    return false;
}

static bool emit(const unsigned char *line,size_t bytes)
{
    while (bytes) {
        size_t chunk=bytes<512 ? bytes : 512;
        if (uos_write(line,chunk)!=(int64_t)chunk) { return false; }
        bytes-=chunk;
        line+=chunk;
    }
    return true;
}

int app_main(void)
{
    char pattern[128],path[256];
    int64_t argc=uos_argc();
    if (argc!=2 && argc!=3) { goto usage; }
    int64_t pattern_bytes=uos_arg(1,pattern,sizeof(pattern));
    if (pattern_bytes<=0 || (argc==3 && uos_arg(2,path,sizeof(path))<=0)) { goto usage; }
    int64_t input=file_input_open(argc==3 ? path : 0);
    if (input<0) { uos_puts("grep: cannot open input\n");return 2; }
    unsigned char line[LINE_MAX_BYTES+1],chunk[512];
    size_t used=0;
    int outcome=1;
    for (;;) {
        int64_t n=file_input_read(input,chunk,sizeof(chunk));
        if (n<0) { uos_puts("grep: read failed\n");return file_input_finish(input,2); }
        if (!n) { break; }
        for (int64_t i=0;i<n;++i) {
            unsigned char c=chunk[i];
            if (c!='\n' && used==LINE_MAX_BYTES) {
                uos_puts("grep: line exceeds 4096 bytes\n");return file_input_finish(input,2);
            }
            if (c!='\n') { line[used++]=c;continue; }
            if (contains(line,used,pattern,(size_t)pattern_bytes)) {
                line[used]='\n';
                if (!emit(line,used+1)) { return file_input_finish(input,2); }
                outcome=0;
            }
            used=0;
        }
    }
    if (used && contains(line,used,pattern,(size_t)pattern_bytes)) {
        outcome=emit(line,used) ? 0 : 2;
    }
    return file_input_finish(input,outcome);
usage:
    uos_puts("Usage: grep <literal> [file|-]\n");return 2;
}
