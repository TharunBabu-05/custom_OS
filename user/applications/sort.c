#include "text_io.h"
#include "elf_app.h"

enum { SORT_BYTES=32768, SORT_LINES=1024 };
struct sort_record { uint16_t offset,bytes; };
struct sort_input {
    unsigned char data[SORT_BYTES];
    struct sort_record records[SORT_LINES];
};

static bool greater(const struct sort_input *input,unsigned a,unsigned b,bool reverse)
{
    struct sort_record left=input->records[a],right=input->records[b];
    int order=text_compare(input->data+left.offset,left.bytes,input->data+right.offset,right.bytes);
    return reverse ? order<0 : order>0;
}

static void swap(struct sort_input *input,unsigned a,unsigned b)
{
    struct sort_record save=input->records[a];
    input->records[a]=input->records[b];
    input->records[b]=save;
}

static void sift(struct sort_input *input,unsigned root,unsigned count,bool reverse)
{
    while (2*root+1<count) {
        unsigned child=2*root+1;
        if (child+1<count && greater(input,child+1,child,reverse)) { ++child; }
        if (!greater(input,child,root,reverse)) { break; }
        swap(input,root,child);
        root=child;
    }
}

static int sorted(struct text_reader *reader,struct sort_input *input,bool reverse)
{
    unsigned char line[TEXT_LINE_BYTES];
    size_t bytes,used=0;
    unsigned count=0;
    bool newline;
    int status;
    while ((status=text_read_line(reader,line,&bytes,&newline))>0) {
        if (count==SORT_LINES || bytes+newline>SORT_BYTES-used) {
            uos_puts("sort: input exceeds 32768 bytes or 1024 lines\n");return 2;
        }
        input->records[count++]=(struct sort_record){(uint16_t)used,(uint16_t)bytes};
        for (size_t i=0;i<bytes;++i) { input->data[used+i]=line[i]; }
        used+=bytes+newline;
    }
    if (status) { return text_line_error("sort",status); }

    /* Sort the record index in place using a heap. */
    for (unsigned i=count/2;i>0;--i) { sift(input,i-1,count,reverse); }
    for (unsigned i=count;i>1;--i) {
        swap(input,0,i-1);
        sift(input,0,i-1,reverse);
    }
    for (unsigned i=0;i<count;++i) {
        struct sort_record record=input->records[i];
        if (!text_write(input->data+record.offset,record.bytes) || !text_write("\n",1)) { return 2; }
    }
    return 0;
}

int app_main(void)
{
    int64_t argc=uos_argc();
    char option[3];
    bool reverse=argc>1 && uos_arg(1,option,sizeof(option))==2 && option[0]=='-' && option[1]=='r';
    unsigned first=reverse ? 2 : 1;
    if (argc>first+1) { uos_puts("Usage: sort [-r] [file|-]\n");return 2; }
    struct text_reader reader={.input=text_open_input(argc>first ? first : 0)};
    if (reader.input<0) { uos_puts("sort: cannot open input\n");return 2; }
    unsigned pages=(sizeof(struct sort_input)+APP_PAGE_BYTES-1)/APP_PAGE_BYTES;
    int64_t storage=uos_alloc_pages(pages);
    if (storage<=0) { uos_puts("sort: cannot allocate input buffer\n");return file_input_finish(reader.input,2); }
    struct sort_input *input=(void *)(uintptr_t)storage;
    int result=sorted(&reader,input,reverse);
    if (uos_free_pages(input)) { result=2; }
    return file_input_finish(reader.input,result);
}
