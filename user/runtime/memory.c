/* Compiler-emitted structure initialization/copies, without a hosted libc. */
#include <stddef.h>
void *memset(void *target,int value,size_t bytes);
void *memcpy(void *target,const void *source,size_t bytes);
void *memset(void *target,int value,size_t bytes)
{ volatile unsigned char *p=target;while (bytes--) { *p++=(unsigned char)value; }return target; }
void *memcpy(void *target,const void *source,size_t bytes)
{ volatile unsigned char *d=target;const unsigned char *s=source;while (bytes--) { *d++=*s++; }return target; }
