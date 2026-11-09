/* Modern virtio-mmio split queue, one outstanding read, no IRQs or writes.
 * Queue/bounce buffers stay in reserved kernel BSS, even after a timeout. */
#include "block.h"
#include "console.h"
#include "cpu.h"
#include "wifi.h"
struct descriptor { uint64_t address; uint32_t length; uint16_t flags,next; };
struct available { uint16_t flags,index,ring[8],event; };
struct used { uint16_t flags,index; struct { uint32_t id,length; } ring[8]; uint16_t event; };
static struct descriptor descriptors[8] __attribute__((aligned(16)));
static volatile struct available avail __attribute__((aligned(2)));
static volatile struct used used __attribute__((aligned(4)));
static struct { uint32_t type,reserved; uint64_t sector; } request __attribute__((aligned(16)));
static volatile unsigned char bounce[512] __attribute__((aligned(16)));
static volatile unsigned char completion;
static uintptr_t base;
static bool ready;
static uint32_t rd(unsigned offset) { uint32_t value=*(volatile uint32_t *)(base+offset); __asm__ volatile("dmb sy" ::: "memory"); return value; }
static void wr(unsigned offset,uint32_t value) { __asm__ volatile("dmb sy" ::: "memory"); *(volatile uint32_t *)(base+offset)=value; }
static void address(unsigned offset,uintptr_t value) { wr(offset,(uint32_t)value); wr(offset+4,(uint32_t)(value>>32)); }
static enum block_result read_sector(void *context,uint64_t lba,unsigned char *data)
{
    (void)context;
    if (!ready) { return BLOCK_NOT_READY; }
    request.type=0; request.reserved=0; request.sector=lba; completion=0xff;
    descriptors[0]=(struct descriptor){(uintptr_t)&request,sizeof(request),1,1};
    descriptors[1]=(struct descriptor){(uintptr_t)bounce,512,3,2};
    descriptors[2]=(struct descriptor){(uintptr_t)&completion,1,2,0};
    uint16_t previous=used.index,index=avail.index;
    avail.ring[index%8]=0;
    __asm__ volatile("dmb sy" ::: "memory"); avail.index=(uint16_t)(index+1);
    __asm__ volatile("dsb sy" ::: "memory"); wr(0x50,0);
    uint64_t start=timer_ticks(),hz=timer_frequency();
    while (used.index==previous) {
        if (timer_ticks()-start>=hz*2) { ready=false; wr(0x70,rd(0x70)|128); return BLOCK_TIMEOUT; }
    }
    __asm__ volatile("dmb sy" ::: "memory");
    if (used.index!=(uint16_t)(previous+1) || used.ring[previous%8].id!=0 ||
        used.ring[previous%8].length<513 || completion!=0) { ready=false; return BLOCK_IO; }
    for (unsigned i=0;i<512;++i) { data[i]=bounce[i]; }
    uint32_t irq=rd(0x60); if (irq) { wr(0x64,irq); }
    return BLOCK_OK;
}
bool platform_storage_attach(struct block_device *device)
{
    if (base) { return false; } /* Do not reset or reuse a queue after failure. */
    for (unsigned i=0;i<32;++i) {
        uintptr_t candidate=0x0a000000UL+i*0x200;
        volatile uint32_t *regs=(volatile uint32_t *)candidate;
        if (regs[0]==0x74726976 && regs[2]==2) { base=candidate; break; }
    }
    if (!base || rd(4)!=2) { return false; }
    wr(0x70,0); if (rd(0x70)!=0) { return false; }
    wr(0x70,1); wr(0x70,3);
    wr(0x14,1); if (!(rd(0x10)&1)) { return false; }
    wr(0x14,0); uint32_t low=rd(0x10);
    wr(0x24,0); wr(0x20,low&(1u<<5)); /* Read-only feature; all writes absent. */
    wr(0x24,1); wr(0x20,1); wr(0x70,11);
    if (!(rd(0x70)&8)) { return false; }
    wr(0x30,0); if (rd(0x34)<8 || rd(0x44)) { return false; }
    wr(0x38,8); avail.flags=1; avail.index=0; used.index=0;
    address(0x80,(uintptr_t)descriptors); address(0x90,(uintptr_t)&avail); address(0xa0,(uintptr_t)&used);
    wr(0x44,1); wr(0x70,15);
    uint64_t capacity=0; bool stable=false;
    for (unsigned i=0;i<8;++i) {
        uint32_t generation=rd(0xfc); uint32_t lo=rd(0x100),hi=rd(0x104);
        if (rd(0xfc)==generation) { capacity=(uint64_t)hi<<32|lo; stable=true; break; }
    }
    if (!stable || !capacity || capacity>UINT64_MAX/512) { return false; }
    ready=true; *device=(struct block_device){"qemu-virtio-blk",capacity,true,read_sector,NULL}; return true;
}
void platform_storage_info(void)
{
    console_write("QEMU virtio-mmio block: "); console_write(ready ? "ready (read-only driver)" : "not attached / no suitable device");
    console_write("\nMMIO base="); console_hex(base); console_write("\n");
}
void platform_wifi_probe(void) { console_write("No Pi Wi-Fi SDIO device on QEMU virt.\n"); }
void platform_wifi_init(void) { console_write("No Pi Wi-Fi SDIO device on QEMU virt; WPA3 association unavailable.\n"); }
void platform_wifi_status(void) { console_write("No live Pi SDIO transport on QEMU virt.\n"); }
