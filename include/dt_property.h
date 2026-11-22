#ifndef OS_DT_PROPERTY_H
#define OS_DT_PROPERTY_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
enum dt_lookup { DT_INVALID,DT_MISSING,DT_FOUND };
enum { DT_PATH_CAPACITY=512 };
/* Views point into a validated blob. Visitor paths are valid during the callback. */
struct dt_node {
    const char *name;
    const unsigned char *properties,*end,*strings;
};
typedef void (*dt_visitor)(const char *,const struct dt_node *,void *);
bool dt_walk(const void *,size_t,dt_visitor,void *);
enum dt_lookup dt_find_node(const void *,size_t,const char *,struct dt_node *);
enum dt_lookup dt_find_phandle(const void *,size_t,uint32_t,struct dt_node *,char [DT_PATH_CAPACITY]);
enum dt_lookup dt_node_property(const struct dt_node *,const char *,const unsigned char **,uint32_t *);
bool dt_direct_child(const char *path,const char *parent);
static inline uint32_t dt_cell(const unsigned char *p)
{ return (uint32_t)p[0]<<24 | (uint32_t)p[1]<<16 | (uint32_t)p[2]<<8 | p[3]; }
/* Exact absolute node/property lookup; returns a slice inside the caller's DTB. */
bool dt_property(const void *blob,size_t available,const char *path,const char *property,
                 const unsigned char **value,uint32_t *length);
enum dt_lookup dt_optional_property(const void *blob,size_t available,const char *path,const char *property,
                 const unsigned char **value,uint32_t *length);
/* Zero-based direct child in DT order. The returned name is NUL-terminated. */
enum dt_lookup dt_child(const void *blob,size_t available,const char *parent,unsigned index,const char **name);
#endif
