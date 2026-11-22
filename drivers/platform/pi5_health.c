#include "dt_property.h"
#include "platform_health.h"

#define SOC "/soc@107c000000"
#define MAILBOX SOC "/mailbox@7c013880"
#define FIRMWARE SOC "/firmware"
#define RTC SOC "/rpi_rtc"
#define AVS SOC "/avs-monitor@7d542000"
#define THERMAL AVS "/thermal"

static uint32_t cell(const unsigned char *p)
{
    return (uint32_t)p[0]<<24 | (uint32_t)p[1]<<16 | (uint32_t)p[2]<<8 | p[3];
}

static bool string_is(const unsigned char *p, uint32_t bytes, const char *name)
{
    for (uint32_t i=0; i<bytes; ++i) {
        if (p[i]!=(unsigned char)name[i]) { return false; }
        if (!p[i]) { return i+1==bytes; }
    }
    return false;
}

static bool node(const void *dtb, size_t bytes, const char *path, const char *compatible)
{
    const unsigned char *p;
    uint32_t n;
    enum dt_lookup status=dt_optional_property(dtb,bytes,path,"status",&p,&n);
    if (status!=DT_MISSING && (status!=DT_FOUND ||
        !(string_is(p,n,"okay") || string_is(p,n,"ok")))) { return false; }
    if (!dt_property(dtb,bytes,path,"compatible",&p,&n)) { return false; }

    bool found=false;
    for (uint32_t i=0; i<n;) {
        uint32_t start=i;
        while (i<n && p[i]) { ++i; }
        if (i==n || i==start) { return false; }
        ++i;
        found |= string_is(p+start,i-start,compatible);
    }
    return found;
}

static bool cells(const void *dtb, size_t bytes, const char *path, const char *key,
                  const uint32_t *expected, uint32_t count)
{
    const unsigned char *p;
    uint32_t n;
    if (!dt_property(dtb,bytes,path,key,&p,&n) || n!=count*4) { return false; }
    for (uint32_t i=0; i<count; ++i) {
        if (cell(p+4*i)!=expected[i]) { return false; }
    }
    return true;
}

static bool ref(const void *dtb, size_t bytes, const char *from, const char *key, const char *to)
{
    const unsigned char *p;
    uint32_t n;
    if (!dt_property(dtb,bytes,to,"phandle",&p,&n) || n!=4) { return false; }
    uint32_t id=cell(p);
    return id && id!=UINT32_MAX && cells(dtb,bytes,from,key,&id,1);
}

struct pi5_health pi5_health_discover(const void *dtb, size_t bytes)
{
    struct pi5_health out={0};
    if (!node(dtb,bytes,"/","raspberrypi,5-model-b") || !node(dtb,bytes,SOC,"simple-bus") ||
        !cells(dtb,bytes,"/","#address-cells",(const uint32_t[]){2},1) ||
        !cells(dtb,bytes,SOC,"#address-cells",(const uint32_t[]){1},1) ||
        !cells(dtb,bytes,SOC,"#size-cells",(const uint32_t[]){1},1) ||
        !cells(dtb,bytes,SOC,"ranges",(const uint32_t[]){0,16,0,0x80000000},4)) { return out; }

    if (node(dtb,bytes,AVS,"brcm,bcm2711-avs-monitor") &&
        cells(dtb,bytes,AVS,"reg",(const uint32_t[]){0x7d542000,0xf00},2) &&
        node(dtb,bytes,THERMAL,"brcm,bcm2711-thermal") &&
        ref(dtb,bytes,"/thermal-zones/cpu-thermal","thermal-sensors",THERMAL) &&
        cells(dtb,bytes,"/thermal-zones/cpu-thermal","coefficients",(const uint32_t[]){0xfffffdda,450000},2)) {
        out.thermal=0x107d542200ULL;
    }
    if (node(dtb,bytes,MAILBOX,"brcm,bcm2835-mbox") &&
        cells(dtb,bytes,MAILBOX,"reg",(const uint32_t[]){0x7c013880,0x40},2) &&
        cells(dtb,bytes,MAILBOX,"#mbox-cells",(const uint32_t[]){0},1) &&
        node(dtb,bytes,FIRMWARE,"raspberrypi,bcm2835-firmware") &&
        ref(dtb,bytes,FIRMWARE,"mboxes",MAILBOX)) {
        out.mailbox=0x107c013880ULL;
        out.rtc=node(dtb,bytes,RTC,"raspberrypi,rpi-rtc") && ref(dtb,bytes,RTC,"firmware",FIRMWARE);
    }
    return out;
}

bool pi5_temperature(uint32_t status, int32_t *millicelsius)
{
    if (!(status & ((1u<<16) | (1u<<10)))) { return false; }
    *millicelsius=450000 - 550*(int32_t)(status&0x3ff);
    return true;
}
