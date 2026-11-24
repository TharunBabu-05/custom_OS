#include "user.h"
#include "wave.h"
#include <stdbool.h>

enum { RATE=8000, MAX_MS=250 };

static unsigned decimal(unsigned index,unsigned limit)
{
    char text[8];
    if (uos_arg(index,text,sizeof(text))<=0) { return 0; }
    unsigned value=0;
    for (const char *p=text;*p;++p) {
        if (*p<'0' || *p>'9') { return 0; }
        value=value*10+(unsigned)(*p-'0');
        if (value>limit) { return 0; }
    }
    return value;
}

int app_main(void)
{
    int64_t argc=uos_argc();
    unsigned hz[2]={decimal(1,2000),0},ms=decimal(2,MAX_MS);
    if (argc==4) { hz[1]=decimal(3,2000); }
    if ((argc!=3 && argc!=4) || hz[0]<20 || !ms || (argc==4 && hz[1]<20)) {
        uos_puts("Usage: WAVTONE <20..2000 Hz> <1..250 ms> [right Hz] > new.wav\n");
        return 2;
    }
    unsigned channels=argc==4 ? 2 : 1;
    uint32_t frames=ms*(RATE/1000);
    struct wave_info info={.channels=channels,.rate=RATE,.bits=16,.align=2*channels,.frames=frames};
    if (!wave_write_header(&info)) { return 2; }
    /* One integer phase accumulator per triangle-wave channel. */
    unsigned phase[2]={0};
    while (frames) {
        uint8_t samples[WAVE_BUFFER_BYTES];
        unsigned batch=sizeof(samples)/info.align;
        unsigned n=frames<batch ? frames : batch;
        for (unsigned i=0;i<n;++i) {
            for (unsigned c=0;c<channels;++c) {
                unsigned rising=phase[c]<=RATE/2 ? phase[c] : RATE-phase[c];
                int sample=(int)(rising*8)-16000;
                wave_put_sample(samples+i*info.align+2*c,16,sample);
                phase[c]+=hz[c];
                if (phase[c]>=RATE) { phase[c]-=RATE; }
            }
        }
        unsigned count=n*info.align;
        if (uos_write(samples,count)!=count) { return 2; }
        frames-=n;
    }
    return 0;
}
