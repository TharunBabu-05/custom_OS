#include "xhci_test.h"
#include "dt_property.h"
static uint32_t be32(const unsigned char *p)
{ return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3]; }
static uint64_t be64(const unsigned char *p) { return (uint64_t)be32(p)<<32|be32(p+4); }
bool pi5_dma_description(const void *dtb,size_t available)
{
    const unsigned char *p; uint32_t n; struct pi5_usb_description d;
    if (!pi5_usb_describe(dtb,available,0,&d)) { return false; }
    if (!dt_property(dtb,available,"/axi/pcie@1000120000","reg",&p,&n) || n!=16 ||
        be64(p)!=0x1000120000ULL || be64(p+8)<0x40c0) { return false; }
    if (!dt_property(dtb,available,"/axi/pcie@1000120000/rp1","dma-ranges",&p,&n) ||
        !n || n%28 || n>28*8) { return false; }
    for (uint32_t i=0;i<n;i+=28) {
        if (be64(p+i)==0x1000000000ULL && be32(p+i+8)==0x43000000 &&
            be64(p+i+12)==0x1000000000ULL && be64(p+i+20)==0x1000000000ULL) { return true; }
    }
    return false;
}
bool pi5_dma_window_read(const struct xhci_observer *io,struct pi5_dma_window *out)
{
    if (!io || !io->read32 || io->span<0x40bc || !out) { return false; }
    *out=(struct pi5_dma_window){0};
    uint32_t endian;
    if (!io->read32(io->context,0x188,&endian) || (endian&12)) { return false; }
    if (!io->read32(io->context,0x4034,&out->bar_lo) || !io->read32(io->context,0x4038,&out->bar_hi) ||
        !io->read32(io->context,0x40b4,&out->remap_lo) || !io->read32(io->context,0x40b8,&out->remap_hi)) { return false; }
    unsigned code=out->bar_lo&31;
    if (!code || (code>21 && code<28) || !(out->remap_lo&1) || (out->remap_lo&0xffe)) { return false; }
    unsigned exponent=code>=28 ? code-28+12 : code+15;
    out->size=1ULL<<exponent;
    out->cpu=(uint64_t)out->remap_hi<<32|(out->remap_lo&~0xfffU);
    out->bus=(uint64_t)out->bar_hi<<32|(out->bar_lo&~31U);
    /* RP1's 0..512 GiB outbound aperture is direct mapped. Firmware on the
     * captured Pi maps RC BAR2 at PCI address zero; Linux's DT describes a
     * different, later layout. Accept only these two known inbound profiles. */
    bool identity=out->bus==0 && out->cpu==0 && out->size<=0x1000000000ULL;
    bool relocated=out->bus>=0x1000000000ULL && out->bus<0x2000000000ULL &&
        out->size<=0x2000000000ULL-out->bus;
    if ((out->cpu&(out->size-1)) || (out->bus&(out->size-1)) || (!identity && !relocated) ||
        out->size-1>UINT64_MAX-out->cpu) { return false; }
    return true;
}
bool pi5_dma_address(const struct pi5_dma_window *w,uint64_t physical,size_t length,uint64_t *bus)
{
    if (!w || !bus || !length || physical<w->cpu || physical-w->cpu>=w->size ||
        length>w->size-(physical-w->cpu) || physical-w->cpu>UINT64_MAX-w->bus) { return false; }
    *bus=w->bus+(physical-w->cpu);
    return length-1<=UINT64_MAX-*bus;
}
