#include "sdhci.h"
#include "dt_resources.h"

#define SD_HOST "/soc@107c000000/mmc@fff000"
#define SD_GPIO "/soc@107c000000/gpio@7d517c00"
#define SD_VCC "/sd-vcc-reg"
#define SD_IO "/sd-io-1v8-reg"
#define HOST_BASE 0x1000fff000ULL
#define GPIO_BASE 0x107d517c00ULL

static bool cells(const struct dt_node *node,const char *key,const uint32_t *expected,unsigned count)
{
    const unsigned char *p;
    uint32_t length;
    if (dt_node_property(node,key,&p,&length)!=DT_FOUND || length!=count*4) { return false; }
    for (unsigned i=0;i<count;++i) { if (dt_cell(p+4*i)!=expected[i]) { return false; } }
    return true;
}

static uint32_t phandle(const struct dt_node *node)
{
    const unsigned char *p;
    uint32_t length;
    if (dt_node_property(node,"phandle",&p,&length)!=DT_FOUND || length!=4 || dt_cell(p)==UINT32_MAX) { return 0; }
    return dt_cell(p);
}

static bool mapped(const void *dtb,size_t bytes,const char *path,struct dt_node *node,uint64_t base,uint64_t span)
{
    struct dt_reg_range reg;
    return dt_find_node(dtb,bytes,path,node)==DT_FOUND && dt_device_state(dtb,bytes,path)==DT_ENABLED &&
        dt_reg_range(dtb,bytes,path,node,0,&reg)==DT_REG_CPU && reg.cpu==base && reg.bytes==span;
}

static bool wiring(const void *dtb,size_t bytes)
{
    static const char model[]="raspberrypi,5-model-b";
    static const char alias[]=SD_HOST;
    const unsigned char *p;
    uint32_t length;
    if (!dt_property(dtb,bytes,"/","compatible",&p,&length) || length<sizeof(model)) { return false; }
    for (unsigned i=0;i<sizeof(model);++i) { if (p[i]!=(unsigned char)model[i]) { return false; } }
    if (!dt_property(dtb,bytes,"/aliases","mmc0",&p,&length) || length!=sizeof(alias)) { return false; }
    for (unsigned i=0;i<sizeof(alias);++i) { if (p[i]!=(unsigned char)alias[i]) { return false; } }

    struct dt_node host,gpio,vcc,io;
    struct dt_reg_range config;
    if (!mapped(dtb,bytes,SD_HOST,&host,HOST_BASE,0x260) ||
        dt_reg_range(dtb,bytes,SD_HOST,&host,1,&config)!=DT_REG_CPU || config.cpu!=HOST_BASE+0x400 || config.bytes!=0x200 ||
        !mapped(dtb,bytes,SD_GPIO,&gpio,GPIO_BASE,0x40) ||
        dt_find_node(dtb,bytes,SD_VCC,&vcc)!=DT_FOUND || dt_local_state(&vcc)!=DT_ENABLED ||
        dt_find_node(dtb,bytes,SD_IO,&io)!=DT_FOUND || dt_local_state(&io)!=DT_ENABLED) { return false; }
    uint32_t g=phandle(&gpio),v=phandle(&vcc),q=phandle(&io);
    return g && v && q && g!=v && g!=q && v!=q &&
        cells(&gpio,"#gpio-cells",(const uint32_t[]){2},1) &&
        cells(&host,"cd-gpios",(const uint32_t[]){g,5,1},3) &&
        cells(&host,"vmmc-supply",&v,1) && cells(&host,"vqmmc-supply",&q,1) &&
        cells(&vcc,"gpios",(const uint32_t[]){g,4,0},3) && cells(&vcc,"enable-active-high",NULL,0) &&
        cells(&vcc,"regulator-min-microvolt",(const uint32_t[]){3300000},1) &&
        cells(&vcc,"regulator-max-microvolt",(const uint32_t[]){3300000},1) &&
        cells(&io,"gpios",(const uint32_t[]){g,3,0},3) &&
        cells(&io,"states",(const uint32_t[]){1800000,1,3300000,0},4);
}

enum block_result pi5_sd_snapshot(const void *dtb,size_t bytes,pi5_sd_read32 read,
                                  void *context,struct pi5_sd_snapshot *out)
{
    if (!read || !out) { return BLOCK_ARGUMENT; }
    if (!wiring(dtb,bytes)) { return BLOCK_UNSUPPORTED; }
    struct pi5_sd_snapshot s;
    if (!read(context,GPIO_BASE+4,&s.gpio_levels) || !read(context,GPIO_BASE+8,&s.gpio_inputs) ||
        !read(context,HOST_BASE+0x24,&s.present) || !read(context,HOST_BASE+0x28,&s.control) ||
        !read(context,HOST_BASE+0x2c,&s.clock) || !read(context,HOST_BASE+0x3c,&s.host_control2) ||
        !read(context,HOST_BASE+0x40,&s.capabilities) || !read(context,HOST_BASE+0xfc,&s.version) ||
        !read(context,HOST_BASE+0x208,&s.cqe) || !read(context,HOST_BASE+0x444,&s.pin_select)) { return BLOCK_IO; }
    *out=s;
    return BLOCK_OK;
}
