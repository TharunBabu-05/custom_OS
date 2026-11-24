#include "text_io.h"

static int hex_digit(unsigned char c)
{
    if (c>='0' && c<='9') { return c-'0'; }
    if (c>='a' && c<='f') { return c-'a'+10; }
    if (c>='A' && c<='F') { return c-'A'+10; }
    return -1;
}

static unsigned decode(unsigned arg,unsigned char bytes[256])
{
    char text[257];
    if (uos_arg(arg,text,sizeof(text))<=0) { return 0; }
    unsigned used=0;
    for (const char *p=text;*p;) {
        unsigned char c=(unsigned char)*p++;
        if (c=='\\') {
            if (!*p) { return 0; }
            switch (*p++) {
            case 'n': c='\n';break;
            case 'r': c='\r';break;
            case 't': c='\t';break;
            case '\\': c='\\';break;
            case 'x': {
                if (!p[0] || !p[1]) { return 0; }
                int high=hex_digit((unsigned char)p[0]),low=hex_digit((unsigned char)p[1]);
                if (high<0 || low<0) { return 0; }
                c=(unsigned char)(16*high+low);
                p+=2;
                break;
            }
            default: return 0;
            }
        }
        bytes[used++]=c;
    }
    return used;
}

static int translate(int64_t input,const int16_t map[256])
{
    unsigned char bytes[TEXT_IO_BYTES];
    for (;;) {
        int64_t n=file_input_read(input,bytes,sizeof(bytes));
        if (n<0) { uos_puts("tr: read failed\n");return 2; }
        if (!n) { return 0; }
        size_t used=0;
        for (int64_t i=0;i<n;++i) {
            int16_t value=map[bytes[i]];
            if (value>=0) { bytes[used++]=(unsigned char)value; }
        }
        if (!text_write(bytes,used)) { return 2; }
    }
}

int app_main(void)
{
    int64_t argc=uos_argc();
    char option[3];
    bool remove=argc>1 && uos_arg(1,option,sizeof(option))==2 && option[0]=='-' && option[1]=='d';
    unsigned char from[256],to[256];
    if (argc!=3 && argc!=4) { goto usage; }
    unsigned count=decode(remove ? 2 : 1,from);
    if (!count || (!remove && decode(2,to)!=count)) { goto usage; }
    int16_t map[256];
    for (unsigned i=0;i<256;++i) { map[i]=(int16_t)i; }
    for (unsigned i=0;i<count;++i) { map[from[i]]=remove ? -1 : to[i]; }
    int64_t input=text_open_input(argc==4 ? 3 : 0);
    if (input<0) { uos_puts("tr: cannot open input\n");return 2; }
    return file_input_finish(input,translate(input,map));
usage:
    uos_puts("Usage: tr FROM TO [file|-] | tr -d BYTES [file|-]\n");
    return 2;
}
