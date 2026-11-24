#include "text_io.h"

bool text_count(const char *text,unsigned limit,unsigned *count)
{
    if (!*text) { return false; }
    unsigned value=0;
    for (;*text;++text) {
        if (*text<'0' || *text>'9') { return false; }
        unsigned digit=(unsigned)(*text-'0');
        if (value>limit/10 || (value==limit/10 && digit>limit%10)) { return false; }
        value=value*10+digit;
    }
    *count=value;
    return true;
}

int64_t text_open_input(unsigned path_arg)
{
    char path[256];
    if (!path_arg) { return file_input_open(NULL); }
    if (uos_arg(path_arg,path,sizeof(path))<=0) { return UOS_ERROR; }
    return file_input_open(path);
}

bool text_write(const void *data,size_t length)
{
    const unsigned char *bytes=data;
    while (length) {
        size_t chunk=length<TEXT_IO_BYTES ? length : TEXT_IO_BYTES;
        if (uos_write(bytes,chunk)!=(int64_t)chunk) { return false; }
        bytes+=chunk;
        length-=chunk;
    }
    return true;
}

int text_compare(const unsigned char *left,size_t left_bytes,const unsigned char *right,size_t right_bytes)
{
    size_t common=left_bytes<right_bytes ? left_bytes : right_bytes;
    for (size_t i=0;i<common;++i) {
        if (left[i]!=right[i]) { return (int)left[i]-right[i]; }
    }
    return (left_bytes>right_bytes)-(left_bytes<right_bytes);
}

int text_read_line(struct text_reader *reader,unsigned char line[TEXT_LINE_BYTES],size_t *bytes,bool *newline)
{
    *bytes=0;
    *newline=false;
    for (;;) {
        if (reader->at==reader->used) {
            int64_t n=file_input_read(reader->input,reader->buffer,sizeof(reader->buffer));
            if (n<=0) { return n<0 ? -1 : *bytes!=0; }
            reader->at=0;
            reader->used=(size_t)n;
        }
        unsigned char c=reader->buffer[reader->at++];
        if (c=='\n') { *newline=true;return 1; }
        if (*bytes==TEXT_LINE_BYTES) { return -2; }
        line[(*bytes)++]=c;
    }
}

int text_line_error(const char *name,int result)
{
    uos_puts(name);
    uos_puts(result==-2 ? ": line exceeds 4096 bytes\n" : ": read failed\n");
    return 2;
}
