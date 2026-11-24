/* Direct-root USB2, one outstanding request. All rings are reusable; each
 * command/transfer and its recovery retain bounded waits and exact matching. */
#include "xhci_test.h"
#include "usb_bot.h"
#include "console.h"
_Static_assert(offsetof(struct xhci_test_memory,bulk_in)-offsetof(struct xhci_test_memory,data)==4096,
               "Keyboard transfers must fit in the xHCI DMA allocation's alignment gap");
struct session {
    const struct xhci_test_io *io; struct xhci_test_memory *m; struct xhci_test_state *s;
    const struct xhci_snapshot *snapshot; unsigned op,rt,db,command,ep,*event;
    unsigned command_cycle,ep_cycle,command_wraps,ep_wraps;
    volatile unsigned char *output_context;
    struct xhci_trb *control_ring;
    unsigned control_trbs;
    uint64_t transfer_wait;
    bool discarding_slot;
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
    uint64_t wait=t->transfer_wait ? t->transfer_wait : t->io->hz*2;
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
                if (address!=pointer ||
                    (type==32 && (((control>>16)&31)!=endpoint || (control>>24)!=t->slot)) ||
                    (type==33 && t->slot && (control>>24)!=endpoint)) { return XHCI_TEST_EVENT; }
                if (transfer_status) { *transfer_status=status; }
                if (type==32 && code==6) { return XHCI_TEST_STALL; }
                if (code!=1 && !(short_ok && code==13)) { return XHCI_TEST_EVENT; }
                if (result) { *result=control; } return XHCI_TEST_OK;
            }
            if (t->discarding_slot && kind==32 && (control>>24)==t->slot) { continue; }
            if (kind!=34 || !(address>>24&255) || (address>>24&255)>t->snapshot->ports) { return XHCI_TEST_EVENT; }
        }
        if (!rd(t,t->op+4,&t->s->last_status)) { return XHCI_TEST_IO; }
        if (t->s->last_status&((1u<<2)|(1u<<12))) { return XHCI_TEST_IO; }
        if (t->io->ticks(t->io->observer.context)-start>=wait) { break; }
    }
    return XHCI_TEST_TIMEOUT;
}
static enum xhci_test_result event(struct session *t,unsigned type,uint64_t pointer,uint32_t *result)
{ return event_for(t,type,pointer,1,false,result,NULL); }
/* Publish ownership last: the device may poll the next TRB before a doorbell.
 * A segment is reused only after the previous synchronous TD completed. */
static void publish(struct session *t,struct xhci_trb *p,struct xhci_trb value)
{
    p->parameter_lo=value.parameter_lo;p->parameter_hi=value.parameter_hi;p->status=value.status;
    sync(t);((volatile struct xhci_trb *)p)->control=value.control;
}
static void reserve_ring(struct session *t,struct xhci_trb *ring,uint64_t dma,
                         unsigned *index,unsigned *cycle,unsigned *wraps,unsigned needed,unsigned count)
{
    if (*index+needed<=count-1) { return; }
    /* Keep a control TD contiguous. Unused tail entries are Transfer No-ops;
     * the single Link TRB stays at the end of the segment (xHCI 6.4.4.1). */
    while (*index<count-1) {
        publish(t,&ring[(*index)++],(struct xhci_trb){0,0,0,(8u<<10)|*cycle});
    }
    publish(t,&ring[count-1],(struct xhci_trb){(uint32_t)dma,(uint32_t)(dma>>32),0,(6u<<10)|2|*cycle});
    *index=0;*cycle^=1;++*wraps;
}
static enum xhci_test_result command(struct session *t,unsigned type,uint64_t pointer,uint32_t extra,uint32_t *result)
{
    reserve_ring(t,t->m->command,t->s->command_dma,&t->command,&t->command_cycle,&t->command_wraps,1,XHCI_RING_TRBS);
    unsigned at=t->command++;
    publish(t,&t->m->command[at],(struct xhci_trb){(uint32_t)pointer,(uint32_t)(pointer>>32),0,(type<<10)|extra|t->command_cycle});
    sync(t); if (!wr(t,t->db,0)) { return XHCI_TEST_IO; }
    return event_for(t,33,t->s->command_dma+at*16,type==23 ? 0 : t->slot,false,result,NULL);
}
static enum xhci_test_result request_index(struct session *t,unsigned request_type,unsigned request_number,unsigned value,unsigned index,unsigned n)
{
    unsigned stages=n ? 3 : 2;
    if (n>sizeof(t->m->data) || (n && request_type!=0x80)) { return XHCI_TEST_ARGUMENT; }
    struct xhci_trb *ring=t->control_ring;
    reserve_ring(t,ring,t->ep0,&t->ep,&t->ep_cycle,&t->ep_wraps,stages,t->control_trbs);
    for (unsigned i=0;i<sizeof(t->m->data);++i) { t->m->data[i]=0; }
    unsigned at=t->ep; t->ep+=stages; t->s->usb_requests=true;
    /* Queue the tail before publishing Setup, which makes the whole TD live. */
    publish(t,&ring[at+stages-1],(struct xhci_trb){0,0,0,(4u<<10)|(1u<<5)|(n ? 0 : 1u<<16)|t->ep_cycle});
    if (n) { publish(t,&ring[at+1],(struct xhci_trb){(uint32_t)t->data,(uint32_t)(t->data>>32),n,(3u<<10)|(1u<<16)|t->ep_cycle}); }
    publish(t,&ring[at],(struct xhci_trb){request_type|(request_number<<8)|(value<<16),(n<<16)|index,8,(2u<<10)|(1u<<6)|(n ? 3u<<16 : 0)|t->ep_cycle});
    sync(t); if (!wr(t,t->db+t->slot*4,1)) { return XHCI_TEST_IO; }
    return event(t,32,t->ep0+(at+stages-1)*16,NULL);
}
static enum xhci_test_result request(struct session *t,unsigned type,unsigned number,unsigned value,unsigned n)
{ return request_index(t,type,number,value,0,n); }
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
    unsigned events,clears; bool online,keyboard_blocked;
} disk={.bot={.error=BLOCK_NOT_READY}};

static struct {
    struct usb_device_info device;
    uintptr_t controller;
    unsigned length;
    unsigned char descriptor[USB_CONFIGURATION_MAX];
} inventory;

void xhci_storage_ports(void)
{
    if (!disk.online || !disk.bot.ready) {
        console_write("USB ports unavailable. Start USB storage first.\n"); return;
    }
    struct xhci_snapshot snapshot;
    enum usb_probe_result result=xhci_observe(&disk.io.observer,&snapshot);
    if (result!=USB_PROBE_OK || snapshot.ports!=disk.snapshot.ports || snapshot.cap_length!=disk.t.op) {
        console_write("USB port snapshot unavailable.\n"); return;
    }
    xhci_report_ports(&disk.io.observer,&snapshot);
}

unsigned xhci_storage_port(uintptr_t controller)
{
    return disk.t.s && (uintptr_t)disk.io.observer.context==controller ? disk.info.port : 0;
}

void xhci_report_ports(const struct xhci_observer *io,const struct xhci_snapshot *snapshot)
{
    unsigned storage=xhci_storage_port((uintptr_t)io->context);
    console_write("USB root ports controller="); console_hex((uintptr_t)io->context); console_write("\n");
    for (unsigned i=0;i<snapshot->ports;++i) {
        const struct usb_port_snapshot *port=&snapshot->port[i];
        uint32_t status=port->status;
        console_write("USB root port="); console_u64(i+1);
        console_write(" protocol="); console_u64(port->protocol_major);
        console_write(status&1 ? " present=yes" : " present=no");
        console_write(status&2 ? " enabled=yes" : " enabled=no");
        console_write(status&(1u<<17) ? " connect_change=yes" : " connect_change=no");
        console_write(" PORTSC="); console_hex(status);
        console_write(i+1==storage ? " owner=usb-bot\n" : " owner=none\n");
    }
}

static void report_configuration(const unsigned char *config,unsigned length,const struct usb_device_info *device)
{
    console_write("USB configuration="); console_u64(config[5]); console_write(" interfaces="); console_u64(config[4]);
    console_write(" descriptor_bytes="); console_u64(length); console_write("\n");
    for (unsigned at=9;at<length;at+=config[at]) {
        const unsigned char *d=config+at;
        if (d[1]==4) {
            console_write("USB interface="); console_u64(d[2]); console_write(" alt="); console_u64(d[3]);
            console_write(" class="); console_hex(d[5]); console_write(" subclass="); console_hex(d[6]);
            console_write(" protocol="); console_hex(d[7]); console_write(" endpoints="); console_u64(d[4]);
            if (device->configured && !d[3] && d[2]==device->interface_number) { console_write(" driver=usb-bot"); }
            if (d[5]==3 && d[6]==1 && d[7]==1) { console_write(" kind=hid-boot-keyboard"); }
            if (d[5]==3 && d[6]==1 && d[7]==2) { console_write(" kind=hid-boot-mouse"); }
            if (d[5]==9) { console_write(" kind=hub"); }
            console_write("\n");
        } else if (d[1]==5) {
            static const char *const transfer[]={"control","isochronous","bulk","interrupt"};
            console_write("  endpoint="); console_hex(d[2]); console_write(d[2]&0x80 ? " direction=in" : " direction=out");
            console_write(" type="); console_write(transfer[d[3]&3]);
            console_write(" max_packet="); console_u64(d[4]|(unsigned)(d[5]&7)<<8);
            console_write(" bInterval="); console_u64(d[6]); console_write("\n");
        }
    }
}

void xhci_device_inventory(void)
{
    if (!inventory.length) { console_write("USB inventory empty. Initialize storage or enumerate a device first.\n"); return; }
    const struct usb_device_info *device=&inventory.device;
    const char *state=device->configured ? (disk.online && disk.bot.ready ? "storage-ready" : "storage-offline") : "descriptors-only";
    console_write("USB cached device controller="); console_hex(inventory.controller);
    console_write(" port="); console_u64(device->port); console_write(" slot="); console_u64(device->slot);
    console_write(" VID="); console_hex(device->vendor); console_write(" PID="); console_hex(device->product);
    console_write(" speed_id="); console_u64(device->speed); console_write(" state="); console_write(state); console_write("\n");
    report_configuration(inventory.descriptor,inventory.length,device);
}
static void next_pipe(struct session *t,struct pipe *p)
{
    if (p->index==XHCI_RING_TRBS-1) {
        p->ring[p->index].control=(6u<<10)|2|p->cycle;
        sync(t);p->index=0;p->cycle^=1;++p->wraps;
    }
}
static enum block_result stop_storage(void)
{
    disk.online=false; disk.bot.ready=false;
    uint32_t value; struct session *t=&disk.t;
    if (!rd(t,t->op,&value) || !wr(t,t->op,value&~13u)) { return BLOCK_IO; }
    uint64_t start=t->io->ticks(t->io->observer.context);
    for (unsigned spin=0;spin<20000000;++spin) {
        if (!rd(t,t->op+4,&value)) { return BLOCK_IO; }
        if (value&1) { t->s->halted=true;return BLOCK_OK; }
        if (t->io->ticks(t->io->observer.context)-start>=t->io->hz*2) { break; }
    }
    return BLOCK_TIMEOUT;
}
enum block_result xhci_storage_quiesce(void)
{
    return !disk.t.s || disk.t.s->halted ? BLOCK_OK : stop_storage();
}
static enum block_result bulk(void *context,bool input,void *data,unsigned length,unsigned *actual)
{
    (void)context; if (!disk.online) { return BLOCK_NOT_READY; }
    if (!data || !actual || !length || length>512 || (!input && length!=31 && length!=512)) { return BLOCK_ARGUMENT; }
    *actual=0;
    struct session *t=&disk.t; struct pipe *p=input ? &disk.in : &disk.out;
    unsigned char *buffer=input ? (length==13 ? t->m->csw : t->m->data) : (length==31 ? t->m->cbw : t->m->data);
    uint64_t dma=input ? (length==13 ? disk.csw : t->data) : (length==31 ? disk.cbw : t->data);
    for (unsigned i=0;i<length;++i) { buffer[i]=input ? 0 : ((const unsigned char *)data)[i]; }
    next_pipe(t,p);
    unsigned at=p->index++;
    publish(t,&p->ring[at],(struct xhci_trb){(uint32_t)dma,(uint32_t)(dma>>32),length,(1u<<10)|(1u<<5)|(input ? 1u<<2 : 0)|p->cycle});
    sync(t);
    if (!wr(t,t->db+t->slot*4,p->endpoint)) { stop_storage(); return BLOCK_IO; }
    uint32_t status=0;
    enum xhci_test_result r=event_for(t,32,p->dma+at*16,p->endpoint,input,NULL,&status);
    /* A validated STALL completion halts this endpoint, not the controller.
     * BOT decides whether it is a recoverable data/status stall. No replay. */
    if (r==XHCI_TEST_STALL) { return BLOCK_STALL; }
    if (r!=XHCI_TEST_OK || (status&0xffffff)>length) { stop_storage(); return r==XHCI_TEST_TIMEOUT ? BLOCK_TIMEOUT : BLOCK_IO; }
    *actual=length-(status&0xffffff);
    if (input) { for (unsigned i=0;i<*actual;++i) { ((unsigned char *)data)[i]=buffer[i]; } }
    return BLOCK_OK;
}
static enum block_result clear_halt(void *context,bool input)
{
    (void)context;if (!disk.online) { return BLOCK_NOT_READY; }
    struct session *t=&disk.t;struct pipe *p=input ? &disk.in : &disk.out;
    uint32_t target=((uint32_t)t->slot<<24)|(p->endpoint<<16);
    /* xHCI 4.6.8: reset host endpoint (TSP=0), clear device ENDPOINT_HALT,
     * then move dequeue past the stalled TD. No request is resubmitted. */
    enum xhci_test_result r=command(t,14,0,target,NULL);
    if (r==XHCI_TEST_OK) { r=request_index(t,2,1,0,input ? disk.info.bulk_in : disk.info.bulk_out,0); }
    if (r==XHCI_TEST_OK) { next_pipe(t,p);r=command(t,16,(p->dma+p->index*16)|p->cycle,target,NULL); }
    if (r!=XHCI_TEST_OK) { stop_storage();return r==XHCI_TEST_TIMEOUT ? BLOCK_TIMEOUT : BLOCK_IO; }
    ++disk.clears;
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
    struct usb_bot_io bot_io={.transfer=bulk,.clear_halt=clear_halt};
    if (!usb_bot_attach(&disk.bot,&bot_io,device)) { disk.online=false; return XHCI_TEST_IO; }
    device->read=read_disk;device->write=write_disk;device->flush=flush_disk; device->context=NULL; return XHCI_TEST_OK;
}
bool xhci_storage_check(void)
{
    if (!disk.online || !disk.bot.ready) { console_write("USB check unavailable; run storageinit on a fresh boot.\n");return false; }
    struct session *t=&disk.t;unsigned char before[512],after[512];
    console_write("USB check: 300 descriptor/No-op pairs and sector-zero comparison; no disk writes.\n");
    if (read_disk(NULL,0,before)!=BLOCK_OK) { goto fail; }
    uint64_t started=t->io->ticks(t->io->observer.context);
    for (unsigned i=0;i<300;++i) {
        if ((t->io->ticks(t->io->observer.context)-started)/t->io->hz>=30) { goto fail; }
        if (command(t,23,0,0,NULL)!=XHCI_TEST_OK || control(t,1,18)!=XHCI_TEST_OK) { goto fail; }
        struct usb_device_info observed={0};
        if (!usb_parse_device(t->m->data,18,&observed) || observed.vendor!=disk.info.vendor || observed.product!=disk.info.product ||
            observed.usb_version!=disk.info.usb_version || t->m->data[7]!=disk.info.max_packet) { goto fail; }
    }
    if (read_disk(NULL,0,after)!=BLOCK_OK) { goto fail; }
    for (unsigned i=0;i<512;++i) { if (before[i]!=after[i]) { goto fail; } }
    console_write("PASS: USB command/control ring reuse, device identity and sector zero unchanged; no disk writes.\n");return true;
fail:
    if (disk.online) { disk.bot.error=BLOCK_IO;stop_storage(); }
    console_write("FAIL: USB ring check; capture storageinfo and reboot. No disk writes issued by this check.\n");return false;
}
void xhci_storage_info(void)
{
    console_write("USB BOT: "); console_write(disk.online && disk.bot.ready ? "ready; boot read-only, data opt-in" : "not ready");
    console_write(" result="); console_write(block_error(disk.bot.error)); console_write("\n");
    if (disk.bot.vendor[0]) {
        console_write("SCSI vendor="); console_write(disk.bot.vendor); console_write(" product="); console_write(disk.bot.product); console_write("\n");
    }
    console_write("sense_key="); console_u64(disk.bot.sense_key); console_write(" ASC="); console_hex(disk.bot.asc);
    console_write(" ASCQ="); console_hex(disk.bot.ascq); console_write("\nring_wraps_in="); console_u64(disk.in.wraps);
    console_write(" out="); console_u64(disk.out.wraps); console_write(" events="); console_u64(disk.events/256); console_write("\n");
    console_write("BOT last_opcode=");console_hex(disk.bot.trace.opcode);console_write(" stage=");console_u64(disk.bot.trace.stage);
    console_write(" lba=");console_u64(disk.bot.trace.lba);console_write(" expected=");console_u64(disk.bot.trace.expected);
    console_write(" actual=");console_u64(disk.bot.trace.actual);console_write(" CSW_status=");console_u64(disk.bot.trace.status);
    console_write(" residue=");console_u64(disk.bot.trace.residue);console_write(" sense_valid=");console_write(disk.bot.sense_valid ? "yes" : "no");
    console_write("\nCache policy=");console_write(disk.bot.write_through ? "device reports WCE=0 (write-through; rechecked at sync)" : "SYNCHRONIZE CACHE required; no assumed write-through");console_write("\n");
    console_write("BOT stalls_recovered=");console_u64(disk.bot.stalls_recovered);console_write(" cache_query=");console_u64(disk.bot.cache_query);console_write("\n");
    console_write("Recovery rings: clears=");console_u64(disk.clears);console_write(" command_wraps=");console_u64(disk.t.command_wraps);
    console_write(" control_wraps=");console_u64(disk.t.ep_wraps);console_write("\n");
    if (disk.t.s) {
        console_write("xHCI event_type=");console_u64(disk.t.s->last_event_type);console_write(" completion=");console_u64(disk.t.s->last_completion);
        console_write(" halted=");console_write(disk.t.s->halted ? "yes" : "no");console_write("\n");
    }
}
static enum xhci_test_result enumerate_port(struct session *t,unsigned port,unsigned protected_slot,
                                             struct usb_device_info *info,unsigned *length_out)
{
    const struct xhci_test_io *io=t->io;
    const struct xhci_snapshot *snapshot=t->snapshot;
    struct xhci_test_memory *m=t->m;
    struct xhci_test_state *s=t->s;
    s->phase=XHCI_PORT;
    info->port=(uint8_t)port; unsigned port_reg=t->op+0x400+(port-1)*16; uint32_t status;
    if (!rd(t,port_reg,&status) || !wr(t,port_reg,(status&((1u<<9)|(7u<<25)))|(1u<<4))) { return XHCI_TEST_IO; }
    uint64_t start=io->ticks(io->observer.context); bool reset=false;
    for (unsigned spin=0;spin<20000000;++spin) {
        if (!rd(t,port_reg,&status)) { return XHCI_TEST_IO; }
        if (!(status&1)) { return XHCI_TEST_IO; }
        if (!(status&(1u<<4)) && (status&2)) { reset=true; break; }
        if (io->ticks(io->observer.context)-start>=io->hz*2) { break; }
    }
    if (!reset) { return XHCI_TEST_TIMEOUT; }
    if (!wr(t,port_reg,(status&((1u<<9)|(7u<<25)))|(status&0xfe0000))) { return XHCI_TEST_IO; }
    unsigned speed=(status>>10)&15;
    if (speed<1 || speed>3) { return XHCI_TEST_UNSUPPORTED; } info->speed=(uint8_t)speed;
    unsigned packet=speed==3 ? 64 : 8,cs=snapshot->context64 ? 64 : 32;
    s->phase=XHCI_SLOT; uint32_t result;
    enum xhci_test_result r=command(t,9,0,0,&result); if (r!=XHCI_TEST_OK) { return r; }
    unsigned slot=result>>24;
    if (!slot || slot>64 || slot>snapshot->slots || slot==protected_slot) { return XHCI_TEST_EVENT; }
    t->slot=info->slot=(uint8_t)slot;
    m->dcbaa[t->slot]=t->output; clear_input(t); put32(m->input+4,3);
    put32(m->input+cs,(speed<<20)|(1u<<27)); put32(m->input+cs+4,port<<16);
    put32(m->input+2*cs+4,(3u<<1)|(4u<<3)|(packet<<16));
    put32(m->input+2*cs+8,(uint32_t)t->ep0|1); put32(m->input+2*cs+12,(uint32_t)(t->ep0>>32));
    put32(m->input+2*cs+16,8);
    t->control_ring[t->control_trbs-1]=(struct xhci_trb){(uint32_t)t->ep0,(uint32_t)(t->ep0>>32),0,(6u<<10)|3};
    s->phase=XHCI_ADDRESS;
    s->usb_requests=true; /* Address Device sends SET_ADDRESS on the USB wire. */
    r=command(t,11,t->input,(uint32_t)t->slot<<24,NULL); if (r!=XHCI_TEST_OK) { return r; }
    s->phase=XHCI_DESCRIPTOR;
    r=control(t,1,8); if (r!=XHCI_TEST_OK) { return r; }
    unsigned mps=m->data[7];
    if (m->data[0]!=18 || m->data[1]!=1 || (mps!=8 && mps!=16 && mps!=32 && mps!=64) ||
        (speed==3 && mps!=64) || (speed==2 && mps!=8)) { return XHCI_TEST_UNSUPPORTED; }
    if (mps!=packet) {
        clear_input(t); put32(m->input+4,2);
        for (unsigned i=0;i<cs;++i) { m->input[2*cs+i]=t->output_context[cs+i]; }
        put32(m->input+2*cs+4,(3u<<1)|(4u<<3)|(mps<<16));
        r=command(t,13,t->input,(uint32_t)t->slot<<24,NULL); if (r!=XHCI_TEST_OK) { return r; }
    }
    info->max_packet=(uint16_t)mps;
    r=control(t,1,18); if (r!=XHCI_TEST_OK) { return r; }
    if (m->data[7]!=mps || !usb_parse_device(m->data,18,info)) { return XHCI_TEST_UNSUPPORTED; }
    s->phase=XHCI_CONFIG; r=control(t,2,9); if (r!=XHCI_TEST_OK) { return r; }
    if (m->data[0]!=9 || m->data[1]!=2) { return XHCI_TEST_UNSUPPORTED; }
    unsigned length=m->data[2]|(unsigned)m->data[3]<<8;
    if (length<9 || length>USB_CONFIGURATION_MAX) { return XHCI_TEST_UNSUPPORTED; }
    r=control(t,2,length); if (r!=XHCI_TEST_OK) { return r; }
    if (!usb_parse_configuration(m->data,length,info)) { return XHCI_TEST_UNSUPPORTED; }
    *length_out=length;
    return XHCI_TEST_OK;
}

static enum xhci_test_result accessory_request(struct session *,const struct xhci_accessory *,unsigned,unsigned *,unsigned *);
static void report_accessory(const struct xhci_accessory *,unsigned,unsigned);
static bool release_slot(struct session *t)
{
    if (!t->slot) { return t->s->phase!=XHCI_SLOT; }
    t->discarding_slot=true;
    if (command(t,10,0,(uint32_t)t->slot<<24,NULL)!=XHCI_TEST_OK) { return false; }
    t->m->dcbaa[t->slot]=0;
    return true;
}

enum xhci_test_result xhci_enumerate(const struct xhci_test_io *io,struct xhci_test_memory *m,struct xhci_test_state *s,
                                  const struct xhci_snapshot *snapshot,unsigned *event_cursor,struct usb_device_info *info,struct block_device *storage,const struct xhci_accessory *accessory)
{
    struct session t={.io=io,.m=m,.s=s,.snapshot=snapshot,.op=snapshot->cap_length,.command=4,.event=event_cursor,.command_cycle=1,.ep_cycle=1,
                      .output_context=m->output,.control_ring=m->ep0,.control_trbs=XHCI_RING_TRBS};
    if (!rd(&t,20,&t.db) || !rd(&t,24,&t.rt)) { return XHCI_TEST_IO; } t.db&=~3u; t.rt&=~31u;
    unsigned limit=io->observer.span<0xc000 ? io->observer.span : 0xc000;
    unsigned slots=snapshot->slots<64 ? snapshot->slots : 64;
    if (t.db>limit-(slots+1)*4) { return XHCI_TEST_LAYOUT; }
    if (!map(&t,m->input,sizeof(m->input),&t.input) || !map(&t,(const void *)m->output,sizeof(m->output),&t.output) ||
        !map(&t,m->ep0,sizeof(m->ep0),&t.ep0) || !map(&t,m->data,sizeof(m->data),&t.data)) { return XHCI_TEST_DMA; }
    if (accessory) {
        unsigned usage=0,modifiers=0;
        enum xhci_test_result result=accessory_request(&t,accessory,0,&usage,&modifiers);
        if (!release_slot(&t) && result==XHCI_TEST_OK) { result=XHCI_TEST_EVENT; }
        if (result==XHCI_TEST_OK) { report_accessory(accessory,usage,modifiers); }
        return result;
    }
    s->phase=XHCI_PORT;
    for (unsigned i=0;i<snapshot->ports;++i) {
        struct usb_port_snapshot port=snapshot->port[i];
        if (!rd(&t,t.op+0x400+i*16,&port.status)) { return XHCI_TEST_IO; }
        if (!(storage ? xhci_storage_candidate(&port) : port.protocol_major==2 && (port.status&1))) { continue; }
        unsigned length;
        *info=(struct usb_device_info){0};
        enum xhci_test_result r=enumerate_port(&t,i+1,0,info,&length);
        if (r!=XHCI_TEST_OK && r!=XHCI_TEST_UNSUPPORTED) { return r; }
        if (storage && (r!=XHCI_TEST_OK || !info->mass_storage || info->speed==2)) {
            if (!release_slot(&t)) { return XHCI_TEST_EVENT; }
            t.slot=0;
            t.discarding_slot=false;
            t.ep=t.ep_wraps=0;
            t.ep_cycle=1;
            for (unsigned j=0;j<t.control_trbs;++j) { t.control_ring[j]=(struct xhci_trb){0}; }
            for (unsigned j=0;j<2048;++j) { t.output_context[j]=0; }
            continue;
        }
        if (r!=XHCI_TEST_OK) { return r; }
        /* BOT can reuse the descriptor DMA buffer during the attach. */
        inventory.length=0;
        for (unsigned j=0;j<length;++j) { inventory.descriptor[j]=m->data[j]; }
        if (storage) {
            r=start_storage(&t,info,storage);
            if (r!=XHCI_TEST_OK) { return r; }
        }
        inventory.device=*info;
        inventory.controller=(uintptr_t)io->observer.context;
        inventory.length=length;
        return XHCI_TEST_OK;
    }
    return storage ? XHCI_TEST_NO_BOT : XHCI_TEST_UNSUPPORTED;
}

static enum xhci_test_result keyboard_input(struct session *t,const struct usb_device_info *info,
                                            const struct usb_boot_keyboard *key,unsigned *usage,unsigned *modifiers)
{
    unsigned ep=(key->endpoint&15)*2+1,cs=t->snapshot->context64 ? 64 : 32;
    unsigned interval=key->interval-1;
    if (info->speed!=3) {
        interval=0;
        for (unsigned uf=(unsigned)key->interval*8;uf>1;uf>>=1) { ++interval; }
        if (info->speed==2 && key->packet!=8) { return XHCI_TEST_UNSUPPORTED; }
    } else if (interval>15) { return XHCI_TEST_UNSUPPORTED; }
    struct xhci_trb *ring=t->m->key_interrupt;
    unsigned char *buffer=t->m->key_report;
    uint64_t ring_dma,buffer_dma;
    if (!map(t,ring,sizeof(t->m->key_interrupt),&ring_dma) || !map(t,buffer,sizeof(t->m->key_report),&buffer_dma)) {
        return XHCI_TEST_DMA;
    }
    clear_input(t);
    put32(t->m->input+4,1u|(1u<<ep));
    unsigned char *slot=t->m->input+cs;
    for (unsigned i=0;i<cs;++i) { slot[i]=t->output_context[i]; }
    slot[3]=(slot[3]&7)|(unsigned char)(ep<<3);
    unsigned char *context=t->m->input+(ep+1)*cs;
    endpoint_context(context,7,key->packet,ring_dma);
    put32(context,interval<<16);
    put32(context+16,8u|((unsigned)key->packet<<16));
    enum xhci_test_result r=command(t,12,t->input,(uint32_t)t->slot<<24,NULL);
    if (r==XHCI_TEST_OK) { r=request(t,0,9,info->configuration,0); }
    if (r==XHCI_TEST_OK) { r=request_index(t,0x21,11,0,key->interface,0); }
    if (r!=XHCI_TEST_OK) { return r; }

    console_write("USB keyboard ready. Press a key within 8 seconds.\n");
    uint64_t start=t->io->ticks(t->io->observer.context),budget=t->io->hz*8;
    /* One completed report at a time. The temporary ring is discarded with its slot. */
    for (unsigned at=0;at<XHCI_KEY_TRBS-1;++at) {
        uint64_t elapsed=t->io->ticks(t->io->observer.context)-start;
        if (elapsed>=budget) { break; }
        publish(t,&ring[at],(struct xhci_trb){(uint32_t)buffer_dma,(uint32_t)(buffer_dma>>32),8,(1u<<10)|(1u<<5)|(1u<<2)|1});
        sync(t);
        if (!wr(t,t->db+t->slot*4,ep)) { return XHCI_TEST_IO; }
        uint32_t status;
        t->transfer_wait=budget-elapsed;
        r=event_for(t,32,ring_dma+at*16,ep,true,NULL,&status);
        t->transfer_wait=0;
        if (r!=XHCI_TEST_OK) { return r; }
        if ((status&0xffffff) || buffer[1]) { continue; }
        unsigned code=0;
        for (unsigned i=2;i<8;++i) {
            unsigned keycode=buffer[i];
            if (keycode && keycode<4) { code=0; break; }
            if (keycode && !code) { code=keycode; }
        }
        if (code) { *usage=code; *modifiers=buffer[0]; return XHCI_TEST_OK; }
    }
    return XHCI_TEST_TIMEOUT;
}

static enum xhci_test_result accessory_request(struct session *t,const struct xhci_accessory *accessory,
                                               unsigned protected,unsigned *usage,unsigned *modifiers)
{
    struct usb_device_info info={0};
    unsigned length;
    enum xhci_test_result result=enumerate_port(t,accessory->port,protected,&info,&length);
    if (result!=XHCI_TEST_OK) { return result; }
    if (accessory->keyboard) {
        struct usb_boot_keyboard key;
        return usb_find_boot_keyboard(t->m->data,length,&key) ? keyboard_input(t,&info,&key,usage,modifiers) : XHCI_TEST_UNSUPPORTED;
    }
    console_write("USB accessory port="); console_u64(info.port);
    console_write(" VID="); console_hex(info.vendor); console_write(" PID="); console_hex(info.product); console_write("\n");
    report_configuration(t->m->data,length,&info);
    return XHCI_TEST_OK;
}

static void report_accessory(const struct xhci_accessory *accessory,unsigned usage,unsigned modifiers)
{
    if (!accessory->keyboard) { console_write("PASS: USB accessory descriptors read.\n"); return; }
    console_write("PASS: USB keyboard usage="); console_hex(usage);
    console_write(" modifiers="); console_hex(modifiers); console_write("; keyboard slot released.\n");
}

void xhci_accessory_active(const struct xhci_accessory *accessory)
{
    unsigned port=accessory->port;
    if (!disk.online || !disk.bot.ready || disk.keyboard_blocked || disk.io.hz>UINT64_MAX/8) {
        console_write("USB keyboard test unavailable in this storage session.\n"); return;
    }
    struct session t=disk.t;
    uint32_t status;
    if (!port || port>t.snapshot->ports || port==disk.info.port || t.snapshot->port[port-1].protocol_major!=2 ||
        !rd(&t,t.op+0x400+(port-1)*16,&status) || !(status&1)) {
        console_write("USB keyboard test requires a connected spare USB2 port. See usbports.\n"); return;
    }
    t.slot=0;
    t.ep=t.ep_wraps=0;
    t.ep_cycle=1;
    t.control_ring=t.m->key_control;
    t.control_trbs=XHCI_KEY_TRBS;
    /* An output context occupies 32 entries of at most 64 bytes. */
    t.output_context=t.m->output+2048;
    if (!map(&t,(const void *)t.output_context,2048,&t.output) ||
        !map(&t,t.control_ring,sizeof(t.m->key_control),&t.ep0)) {
        console_write("USB keyboard DMA mapping failed.\n"); return;
    }
    for (unsigned i=0;i<2048;++i) { t.output_context[i]=0; }
    for (unsigned i=0;i<XHCI_KEY_TRBS;++i) {
        t.control_ring[i]=(struct xhci_trb){0};
        t.m->key_interrupt[i]=(struct xhci_trb){0};
    }

    enum xhci_test_phase storage_phase=t.s->phase;
    unsigned usage=0,modifiers=0;
    enum xhci_test_result result=accessory_request(&t,accessory,disk.t.slot,&usage,&modifiers);
    enum xhci_test_phase failed_phase=t.s->phase;
    if (!release_slot(&t)) { disk.keyboard_blocked=true; }
    disk.t.command=t.command;
    disk.t.command_cycle=t.command_cycle;
    disk.t.command_wraps=t.command_wraps;
    t.s->phase=storage_phase;
    if (disk.keyboard_blocked) { console_write("USB keyboard slot cleanup unverified. Reboot before another test.\n"); return; }
    if (result!=XHCI_TEST_OK) {
        console_write("USB keyboard test ended: "); console_write(xhci_test_error(result));
        console_write(" phase="); console_u64(failed_phase); console_write("\n"); return;
    }
    report_accessory(accessory,usage,modifiers);
}
