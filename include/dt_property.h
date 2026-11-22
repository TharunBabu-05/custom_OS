#ifndef OS_DT_PROPERTY_H
#define OS_DT_PROPERTY_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
enum dt_lookup { DT_INVALID,DT_MISSING,DT_FOUND };
/* Exact absolute node/property lookup; returns a slice inside the caller's DTB. */
bool dt_property(const void *blob,size_t available,const char *path,const char *property,
                 const unsigned char **value,uint32_t *length);
enum dt_lookup dt_optional_property(const void *blob,size_t available,const char *path,const char *property,
                 const unsigned char **value,uint32_t *length);
/* Zero-based direct child in DT order. The returned name is NUL-terminated. */
enum dt_lookup dt_child(const void *blob,size_t available,const char *parent,unsigned index,const char **name);
#endif
