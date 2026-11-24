#ifndef OUROS_WAVE_H
#define OUROS_WAVE_H
#include <stdint.h>

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
#endif
