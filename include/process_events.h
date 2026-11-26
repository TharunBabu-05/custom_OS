#ifndef OS_PROCESS_EVENTS_H
#define OS_PROCESS_EVENTS_H
#include "user_abi.h"

enum { PROCESS_EVENT_CAPACITY=32 };
/* Boot-CPU process lifecycle only. Calls do not yield.
 * scope=0 and self=0 select the monitor's unfiltered history. */
void process_event_append(uint64_t scope,struct uos_process_event event);
void process_events_read(uint64_t after,uint64_t scope,uint64_t self,struct uos_process_events *out);
#endif
