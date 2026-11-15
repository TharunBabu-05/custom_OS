/* Real BOT/xHCI/persistent-volume code against a SanDisk-style device model.
 * Independent hardware ring cursors follow Link/No-op TRBs, not driver indices.
 * Every CURRENT mode-page CSW stalls once, including after 32 recoveries. */
#include "host_assert.h"
#include <stdio.h>
#include <string.h>
#include "persist.h"
#include "../drivers/usb/xhci_enumerate.c"
void console_write(const char *s) { fputs(s,stdout); }
void console_u64(uint64_t n) { (void)n; }
void console_hex(uint64_t n) { (void)n; }
enum { SECTORS=6144,START=4096 };
static unsigned char media[SECTORS*512];
static struct xhci_test_memory memory;
static struct xhci_test_state state;
static struct persist_volume volume,reopened;
struct cursor { unsigned index,cycle,wraps; };
struct model {
    struct cursor command,ep,in,out;
    unsigned produced,acked,stage,op,length,lba,tag,stalls,commands,writes,mode;
    unsigned recovery,endpoint,csw_attempts;
    bool halted,stopped;
    uint64_t time;
};
static uint64_t address(const void *p) { return 0x100000000ULL+(uintptr_t)p-(uintptr_t)&memory; }
static unsigned le(const unsigned char *p) { return p[0]|(unsigned)p[1]<<8|(unsigned)p[2]<<16|(unsigned)p[3]<<24; }
static unsigned be(const unsigned char *p) { return (unsigned)p[0]<<24|(unsigned)p[1]<<16|(unsigned)p[2]<<8|p[3]; }
static void put(unsigned char *p,unsigned v) { for (unsigned i=0;i<4;++i) { p[i]=(unsigned char)(v>>(i*8)); } }
static struct xhci_trb *take(struct xhci_trb *ring,struct cursor *c,bool control)
{
    for (unsigned n=0;n<4;++n) {
        assert(c->index<256);struct xhci_trb *t=&ring[c->index];
        assert((t->control&1)==c->cycle);unsigned type=(t->control>>10)&63;
        if (type==6) {
            assert(c->index==255 && t->control==((6u<<10)|2|c->cycle));
            assert(((uint64_t)t->parameter_hi<<32|t->parameter_lo)==address(ring));
            c->index=0;c->cycle^=1;++c->wraps;continue;
        }
        ++c->index;if (control && type==8) { assert(!(t->control&(1u<<5)));continue; }
        return t;
    }
    assert(0);return NULL;
}
static void emit(struct model *m,struct xhci_trb *t,unsigned type,unsigned endpoint,unsigned code,unsigned residual)
{
    assert(m->produced-m->acked<256);unsigned at=m->produced%256,cycle=1^((m->produced/256)&1);++m->produced;
    uint64_t ptr=address(t);
    memory.event[at]=(struct xhci_trb){(uint32_t)ptr,(uint32_t)(ptr>>32),(code<<24)|residual,
        (1u<<24)|(endpoint<<16)|(type<<10)|cycle};
}
static bool read_reg(void *ctx,uint32_t at,uint32_t *v)
{ struct model *m=ctx;assert(at==0x40 || at==0x44);*v=at==0x40 ? 1 : m->stopped ? 1 : 0;return true; }
static bool write_reg(void *ctx,uint32_t at,uint32_t value)
{
    struct model *m=ctx;
    if (at==0x40) { assert(!value);m->stopped=true;return true; }
    if (at==0x2038) { ++m->acked;assert((value&0xfff)==((m->acked%256)*16|8));return true; }
    if (at==0x203c) { return true; }
    if (at==0x1000) {
        struct xhci_trb *t=take(memory.command,&m->command,false);unsigned type=t->control>>10&63;
        if (type==23) {
            assert(!(t->control>>16));emit(m,t,33,0,1,0);memory.event[(m->produced-1)%256].control&=~(255u<<24);return true;
        }
        assert((t->control>>24)==1 && (t->control>>16&31)==m->endpoint);
        if (type==14) { assert(m->halted && !m->recovery);m->recovery=1; }
        else {
            assert(type==16 && m->recovery==2);struct pipe *p=m->endpoint==3 ? &disk.in : &disk.out;
            struct cursor *c=m->endpoint==3 ? &m->in : &m->out;
            uint64_t ptr=(uint64_t)t->parameter_hi<<32|t->parameter_lo;
            assert(ptr==((p->dma+p->index*16)|p->cycle));c->index=p->index;c->cycle=p->cycle;
            m->recovery=0;m->halted=false;
        }
        emit(m,t,33,0,1,0);return true;
    }
    assert(at==0x1004 && !m->stopped);
    if (value==1) {
        struct xhci_trb *t=take(memory.ep0,&m->ep,true);
        assert((t->control>>10&63)==2);
        if ((t->parameter_lo&0xffff)==0x680) {
            unsigned n=t->parameter_hi>>16;assert(!m->recovery && (n==8 || n==18));t=take(memory.ep0,&m->ep,true);
            assert((t->control>>10&63)==3 && t->status==n);
            const unsigned char desc[]={18,1,0,2,0,0,0,64,0x81,7,0x67,0x55,0,1,0,0,0,1};memcpy(memory.data,desc,n);
        } else {
            assert(m->recovery==1 && t->parameter_lo==0x102 && t->parameter_hi==(m->endpoint==3 ? 0x81u : 2u));m->recovery=2;
        }
        t=take(memory.ep0,&m->ep,true);assert((t->control>>10&63)==4);emit(m,t,32,1,1,0);return true;
    }
    assert(!m->halted && (value==3 || value==4));bool input=value==3;
    struct xhci_trb *t=take(input ? memory.bulk_in : memory.bulk_out,input ? &m->in : &m->out,false);
    assert((t->control>>10&63)==1);unsigned n=t->status;uint64_t ptr=(uint64_t)t->parameter_hi<<32|t->parameter_lo;
    assert(ptr>=address(&memory) && ptr+n<=address(&memory)+sizeof(memory));
    unsigned char *p=(unsigned char *)&memory+(ptr-address(&memory));
    if (!m->stage) {
        assert(!input && n==31 && le(p)==0x43425355);m->tag=le(p+4);assert(m->tag==++m->commands);
        m->op=p[15];m->length=le(p+8);m->lba=be(p+17);m->csw_attempts=0;m->stage=m->length ? 1 : 2;
    } else if (m->stage==1) {
        assert(n==m->length);m->stage=2;
        if (m->op==0x2a) { assert(!input && n==512 && m->lba>=START && m->lba<START+DATA_V2_MIN_SECTORS);memcpy(media+m->lba*512,p,512);++m->writes; }
        else {
            assert(input);memset(p,0,n);
            if (m->op==0x12) { memcpy(p+8,"SanDisk ",8);memcpy(p+16,"Cruzer Blade    ",16); }
            else if (m->op==0x25) { p[2]=(SECTORS-1)>>8;p[3]=(SECTORS-1)&255;p[6]=2; }
            else if (m->op==3) { p[0]=0x70;p[2]=5;p[7]=10;p[12]=0x20; }
            else if (m->op==0x1a) { assert(n==64);p[0]=23;p[4]=8;p[5]=18; }
            else { assert(m->op==0x28 && n==512 && m->lba<SECTORS);memcpy(p,media+m->lba*512,512); }
        }
    } else {
        assert(input && n==13);
        if (m->op==0x1a && !m->csw_attempts++) {
            ++m->stalls;m->halted=true;m->endpoint=value;emit(m,t,32,value,6,0);return true;
        }
        memset(p,0,n);put(p,0x53425355);put(p+4,m->tag);p[12]=m->op==0x35 ? 1 : 0;
        if (m->op==0x1a) { put(p+8,40); }m->stage=0;
    }
    emit(m,t,32,value,1,0);return true;
}
static bool mapping(void *ctx,const void *p,size_t n,uint64_t *v) { (void)ctx;(void)n;*v=address(p);return true; }
static void barrier(void *ctx) { (void)ctx; }
static uint64_t ticks(void *ctx) { return ++((struct model *)ctx)->time; }
int main(int argc,char **argv)
{
    assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f);
    assert(fread(media+START*512,1,2048*512,f)==2048*512);assert(!fclose(f));
    media[510]=0x55;media[511]=0xaa;media[450]=0xda;put(media+454,START);put(media+458,2048);
    struct model m={.command={.cycle=1},.ep={.cycle=1},.in={.cycle=1},.out={.cycle=1}};
    disk.online=true;disk.io=(struct xhci_test_io){{read_reg,&m,0x5000},write_reg,mapping,barrier,ticks,100};disk.snapshot.ports=3;
    disk.info.bulk_in=0x81;disk.info.bulk_out=2;state.command_dma=address(memory.command);state.event_dma=address((const void *)memory.event);
    disk.info.vendor=0x781;disk.info.product=0x5567;disk.info.usb_version=0x200;disk.info.max_packet=64;
    disk.t=(struct session){.io=&disk.io,.m=&memory,.s=&state,.snapshot=&disk.snapshot,.event=&disk.events,
        .op=0x40,.rt=0x2000,.db=0x1000,.slot=1,.ep0=address(memory.ep0),.data=address(memory.data),.command_cycle=1,.ep_cycle=1};
    disk.in=(struct pipe){.ring=memory.bulk_in,.dma=address(memory.bulk_in),.endpoint=3,.cycle=1};
    disk.out=(struct pipe){.ring=memory.bulk_out,.dma=address(memory.bulk_out),.endpoint=4,.cycle=1};
    memory.bulk_in[255]=(struct xhci_trb){(uint32_t)disk.in.dma,(uint32_t)(disk.in.dma>>32),0,(6u<<10)|3};
    memory.bulk_out[255]=(struct xhci_trb){(uint32_t)disk.out.dma,(uint32_t)(disk.out.dma>>32),0,(6u<<10)|3};
    disk.cbw=address(memory.cbw);disk.csw=address(memory.csw);
    struct block_device device;struct usb_bot_io io={.transfer=bulk,.clear_halt=clear_halt};assert(usb_bot_attach(&disk.bot,&io,&device));
    device.read=read_disk;device.write=write_disk;device.flush=flush_disk;device.context=NULL;
    assert(persist_mount(&volume,&device)==FS_OK && volume.version==2);
    unsigned char bytes[8192],readback[8192];for (unsigned i=0;i<sizeof(bytes);++i) { bytes[i]=(unsigned char)i; }
    /* Same sixteen chunk commits as DATACAP, plus enough to wrap all rings repeatedly. */
    for (unsigned i=0;i<160;++i) {
        unsigned n=(i%16+1)*512;enum fs_result r=persist_write(&volume,"/limit.bin",bytes,n,false);
        if (r!=FS_OK) { fprintf(stderr,"commit=%u stalls=%u commands=%u result=%u\n",i,m.stalls,m.commands,r); }
        assert(r==FS_OK && !volume.failed && !m.stopped);
    }
    assert(persist_mount(&reopened,&device)==FS_OK && reopened.generation==161);
    size_t n=0;assert(persist_read(&reopened,"/limit.bin",0,readback,sizeof(readback),&n)==FS_OK && n==8192 && !memcmp(bytes,readback,n));
    assert(persist_sync(&reopened)==FS_OK && m.stalls>480 && disk.clears==m.stalls && m.command.wraps>=3 && m.ep.wraps>=3);
    for (unsigned i=0;i<300;++i) { assert(control(&disk.t,1,8)==XHCI_TEST_OK); }
    assert(m.ep.wraps>=6 && disk.t.ep_wraps==m.ep.wraps);
    unsigned before=m.writes;bool checked=xhci_storage_check();
    if (!checked) { fprintf(stderr,"check failed: cmd=%u ep=%u completion=%u ptr=%llx online=%u stage=%u events=%u/%u/%u last=%x/%x\n",m.command.index,m.ep.index,state.last_completion,(unsigned long long)state.last_pointer,disk.online,m.stage,m.acked,m.produced,disk.events,memory.event[(m.produced-1)%256].control,memory.event[(m.produced-1)%256].status); }
    assert(checked && m.writes==before && m.command.wraps>=4);
    assert(m.in.wraps>400 && m.out.wraps>400 && m.acked==m.produced && disk.online);
    assert(m.writes==160*(DATA_V2_BANK_SECTORS+1));
    printf("PASS: 160 v2 commits, reboot-style remount and all 8192 bytes; %u CSW stalls recovered; command/control/bulk/event cycles reused; no WRITE replay.\n",m.stalls);
    return 0;
}
