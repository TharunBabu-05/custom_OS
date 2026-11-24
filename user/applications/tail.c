#include "text_io.h"
#include "elf_app.h"

enum { TAIL_BYTES=32768 };
struct tail_options { unsigned count,path_arg;bool bytes; };

static bool options(struct tail_options *out)
{
    int64_t argc=uos_argc();
    unsigned first=1;
    out->count=10;
    char option[3];
    if (argc>1 && uos_arg(1,option,sizeof(option))==2 &&
        option[0]=='-' && (option[1]=='n' || option[1]=='c')) {
        char number[12];
        if (argc<3 || uos_arg(2,number,sizeof(number))<=0 ||
            !text_count(number,TAIL_BYTES,&out->count)) { return false; }
        out->bytes=option[1]=='c';
        first=3;
    }
    out->path_arg=argc>first ? first : 0;
    return argc<=first+1;
}

static int suffix(int64_t input,const struct tail_options *args,unsigned char *buffer)
{
    unsigned char chunk[TEXT_IO_BYTES],before=0;
    size_t first=0,length=0;
    bool discarded=false;
    for (;;) {
        int64_t n=file_input_read(input,chunk,sizeof(chunk));
        if (n<0) { uos_puts("tail: read failed\n");return 2; }
        if (!n) { break; }
        if (!args->count) { continue; }
        for (int64_t i=0;i<n;++i) {
            if (length<TAIL_BYTES) { buffer[length++]=chunk[i]; }
            else {
                before=buffer[first];
                buffer[first]=chunk[i];
                first=(first+1)%TAIL_BYTES;
                discarded=true;
            }
        }
    }
    if (!length) { return 0; }

    size_t begin=0;
    if (args->bytes) { begin=length>args->count ? length-args->count : 0; }
    else {
        unsigned boundaries=0;
        /* A terminal LF closes the last record. It does not add an empty record. */
        for (size_t i=length-1;i>0;--i) {
            if (buffer[(first+i-1)%TAIL_BYTES]=='\n' && ++boundaries==args->count) {
                begin=i;
                break;
            }
        }
        if (!begin && discarded && (before!='\n' || boundaries+1!=args->count)) {
            uos_puts("tail: requested lines exceed the 32768-byte suffix buffer\n");
            return 2;
        }
    }
    size_t start=(first+begin)%TAIL_BYTES,bytes=length-begin;
    size_t contiguous=bytes<TAIL_BYTES-start ? bytes : TAIL_BYTES-start;
    return text_write(buffer+start,contiguous) && text_write(buffer,bytes-contiguous) ? 0 : 2;
}

int app_main(void)
{
    struct tail_options args={0};
    if (!options(&args)) { uos_puts("Usage: tail [-n N|-c N] [file|-]\n");return 2; }
    int64_t input=text_open_input(args.path_arg);
    if (input<0) { uos_puts("tail: cannot open input\n");return 2; }
    int64_t pages=args.count ? uos_alloc_pages(TAIL_BYTES/APP_PAGE_BYTES) : 0;
    if (args.count && pages<=0) { uos_puts("tail: cannot allocate suffix buffer\n");return file_input_finish(input,2); }
    unsigned char *buffer=(void *)(uintptr_t)pages;
    int result=suffix(input,&args,buffer);
    if (pages && uos_free_pages(buffer)) { result=2; }
    return file_input_finish(input,result);
}
