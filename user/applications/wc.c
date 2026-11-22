#include "file_input.h"
#include <stdbool.h>

static bool whitespace(unsigned char c)
{ return c==' ' || (c>='\t' && c<='\r'); }

static void decimal(char *out,size_t *at,uint64_t value)
{
    char digits[20];
    unsigned n=0;
    do { digits[n++]=(char)('0'+value%10);value/=10; } while (value);
    while (n) { out[(*at)++]=digits[--n]; }
}

int app_main(void)
{
    int64_t argc=uos_argc();
    char path[256],output[63];
    if (argc>2 || (argc==2 && uos_arg(1,path,sizeof(path))<=0)) {
        uos_puts("Usage: wc [file|-]\n");return 2;
    }
    int64_t input=file_input_open(argc==2 ? path : 0);
    if (input<0) { uos_puts("wc: cannot open input\n");return 2; }
    unsigned char chunk[512];
    uint64_t lines=0,words=0,bytes=0;
    bool in_word=false;
    for (;;) {
        int64_t n=file_input_read(input,chunk,sizeof(chunk));
        if (n<0) {
            uos_puts("wc: read failed\n");return file_input_finish(input,2);
        }
        if (!n) { break; }
        bytes+=(uint64_t)n;
        for (int64_t i=0;i<n;++i) {
            unsigned char c=chunk[i];
            if (c=='\n') { ++lines; }
            bool word=!whitespace(c);
            if (word && !in_word) { ++words; }
            in_word=word;
        }
    }
    size_t at=0;
    decimal(output,&at,lines);output[at++]=' ';
    decimal(output,&at,words);output[at++]=' ';
    decimal(output,&at,bytes);output[at++]='\n';
    return file_input_finish(input,uos_write(output,at)==(int64_t)at ? 0 : 2);
}
