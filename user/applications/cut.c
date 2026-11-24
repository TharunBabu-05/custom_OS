#include "text_io.h"

struct cut_options { unsigned first,last,path_arg;unsigned char delimiter; };

static bool field_range(char *range,struct cut_options *out)
{
    char *end=range;
    while (*end && *end!='-') { ++end; }
    bool interval=*end=='-';
    *end=0;
    if (!text_count(range,TEXT_LINE_BYTES+1,&out->first) || !out->first) { return false; }
    out->last=out->first;
    return !interval || (text_count(end+1,TEXT_LINE_BYTES+1,&out->last) && out->last>=out->first);
}

static bool options(struct cut_options *out)
{
    int64_t argc=uos_argc();
    bool have_delimiter=false;
    out->delimiter='\t';
    for (unsigned i=1;i<argc;++i) {
        char option[3];
        if (uos_arg(i,option,sizeof(option))!=2 || option[0]!='-' ||
            (option[1]!='d' && option[1]!='f')) {
            out->path_arg=i;
            return out->first && i+1==argc;
        }
        if (++i==argc) { return false; }
        if (option[1]=='d') {
            char byte[2];
            if (have_delimiter || uos_arg(i,byte,sizeof(byte))!=1 || byte[0]=='\n') { return false; }
            out->delimiter=(unsigned char)byte[0];
            have_delimiter=true;
        } else {
            char range[12];
            if (out->first || uos_arg(i,range,sizeof(range))<=0 || !field_range(range,out)) { return false; }
        }
    }
    return out->first!=0;
}

static bool select_fields(const struct cut_options *args,const unsigned char *line,size_t bytes,bool newline)
{
    size_t at=0;
    while (at<bytes && line[at]!=args->delimiter) { ++at; }
    size_t start=0,end=bytes;
    if (at<bytes) {
        unsigned field=1;
        for (;at<bytes;++at) {
            if (line[at]!=args->delimiter) { continue; }
            if (field==args->last) { end=at;break; }
            if (++field==args->first) { start=at+1; }
        }
        if (field<args->first) { start=end; }
    }
    return text_write(line+start,end-start) && (!newline || text_write("\n",1));
}

int app_main(void)
{
    struct cut_options args={0};
    if (!options(&args)) { uos_puts("Usage: cut [-d BYTE] -f N[-M] [file|-]\n");return 2; }
    struct text_reader reader={.input=text_open_input(args.path_arg)};
    if (reader.input<0) { uos_puts("cut: cannot open input\n");return 2; }
    unsigned char line[TEXT_LINE_BYTES];
    size_t bytes;
    bool newline;
    int status;
    while ((status=text_read_line(&reader,line,&bytes,&newline))>0) {
        if (!select_fields(&args,line,bytes,newline)) { return file_input_finish(reader.input,2); }
    }
    return file_input_finish(reader.input,status ? text_line_error("cut",status) : 0);
}
