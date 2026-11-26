#include "user.h"

static void number(uint64_t value)
{
    char text[21];
    unsigned at=sizeof(text)-1;
    text[at]=0;
    do { text[--at]=(char)('0'+value%10);value/=10; } while (value);
    (void)uos_puts(text+at);
}

int app_main(void)
{
    if (uos_argc()!=1) { uos_puts("Usage: ports\n");return 2; }
    struct uos_port_table table;
    if (uos_port_list(&table) || table.count>UOS_PORT_MAX) {
        uos_puts("ports: inventory failed\n");return 1;
    }
    if (!table.count) { uos_puts("No listening ports.\n");return 0; }
    uos_puts("NAME PID PENDING ACTIVE\n");
    for (unsigned i=0;i<table.count;++i) {
        struct uos_port_entry *entry=&table.entries[i];
        entry->name[UOS_PORT_NAME_MAX]=0;
        uos_puts(entry->name);uos_puts(" ");number(entry->pid);
        uos_puts(" ");number(entry->pending);uos_puts(" ");number(entry->active);uos_puts("\n");
    }
    return 0;
}
