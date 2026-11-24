#include "dt_property.h"
#include "arm_platform.h"
#include "reboot.h"

static bool same(const char *a,const char *b)
{
    if (!a) { return false; }
    while (*a && *a==*b) { ++a;++b; }
    return *a==*b;
}
static bool string_is(const unsigned char *value,uint32_t length,const char *expected)
{
    for (uint32_t i=0;i<length;++i) {
        if (value[i]!=(unsigned char)expected[i]) { return false; }
        if (!value[i]) { return i+1==length; }
    }
    return false;
}
static bool has_compatible(const unsigned char *value,uint32_t length,const char *expected)
{
    bool found=false;
    for (uint32_t start=0;start<length;) {
        uint32_t end=start;
        while (end<length && value[end]) { ++end; }
        if (end==length || end==start) { return false; }
        if (string_is(value+start,end-start+1,expected)) { found=true; }
        start=end+1;
    }
    return found;
}
static bool enabled(const void *dtb,size_t bytes,const char *node)
{
    const unsigned char *p;uint32_t n;
    return !dt_property(dtb,bytes,node,"status",&p,&n) || string_is(p,n,"okay") || string_is(p,n,"ok");
}
static uint32_t be32(const unsigned char *p)
{ return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3]; }

enum psci_conduit psci_discover(const void *dtb,size_t bytes)
{
    const unsigned char *p;
    uint32_t n;
    if (!enabled(dtb,bytes,"/psci") ||
        !dt_property(dtb,bytes,"/psci","compatible",&p,&n) ||
        !(has_compatible(p,n,"arm,psci-0.2") || has_compatible(p,n,"arm,psci-1.0")) ||
        !dt_property(dtb,bytes,"/psci","method",&p,&n)) { return PSCI_NONE; }
    if (string_is(p,n,"hvc")) { return PSCI_HVC; }
    if (string_is(p,n,"smc")) { return PSCI_SMC; }
    return PSCI_NONE;
}

bool reset_discover(const char *board,const void *dtb,size_t bytes,struct reset_target *out)
{
    const unsigned char *p;uint32_t n;
    if (!out) { return false; }
    if (same(board,"rpi5-header") || same(board,"rpi5")) {
        const char *soc="/soc@107c000000";
        const char *pm="/soc@107c000000/watchdog@7d200000";
        if (!enabled(dtb,bytes,soc) || !enabled(dtb,bytes,pm) ||
            !dt_property(dtb,bytes,pm,"compatible",&p,&n) || !has_compatible(p,n,"brcm,bcm2712-pm") ||
            !dt_property(dtb,bytes,pm,"system-power-controller",&p,&n) || n ||
            !dt_property(dtb,bytes,pm,"reg",&p,&n) || n!=8 || be32(p)!=0x7d200000 || be32(p+4)!=0x308 ||
            !dt_property(dtb,bytes,soc,"ranges",&p,&n) || n!=16 ||
            be32(p)!=0 || be32(p+4)!=16 || be32(p+8)!=0 || be32(p+12)!=0x80000000) { return false; }
        *out=(struct reset_target){RESET_BCM2712,0x107d200000ULL};
        return true;
    }
    if (!same(board,"qemu_virt")) { return false; }
    enum psci_conduit method=psci_discover(dtb,bytes);
    if (method==PSCI_NONE) { return false; }
    *out=(struct reset_target){method==PSCI_HVC ? RESET_PSCI_HVC : RESET_PSCI_SMC,0};
    return true;
}
