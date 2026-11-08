#include "usb.h"
static bool read32(const struct xhci_observer *io,uint32_t offset,uint32_t *value)
{
    if ((offset&3) || offset>io->span || io->span-offset<4) { return false; }
    return io->read32(io->context,offset,value) && *value!=UINT32_MAX;
}
enum usb_probe_result xhci_observe(const struct xhci_observer *io,struct xhci_snapshot *out)
{
    if (!io || !io->read32 || !out || io->span<0x20) { return USB_PROBE_ARGUMENT; }
    *out=(struct xhci_snapshot){0};
    uint32_t cap,hcs,hcc,doorbell,runtime;
    if (!read32(io,0,&cap) || !read32(io,4,&hcs) || !read32(io,16,&hcc) ||
        !read32(io,20,&doorbell) || !read32(io,24,&runtime)) { return USB_PROBE_IO; }
    unsigned length=cap&255,version=cap>>16,ports=hcs>>24;
    /* DWC3 xHCI register space ends before its globals at 0xc100. */
    uint32_t limit=io->span<0xc000 ? io->span : 0xc000;
    if (length<0x20 || (length&3) || version<0x100 || version>0x120 ||
        !ports || ports>USB_MAX_PORTS || !(hcs&255) || !((hcs>>8)&0x7ff) ||
        (doorbell&~3u)<length || (doorbell&~3u)>=limit ||
        (runtime&~31u)<length || (runtime&~31u)>=limit ||
        length+0x400+ports*16>limit) { return USB_PROBE_LAYOUT; }
    out->cap_length=(uint8_t)length; out->version=(uint16_t)version;
    out->ports=(uint8_t)ports; out->slots=(uint8_t)hcs;
    out->interrupters=(uint16_t)((hcs>>8)&0x7ff);
    out->address64=(hcc&1)!=0; out->context64=(hcc&4)!=0;
    if (!read32(io,length,&out->command) || !read32(io,length+4,&out->status) ||
        !read32(io,length+8,&out->page_sizes)) { return USB_PROBE_IO; }
    for (unsigned i=0;i<ports;++i) {
        if (!read32(io,length+0x400+i*16,&out->port[i].status)) { return USB_PROBE_IO; }
    }
    uint32_t at=(hcc>>16)*4; unsigned count=0;
    while (at) {
        /* Forward-only links and a hard count bound prevent malformed chains. */
        if (at<length || at>limit-4 || ++count>64) { return USB_PROBE_LAYOUT; }
        uint32_t header;
        if (!read32(io,at,&header)) { return USB_PROBE_IO; }
        unsigned next=(header>>8)&255;
        if ((header&255)==2) { /* Supported Protocol extended capability. */
            uint32_t name,range;
            if (at>limit-16) { return USB_PROBE_LAYOUT; }
            if (!read32(io,at+4,&name) || !read32(io,at+8,&range)) { return USB_PROBE_IO; }
            unsigned first=range&255,n=(range>>8)&255,psi=range>>28;
            if (name!=0x20425355 || !first || !n || first>ports || n>ports-first+1 ||
                at+16+psi*4>limit || (next && next*4<16+psi*4)) { return USB_PROBE_LAYOUT; }
            for (unsigned i=first-1;i<first-1+n;++i) {
                if (out->port[i].protocol_major) { return USB_PROBE_LAYOUT; }
                out->port[i].protocol_major=(uint8_t)(header>>24);
                out->port[i].protocol_minor=(uint8_t)(header>>16);
            }
        }
        if (next && next*4>limit-at-4) { return USB_PROBE_LAYOUT; }
        at=next ? at+next*4 : 0;
    }
    return USB_PROBE_OK;
}
const char *usb_probe_error(enum usb_probe_result result)
{
    switch (result) {
    case USB_PROBE_OK: return "controller registers observed";
    case USB_PROBE_ARGUMENT: return "invalid observer arguments";
    case USB_PROBE_IO: return "register read failed or returned all ones";
    default: return "unsupported or malformed xHCI register layout";
    }
}
