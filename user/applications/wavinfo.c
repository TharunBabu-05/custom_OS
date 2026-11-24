#include "file_input.h"
#include "wave.h"
#include <stdbool.h>

enum { BUFFER_BYTES=512, MAX_RIFF_BYTES=16*1024*1024 };
struct wave_info { unsigned channels,rate,bits,align; uint32_t frames; };
struct channel_stats { int min,max; unsigned peak,clipped; };

static bool id(const uint8_t *bytes,const char *name)
{
    for (unsigned i=0;i<4;++i) {
        if (bytes[i]!=(uint8_t)name[i]) { return false; }
    }
    return true;
}

static bool stats_option(void)
{
    const char expected[]="--stats";
    char text[sizeof(expected)];
    if (uos_arg(1,text,sizeof(text))!=sizeof(expected)-1) { return false; }
    for (unsigned i=0;i<sizeof(expected);++i) {
        if (text[i]!=expected[i]) { return false; }
    }
    return true;
}

static bool read_exact(int64_t file,uint8_t *out,size_t bytes)
{
    while (bytes) {
        int64_t n=file_input_read(file,out,bytes);
        if (n<=0) { return false; }
        bytes-=(size_t)n;
        out+=n;
    }
    return true;
}

static bool format(const uint8_t *p,struct wave_info *info)
{
    info->channels=wave_u16(p+2);
    info->rate=wave_u32(p+4);
    info->align=wave_u16(p+12);
    info->bits=wave_u16(p+14);
    return wave_u16(p)==1 && info->channels>=1 && info->channels<=2 &&
        (info->bits==8 || info->bits==16) && info->rate && info->rate<=192000 &&
        info->align==info->channels*(info->bits/8) && wave_u32(p+8)==info->rate*info->align;
}

static void measure(const uint8_t *p,size_t bytes,const struct wave_info *info,
                    struct channel_stats *channels)
{
    const int maximum=info->bits==8 ? 127 : 32767;
    for (size_t at=0;at<bytes;at+=info->align) {
        for (unsigned i=0;i<info->channels;++i) {
            const uint8_t *sample=p+at+i*(info->bits/8);
            int n=(int)sample[0]-128;
            if (info->bits==16) {
                int raw=(int)wave_u16(sample);
                n=raw-(raw&0x8000 ? 65536 : 0);
            }
            struct channel_stats *c=&channels[i];
            if (n<c->min) { c->min=n; }
            if (n>c->max) { c->max=n; }
            unsigned magnitude=(unsigned)(n<0 ? -n : n);
            if (magnitude>c->peak) { c->peak=magnitude; }
            c->clipped+=n==maximum || n==-maximum-1;
        }
    }
}

static bool read_wave(int64_t file,struct wave_info *info,struct channel_stats *channels)
{
    uint8_t buffer[BUFFER_BYTES];
    if (!read_exact(file,buffer,12) || !id(buffer,"RIFF") || !id(buffer+8,"WAVE")) { return false; }
    uint32_t size=wave_u32(buffer+4);
    if (size<4 || size>MAX_RIFF_BYTES) { return false; }
    uint32_t remaining=size-4;
    bool have_format=false,have_data=false;
    while (remaining) {
        if (remaining<8 || !read_exact(file,buffer,8)) { return false; }
        remaining-=8;
        uint32_t chunk=wave_u32(buffer+4),pad=chunk&1;
        if (chunk>remaining || pad>remaining-chunk) { return false; }
        remaining-=chunk+pad;
        bool data=id(buffer,"data");
        if (id(buffer,"fmt ")) {
            if (have_format || (chunk!=16 && chunk!=18) || !read_exact(file,buffer,chunk) ||
                (chunk==18 && wave_u16(buffer+16)) || !format(buffer,info)) { return false; }
            have_format=true;
            continue;
        }
        if (data) {
            if (!have_format || have_data || chunk%info->align) { return false; }
            have_data=true;
            info->frames=chunk/info->align;
        }
        while (chunk) {
            size_t count=chunk<sizeof(buffer) ? chunk : sizeof(buffer);
            if (!read_exact(file,buffer,count)) { return false; }
            if (data && channels) { measure(buffer,count,info,channels); }
            chunk-=(uint32_t)count;
        }
        if (pad && !read_exact(file,buffer,1)) { return false; }
    }
    return have_data && file_input_read(file,buffer,1)==0;
}

static bool number(const char *label,int64_t value)
{
    char text[21];
    unsigned at=sizeof(text);
    uint64_t n=(uint64_t)(value<0 ? -value : value);
    do { text[--at]=(char)('0'+n%10); n/=10; } while (n);
    if (value<0) { text[--at]='-'; }
    return uos_puts(label)==(int64_t)uos_length(label) &&
        uos_write(text+at,sizeof(text)-at)==(int64_t)(sizeof(text)-at);
}

int app_main(void)
{
    int64_t argc=uos_argc();
    char path[256];
    bool stats=argc==3;
    if ((argc!=2 && !stats) || (stats && !stats_option()) ||
        uos_arg(stats ? 2 : 1,path,sizeof(path))<=0) {
        uos_puts("Usage: WAVINFO [--stats] <file|->\n");
        return 2;
    }
    int64_t file=file_input_open(path);
    if (file<0) { uos_puts("WAVINFO: cannot open input\n"); return 2; }
    struct wave_info info={0};
    struct channel_stats channels[2]={{.min=32767,.max=-32768},{.min=32767,.max=-32768}};
    int result=file_input_finish(file,read_wave(file,&info,stats ? channels : NULL) ? 0 : 2);
    if (result) { uos_puts("WAVINFO: invalid, unsupported or unreadable PCM WAV\n"); return result; }
    if (!number("PCM channels=",info.channels) || !number(" rate_hz=",info.rate) ||
        !number(" bits=",info.bits) || !number(" frames=",info.frames) ||
        !number(" duration_ms=",1000LL*info.frames/info.rate) || uos_puts("\n")!=1) { return 2; }
    for (unsigned i=0;stats && i<info.channels;++i) {
        const struct channel_stats *c=&channels[i];
        if (!number("channel=",i+1) || !number(" min=",info.frames ? c->min : 0) ||
            !number(" max=",info.frames ? c->max : 0) || !number(" peak=",c->peak) ||
            !number(" clipped=",c->clipped) || uos_puts("\n")!=1) { return 2; }
    }
    return 0;
}
