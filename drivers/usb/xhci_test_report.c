#include "xhci_test.h"
#include "console.h"
void xhci_test_report(enum xhci_test_result r,const struct xhci_test_state *s)
{
    console_write(xhci_test_error(r)); console_write("\nphase="); console_u64(s->phase);
    console_write(" completed="); console_u64(s->completed); console_write(" scratchpads="); console_u64(s->scratchpads);
    console_write(s->attempted ? " controller_changed=yes" : " controller_changed=no");
    console_write(s->halted ? " halted=yes\n" : " halted=not-confirmed\n");
    console_write("command_dma="); console_hex(s->command_dma); console_write(" event_dma="); console_hex(s->event_dma);
    console_write("\nUSBSTS="); console_hex(s->last_status); console_write(" event_type="); console_u64(s->last_event_type);
    console_write(" completion="); console_u64(s->last_completion); console_write(" pointer="); console_hex(s->last_pointer);
    console_write("\nNo USB transfers or disk writes were issued.\n");
    if (s->attempted) { console_write("Reboot to restore firmware USB state and repeat this test.\n"); }
}
