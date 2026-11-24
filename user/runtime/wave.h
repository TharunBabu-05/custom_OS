#ifndef OUROS_WAVE_H
#define OUROS_WAVE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { WAVE_BUFFER_BYTES=512, WAVE_HEADER_BYTES=44, WAVE_MAX_RIFF_BYTES=16*1024*1024 };

struct wave_info {
    unsigned channels,rate,bits,align;
    uint32_t frames,data_offset;
};

typedef void (*wave_visitor)(const uint8_t *,size_t,const struct wave_info *,void *);

bool wave_read_exact(int64_t input,uint8_t *out,size_t bytes);
bool wave_scan(int64_t input,struct wave_info *info,wave_visitor visit,void *context);
bool wave_write_header(const struct wave_info *info);

static inline unsigned wave_u16(const uint8_t *p)
{
    return (unsigned)p[0]|(unsigned)p[1]<<8;
}

static inline uint32_t wave_u32(const uint8_t *p)
{
    return wave_u16(p)|(uint32_t)wave_u16(p+2)<<16;
}

static inline void wave_put16(uint8_t *p,unsigned value)
{
    p[0]=(uint8_t)value;
    p[1]=(uint8_t)(value>>8);
}

static inline void wave_put32(uint8_t *p,uint32_t value)
{
    wave_put16(p,value);
    wave_put16(p+2,value>>16);
}

static inline int wave_sample(const uint8_t *p,unsigned bits)
{
    if (bits==8) { return (int)p[0]-128; }
    int raw=(int)wave_u16(p);
    return raw-(raw&0x8000 ? 65536 : 0);
}

static inline void wave_put_sample(uint8_t *p,unsigned bits,int value)
{
    if (bits==8) { p[0]=(uint8_t)(value+128); }
    else { wave_put16(p,(unsigned)value); }
}
#endif
