#ifndef OS_NETWORK_SESSION_H
#define OS_NETWORK_SESSION_H
#include "netprobe.h"
enum network_session_reason { NS_NONE,NS_USER,NS_LEASE,NS_LINK,NS_LIMIT,NS_IO,NS_CONFLICT };
struct network_session {
    bool attempted,active,ended;enum network_session_reason reason;enum block_result result;
    unsigned polls,commands,pings_started,pings_ok,pings_timeout;uint64_t started,ended_at;
};
struct network_session_io {
    struct netprobe_io link;
    int (*getc)(void *);void (*write)(void *,const char *);
    bool (*random)(void *,unsigned char out[32]);
    enum block_result (*inspect)(void *);void (*info)(void *);
};
bool network_parse_ipv4(const char *,unsigned char address[4]);
/* Foreground console: services RX/ARP/M3 while reading commands. Caller must
 * deauthorize/DOWN/delete/restore after return, including every failure. */
enum block_result network_session_run(const struct network_session_io *,struct netprobe *,struct network_session *);
#endif
