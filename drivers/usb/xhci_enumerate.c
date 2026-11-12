/* Direct-root USB2 with one outstanding request. Command/control setup is
 * bounded; persistent bulk and event rings cycle for read-only storage. */
#include "xhci_test.h"
#include "usb_bot.h"
#include "console.h"
struct session {
    const struct xhci_test_io *io; struct xhci_test_memory *m; struct xhci_test_state *s;
    const struct xhci_snapshot *snapshot; unsigned op,rt,db,command,ep,*event;
    uint64_t input,output,ep0,data; uint8_t slot;
};
static bool rd(struct session *t,unsigned offset,uint32_t *value)
{ return offset<=t->io->observer.span-4 && t->io->observer.read32(t->io->observer.context,offset,value) && *value!=UINT32_MAX; }
static bool wr(struct session *t,unsigned offset,uint32_t value)
{ return offset<=t->io->observer.span-4 && t->io->write32(t->io->observer.context,offset,value); }
static bool wr64(struct session *t,unsigned offset,uint64_t value)
{ return wr(t,offset,(uint32_t)value) && wr(t,offset+4,(uint32_t)(value>>32)); }
static void sync(struct session *t) { t->io->barrier(t->io->observer.context); }
static bool map(struct session *t,const void *p,size_t n,uint64_t *dma)
{ return t->io->map_dma(t->io->observer.context,p,n,dma) && !(*dma&63) && n-1<=UINT64_MAX-*dma; }
static enum xhci_test_result event_for(struct session *t,unsigned type,uint64_t pointer,unsigned endpoint,bool short_ok,uint32_t *result,uint32_t *transfer_status)
{
    uint64_t start=t->io->ticks(t->io->observer.context);
    for (unsigned spin=0;spin<20000000;++spin) {
        unsigned index=*t->event%XHCI_RING_TRBS,cycle=1^((*t->event/XHCI_RING_TRBS)&1);
        volatile struct xhci_trb *e=&t->m->event[index]; uint32_t control=e->control;
        if ((control&1)==cycle) {
            sync(t); uint64_t address=(uint64_t)e->parameter_hi<<32|e->parameter_lo;
            uint32_t status=e->status; unsigned code=status>>24,kind=(control>>10)&63;
            t->s->last_event_type=kind; t->s->last_completion=code; t->s->last_pointer=address;
            ++*t->event;
            if (!wr64(t,t->rt+0x38,(t->s->event_dma+(*t->event%XHCI_RING_TRBS)*16)|8)) { return XHCI_TEST_IO; }
            if (kind==type) {
                if (address!=pointer || (code!=1 && !(short_ok && code==13)) ||
                    (type==32 && (((control>>16)&31)!=endpoint || (control>>24)!=t->slot)) ||
                    (type==33 && t->slot && (control>>24)!=t->slot)) { return XHCI_TEST_EVENT; }
                if (transfer_status) { *transfer_status=status; }
                if (result) { *result=control; } return XHCI_TEST_OK;
            }
            if (kind!=34 || !(address>>24&255) || (address>>24&255)>t->snapshot->ports) { return XHCI_TEST_EVENT; }
        }
        if (!rd(t,t->op+4,&t->s->last_status)) { return XHCI_TEST_IO; }
        if (t->s->last_status&((1u<<2)|(1u<<12))) { return XHCI_TEST_IO; }
        if (t->io->ticks(t->io->observer.context)-start>=t->io->hz*2) { break; }
    }
    return XHCI_TEST_TIMEOUT;
}
static enum xhci_test_result event(struct session *t,unsigned type,uint64_t pointer,uint32_t *result)
{ return event_for(t,type,pointer,1,false,result,NULL); }
static enum xhci_test_result command(struct session *t,unsigned type,uint64_t pointer,uint32_t extra,uint32_t *result)
{
    if (t->command>=XHCI_RING_TRBS-1) { return XHCI_TEST_EVENT; }
    unsigned at=t->command++;
    t->m->command[at]=(struct xhci_trb){(uint32_t)pointer,(uint32_t)(pointer>>32),0,(type<<10)|extra|1};
    sync(t); if (!wr(t,t->db,0)) { return XHCI_TEST_IO; }
    return event(t,33,t->s->command_dma+at*16,result);
}
static enum xhci_test_result request(struct session *t,unsigned request_type,unsigned request_number,unsigned value,unsigned n)
{
    unsigned stages=n ? 3 : 2;
    if (n>sizeof(t->m->data) || t->ep+stages>=XHCI_RING_TRBS-1 || (n && request_type!=0x80)) { return XHCI_TEST_ARGUMENT; }
    for (unsigned i=0;i<sizeof(t->m->data);++i) { t->m->data[i]=0; }
    unsigned at=t->ep; t->ep+=stages; t->s->usb_requests=true;
    t->m->ep0[at]=(struct xhci_trb){request_type|(request_number<<8)|(value<<16),n<<16,8,(2u<<10)|(1u<<6)|(n ? 3u<<16 : 0)|1};
    if (n) { t->m->ep0[at+1]=(struct xhci_trb){(uint32_t)t->data,(uint32_t)(t->data>>32),n,(3u<<10)|(1u<<16)|1}; }
    t->m->ep0[at+stages-1]=(struct xhci_trb){0,0,0,(4u<<10)|(1u<<5)|(n ? 0 : 1u<<16)|1};
    sync(t); if (!wr(t,t->db+t->slot*4,1)) { return XHCI_TEST_IO; }
    return event(t,32,t->ep0+(at+stages-1)*16,NULL);
}
static enum xhci_test_result control(struct session *t,unsigned descriptor,unsigned n)
{ return n ? request(t,0x80,6,descriptor<<8,n) : XHCI_TEST_ARGUMENT; }
static void put32(unsigned char *p,uint32_t value)
{ for (unsigned i=0;i<4;++i) { p[i]=(unsigned char)(value>>(i*8)); } }
static void clear_input(struct session *t)
{ for (unsigned i=0;i<sizeof(t->m->input);++i) { t->m->input[i]=0; } }
struct pipe {
    struct xhci_trb *ring; uint64_t dma;
    unsigned endpoint,index,cycle,wraps;
};
static struct {
    struct session t; struct xhci_test_io io; struct xhci_snapshot snapshot;
    struct usb_device_info info; struct usb_bot bot;
    struct pipe in,out; uint64_t cbw,csw;
    unsigned events; bool online;
} disk={.bot={.error=BLOCK_NOT_READY}};
static void stop_storage(void)
{
    disk.online=false; disk.bot.ready=false;
    uint32_t value; struct session *t=&disk.t;
    if (!rd(t,t->op,&value) || !wr(t,t->op,value&~13u)) { return; }
    uint64_t start=t->io->ticks(t->io->observer.context);
    for (unsigned spin=0;spin<20000000;++spin) {
        if (rd(t,t->op+4,&value) && (value&1)) { t->s->halted=true; return; }
        if (t->io->ticks(t->io->observer.context)-start>=t->io->hz*2) { return; }
    }
}
static enum block_result bulk(void *context,bool input,void *data,unsigned length,unsigned *actual)
{
    (void)context; if (!disk.online) { return BLOCK_NOT_READY; }
    if (!data || !actual || !length || length>512 || (!input && length!=31 && length!=512)) { return BLOCK_ARGUMENT; }
    struct session *t=&disk.t; struct pipe *p=input ? &disk.in : &disk.out;
    unsigned char *buffer=input ? (length==13 ? t->m->csw : t->m->data) : (length==31 ? t->m->cbw : t->m->data);
    uint64_t dma=input ? (length==13 ? disk.csw : t->data) : (length==31 ? disk.cbw : t->data);
    for (unsigned i=0;i<length;++i) { buffer[i]=input ? 0 : ((const unsigned char *)data)[i]; }
    if (p->index==XHCI_RING_TRBS-1) {
        p->ring[p->index].control=(6u<<10)|2|p->cycle;
        sync(t); p->index=0; p->cycle^=1; ++p->wraps;
    }
    unsigned at=p->index++;
    p->ring[at]=(struct xhci_trb){(uint32_t)dma,(uint32_t)(dma>>32),length,(1u<<10)|(1u<<5)|(input ? 1u<<2 : 0)|p->cycle};
    sync(t);
    if (!wr(t,t->db+t->slot*4,p->endpoint)) { stop_storage(); return BLOCK_IO; }
    uint32_t status=0;
    enum xhci_test_result r=event_for(t,32,p->dma+at*16,p->endpoint,input,NULL,&status);
    if (r!=XHCI_TEST_OK || (status&0xffffff)>length) { stop_storage(); return r==XHCI_TEST_TIMEOUT ? BLOCK_TIMEOUT : BLOCK_IO; }
    *actual=length-(status&0xffffff);
    if (input) { for (unsigned i=0;i<*actual;++i) { ((unsigned char *)data)[i]=buffer[i]; } }
    return BLOCK_OK;
}
static enum block_result read_disk(void *context,uint64_t lba,unsigned char *sector)
{
    (void)context; enum block_result r=usb_bot_read(&disk.bot,lba,sector);
    if (r!=BLOCK_OK && r!=BLOCK_ARGUMENT && r!=BLOCK_RANGE && disk.online) { stop_storage(); }
    return r;
}
static enum block_result write_disk(void *context,uint64_t lba,const unsigned char *sector)
{
    (void)context;enum block_result r=usb_bot_write(&disk.bot,lba,sector);
    if (r!=BLOCK_OK && r!=BLOCK_ARGUMENT && r!=BLOCK_RANGE && disk.online) { stop_storage(); }return r;
}
static enum block_result flush_disk(void *context)
{
    (void)context;enum block_result r=usb_bot_flush(&disk.bot);
    if (r!=BLOCK_OK && disk.online) { stop_storage(); }return r;
}
static void endpoint_context(unsigned char *p,unsigned type,unsigned packet,uint64_t ring)
{
    put32(p+4,(3u<<1)|(type<<3)|(packet<<16));
    put32(p+8,(uint32_t)ring|1); put32(p+12,(uint32_t)(ring>>32)); put32(p+16,512);
}
static enum xhci_test_result start_storage(struct session *source,struct usb_device_info *info,struct block_device *device)
{
    if (!info->mass_storage || info->speed==2) { return XHCI_TEST_UNSUPPORTED; }
    unsigned in_packet=info->bulk_in_packet,out_packet=info->bulk_out_packet;
    if ((info->speed==3 && (in_packet!=512 || out_packet!=512)) ||
        (info->speed==1 && ((in_packet!=8 && in_packet!=16 && in_packet!=32 && in_packet!=64) ||
                           (out_packet!=8 && out_packet!=16 && out_packet!=32 && out_packet!=64)))) { return XHCI_TEST_UNSUPPORTED; }
    disk.io=*source->io; disk.snapshot=*source->snapshot; disk.events=*source->event;
    disk.info=*info; disk.t=*source;
    struct session *t=&disk.t; t->io=&disk.io; t->snapshot=&disk.snapshot; t->event=&disk.events;
    disk.in=(struct pipe){.ring=t->m->bulk_in,.endpoint=(info->bulk_in&15)*2+1,.cycle=1};
    disk.out=(struct pipe){.ring=t->m->bulk_out,.endpoint=(info->bulk_out&15)*2,.cycle=1};
    if (!map(t,t->m->bulk_in,sizeof(t->m->bulk_in),&disk.in.dma) ||
        !map(t,t->m->bulk_out,sizeof(t->m->bulk_out),&disk.out.dma) ||
        !map(t,t->m->cbw,sizeof(t->m->cbw),&disk.cbw) || !map(t,t->m->csw,sizeof(t->m->csw),&disk.csw)) { return XHCI_TEST_DMA; }
    unsigned cs=t->snapshot->context64 ? 64 : 32;
    clear_input(t);
    put32(t->m->input+4,1u|(1u<<disk.in.endpoint)|(1u<<disk.out.endpoint));
    for (unsigned i=0;i<cs;++i) { t->m->input[cs+i]=t->m->output[i]; }
    unsigned highest=disk.in.endpoint>disk.out.endpoint ? disk.in.endpoint : disk.out.endpoint;
    uint32_t slot=(uint32_t)t->m->input[cs]|(uint32_t)t->m->input[cs+1]<<8|(uint32_t)t->m->input[cs+2]<<16|(uint32_t)t->m->input[cs+3]<<24;
    put32(t->m->input+cs,(slot&~(31u<<27))|(highest<<27));
    endpoint_context(t->m->input+(disk.in.endpoint+1)*cs,6,info->bulk_in_packet,disk.in.dma);
    endpoint_context(t->m->input+(disk.out.endpoint+1)*cs,2,info->bulk_out_packet,disk.out.dma);
    disk.in.ring[255]=(struct xhci_trb){(uint32_t)disk.in.dma,(uint32_t)(disk.in.dma>>32),0,(6u<<10)|3};
    disk.out.ring[255]=(struct xhci_trb){(uint32_t)disk.out.dma,(uint32_t)(disk.out.dma>>32),0,(6u<<10)|3};
    t->s->phase=XHCI_STORAGE_CONFIG;
    enum xhci_test_result r=command(t,12,t->input,(uint32_t)t->slot<<24,NULL);
    if (r!=XHCI_TEST_OK) { return r; }
    r=request(t,0,9,info->configuration,0); if (r!=XHCI_TEST_OK) { return r; }
    info->configured=true; disk.info.configured=true; disk.online=true;
    struct usb_bot_io bot_io={bulk,NULL};
    if (!usb_bot_attach(&disk.bot,&bot_io,device)) { disk.online=false; return XHCI_TEST_IO; }
    device->read=read_disk;device->write=write_disk;device->flush=flush_disk; device->context=NULL; return XHCI_TEST_OK;
}
void xhci_storage_info(void)
{
    console_write("USB BOT: "); console_write(disk.online && disk.bot.ready ? "ready, read-only" : "not ready");
    console_write(" result="); console_write(block_error(disk.bot.error)); console_write("\n");
    if (disk.bot.vendor[0]) {
        console_write("SCSI vendor="); console_write(disk.bot.vendor); console_write(" product="); console_write(disk.bot.product); console_write("\n");
    }
    console_write("sense_key="); console_u64(disk.bot.sense_key); console_write(" ASC="); console_hex(disk.bot.asc);
    console_write(" ASCQ="); console_hex(disk.bot.ascq); console_write("\nring_wraps_in="); console_u64(disk.in.wraps);
    console_write(" out="); console_u64(disk.out.wraps); console_write(" events="); console_u64(disk.events/256); console_write("\n");
}
enum xhci_test_result xhci_enumerate(const struct xhci_test_io *io,struct xhci_test_memory *m,struct xhci_test_state *s,
                                  const struct xhci_snapshot *snapshot,unsigned *event_cursor,struct usb_device_info *info,struct block_device *storage)
{
    struct session t={.io=io,.m=m,.s=s,.snapshot=snapshot,.op=snapshot->cap_length,.command=4,.event=event_cursor};
    if (!rd(&t,20,&t.db) || !rd(&t,24,&t.rt)) { return XHCI_TEST_IO; } t.db&=~3u; t.rt&=~31u;
    unsigned limit=io->observer.span<0xc000 ? io->observer.span : 0xc000;
    unsigned slots=snapshot->slots<64 ? snapshot->slots : 64;
    if (t.db>limit-(slots+1)*4) { return XHCI_TEST_LAYOUT; }
    if (!map(&t,m->input,sizeof(m->input),&t.input) || !map(&t,(const void *)m->output,sizeof(m->output),&t.output) ||
        !map(&t,m->ep0,sizeof(m->ep0),&t.ep0) || !map(&t,m->data,sizeof(m->data),&t.data)) { return XHCI_TEST_DMA; }
    s->phase=XHCI_PORT; unsigned port=0;
    for (unsigned i=0;i<snapshot->ports;++i) {
        uint32_t status;
        if (!rd(&t,t.op+0x400+i*16,&status)) { return XHCI_TEST_IO; }
        if ((status&1) && snapshot->port[i].protocol_major==2) { port=i+1; break; }
    }
    if (!port) { return XHCI_TEST_UNSUPPORTED; }
    info->port=(uint8_t)port; unsigned port_reg=t.op+0x400+(port-1)*16; uint32_t status;
    if (!rd(&t,port_reg,&status) || !wr(&t,port_reg,(status&((1u<<9)|(7u<<25)))|(1u<<4))) { return XHCI_TEST_IO; }
    uint64_t start=io->ticks(io->observer.context); bool reset=false;
    for (unsigned spin=0;spin<20000000;++spin) {
        if (!rd(&t,port_reg,&status)) { return XHCI_TEST_IO; }
        if (!(status&1)) { return XHCI_TEST_IO; }
        if (!(status&(1u<<4)) && (status&2)) { reset=true; break; }
        if (io->ticks(io->observer.context)-start>=io->hz*2) { break; }
    }
    if (!reset) { return XHCI_TEST_TIMEOUT; }
    if (!wr(&t,port_reg,(status&((1u<<9)|(7u<<25)))|(status&0xfe0000))) { return XHCI_TEST_IO; }
    unsigned speed=(status>>10)&15;
    if (speed<1 || speed>3) { return XHCI_TEST_UNSUPPORTED; } info->speed=(uint8_t)speed;
    unsigned packet=speed==3 ? 64 : 8,cs=snapshot->context64 ? 64 : 32;
    s->phase=XHCI_SLOT; uint32_t result;
    enum xhci_test_result r=command(&t,9,0,0,&result); if (r!=XHCI_TEST_OK) { return r; }
    t.slot=(uint8_t)(result>>24); info->slot=t.slot;
    if (!t.slot || t.slot>64) { return XHCI_TEST_EVENT; }
    m->dcbaa[t.slot]=t.output; clear_input(&t); put32(m->input+4,3);
    put32(m->input+cs,(speed<<20)|(1u<<27)); put32(m->input+cs+4,port<<16);
    put32(m->input+2*cs+4,(3u<<1)|(4u<<3)|(packet<<16));
    put32(m->input+2*cs+8,(uint32_t)t.ep0|1); put32(m->input+2*cs+12,(uint32_t)(t.ep0>>32));
    put32(m->input+2*cs+16,8);
    m->ep0[XHCI_RING_TRBS-1]=(struct xhci_trb){(uint32_t)t.ep0,(uint32_t)(t.ep0>>32),0,(6u<<10)|3};
    s->phase=XHCI_ADDRESS;
    s->usb_requests=true; /* Address Device sends SET_ADDRESS on the USB wire. */
    r=command(&t,11,t.input,(uint32_t)t.slot<<24,NULL); if (r!=XHCI_TEST_OK) { return r; }
    s->phase=XHCI_DESCRIPTOR;
    r=control(&t,1,8); if (r!=XHCI_TEST_OK) { return r; }
    unsigned mps=m->data[7];
    if (m->data[0]!=18 || m->data[1]!=1 || (mps!=8 && mps!=16 && mps!=32 && mps!=64) ||
        (speed==3 && mps!=64) || (speed==2 && mps!=8)) { return XHCI_TEST_UNSUPPORTED; }
    if (mps!=packet) {
        clear_input(&t); put32(m->input+4,2);
        for (unsigned i=0;i<cs;++i) { m->input[2*cs+i]=m->output[cs+i]; }
        put32(m->input+2*cs+4,(3u<<1)|(4u<<3)|(mps<<16));
        r=command(&t,13,t.input,(uint32_t)t.slot<<24,NULL); if (r!=XHCI_TEST_OK) { return r; }
    }
    info->max_packet=(uint16_t)mps;
    r=control(&t,1,18); if (r!=XHCI_TEST_OK) { return r; }
    if (m->data[7]!=mps || !usb_parse_device(m->data,18,info)) { return XHCI_TEST_UNSUPPORTED; }
    s->phase=XHCI_CONFIG; r=control(&t,2,9); if (r!=XHCI_TEST_OK) { return r; }
    if (m->data[0]!=9 || m->data[1]!=2) { return XHCI_TEST_UNSUPPORTED; }
    unsigned length=m->data[2]|(unsigned)m->data[3]<<8;
    if (length<9 || length>256) { return XHCI_TEST_UNSUPPORTED; }
    r=control(&t,2,length); if (r!=XHCI_TEST_OK) { return r; }
    if (!usb_parse_configuration(m->data,length,info)) { return XHCI_TEST_UNSUPPORTED; }
    return storage ? start_storage(&t,info,storage) : XHCI_TEST_OK;
}
