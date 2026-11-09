#include "xhci_test.h"
static uint16_t le16(const unsigned char *p) { return (uint16_t)(p[0]|(unsigned)p[1]<<8); }
static void save_interface(struct usb_device_info *out,uint8_t interface,uint8_t in,uint8_t epout,uint16_t in_packet,uint16_t out_packet)
{
    if (out->mass_storage || !in || !epout) { return; }
    out->mass_storage=true; out->interface_number=interface; out->bulk_in=in; out->bulk_out=epout;
    out->bulk_in_packet=in_packet; out->bulk_out_packet=out_packet;
}
bool usb_parse_device(const unsigned char *p,size_t n,struct usb_device_info *out)
{
    if (!p || !out || n!=18 || p[0]!=18 || p[1]!=1 || !p[17] ||
        (p[7]!=8 && p[7]!=16 && p[7]!=32 && p[7]!=64)) { return false; }
    out->usb_version=le16(p+2); out->device_class=p[4];
    out->vendor=le16(p+8); out->product=le16(p+10); out->configurations=p[17]; out->identified=true;
    return true;
}
bool usb_parse_configuration(const unsigned char *p,size_t n,struct usb_device_info *out)
{
    if (!p || !out || n<9 || n>256 || p[0]!=9 || p[1]!=2 || le16(p+2)!=n || !p[4] || !p[5]) { return false; }
    struct usb_device_info parsed=*out; parsed.configuration=p[5]; parsed.mass_storage=false;
    uint8_t in=0,epout=0,interface=0; uint16_t in_packet=0,out_packet=0;
    bool candidate=false,have_interface=false; unsigned endpoint_count=0,expected=0;
    unsigned char interfaces[32]={0}; unsigned interface_count=0;
    for (size_t at=9;at<n;) {
        if (n-at<2 || p[at]<2 || p[at]>n-at) { return false; }
        unsigned size=p[at],type=p[at+1];
        if (type==4) {
            if (size<9 || (have_interface && endpoint_count!=expected)) { return false; }
            if (candidate) { save_interface(&parsed,interface,in,epout,in_packet,out_packet); }
            have_interface=true;
            if (!p[at+3]) {
                unsigned number=p[at+2];
                if (interfaces[number/8]&(1u<<(number%8))) { return false; }
                interfaces[number/8]|=(unsigned char)(1u<<(number%8)); ++interface_count;
            }
            candidate=p[at+3]==0 && p[at+4]==2 && p[at+5]==8 && p[at+6]==6 && p[at+7]==0x50;
            interface=p[at+2]; in=0; epout=0; endpoint_count=0; expected=p[at+4];
        } else if (type==5) {
            if (!have_interface || size<7 || ++endpoint_count>expected || !((p[at+2])&15) || (p[at+2]&0x70)) { return false; }
            if (candidate && (p[at+3]&3)==2) {
                uint16_t packet=le16(p+at+4);
                if (!packet || packet>512) { return false; }
                if (p[at+2]&0x80) { if (in) { return false; } in=p[at+2]; in_packet=packet; }
                else { if (epout) { return false; } epout=p[at+2]; out_packet=packet; }
            }
        }
        at+=size;
    }
    if (!have_interface || endpoint_count!=expected || interface_count!=p[4]) { return false; }
    if (candidate) { save_interface(&parsed,interface,in,epout,in_packet,out_packet); }
    *out=parsed; return true;
}
