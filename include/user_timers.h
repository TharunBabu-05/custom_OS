#ifndef OS_USER_TIMERS_H
#define OS_USER_TIMERS_H
#include "user_abi.h"
#include <stdbool.h>

/* Boot-CPU scheduler calls only. Time inputs share the monotonic ms clock. */
int64_t user_timer_open(uint64_t owner);
int64_t user_timer_arm(uint64_t owner,uint64_t token,uint64_t now,uint64_t value,uint64_t interval,bool absolute);
int64_t user_timer_disarm(uint64_t owner,uint64_t token);
int64_t user_timer_info(uint64_t owner,uint64_t token,uint64_t now,struct uos_timer_info *out);
int64_t user_timer_read(uint64_t owner,uint64_t token,uint64_t now,uint64_t *out);
int64_t user_timer_close(uint64_t owner,uint64_t token);
void user_timers_close_owner(uint64_t owner);
unsigned user_timers_count(void);
#endif
