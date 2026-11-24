#include "user.h"
#include "wave.h"
#include <stdbool.h>

enum { RATE=8000, FRAME_BYTES=2, MAX_MS=250 };

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
    unsigned hz=decimal(1,2000),ms=decimal(2,MAX_MS);
    if (uos_argc()!=3 || hz<20 || !ms) {
        uos_puts("Usage: WAVTONE <20..2000 Hz> <1..250 ms> > new.wav\n");
        return 2;
    }
    uint32_t frames=ms*(RATE/1000),bytes=frames*FRAME_BYTES;
    uint8_t header[44]={
        'R','I','F','F',0,0,0,0,'W','A','V','E','f','m','t',' ',
        16,0,0,0,1,0,1,0,0,0,0,0,0,0,0,0,2,0,16,0,'d','a','t','a',0,0,0,0
    };
    wave_put32(header+4,36+bytes);
    wave_put32(header+24,RATE);
    wave_put32(header+28,RATE*FRAME_BYTES);
    wave_put32(header+40,bytes);
    if (uos_write(header,sizeof(header))!=sizeof(header)) { return 2; }
    /* Integer phase accumulator for a symmetric triangle wave. */
    unsigned phase=0;
    while (frames) {
        uint8_t samples[512];
        unsigned n=frames<sizeof(samples)/FRAME_BYTES ? frames : sizeof(samples)/FRAME_BYTES;
        for (unsigned i=0;i<n;++i) {
            unsigned rising=phase<=RATE/2 ? phase : RATE-phase;
            int sample=(int)(rising*8)-16000;
            wave_put16(samples+i*FRAME_BYTES,(unsigned)sample);
            phase+=hz;
            if (phase>=RATE) { phase-=RATE; }
        }
        unsigned count=n*FRAME_BYTES;
        if (uos_write(samples,count)!=count) { return 2; }
        frames-=n;
    }
    return 0;
}
