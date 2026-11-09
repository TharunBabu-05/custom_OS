/* Narrow Pi 5 WLAN bring-up. No RF/firmware commands and no RP1/SD-slot access.
 * GPIO mux/pull fields: Raspberry Pi's pinctrl-bcm2712 C0/D0 tables.
 * GPIO directions: gpio-brcmstb (1=input). Host: SDHCI legacy 3.3V, 400 kHz. */
#include "wifi_power.h"
#include "dt_property.h"
#define HOST 0x1001100000ULL
#define GPIO 0x107d508500ULL
#define MUX  0x107d504100ULL
#define STEP 0x1001504004ULL
static uint32_t be32(const unsigned char *p)
{ return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3]; }
static uint64_t be64(const unsigned char *p) { return (uint64_t)be32(p)<<32|be32(p+4); }
static bool text_is(const unsigned char *p,uint32_t n,const char *s)
{
    unsigned i=0; while (i<n && s[i] && p[i]==(unsigned char)s[i]) { ++i; }
    return i+1==n && !s[i] && !p[i];
}
bool pi5_wifi_power_description(const void *dtb,size_t available)
{
    const unsigned char *p; uint32_t n,controller,clock;
    if (!dt_property(dtb,available,"/","compatible",&p,&n)) { return false; }
    const char expected[]="raspberrypi,5-model-b";
    if (n<sizeof(expected)) { return false; }
    for (unsigned i=0;i<sizeof(expected);++i) { if (p[i]!=(unsigned char)expected[i]) { return false; } }
    if (!dt_property(dtb,available,"/aliases","wifi0",&p,&n) ||
        !text_is(p,n,"/axi/mmc@1100000/wifi@1")) { return false; }
    if (!dt_property(dtb,available,"/axi/mmc@1100000","reg",&p,&n) || n!=32 ||
        be64(p)!=HOST || be64(p+8)!=0x260 || be64(p+16)!=HOST+0x400 || be64(p+24)!=0x200) { return false; }
    if (!dt_property(dtb,available,"/soc@107c000000/gpio@7d508500","reg",&p,&n) ||
        n!=8 || be32(p)!=0x7d508500 || be32(p+4)<0x2c) { return false; }
    if (!dt_property(dtb,available,"/soc@107c000000/gpio@7d508500","phandle",&p,&n) || n!=4) { return false; }
    controller=be32(p);
    if (!dt_property(dtb,available,"/wl-on-reg","gpio",&p,&n) || n!=12 ||
        be32(p)!=controller || be32(p+4)!=28 || be32(p+8)!=0) { return false; }
    if (!dt_property(dtb,available,"/wl-on-reg","startup-delay-us",&p,&n) || n!=4 || be32(p)!=150000) { return false; }
    if (!dt_property(dtb,available,"/wl-on-reg","regulator-min-microvolt",&p,&n) || n!=4 || be32(p)!=3300000) { return false; }
    if (!dt_property(dtb,available,"/soc@107c000000/pinctrl@7d504100","reg",&p,&n) ||
        n!=8 || be32(p)!=0x7d504100 || be32(p+4)<0x1c) { return false; }
    if (!dt_property(dtb,available,"/axi/mmc@1100000","clocks",&p,&n) || n!=4) { return false; } clock=be32(p);
    if (!dt_property(dtb,available,"/clocks/clk-emmc2","phandle",&p,&n) || n!=4 || be32(p)!=clock) { return false; }
    return dt_property(dtb,available,"/clocks/clk-emmc2","clock-frequency",&p,&n) && n==4 && be32(p)==200000000;
}
static bool rd(const struct wifi_power_io *io,uint64_t address,uint32_t *value)
{ return io->read32(io->context,address,value); }
static bool wr(const struct wifi_power_io *io,uint64_t address,uint32_t value)
{ return io->write32(io->context,address,value); }
static bool field(const struct wifi_power_io *io,uint64_t address,uint32_t mask,uint32_t value)
{
    uint32_t old; return rd(io,address,&old) && wr(io,address,(old&~mask)|(value&mask));
}
static enum block_result wait_reg(const struct wifi_power_io *io,uint64_t address,uint32_t mask,uint32_t wanted)
{
    uint64_t start=io->ticks(io->context);
    for (unsigned spin=0;spin<10000000;++spin) {
        uint32_t value; if (!rd(io,address,&value)) { return BLOCK_IO; }
        if ((value&mask)==wanted) { return BLOCK_OK; }
        if (io->ticks(io->context)-start>=io->hz/10) { break; }
    }
    return BLOCK_TIMEOUT;
}
static bool delay(const struct wifi_power_io *io,unsigned us)
{
    uint64_t start=io->ticks(io->context),duration=(io->hz*us+999999)/1000000;
    for (unsigned spin=0;spin<100000000;++spin) {
        if (io->ticks(io->context)-start>=duration) { return true; }
    }
    return false;
}
static bool pin(const struct wifi_power_io *io,bool d0,unsigned gpio,unsigned function,unsigned pull)
{
    unsigned mux_reg=d0 ? (gpio<32 ? 2 : 3) : (gpio<32 ? 3 : 4);
    unsigned shift=(gpio%8)*4;
    unsigned pad_reg=d0 ? (gpio<33 ? 5 : 6) : 9;
    unsigned pad_shift=d0 ? (gpio<33 ? (gpio-18)*2 : (gpio-33)*2) : (gpio-23)*2;
    return field(io,MUX+mux_reg*4,15u<<shift,function<<shift) &&
        field(io,MUX+pad_reg*4,3u<<pad_shift,pull<<pad_shift);
}
enum block_result pi5_wifi_power_up(const struct wifi_power_io *io,const void *dtb,size_t available,struct wifi_power_state *s)
{
    if (!io || !s || !io->read32 || !io->write32 || !io->ticks || io->hz<1000 || io->hz>10000000000ULL) { return BLOCK_ARGUMENT; }
    if (s->attempted) { return BLOCK_NOT_READY; }
    *s=(struct wifi_power_state){.error=BLOCK_UNSUPPORTED};
    if (!pi5_wifi_power_description(dtb,available)) { return s->error; }
    uint32_t control,present,cqe;
    if (!rd(io,STEP,&s->stepping) || !rd(io,HOST+0x40,&s->caps) || !rd(io,HOST+0xfc,&s->version) ||
        !rd(io,HOST+0x2c,&s->clock) || !rd(io,HOST+0x28,&control) || !rd(io,HOST+0x24,&present) || !rd(io,HOST+0x208,&cqe)) {
        s->error=BLOCK_IO; return s->error;
    }
    bool d0=s->stepping==0x27120030;
    if (!d0 && s->stepping!=0x27120020 && s->stepping!=0x27120021) { return s->error; }
    if (!d0) {
        const unsigned char *p; uint32_t n;
        if (!dt_property(dtb,available,"/soc@107c000000/pinctrl@7d504100","reg",&p,&n) || n!=8 || be32(p+4)<0x28) { return s->error; }
    }
    s->source_hz=((s->caps>>8)&255)*1000000;
    unsigned version=(s->version>>16)&255;
    if (s->source_hz!=200000000 || !(s->caps&(1u<<24)) || version<2 || version>4 ||
        (s->clock&0x07000007) || (control&256) || (present&3) || (cqe&1)) { return s->error; }
    s->attempted=true; s->phase=WIFI_POWER_RESET;
    if (!wr(io,HOST+0x38,0) || !wr(io,HOST+0x2c,1u<<24)) { s->error=BLOCK_IO; goto failed; }
    s->error=wait_reg(io,HOST+0x2c,1u<<24,0); if (s->error!=BLOCK_OK) { goto failed; }
    /* BCM2712 routes this low-rate host through the SD pins, not eMMC.
     * Its soldered SDIO device needs the non-removable presence override. */
    if (!field(io,HOST+0x444,3,2) || !field(io,HOST+0x400,3u<<30,1u<<31)) { s->error=BLOCK_IO; goto failed; }
    s->phase=WIFI_POWER_PINS;
    if (!field(io,GPIO+4,1u<<28,0) || !pin(io,d0,28,0,0) ||
        !field(io,GPIO+8,(1u<<28)|(3u<<30),3u<<30) || !field(io,GPIO+0x28,15,15)) { s->error=BLOCK_IO; goto failed; }
    for (unsigned gpio=30;gpio<=35;++gpio) {
        unsigned function=d0 ? 1 : ((gpio==33 || gpio==35) ? 3 : 4);
        if (!pin(io,d0,gpio,function,gpio==30 ? 0 : 2)) { s->error=BLOCK_IO; goto failed; }
    }
    s->phase=WIFI_POWER_LOW;
    if (!delay(io,10000)) { s->error=BLOCK_TIMEOUT; goto failed; }
    s->phase=WIFI_POWER_VDD;
    if (!field(io,HOST+0x28,0xf3e,0xf00) || !field(io,GPIO+4,1u<<28,1u<<28)) { s->error=BLOCK_IO; goto failed; }
    if (!delay(io,150000)) { s->error=BLOCK_TIMEOUT; goto failed; }
    s->phase=WIFI_POWER_CLOCK;
    /* SDHCI v3 divided clock: 200 MHz / (2*250) = 400 kHz. */
    uint32_t clock=(14u<<16)|(250u<<8)|1;
    if (!wr(io,HOST+0x2c,clock)) { s->error=BLOCK_IO; goto failed; }
    s->error=wait_reg(io,HOST+0x2c,2,2); if (s->error!=BLOCK_OK) { goto failed; }
    if (!wr(io,HOST+0x2c,clock|4) || !delay(io,1000) || !rd(io,HOST+0x2c,&s->clock)) { s->error=BLOCK_IO; goto failed; }
    if ((s->clock&0x07000007)!=7) { s->error=BLOCK_IO; goto failed; }
    s->sd_hz=400000; s->ready=true; s->error=BLOCK_OK; s->phase=WIFI_POWER_READY; return BLOCK_OK;
failed:
    /* Quiesce only WLAN resources; leave mux changes for diagnostics/reboot. */
    (void)wr(io,HOST+0x2c,14u<<16);
    (void)field(io,HOST+0x28,256,0); (void)field(io,GPIO+4,1u<<28,0);
    return s->error;
}
