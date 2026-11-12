#ifndef OS_EXCEPTION_H
#define OS_EXCEPTION_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
struct exception_frame { uint64_t x[31],elr,spsr,esr,far,vector; };
_Static_assert(sizeof(struct exception_frame)==288,"vector frame size");
_Static_assert(offsetof(struct exception_frame,elr)==248,"vector ELR offset");
void exception_dispatch(struct exception_frame *);
void exception_frame_fatal(const struct exception_frame *);
bool exception_command(const char *);
const char *exception_class(uint64_t esr);
#endif
