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
    console_write(s->phase>=XHCI_STORAGE_CONFIG ? "\nUSB read-only storage setup attempted; no disk writes.\n" : s->usb_requests ? "\nUSB enumeration requests only; no storage commands or disk writes.\n" : "\nNo USB transfers or disk writes were issued.\n");
    if (s->attempted) { console_write("Reboot to restore firmware USB state and repeat this test.\n"); }
}
void usb_device_report(const struct usb_device_info *d)
{
    console_write("PASS: USB device and configuration descriptors read.\n");
    console_write("port="); console_u64(d->port); console_write(" slot="); console_u64(d->slot);
    console_write(" speed_id="); console_u64(d->speed); console_write(" ep0_packet="); console_u64(d->max_packet);
    console_write("\nUSB VID="); console_hex(d->vendor); console_write(" PID="); console_hex(d->product);
    console_write(" bcdUSB="); console_hex(d->usb_version); console_write(" device_class="); console_hex(d->device_class);
    console_write(" configurations="); console_u64(d->configurations); console_write("\n");
    if (d->mass_storage) {
        console_write("USB mass storage BOT interface="); console_u64(d->interface_number);
        console_write(" bulk_in="); console_hex(d->bulk_in); console_write(" bulk_out="); console_hex(d->bulk_out);
        console_write(" in_packet="); console_u64(d->bulk_in_packet); console_write(" out_packet="); console_u64(d->bulk_out_packet); console_write("\n");
    }
    console_write(d->configured ? "Configuration and bulk endpoints active for read-only storage.\n" : "Descriptors only; configuration/endpoints/storage are not activated.\n");
}
