#include "gic.h"
#include "dt_property.h"
static uint32_t be32(const unsigned char *p) { return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3]; }
static uint64_t be64(const unsigned char *p) { return (uint64_t)be32(p)<<32|be32(p+4); }
static bool same(const char *a,const char *b) { if (!a || !b) { return false; }while (*a && *a==*b) { ++a;++b; }return *a==*b; }
static bool string_list(const unsigned char *p,uint32_t n,const char *wanted)
{
    bool found=false;for (uint32_t at=0;at<n;) { uint32_t end=at;while (end<n && p[end]) { ++end; }if (end==n || end==at) { return false; }if (same((const char *)p+at,wanted)) { found=true; }at=end+1; }return found;
}
static bool scalar(const void *d,size_t n,const char *node,const char *property,uint32_t expected)
{ const unsigned char *p;uint32_t bytes;return dt_property(d,n,node,property,&p,&bytes) && bytes==4 && be32(p)==expected; }
bool gic_discover(const char *board,const void *dtb,size_t bytes,struct gic_description *out)
{
    if (!out) { return false; }bool pi=same(board,"rpi5-header") || same(board,"rpi5");if (!pi && !same(board,"qemu_virt")) { return false; }
    const char *node=pi ? "/soc@107c000000/interrupt-controller@7fff9000" : "/intc@8000000";
    const unsigned char *p;uint32_t n;
    if (!dt_property(dtb,bytes,node,"compatible",&p,&n) || !string_list(p,n,pi ? "arm,gic-400" : "arm,cortex-a15-gic") ||
        !scalar(dtb,bytes,node,"#interrupt-cells",3) || !dt_property(dtb,bytes,node,"interrupt-controller",&p,&n) || n) { return false; }
    if (!scalar(dtb,bytes,"/","#address-cells",2) || !scalar(dtb,bytes,"/","#size-cells",2) ||
        !dt_property(dtb,bytes,node,"phandle",&p,&n) || n!=4 || !be32(p)) { return false; }
    uint32_t parent=be32(p);
    if (dt_property(dtb,bytes,"/timer","interrupt-parent",&p,&n)) { if (n!=4 || be32(p)!=parent) { return false; } }
    else if (!scalar(dtb,bytes,"/","interrupt-parent",parent)) { return false; }
    if (dt_property(dtb,bytes,node,"status",&p,&n) && !((n==3 && same((const char *)p,"ok")) || (n==5 && same((const char *)p,"okay")))) { return false; }
    struct gic_description result={.timer_irq=30};
    if (!dt_property(dtb,bytes,node,"reg",&p,&n)) { return false; }
    if (pi) {
        if (n!=32 || be32(p)!=0x7fff9000 || be32(p+4)!=0x1000 || be32(p+8)!=0x7fffa000 || be32(p+12)!=0x2000) { return false; }
        result.distributor=0x107fff9000ULL;result.cpu=0x107fffa000ULL;result.distributor_span=0x1000;result.cpu_span=0x2000;
        if (!dt_property(dtb,bytes,"/soc@107c000000","ranges",&p,&n) || n!=16 || be32(p)!=0 || be32(p+4)!=16 || be32(p+8)!=0 || be32(p+12)!=0x80000000) { return false; }
    }else {
        if ((n!=32 && n!=64) || be64(p)!=0x08000000 || be64(p+8)!=0x10000 || be64(p+16)!=0x08010000 || be64(p+24)!=0x10000) { return false; }
        /* QEMU virtualization=on additionally describes GICH/GICV. They are
         * validated as this known profile but never accessed by our EL1 IRQ
         * driver; physical timer/CPU interface selection is unchanged. */
        if (n==64 && (be64(p+32)!=0x08030000 || be64(p+40)!=0x10000 || be64(p+48)!=0x08040000 || be64(p+56)!=0x10000)) { return false; }
        result.distributor=0x08000000;result.cpu=0x08010000;result.distributor_span=result.cpu_span=0x10000;
    }
    if (!dt_property(dtb,bytes,"/timer","compatible",&p,&n) || !string_list(p,n,"arm,armv8-timer") ||
        !dt_property(dtb,bytes,"/timer","interrupts",&p,&n) || (n!=48 && (pi || n!=60)) || be32(p+12)!=1 || be32(p+16)!=14) { return false; }
    if (n==60 && (be32(p+48)!=1 || be32(p+52)!=12 || be32(p+56)!=0x104)) { return false; }
    uint32_t flags=be32(p+20);if ((flags&15)!=4 && (flags&15)!=8) { return false; }if (flags&~0xff0fu) { return false; }
    if (!(flags&0x100)) { return false; } /* CPU0 wired according to DT PPI mask. */
    if (dt_property(dtb,bytes,"/timer","status",&p,&n) && !((n==3 && same((const char *)p,"ok")) || (n==5 && same((const char *)p,"okay")))) { return false; }
    *out=result;return true;
}
