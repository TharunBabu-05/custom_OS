#include "process_events.h"
#include <stdbool.h>

static struct {
    uint64_t scope;
    struct uos_process_event event;
} history[PROCESS_EVENT_CAPACITY];
static uint64_t latest;

void process_event_append(uint64_t scope,struct uos_process_event event)
{
    if (latest==UINT64_MAX) { return; }
    unsigned slot=(unsigned)(latest%PROCESS_EVENT_CAPACITY);
    event.sequence=++latest;
    history[slot].scope=scope;
    history[slot].event=event;
}

void process_events_read(uint64_t after,uint64_t scope,uint64_t self,struct uos_process_events *out)
{
    *out=(struct uos_process_events){.cursor=after,.latest=latest,.version=UOS_PROCESS_EVENT_VERSION};
    uint64_t floor=latest>PROCESS_EVENT_CAPACITY ? latest-PROCESS_EVENT_CAPACITY : 0;
    if (after<floor) {
        out->expired=floor-after;
        out->cursor=floor;
    }
    while (out->cursor<latest && out->count<UOS_PROCESS_EVENT_ROWS) {
        unsigned slot=(unsigned)(out->cursor%PROCESS_EVENT_CAPACITY);
        ++out->cursor;
        const struct uos_process_event *event=&history[slot].event;
        bool visible=scope ? history[slot].scope==scope :
                     self ? event->pid==self : true;
        if (visible) { out->rows[out->count++]=*event; }
    }
}
