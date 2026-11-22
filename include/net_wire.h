#ifndef OS_NET_WIRE_H
#define OS_NET_WIRE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static inline unsigned net_be16(const unsigned char *p)
{ return (unsigned)p[0]<<8|p[1]; }
static inline bool net_equal(const unsigned char *a,const unsigned char *b,size_t n)
{
    for (size_t i=0;i<n;++i) { if (a[i]!=b[i]) { return false; } }
    return true;
}
static inline unsigned net_checksum(const unsigned char *p,size_t n,uint32_t sum)
{
    for (size_t i=0;i<n;i+=2) { sum+=(unsigned)p[i]<<8|(i+1<n ? p[i+1] : 0); }
    while (sum>>16) { sum=(sum&65535)+(sum>>16); }
    return (~sum)&65535;
}
static inline unsigned net_udp_checksum(const unsigned char *v,const unsigned char *u,size_t n)
{
    uint32_t sum=17+(unsigned)n;
    for (unsigned i=12;i<20;i+=2) { sum+=net_be16(v+i); }
    return net_checksum(u,n,sum);
}
#endif
