#include "text_io.h"

static bool emit(const unsigned char *line,size_t bytes,uint64_t count,bool counts)
{
    if (counts) {
        char prefix[21];
        size_t at=sizeof(prefix)-1;
        prefix[at]=' ';
        do { prefix[--at]=(char)('0'+count%10);count/=10; } while (count);
        if (!text_write(prefix+at,sizeof(prefix)-at)) { return false; }
    }
    return text_write(line,bytes) && text_write("\n",1);
}

static int unique(struct text_reader *reader,bool counts)
{
    unsigned char lines[2][TEXT_LINE_BYTES];
    size_t bytes[2];
    bool newline;
    unsigned current=0;
    int status=text_read_line(reader,lines[current],&bytes[current],&newline);
    if (status<=0) { return status ? text_line_error("uniq",status) : 0; }
    uint64_t count=1;
    for (;;) {
        unsigned next=current^1;
        status=text_read_line(reader,lines[next],&bytes[next],&newline);
        if (status<0) { return text_line_error("uniq",status); }
        if (status && !text_compare(lines[current],bytes[current],lines[next],bytes[next])) {
            ++count;
            continue;
        }
        if (!emit(lines[current],bytes[current],count,counts)) { return 2; }
        if (!status) { return 0; }
        current=next;
        count=1;
    }
}

int app_main(void)
{
    int64_t argc=uos_argc();
    char option[3];
    bool counts=argc>1 && uos_arg(1,option,sizeof(option))==2 && option[0]=='-' && option[1]=='c';
    unsigned first=counts ? 2 : 1;
    if (argc>first+1) { uos_puts("Usage: uniq [-c] [file|-]\n");return 2; }
    struct text_reader reader={.input=text_open_input(argc>first ? first : 0)};
    if (reader.input<0) { uos_puts("uniq: cannot open input\n");return 2; }
    return file_input_finish(reader.input,unique(&reader,counts));
}
