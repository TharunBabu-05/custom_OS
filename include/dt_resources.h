#ifndef OS_DT_RESOURCES_H
#define OS_DT_RESOURCES_H
#include "dt_property.h"

enum dt_state { DT_ENABLED, DT_DISABLED, DT_BAD_STATUS };
enum dt_reg_result { DT_REG_END, DT_REG_RAW, DT_REG_LOCAL, DT_REG_CPU };
struct dt_reg_range { uint64_t address,bytes,cpu; };
bool dt_parent_path(char *);
enum dt_state dt_local_state(const struct dt_node *);
enum dt_state dt_device_state(const void *,size_t,const char *);
enum dt_reg_result dt_reg_range(const void *,size_t,const char *,const struct dt_node *,
                               unsigned,struct dt_reg_range *);
/* Translate a span expressed in the named bus's address space to the root. */
bool dt_bus_translate(const void *,size_t,const char *,const char *property,
                      uint64_t address,uint64_t size,uint64_t *cpu);
struct board_info;
bool device_inventory_command(const char *,const void *,const struct board_info *);
#endif
