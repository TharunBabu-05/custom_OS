#include "file_input.h"
#include <stdbool.h>

enum { ROW_BYTES=16, MAX_RANGE=65536 };
struct dump_options { char path[256];unsigned skip,limit; };

static bool decimal(const char *text,unsigned *value)
{
    if (!*text) { return false; }
    unsigned n=0;
    while (*text) {
        if (*text<'0' || *text>'9') { return false; }
        n=n*10+(unsigned)(*text++-'0');
        if (n>MAX_RANGE) { return false; }
    }
    *value=n;
    return true;
}

static bool options(struct dump_options *out)
{
    int64_t argc=uos_argc();
    out->limit=256;
    bool skip_seen=false,limit_seen=false;
    for (unsigned i=1;i<(uint64_t)argc;++i) {
        if (uos_arg(i,out->path,sizeof(out->path))<=0) { return false; }
        const char *arg=out->path;
        if (arg[0]!='-' || (arg[1]!='s' && arg[1]!='n') || arg[2]) { return i+1==(uint64_t)argc; }
        bool skip=arg[1]=='s';
        bool *seen=skip ? &skip_seen : &limit_seen;
        char number[16];
        if (*seen || ++i>=(uint64_t)argc || uos_arg(i,number,sizeof(number))<=0 ||
            !decimal(number,skip ? &out->skip : &out->limit)) { return false; }
        *seen=true;
        out->path[0]=0;
    }
    return true;
}

static bool show_row(unsigned offset,const unsigned char *bytes,size_t length)
{
    static const char hex[]="0123456789abcdef";
    char line[80];
    size_t out=0;
    for (unsigned i=0;i<8;++i) { line[out++]=hex[(offset>>(28-4*i))&15]; }
    line[out++]=' ';line[out++]=' ';
    for (unsigned i=0;i<ROW_BYTES;++i) {
        if (i==ROW_BYTES/2) { line[out++]=' '; }
        line[out++]=i<length ? hex[bytes[i]>>4] : ' ';
        line[out++]=i<length ? hex[bytes[i]&15] : ' ';
        line[out++]=' ';
    }
    line[out++]='|';
    for (size_t i=0;i<length;++i) { line[out++]=bytes[i]>=32 && bytes[i]<=126 ? (char)bytes[i] : '.'; }
    line[out++]='|';line[out++]='\n';
    return uos_write(line,out)==(int64_t)out;
}

static int dump(int64_t input,unsigned skip,unsigned limit)
{
    unsigned char chunk[512],row[ROW_BYTES];
    unsigned offset=skip,used=0;
    while (limit || !input) {
        int64_t n=file_input_read(input,chunk,sizeof(chunk));
        if (n<0) { uos_puts("hexdump: read failed\n");return 2; }
        if (!n) { break; }
        size_t start=skip<(uint64_t)n ? skip : (size_t)n;
        skip-=(unsigned)start;
        for (size_t i=start;i<(size_t)n && limit;++i) {
            row[used++]=chunk[i];
            --limit;
            if (used==ROW_BYTES) {
                if (!show_row(offset,row,used)) { return 2; }
                offset+=used;
                used=0;
            }
        }
    }
    return used && !show_row(offset,row,used) ? 2 : 0;
}

int app_main(void)
{
    struct dump_options args={0};
    if (!options(&args)) {
        uos_puts("Usage: hexdump [-s offset] [-n length] [file|-]\n");return 2;
    }
    int64_t input=file_input_open(args.path[0] ? args.path : 0);
    if (input<0) { uos_puts("hexdump: cannot open input\n");return 2; }
    return file_input_finish(input,dump(input,args.skip,args.limit));
}
