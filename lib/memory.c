/* GCC can emit memory helpers for freestanding aggregate initialization/copies. */
#include <stddef.h>
void *memset(void *destination, int value, size_t count);
void *memcpy(void *destination, const void *source, size_t count);
void *memcpy(void *destination, const void *source, size_t count)
{
    unsigned char *out=destination;
    const unsigned char *in=source;
    for (size_t i=0; i<count; ++i) { out[i]=in[i]; }
    return destination;
}
void *memset(void *destination, int value, size_t count)
{
    unsigned char *p=destination;
    for (size_t i=0; i<count; ++i) { p[i]=(unsigned char)value; }
    return destination;
}
