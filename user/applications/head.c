#include "file_input.h"
#include <stdbool.h>

static bool line_count(const char *text,unsigned *count)
{
    if (!*text) { return false; }
    unsigned value=0;
    while (*text) {
        if (*text<'0' || *text>'9') { return false; }
        value=value*10+(unsigned)(*text++-'0');
        if (value>65536) { return false; }
    }
    *count=value;
    return true;
}

int app_main(void)
{
    unsigned lines=10,first=1;
    int64_t argc=uos_argc();
    char path[256];
    if (argc>1) {
        if (uos_arg(1,path,sizeof(path))<=0) { goto usage; }
        if (path[0]=='-' && path[1]=='n' && !path[2]) {
            char count[16];
            if (argc<3 || uos_arg(2,count,sizeof(count))<=0 || !line_count(count,&lines)) { goto usage; }
            first=3;
        }
    }
    if (argc>first+1 || (argc>first && uos_arg(first,path,sizeof(path))<=0)) { goto usage; }
    int64_t input=file_input_open(argc>first ? path : 0);
    if (input<0) { uos_puts("head: cannot open input\n");return 2; }
    unsigned char chunk[512];
    /* Drain a pipeline so its producer can finish without a broken pipe. */
    while (lines || !input) {
        int64_t n=file_input_read(input,chunk,sizeof(chunk));
        if (n<0) { uos_puts("head: read failed\n");return file_input_finish(input,2); }
        if (!n) { break; }
        size_t prefix=0;
        while (lines && prefix<(size_t)n) {
            if (chunk[prefix++]=='\n') { --lines; }
        }
        if (prefix && uos_write(chunk,prefix)!=(int64_t)prefix) { return file_input_finish(input,2); }
    }
    return file_input_finish(input,0);
usage:
    uos_puts("Usage: head [-n 0..65536] [file|-]\n");return 2;
}
