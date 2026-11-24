#include "file_input.h"
#include "wave.h"
struct channel_stats { int min,max; unsigned peak,clipped; };

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

static void measure(const uint8_t *p,size_t bytes,const struct wave_info *info,void *context)
{
    struct channel_stats *channels=context;
    const int maximum=info->bits==8 ? 127 : 32767;
    for (size_t at=0;at<bytes;at+=info->align) {
        for (unsigned i=0;i<info->channels;++i) {
            int n=wave_sample(p+at+i*(info->bits/8),info->bits);
            struct channel_stats *c=&channels[i];
            if (n<c->min) { c->min=n; }
            if (n>c->max) { c->max=n; }
            unsigned magnitude=(unsigned)(n<0 ? -n : n);
            if (magnitude>c->peak) { c->peak=magnitude; }
            c->clipped+=n==maximum || n==-maximum-1;
        }
    }
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
    int result=file_input_finish(file,wave_scan(file,&info,stats ? measure : NULL,channels) ? 0 : 2);
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
