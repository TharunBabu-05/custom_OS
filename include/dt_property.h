#ifndef OS_DT_PROPERTY_H
#define OS_DT_PROPERTY_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* Exact absolute node/property lookup; returns a slice inside the caller's DTB. */
bool dt_property(const void *blob,size_t available,const char *path,const char *property,
                 const unsigned char **value,uint32_t *length);
#endif
