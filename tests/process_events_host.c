#include "process_events.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    struct uos_process_events page;
    process_events_read(0,0,0,&page);
    assert(page.version==UOS_PROCESS_EVENT_VERSION && !page.count && !page.latest && !page.cursor);

    for (unsigned i=1;i<=40;++i) {
        uint64_t scope=i%3;
        process_event_append(scope,(struct uos_process_event){
            .pid=scope ? scope+1 : 1,.event=UOS_PROCESS_EVENT_START,.uptime_ms=i*10
        });
    }
    uint64_t cursor=0;
    for (unsigned i=0;i<4;++i) {
        process_events_read(cursor,0,0,&page);
        assert(page.count==8 && page.latest==40 && page.expired==(i ? 0 : 8));
        assert(page.cursor==16+i*8);
        for (unsigned j=0;j<page.count;++j) {
            uint64_t sequence=9+i*8+j;
            assert(page.rows[j].sequence==sequence && page.rows[j].uptime_ms==sequence*10);
        }
        cursor=page.cursor;
    }

    process_events_read(10,1,2,&page);
    assert(page.count==8 && page.cursor==34 && page.rows[0].sequence==13);
    process_events_read(page.cursor,1,2,&page);
    assert(page.count==2 && page.cursor==40 && page.rows[0].sequence==37 && page.rows[1].sequence==40);
    assert(!page.rows[2].sequence);
    process_events_read(0,999,2,&page);
    assert(!page.count && page.cursor==40 && page.expired==8);

    process_events_read(13,0,1,&page);
    assert(page.count==8 && page.rows[0].sequence==15 && page.cursor==36);
    for (unsigned i=0;i<page.count;++i) { assert(page.rows[i].pid==1); }
    process_events_read(40,0,1,&page);
    assert(!page.count && page.cursor==40 && !page.expired);
    puts("PASS: process event wrap, paging, order, session and service visibility.");
}
