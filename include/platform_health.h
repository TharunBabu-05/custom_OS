#ifndef OS_PLATFORM_HEALTH_H
#define OS_PLATFORM_HEALTH_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct pi5_health {
    uintptr_t thermal, mailbox;
    bool rtc;
};
struct pi5_health pi5_health_discover(const void *dtb, size_t bytes);
bool pi5_temperature(uint32_t status, int32_t *millicelsius);

enum pi5_property { PI5_THROTTLED, PI5_RTC_TIME };
enum pi5_reply {
    PI5_REPLY_OK, PI5_REPLY_BUSY, PI5_REPLY_TIMEOUT, PI5_REPLY_MISMATCH,
    PI5_REPLY_REJECTED, PI5_REPLY_INVALID, PI5_REPLY_BLOCKED
};
enum { PI5_MESSAGE_WORDS = 8 };
struct pi5_mailbox {
    volatile uint32_t *message;
    uint32_t bus_address;
    bool blocked;
};
struct pi5_mailbox_io {
    uint32_t (*read)(uintptr_t base, unsigned offset);
    void (*write)(uintptr_t base, unsigned offset, uint32_t value);
    uint64_t (*ticks)(void);
    void (*barrier)(void);
};
enum pi5_reply pi5_property_read(struct pi5_mailbox *, const struct pi5_mailbox_io *,
                               uintptr_t base, uint64_t timeout_ticks, enum pi5_property, uint32_t *);
bool platform_health_command(const char *line, const void *dtb, bool pi5);
#endif
