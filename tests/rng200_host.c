#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rng200.h"
#include "dt_property.h"
#include "crypto.h"
#include "host_assert.h"
static struct { uint32_t control,value;unsigned mode,calls,fail_at,reads,writes,data_words;uint64_t time; } m;
void console_write(const char *text) { (void)text; }
static bool touch(void) { return ++m.calls!=m.fail_at; }
static bool rd(void *ctx,unsigned at,uint32_t *v)
{
    (void)ctx;if (!touch()) { return false; }++m.reads;
    switch (at) {
    case 0:*v=m.mode==1 ? UINT32_MAX : m.control;break;
    case 0x18:*v=m.mode==2 || (m.mode==8 && m.data_words>35) ? 0x80000000 : m.mode==3 ? 32 : 0;break;
    case 0x0c:*v=m.mode==4 ? 0 : m.mode==5 ? UINT32_MAX : 64;break;
    case 0x24:*v=m.mode==6 ? 0 : m.mode==7 ? UINT32_MAX : 16;break;
    case 0x20:
        ++m.data_words;if (m.mode==9 || (m.mode==10 && m.data_words>35)) { *v=0x12345678; }
        else { m.value=m.value*1664525u+1013904223u;*v=m.value; }break;
    default:assert(0);
    }return true;
}
static bool wr(void *ctx,unsigned at,uint32_t v)
{
    (void)ctx;if (!touch()) { return false; }++m.writes;
    if (at==0) { assert((v&0x7fff)==0x7fff);m.control=m.mode==11 ? 0 : v; }
    else { assert((at==0x10 && v==0x40000) || (at==0x24 && v==512)); }return true;
}
static uint64_t ticks(void *ctx) { (void)ctx;return ++m.time; }
static void seed(bool running) { memset(&m,0,sizeof(m));m.control=running ? 0x12347fff : 0x100000; }
int main(void)
{
    FILE *f=fopen(".dist/rpi5-firmware/bcm2712-rpi-5-b.dtb","rb");assert(f && !fseek(f,0,SEEK_END));long bytes=ftell(f);rewind(f);unsigned char *dt=malloc((size_t)bytes);assert(dt && fread(dt,1,(size_t)bytes,f)==(size_t)bytes);fclose(f);
    uint64_t address=0;assert(pi5_rng200_description(dt,(size_t)bytes,&address) && address==0x107d208000ULL);
    const unsigned char *p;uint32_t n;assert(dt_property(dt,(size_t)bytes,"/soc@107c000000/rng@7d208000","reg",&p,&n));
    unsigned char *reg=dt+(p-dt);reg[0]^=1;assert(!pi5_rng200_description(dt,(size_t)bytes,&address));reg[0]^=1;
    assert(dt_property(dt,(size_t)bytes,"/soc@107c000000","ranges",&p,&n));unsigned char *ranges=dt+(p-dt);ranges[7]^=1;assert(!pi5_rng200_description(dt,(size_t)bytes,&address));ranges[7]^=1;
    assert(!pi5_rng200_description(dt,40,&address));free(dt);
    struct rng200_io io={NULL,rd,wr,ticks,1000};struct rng200_state s={0};unsigned char first[256],second[256];
    seed(false);assert(rng200_init(&io,&s)==BLOCK_OK && s.ready && s.discarded==16 && m.writes==3 && s.control==0x107fff);
    unsigned init_calls=m.calls;assert(rng200_bytes(&s,first,256)==BLOCK_OK && s.blocks==8);
    unsigned total=m.calls;assert(rng200_bytes(&s,second,256)==BLOCK_OK && !crypto_equal(first,second,256));
    unsigned calls=m.calls;assert(rng200_init(&io,&s)==BLOCK_OK && m.calls==calls);
    for (unsigned mode=1;mode<=11;++mode) {
        seed(false);m.mode=mode;s=(struct rng200_state){0};enum block_result r=rng200_init(&io,&s);
        if (mode==8 || mode==10) { assert(r==BLOCK_OK);memset(first,0xa5,64);assert(rng200_bytes(&s,first,64)==BLOCK_IO && !s.ready);for (unsigned i=0;i<64;++i) { assert(!first[i]); } }
        else { assert(r!=BLOCK_OK && !s.ready); }
        calls=m.calls;assert(rng200_init(&io,&s)!=BLOCK_OK && m.calls==calls);memset(first,0xa5,32);assert(rng200_bytes(&s,first,32)==BLOCK_NOT_READY && m.calls==calls);for (unsigned i=0;i<32;++i) { assert(!first[i]); }
    }
    seed(true);s=(struct rng200_state){0};assert(rng200_init(&io,&s)==BLOCK_OK && s.inherited && !m.writes);
    calls=m.calls;assert(rng200_bytes(&s,first,0)==BLOCK_ARGUMENT && rng200_bytes(&s,first,257)==BLOCK_ARGUMENT && m.calls==calls);
    for (unsigned fail=1;fail<=total;++fail) {
        seed(false);s=(struct rng200_state){0};m.fail_at=fail;enum block_result r=rng200_init(&io,&s);
        if (fail<=init_calls) { assert(r!=BLOCK_OK && !s.ready && m.calls==fail); }
        else { assert(r==BLOCK_OK);memset(first,0xa5,256);assert(rng200_bytes(&s,first,256)!=BLOCK_OK && !s.ready && m.calls==fail);for (unsigned i=0;i<256;++i) { assert(!first[i]); } }
    }
    printf("PASS: real Pi DT RNG translation/bounds, cold/inherited setup, warmup/discard, status/repeat/timeout fail-closed checks and %u injected register I/O failures\n",total);
    puts("PASS: fresh hardware-word conditioning, output erasure on partial failure, no source access after failure and no time/MAC fallback (mock randomness is test-only)");return 0;
}
