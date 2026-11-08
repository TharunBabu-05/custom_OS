/* GCC can emit memset even for freestanding aggregate initialization. */
#include <stddef.h>
void *memset(void *destination, int value, size_t count);
void *memset(void *destination, int value, size_t count)
{
    unsigned char *p=destination;
    for (size_t i=0; i<count; ++i) { p[i]=(unsigned char)value; }
    return destination;
}
