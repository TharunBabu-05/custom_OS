#ifndef OS_PROCESS_EVENT_NAMES_H
#define OS_PROCESS_EVENT_NAMES_H
#include "user_abi.h"

static inline const char *process_event_name(unsigned event)
{
    static const char *const names[]={"unknown","start","exec","stop","reap"};
    return event<sizeof(names)/sizeof(*names) ? names[event] : names[0];
}
static inline const char *process_role_name(unsigned role)
{
    static const char *const names[]={"unknown","root","child","pipe","service"};
    return role<sizeof(names)/sizeof(*names) ? names[role] : names[0];
}
static inline const char *process_stop_name(unsigned reason)
{
    static const char *const names[]={"none","exit","fault","timeout","signal","kernel"};
    return reason<sizeof(names)/sizeof(*names) ? names[reason] : names[0];
}
#endif
