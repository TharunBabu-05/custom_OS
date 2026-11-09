#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "usb.h"
#include "dt_property.h"
#include "console.h"
#include "xhci_test.h"
void ring_tests(void);
void platform_usb_test(uintptr_t dtb,uintptr_t uart_base) { (void)dtb; (void)uart_base; }
static char output[16384]; static size_t output_length;
void console_write(const char *text)
{
    size_t n=strlen(text); assert(n<sizeof(output)-output_length);
    memcpy(output+output_length,text,n+1); output_length+=n;
}
void console_putc(char value) { char s[2]={value,0}; console_write(s); }
void console_u64(uint64_t value)
{ char s[32]; snprintf(s,sizeof(s),"%llu",(unsigned long long)value); console_write(s); }
void console_hex(uint64_t value)
{ char s[32]; snprintf(s,sizeof(s),"0x%016llx",(unsigned long long)value); console_write(s); }
static void clear_output(void) { output_length=0; output[0]=0; }
static uint32_t registers[0xc000/4],reads,fail_at;
static bool read_mock(void *context,uint32_t offset,uint32_t *value)
{
    (void)context; assert(!(offset&3) && offset<sizeof(registers)); ++reads;
    if (offset==fail_at) { return false; } *value=registers[offset/4]; return true;
}
static void seed(void)
{
    memset(registers,0,sizeof(registers)); reads=0; fail_at=UINT32_MAX;
    registers[0]=0x01100040; registers[1]=(4u<<24)|(2u<<8)|32;
    registers[4]=(0x100u<<16)|5; registers[5]=0x1000; registers[6]=0x2000;
    registers[0x40/4]=1; registers[0x48/4]=1;
    registers[0x440/4]=1|2|512|(3u<<10); /* Connected HS port. */
    registers[0x450/4]=512; /* Disconnected companion port. */
    registers[0x460/4]=1|2|512|(4u<<10);
    registers[0x470/4]=512;
    registers[0x400/4]=0x02000402; registers[0x404/4]=0x20425355; registers[0x408/4]=0x0201;
    registers[0x410/4]=0x03000002; registers[0x414/4]=0x20425355; registers[0x418/4]=0x0203;
}
static void observer_tests(void)
{
    struct xhci_observer io={read_mock,NULL,sizeof(registers)}; struct xhci_snapshot out;
    seed(); uint32_t saved[sizeof(registers)/4]; memcpy(saved,registers,sizeof(saved));
    assert(xhci_observe(&io,&out)==USB_PROBE_OK);
    assert(out.version==0x110 && out.ports==4 && out.slots==32 && out.interrupters==2);
    assert(out.address64 && out.context64 && out.command==1 && !out.status && out.page_sizes==1);
    assert(out.port[0].protocol_major==2 && out.port[2].protocol_major==3);
    assert((out.port[0].status>>10&15)==3 && !(out.port[1].status&1));
    assert(!memcmp(saved,registers,sizeof(saved))); /* No writable I/O interface exists. */
    seed(); fail_at=0x440; assert(xhci_observe(&io,&out)==USB_PROBE_IO);
    seed(); registers[0]=UINT32_MAX; assert(xhci_observe(&io,&out)==USB_PROBE_IO && reads==1);
    seed(); registers[0]=0; assert(xhci_observe(&io,&out)==USB_PROBE_LAYOUT);
    seed(); registers[0]=0x01000021; assert(xhci_observe(&io,&out)==USB_PROBE_LAYOUT);
    seed(); registers[1]=0; assert(xhci_observe(&io,&out)==USB_PROBE_LAYOUT);
    seed(); registers[1]=(33u<<24)|0x101; assert(xhci_observe(&io,&out)==USB_PROBE_LAYOUT);
    seed(); registers[1]=(32u<<24)|0x101; assert(xhci_observe(&io,&out)==USB_PROBE_OK);
    seed(); registers[5]=0xc000; assert(xhci_observe(&io,&out)==USB_PROBE_LAYOUT);
    seed(); registers[6]=0; assert(xhci_observe(&io,&out)==USB_PROBE_LAYOUT);
    seed(); registers[4]=(0x3000u<<16)|1; assert(xhci_observe(&io,&out)==USB_PROBE_LAYOUT);
    seed(); registers[0x408/4]=0x0200; assert(xhci_observe(&io,&out)==USB_PROBE_LAYOUT);
    seed(); registers[0x418/4]=0x0202; assert(xhci_observe(&io,&out)==USB_PROBE_LAYOUT);
    seed(); registers[0x408/4]|=1u<<28; assert(xhci_observe(&io,&out)==USB_PROBE_LAYOUT);
    seed(); registers[0x404/4]=0; assert(xhci_observe(&io,&out)==USB_PROBE_LAYOUT);
    seed(); registers[4]=0; assert(xhci_observe(&io,&out)==USB_PROBE_OK && !out.port[0].protocol_major);
    seed(); registers[4]=(8u<<16)|1; registers[0x20/4]=0x02000402;
    registers[0x24/4]=0x20425355; registers[0x28/4]=0x0201;
    registers[0x30/4]=0x03000002; registers[0x34/4]=0x20425355; registers[0x38/4]=0x0203;
    assert(xhci_observe(&io,&out)==USB_PROBE_OK && out.port[0].protocol_major==2 && out.port[2].protocol_major==3);
    seed(); registers[0x44/4]=1|(1u<<11); assert(xhci_observe(&io,&out)==USB_PROBE_OK && (out.status&1));
    seed(); io.span=0x450; assert(xhci_observe(&io,&out)==USB_PROBE_LAYOUT);
    io.span=sizeof(registers);
    seed(); registers[4]=0x100u<<16;
    for (unsigned i=0;i<65;++i) { registers[0x100+i]=0x104; }
    assert(xhci_observe(&io,&out)==USB_PROBE_LAYOUT);
    assert(xhci_observe(NULL,&out)==USB_PROBE_ARGUMENT);
    assert(xhci_observe(&io,NULL)==USB_PROBE_ARGUMENT);
    uint32_t random=0x12345678;
    for (unsigned i=0;i<10000;++i) {
        seed(); random=random*1664525u+1013904223u;
        unsigned at=random%(sizeof(registers)/4); registers[at]=random^(random>>11);
        (void)xhci_observe(&io,&out);
    }
    puts("PASS: xHCI capabilities, port/protocol snapshots, invalid layouts, I/O failures, bounded chains and 10000 mutations");
}
static void dt_tests(void)
{
    FILE *f=fopen(".dist/rpi5-firmware/bcm2712-rpi-5-b.dtb","rb"); assert(f);
    assert(!fseek(f,0,SEEK_END)); long n=ftell(f); assert(n>40); rewind(f);
    unsigned char *data=malloc((size_t)n); assert(data && fread(data,1,(size_t)n,f)==(size_t)n); fclose(f);
    struct pi5_usb_description d;
    for (unsigned i=0;i<2;++i) {
        assert(pi5_usb_describe(data,(size_t)n,i,&d));
        assert(d.bus_address==0xc040200000ULL+(uint64_t)i*0x100000 && d.span==0x100000);
        assert(!strcmp(d.status,"okay") && strstr(d.path,"/rp1/usb@"));
    }
    assert(!pi5_usb_describe(data,(size_t)n,2,&d));
    assert(!pi5_usb_describe(data,(size_t)n-1,0,&d));
    assert(!pi5_usb_describe(NULL,0,0,&d));
    assert(!pi5_usb_describe(data,(size_t)n,0,NULL));
    assert(pi5_usb_describe(data,(size_t)n,0,&d));
    /* Command routing must refuse Pi MMIO on QEMU and unconfirmed mappings. */
    usb_setup("qemu_virt",1,0x9000000); clear_output();
    assert(usb_command("usbprobe") && strstr(output,"no MMIO attempted"));
    assert(!usb_command("unrecognized"));
    usb_setup("rpi5",(uintptr_t)data,0x107d001000ULL); clear_output();
    assert(usb_command("usbprobe") && strstr(output,"probe disabled"));
    usb_setup("rpi5-header",(uintptr_t)data,0x1c00030000ULL); clear_output();
    assert(usb_command("usbinfo"));
    assert(strstr(output,"0x0000001c00200000") && strstr(output,"0x0000001c00300000"));
    assert(!strstr(output,"Reading controller registers now"));
    /* Native command build intentionally excludes the Pi MMIO implementation. */
    clear_output(); assert(usb_command("usbprobe") && strstr(output,"Pi MMIO unavailable in this build"));
    const unsigned char *p; uint32_t length;
    assert(dt_property(data,(size_t)n,d.path,"reg",&p,&length) && length==16);
    unsigned char *reg=data+(p-data); unsigned char saved=reg[7]; reg[7]^=4;
    assert(!pi5_usb_describe(data,(size_t)n,0,&d)); reg[7]=saved;
    assert(dt_property(data,(size_t)n,"/aliases","usb0",&p,&length));
    unsigned char *alias=data+(p-data); saved=alias[length-1]; alias[length-1]='x';
    assert(!pi5_usb_describe(data,(size_t)n,0,&d)); alias[length-1]=saved;
    saved=alias[3]; alias[3]=0; assert(!pi5_usb_describe(data,(size_t)n,0,&d)); alias[3]=saved;
    uint32_t random=0x51bc762;
    for (unsigned i=0;i<2000;++i) {
        random=random*1664525u+1013904223u; size_t at=random%(size_t)n;
        saved=data[at]; data[at]^=(unsigned char)(1u<<(i%8));
        (void)pi5_usb_describe(data,(size_t)n,i%2,&d); data[at]=saved;
    }
    free(data); puts("PASS: both actual Pi DT USB hosts, alias/reg validation, truncation and 2000 DT mutations");
    puts("PASS: command routing, QEMU refusal, UART mapping guard and DT-only USB inventory with correct CPU candidates");
}
int main(void) { observer_tests(); dt_tests(); ring_tests(); return 0; }
