/* Narrow BCM2711-compatible RNG200 mode used in the Pi 5 DT. No resets,
 * status clearing or fallback entropy; stop on the first failure. */
#include "rng200.h"
#include "crypto.h"
#define RNG_FAIL 0x80000020u
static enum block_result failed(struct rng200_state *s,enum block_result r)
{ s->ready=false;s->error=r;s->previous=0;s->have_previous=false;return r; }
static bool read_reg(struct rng200_state *s,unsigned at,uint32_t *v)
{ return s->io.read(s->io.context,at,v); }
static enum block_result status(struct rng200_state *s)
{
    if (!read_reg(s,0x18,&s->status) || (s->status&RNG_FAIL)) { return BLOCK_IO; }return BLOCK_OK;
}
static enum block_result word(struct rng200_state *s,uint64_t start,uint32_t *v)
{
    for (unsigned spin=0;spin<10000000;++spin) {
        enum block_result r=status(s);if (r!=BLOCK_OK) { return r; }
        if (!read_reg(s,0x24,&s->fifo_count) || s->fifo_count==UINT32_MAX) { return BLOCK_IO; }
        if (s->fifo_count&255) {
            if (!read_reg(s,0x20,v)) { return BLOCK_IO; }
            if (s->have_previous && *v==s->previous) { return BLOCK_IO; }
            s->have_previous=true;s->previous=*v;++s->words;
            return status(s);
        }
        if (s->io.ticks(s->io.context)-start>=s->io.hz) { return BLOCK_TIMEOUT; }
    }return BLOCK_TIMEOUT;
}
enum block_result rng200_init(const struct rng200_io *io,struct rng200_state *s)
{
    if (!io || !s || !io->read || !io->write || !io->ticks || !io->hz || io->hz>10000000000ULL) { return BLOCK_ARGUMENT; }
    if (s->attempted) { return s->error; }*s=(struct rng200_state){.io=*io,.attempted=true,.phase=1,.error=BLOCK_NOT_READY};
    if (!crypto_selftest() || !read_reg(s,0,&s->control) || s->control==UINT32_MAX || status(s)!=BLOCK_OK) { return failed(s,BLOCK_IO); }
    s->inherited=!!(s->control&0x1fff);s->phase=2;
    if (!s->inherited) {
        s->changed=true;
        uint32_t control=(s->control&~0x7fffu)|0x7fffu;
        if (!s->io.write(io->context,0x10,0x40000) || !s->io.write(io->context,0x24,2u<<8) || !s->io.write(io->context,0,control) || !read_reg(s,0,&s->control) || s->control!=control) { return failed(s,BLOCK_IO); }
    }
    s->phase=3;uint64_t start=io->ticks(io->context);bool warm=false;
    for (unsigned spin=0;spin<10000000;++spin) {
        if (status(s)!=BLOCK_OK || !read_reg(s,0x0c,&s->total_bits) || s->total_bits==UINT32_MAX) { return failed(s,BLOCK_IO); }
        if (s->total_bits>16) { warm=true;break; }
        if (io->ticks(io->context)-start>=io->hz) { break; }
    }
    if (!warm) { return failed(s,BLOCK_TIMEOUT); }
    s->phase=4;uint32_t ignored=0;
    for (unsigned i=0;i<16;++i) {
        enum block_result r=word(s,start,&ignored);if (r!=BLOCK_OK) { crypto_wipe(&ignored,sizeof(ignored));return failed(s,r); }++s->discarded;
    }
    crypto_wipe(&ignored,sizeof(ignored));s->ready=true;s->phase=5;s->error=BLOCK_OK;return BLOCK_OK;
}
enum block_result rng200_bytes(struct rng200_state *s,void *out,size_t n)
{
    if (!s || !out || !n || n>256) { return BLOCK_ARGUMENT; }
    if (!s->ready || s->error!=BLOCK_OK) { crypto_wipe(out,n);return BLOCK_NOT_READY; }
    unsigned char raw[64]={0},digest[32]={0};struct sha256 hash={0};enum block_result r=BLOCK_OK;
    uint64_t start=s->io.ticks(s->io.context);
    for (size_t at=0;at<n;) {
        for (unsigned i=0;i<16;++i) {
            uint32_t value=0;r=word(s,start,&value);
            if (r!=BLOCK_OK) { crypto_wipe(&value,sizeof(value));goto done; }
            for (unsigned j=0;j<4;++j) { raw[i*4+j]=(unsigned char)(value>>(8*j)); }crypto_wipe(&value,sizeof(value));
        }
        sha256_init(&hash);sha256_update(&hash,raw,sizeof(raw));sha256_final(&hash,digest);
        size_t amount=n-at;if (amount>32) { amount=32; }
        for (size_t i=0;i<amount;++i) { ((unsigned char *)out)[at+i]=digest[i]; }at+=amount;++s->blocks;
        crypto_wipe(raw,sizeof(raw));crypto_wipe(digest,sizeof(digest));crypto_wipe(&hash,sizeof(hash));
    }
done:
    crypto_wipe(raw,sizeof(raw));crypto_wipe(digest,sizeof(digest));crypto_wipe(&hash,sizeof(hash));
    if (r!=BLOCK_OK) { crypto_wipe(out,n);return failed(s,r); }return BLOCK_OK;
}
