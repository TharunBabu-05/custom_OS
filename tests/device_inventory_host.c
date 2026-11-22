#include "board.h"
#include "dt_resources.h"
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static unsigned char dtb[128*1024];
static size_t bytes;
static char output[16384];
static size_t length;

void console_putc(char c)
{
    assert(length+1<sizeof(output));
    output[length++]=c;
    output[length]='\0';
}
void console_write(const char *s) { while (*s) { console_putc(*s++); } }
void console_hex(uint64_t value)
{
    char line[20];
    snprintf(line,sizeof(line),"0x%016" PRIx64,value);
    console_write(line);
}
void console_u64(uint64_t value)
{
    char line[24];
    snprintf(line,sizeof(line),"%" PRIu64,value);
    console_write(line);
}

#define SOC "/soc@107c000000"
#define MAILBOX SOC "/mailbox@7c013880"
#define FIRMWARE SOC "/firmware"
#define PCIE "/axi/pcie@1000120000"
#define RP1 PCIE "/rp1"
#define USB1 RP1 "/usb@300000"

static struct dt_node find(const char *path)
{
    struct dt_node node;
    assert(dt_find_node(dtb,bytes,path,&node)==DT_FOUND);
    return node;
}

static unsigned char *property(const char *path,const char *key,uint32_t expected_bytes)
{
    const unsigned char *p;
    uint32_t n;
    assert(dt_property(dtb,bytes,path,key,&p,&n) && n==expected_bytes);
    return dtb+(p-dtb);
}

static enum dt_reg_result reg(const char *path,unsigned index,struct dt_reg_range *range)
{
    struct dt_node node=find(path);
    return dt_reg_range(dtb,bytes,path,&node,index,range);
}

static void write_cell(unsigned char *p,uint32_t cell)
{
    for (unsigned i=0;i<4;++i) { p[i]=(unsigned char)(cell>>(24-8*i)); }
}

static void test_resources(void)
{
    struct dt_reg_range range;
    assert(reg(MAILBOX,0,&range)==DT_REG_CPU && range.address==0x7c013880 &&
           range.bytes==0x40 && range.cpu==0x107c013880ULL);
    assert(reg(USB1,0,&range)==DT_REG_CPU && range.address==0xc040300000ULL &&
           range.bytes==0x100000 && range.cpu==0x1f00300000ULL);
    assert(reg(RP1 "/gpio@d0000",2,&range)==DT_REG_CPU && range.cpu==0x1f000f0000ULL);
    assert(reg(RP1 "/gpio@d0000",3,&range)==DT_REG_END);
    assert(reg(SOC "/rpi_rtc",0,&range)==DT_REG_END);
    assert(dt_device_state(dtb,bytes,RP1 "/spi@50000")==DT_DISABLED);
    assert(dt_device_state(dtb,bytes,USB1)==DT_ENABLED);
    unsigned char *status=property(PCIE,"status",5);
    memcpy(status,"fail",5);
    assert(dt_device_state(dtb,bytes,USB1)==DT_DISABLED);
    memcpy(status,"okay",5);

    unsigned char *address=property(MAILBOX,"reg",8);
    write_cell(address,0x7fffffe0);
    assert(reg(MAILBOX,0,&range)==DT_REG_LOCAL && range.address==0x7fffffe0);
    write_cell(address,0x7c013880);
    unsigned char *window=property(RP1,"ranges",28);
    window[1]^=1;
    assert(reg(USB1,0,&range)==DT_REG_LOCAL);
    window[1]^=1;
    unsigned char *count=property(SOC,"#address-cells",4);
    write_cell(count,3);
    assert(reg(MAILBOX,0,&range)==DT_REG_RAW);
    write_cell(count,1);

    struct dt_node provider;
    char path[DT_PATH_CAPACITY];
    uint32_t id=dt_cell(property(MAILBOX,"phandle",4));
    assert(dt_find_phandle(dtb,bytes,id,&provider,path)==DT_FOUND && !strcmp(path,MAILBOX));
    unsigned char *phandle=property(FIRMWARE,"phandle",4);
    uint32_t old=dt_cell(phandle);
    write_cell(phandle,id);
    assert(dt_find_phandle(dtb,bytes,id,&provider,path)==DT_INVALID);
    write_cell(phandle,old);

    assert(!dt_walk(dtb,bytes-1,NULL,NULL));
    uint32_t end=dt_cell(dtb+8)+dt_cell(dtb+36)-4;
    assert(dt_cell(dtb+end)==9);
    write_cell(dtb+end,4);
    assert(dt_find_node(dtb,bytes,USB1,&provider)==DT_INVALID);
    write_cell(dtb+end,9);
}

static void command(const char *line,const char *expected)
{
    struct board_info board={.name="rpi5-header",.uart_base=0x1c00030000ULL};
    length=0;
    output[0]='\0';
    assert(device_inventory_command(line,dtb,&board));
    assert(strstr(output,expected));
}

int main(int argc,char **argv)
{
    assert(argc==2);
    FILE *input=fopen(argv[1],"rb");
    assert(input);
    bytes=fread(dtb,1,sizeof(dtb),input);
    assert(bytes>40 && feof(input) && !ferror(input));
    fclose(input);
    test_resources();
    command("devlist /axi/pcie@1000120000/rp1","spi@50000 status=disabled compatible=snps,dw-apb-ssi");
    command("devinfo usb1","dt_cpu=0x0000001f00300000");
    assert(strstr(output,"interrupt-parent: /axi/pcie@1000120000/rp1\n"));
    command("devinfo serial0","owner=serial-console live_mmio=0x0000001c00030000");
    assert(strstr(output,"dt_cpu=0x0000001f00030000"));
    command("devinfo ethernet0","clocks: /axi/pcie@1000120000/rp1/");
    command("devinfo " SOC "/rpi_rtc","firmware: " FIRMWARE);
    command("devinfo /does-not-exist","Device-tree node or alias not found.");
    puts("PASS: Pi 5 device inventory, BCM2712/RP1 ranges, providers, disabled ancestry and invalid tree handling.");
    return 0;
}
