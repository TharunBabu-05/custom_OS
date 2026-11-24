#include "file_input.h"
#include "wave.h"

enum operation { TRIM, GAIN, MONO, FADE, JOIN };

struct edit {
    enum operation operation;
    uint32_t first,frames,gain,fade_in,fade_out;
};

static bool same_text(const char *a,const char *b)
{
    while (*a && *a==*b) { ++a; ++b; }
    return *a==*b;
}

static bool decimal(unsigned arg,uint32_t *result)
{
    char text[11];
    if (uos_arg(arg,text,sizeof(text))<=0) { return false; }
    uint32_t value=0;
    for (const char *p=text;*p;++p) {
        if (*p<'0' || *p>'9') { return false; }
        unsigned digit=(unsigned)(*p-'0');
        if (value>(UINT32_MAX-digit)/10) { return false; }
        value=value*10+digit;
    }
    *result=value;
    return true;
}

static int64_t open_wave(unsigned arg,struct wave_info *info)
{
    char path[256];
    if (uos_arg(arg,path,sizeof(path))<=0) { return -1; }
    int64_t file=file_input_open(path);
    if (file<=0) { return -1; }
    if (wave_scan(file,info,NULL,NULL)) { return file; }
    uos_close((uint64_t)file);
    return -1;
}

static uint64_t milliseconds_to_frames(uint32_t ms,unsigned rate)
{
    return (uint64_t)ms*rate/1000;
}

static bool prepare(struct edit *edit,struct wave_info *output)
{
    uint32_t a,b;
    edit->frames=output->frames;
    switch (edit->operation) {
    case TRIM: {
        if (!decimal(2,&a) || !decimal(3,&b)) { return false; }
        uint64_t first=milliseconds_to_frames(a,output->rate);
        uint64_t count=milliseconds_to_frames(b,output->rate);
        if (!count || first+count>output->frames) { return false; }
        edit->first=(uint32_t)first;
        edit->frames=output->frames=(uint32_t)count;
        break;
    }
    case GAIN:
        if (!decimal(2,&edit->gain) || edit->gain>400) { return false; }
        break;
    case MONO:
        output->channels=1;
        output->align=output->bits/8;
        break;
    case FADE: {
        if (!decimal(2,&a) || !decimal(3,&b)) { return false; }
        uint64_t in=milliseconds_to_frames(a,output->rate);
        uint64_t out=milliseconds_to_frames(b,output->rate);
        if (in+out>output->frames) { return false; }
        edit->fade_in=(uint32_t)in;
        edit->fade_out=(uint32_t)out;
        break;
    }
    case JOIN:
        break;
    }
    return true;
}

static int gained(int sample,unsigned percent,int maximum)
{
    int scaled=sample*(int)percent/100;
    if (scaled>maximum) { return maximum; }
    if (scaled<-maximum-1) { return -maximum-1; }
    return scaled;
}

static int faded(int sample,uint32_t index,uint32_t length)
{
    if (length==1) { return 0; }
    return (int)((int64_t)sample*index/(length-1));
}

static unsigned transform(uint8_t *bytes,unsigned count,const struct wave_info *info,
                          const struct edit *edit,uint32_t first)
{
    unsigned width=info->bits/8;
    if (edit->operation==MONO && info->channels==2) {
        for (unsigned i=0;i<count;++i) {
            const uint8_t *in=bytes+i*info->align;
            int average=(wave_sample(in,info->bits)+wave_sample(in+width,info->bits))/2;
            wave_put_sample(bytes+i*width,info->bits,average);
        }
        return count*width;
    }
    if (edit->operation==GAIN || edit->operation==FADE) {
        int maximum=info->bits==8 ? 127 : 32767;
        for (unsigned i=0;i<count;++i) {
            uint32_t from_start=first+i,from_end=edit->frames-1-from_start;
            for (unsigned channel=0;channel<info->channels;++channel) {
                uint8_t *p=bytes+i*info->align+channel*width;
                int value=wave_sample(p,info->bits);
                if (edit->operation==GAIN) { value=gained(value,edit->gain,maximum); }
                else if (from_start<edit->fade_in) { value=faded(value,from_start,edit->fade_in); }
                else if (from_end<edit->fade_out) { value=faded(value,from_end,edit->fade_out); }
                wave_put_sample(p,info->bits,value);
            }
        }
    }
    return count*info->align;
}

static bool write_frames(int64_t file,const struct wave_info *info,const struct edit *edit)
{
    uint8_t buffer[WAVE_BUFFER_BYTES];
    uint32_t offset=info->data_offset+edit->first*info->align;
    if (uos_seek((uint64_t)file,offset,UOS_SEEK_SET)!=offset) { return false; }
    unsigned batch=sizeof(buffer)/info->align;
    for (uint32_t at=0;at<edit->frames;) {
        uint32_t count=edit->frames-at;
        if (count>batch) { count=batch; }
        if (!wave_read_exact(file,buffer,count*info->align)) { return false; }
        unsigned bytes=transform(buffer,count,info,edit,at);
        if (uos_write(buffer,bytes)!=bytes) { return false; }
        at+=count;
    }
    return true;
}

static int run(enum operation operation,unsigned input_arg)
{
    struct wave_info input={0},right={0};
    int64_t file=open_wave(input_arg,&input);
    if (file<0) { return 2; }
    struct wave_info output=input;
    struct edit edit={.operation=operation};
    bool valid=prepare(&edit,&output);
    int64_t second=0;
    if (operation==JOIN) {
        second=open_wave(input_arg+1,&right);
        valid=second>0 && right.rate==input.rate && right.bits==input.bits && right.channels==input.channels;
        output.frames+=right.frames;
    }
    bool written=valid && wave_write_header(&output) && write_frames(file,&input,&edit);
    if (written && second) {
        edit.frames=right.frames;
        written=write_frames(second,&right,&edit);
    }
    if (written && ((output.frames*output.align)&1)) {
        const uint8_t pad=0;
        written=uos_write(&pad,1)==1;
    }
    int result=file_input_finish(file,written ? 0 : 2);
    return second>0 ? file_input_finish(second,result) : result;
}

int app_main(void)
{
    static const struct { const char *name; enum operation operation; unsigned argc,input; } commands[]={
        {"trim",TRIM,5,4},{"gain",GAIN,4,3},{"mono",MONO,3,2},{"fade",FADE,5,4},{"join",JOIN,4,2}
    };
    int64_t argc=uos_argc();
    char name[8]={0};
    if (argc==1 || (argc==2 && uos_arg(1,name,sizeof(name))==4 && same_text(name,"help"))) {
        uos_puts("WAVEDIT trim <start-ms> <length-ms> <file> > new.wav\n"
                 "WAVEDIT gain <0..400 percent> <file> > new.wav\n"
                 "WAVEDIT mono <file> > new.wav\n"
                 "WAVEDIT fade <in-ms> <out-ms> <file> > new.wav\n"
                 "WAVEDIT join <left> <right> > new.wav\n");
        return 0;
    }
    if (uos_arg(1,name,sizeof(name))<=0) { return 2; }
    for (unsigned i=0;i<sizeof(commands)/sizeof(commands[0]);++i) {
        if (same_text(name,commands[i].name) && argc==commands[i].argc) {
            return run(commands[i].operation,commands[i].input);
        }
    }
    return 2;
}
