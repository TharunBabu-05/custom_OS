/* Original polling SDPCM/BCDC control bring-up. Register/packet definitions:
 * brcmfmac sdio.c, bcmsdh.c, bcdc.c and fwil.h (see docs/wifi-control.md). */
#include "brcm_control.h"
static uint32_t le32(const unsigned char *p)
{ return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static void put32(unsigned char *p,uint32_t v)
{ for (unsigned i=0;i<4;++i) { p[i]=(unsigned char)(v>>(8*i)); } }
enum block_result brcm_sdpcm_header(const unsigned char *p,size_t n,struct brcm_sdpcm_header *h)
{
    if (!p || !h || n<12) { return BLOCK_ARGUMENT; }
    *h=(struct brcm_sdpcm_header){0};uint32_t hardware=le32(p);
    if (!hardware) { return BLOCK_NOT_READY; }
    uint16_t len=(uint16_t)hardware,complement=(uint16_t)(hardware>>16);
    if ((uint16_t)(len^complement)!=65535) { return BLOCK_IO; }
    if (len<12 || len>BRCM_CONTROL_FRAME_MAX || p[7]<12 || p[7]>len) { return BLOCK_RANGE; }
    if (p[5]>2) { return BLOCK_UNSUPPORTED; } /* no glom/test/descriptor */
    *h=(struct brcm_sdpcm_header){len,p[4],p[5],p[7],p[9]};return BLOCK_OK;
}
#define TRY(call) do { r=(call);if (r!=BLOCK_OK) { goto fail; } } while (0)
#define REQUIRE(test,err) do { if (!(test)) { r=(err);goto fail; } } while (0)
static enum block_result read8(const struct brcm_control_io *io,struct brcm_control_state *s,unsigned fn,uint32_t at,uint8_t *v)
{ s->last_address=at;enum block_result r=io->read8(io->context,fn,at,v);if (r==BLOCK_OK) { s->last_word=*v; }return r; }
static enum block_result write8(const struct brcm_control_io *io,struct brcm_control_state *s,unsigned fn,uint32_t at,uint8_t v)
{ s->last_address=at;return io->write8(io->context,fn,at,v); }
static enum block_result read32(const struct brcm_control_io *io,struct brcm_control_state *s,uint32_t at,uint32_t *v)
{
    s->last_address=at;enum block_result r=io->read32(io->context,at,v);
    if (r==BLOCK_OK) { s->last_word=*v;if (*v==UINT32_MAX) { return BLOCK_IO; } }return r;
}
static enum block_result write32(const struct brcm_control_io *io,struct brcm_control_state *s,uint32_t at,uint32_t v)
{ s->last_address=at;return io->write32(io->context,at,v); }
static enum block_result packet(const struct brcm_control_io *io,struct brcm_control_state *s,bool write,void *p,size_t n)
{ s->last_address=0x8000;return io->packet(io->context,write,p,n); }
static enum block_result receive(const struct brcm_control_io *io,struct brcm_control_state *s)
{
    enum block_result r=packet(io,s,false,s->frame,64);if (r!=BLOCK_OK) { return r; }
    for (unsigned i=0;i<3;++i) { s->last_header[i]=le32(s->frame+i*4); }
    struct brcm_sdpcm_header h;r=brcm_sdpcm_header(s->frame,64,&h);
    if (r==BLOCK_NOT_READY) { s->pending=false;++s->empty_frames;return BLOCK_OK; }
    if (r!=BLOCK_OK) { return r; }s->pending=true;s->frame_length=h.length;
    if (++s->rx_frames>(s->rx_limit ? s->rx_limit : 64)) { return BLOCK_RANGE; }
    if (h.sequence!=s->rx_seq) { ++s->seq_resync; }s->rx_seq=(uint8_t)(h.sequence+1);
    if ((uint8_t)(h.window-s->tx_seq)>64) { return BLOCK_IO; }s->tx_max=h.window;
    size_t total=(h.length+3u)&~3u;
    for (size_t at=64;at<total;) {
        size_t n=total-at;if (n>512) { n=512; }
        r=packet(io,s,false,s->frame+at,n);if (r!=BLOCK_OK) { return r; }at+=n;
    }
    if (h.channel==1) { ++s->event_frames;if (s->event) { s->event(s->event_context,s->frame+h.offset,h.length-h.offset); }return BLOCK_OK; }
    if (h.channel==2) { ++s->data_frames;return BLOCK_OK; }
    size_t payload=h.length-h.offset;
    if (!payload) { return BLOCK_OK; } /* credit-only frame */
    if (payload<16) { return BLOCK_RANGE; }
    const unsigned char *cdc=s->frame+h.offset;
    uint32_t command=le32(cdc),length=le32(cdc+4)&65535,flags=le32(cdc+8),status=le32(cdc+12);
    if (!s->waiting || (flags>>16)!=s->request_id) {
        if (++s->stale_replies>8) { return BLOCK_IO; }return BLOCK_OK;
    }
    s->firmware_status=status;
    if (command!=s->command || (flags&0xf000) || (!s->set_request && (flags&2))) { return BLOCK_IO; }
    if (flags&1) { s->firmware_rejected=true;s->replied=true;s->response_length=0;return BLOCK_OK; }
    if (status) { return BLOCK_IO; }
    /* SET completion has no output contract. Some firmware echoes the input
     * length without returning those bytes. Like the reference set path,
     * validate its matching CDC header/status, then consume no output. */
    if (s->set_request) { s->response_length=0;s->replied=true;return BLOCK_OK; }
    if (length>payload-16 || length>s->capacity) { return BLOCK_RANGE; }
    for (unsigned i=0;i<length;++i) { s->response[i]=cdc[16+i]; }
    s->response_length=length;s->replied=true;return BLOCK_OK;
}
static enum block_result pump(const struct brcm_control_io *io,struct brcm_control_state *s)
{
    enum block_result r;TRY(read32(io,s,0x18004020,&s->interrupt));
    REQUIRE(!(s->interrupt&0xff00),BLOCK_IO);
    TRY(read32(io,s,0x1800404c,&s->mailbox));
    REQUIRE(!(s->mailbox&0x10),BLOCK_IO);
    if (s->mailbox&0xa) { REQUIRE(((s->mailbox>>16)&255)==4,BLOCK_UNSUPPORTED);s->device_ready=true; }
    if (s->interrupt&0x80) { TRY(write32(io,s,0x18004040,2)); }
    if (s->interrupt&0xf0) { TRY(write32(io,s,0x18004020,s->interrupt&0xf0)); }
    if (s->pending || (s->interrupt&0x40)) { TRY(receive(io,s)); }
    return BLOCK_OK;
fail:return r;
}
static bool credit(const struct brcm_control_state *s)
{ uint8_t n=(uint8_t)(s->tx_max-s->tx_seq);return n && n<=64; }
static enum block_result wait_for(const struct brcm_control_io *io,struct brcm_control_state *s,bool reply)
{
    for (unsigned i=0;i<500;++i) {
        ++s->polls;enum block_result r=pump(io,s);if (r!=BLOCK_OK) { return r; }
        if (s->device_ready && (reply ? s->replied : credit(s))) { return BLOCK_OK; }
        r=io->delay_us(io->context,10000);if (r!=BLOCK_OK) { return r; }
    }return BLOCK_TIMEOUT;
}
static enum block_result exchange(const struct brcm_control_io *io,struct brcm_control_state *s,uint32_t command,bool set,const unsigned char *input,unsigned capacity)
{
    s->waiting=false;s->firmware_rejected=false;s->firmware_status=0;
    enum block_result r=wait_for(io,s,false);if (r!=BLOCK_OK) { return r; }
    for (unsigned i=0;i<sizeof(s->response);++i) { s->response[i]=0; }
    unsigned length=12+16+capacity,wire=(length+3)&~3u;for (unsigned i=0;i<wire;++i) { s->frame[i]=0; }
    put32(s->frame,((uint32_t)(uint16_t)~length<<16)|length);s->frame[4]=s->tx_seq;s->frame[7]=12;
    ++s->request_id;s->command=command;s->capacity=capacity;s->response_length=0;s->replied=false;s->set_request=set;s->firmware_rejected=false;s->firmware_status=0;
    put32(s->frame+12,command);put32(s->frame+16,capacity);put32(s->frame+20,(uint32_t)s->request_id<<16|(set ? 2u : 0));
    for (unsigned i=0;i<capacity;++i) { s->frame[28+i]=input[i]; }
    s->waiting=true;r=packet(io,s,true,s->frame,wire);if (r!=BLOCK_OK) { s->waiting=false;return r; }
    ++s->tx_seq;++s->tx_frames;r=wait_for(io,s,true);s->waiting=false;
    return r==BLOCK_OK && s->firmware_rejected ? BLOCK_UNSUPPORTED : r;
}
static enum block_result query(const struct brcm_control_io *io,struct brcm_control_state *s,uint32_t command,unsigned capacity,const char *name)
{
    unsigned char input[256]={0};if (capacity>sizeof(input)) { return BLOCK_ARGUMENT; }
    if (name) { unsigned i=0;while (name[i] && i+1<capacity) { input[i]=(unsigned char)name[i];++i; } }
    return exchange(io,s,command,false,input,capacity);
}
static bool same(const char *a,const char *b)
{ while (*a && *a==*b) { ++a;++b; }return *a==*b; }
enum block_result brcm_control_iovar(const struct brcm_control_io *io,struct brcm_control_state *s,bool set,const char *name,const void *input,size_t n,void *output,size_t capacity,size_t *actual,uint32_t *status)
{
    if (!io || !s || !name || !actual || !status || !io->packet || !io->read32 || !io->write32 || !io->delay_us || (n && !input) || (capacity && !output)) { return BLOCK_ARGUMENT; }
    *actual=0;*status=0;
    if (!s->ready || s->error!=BLOCK_OK || !s->f2_ready) { return BLOCK_NOT_READY; }
    unsigned name_length=0;while (name_length<32 && name[name_length]) { ++name_length; }
    if (!name_length || name_length==32) { return BLOCK_ARGUMENT; }++name_length;
    bool allowed=set ? same(name,"clmload") || same(name,"country") || same(name,"event_msgs") || same(name,"escan") :
        same(name,"cap") || same(name,"mfp") || same(name,"sup_wpa") || same(name,"cur_etheraddr") || same(name,"clmver") || same(name,"clmload_status") || same(name,"country") || same(name,"event_msgs") || same(name,"scan_ver");
    if (!allowed) { return BLOCK_UNSUPPORTED; }
    size_t bytes=name_length+(set ? n : capacity);
    if (n>1536-name_length || capacity>1536-name_length || bytes>1536 || (!set && (!capacity || n>capacity))) { return BLOCK_ARGUMENT; }
    unsigned char payload[1536]={0};for (unsigned i=0;i<name_length;++i) { payload[i]=(unsigned char)name[i]; }
    for (size_t i=0;i<n;++i) { payload[name_length+i]=((const unsigned char *)input)[i]; }
    enum block_result r=exchange(io,s,set ? 263 : 262,set,payload,(unsigned)bytes);*status=s->firmware_status;
    if (r!=BLOCK_OK) {
        if (!s->firmware_rejected) { s->ready=false;s->error=r; }return r;
    }
    size_t amount=s->response_length;if (amount>capacity) { amount=capacity; }
    for (size_t i=0;i<amount;++i) { ((unsigned char *)output)[i]=s->response[i]; }*actual=amount;return BLOCK_OK;
}
enum block_result brcm_control_poll(const struct brcm_control_io *io,struct brcm_control_state *s)
{
    if (!io || !s || !io->read32 || !io->write32 || !io->packet) { return BLOCK_ARGUMENT; }
    if (!s->ready || !s->f2_ready || s->waiting) { return BLOCK_NOT_READY; }
    enum block_result r=pump(io,s);if (r!=BLOCK_OK) { s->ready=false;s->error=r; }return r;
}
enum block_result brcm_control_radio(const struct brcm_control_io *io,struct brcm_control_state *s,bool up,uint32_t *status)
{
    if (!io || !s || !status || !io->read32 || !io->write32 || !io->packet || !io->delay_us) { return BLOCK_ARGUMENT; }
    if (!s->ready || !s->f2_ready || s->error!=BLOCK_OK) { return BLOCK_NOT_READY; }
    const unsigned char value[4]={0};enum block_result r=exchange(io,s,up ? 2 : 3,true,value,4);*status=s->firmware_status;
    if (r!=BLOCK_OK && !s->firmware_rejected) { s->ready=false;s->error=r; }return r;
}
enum block_result brcm_control_start(const struct brcm_control_io *io,const struct brcm_start_state *boot,struct brcm_control_state *s)
{
    if (!io || !boot || !s || !io->read8 || !io->write8 || !io->read32 || !io->write32 || !io->packet || !io->delay_us) { return BLOCK_ARGUMENT; }
    if (s->attempted) { return s->error; }
    *s=(struct brcm_control_state){.phase=CONTROL_CHECK,.error=BLOCK_NOT_READY};
    if (!boot->ready || boot->error!=BLOCK_OK || boot->phase!=START_READY) { return s->error; }
    enum block_result r;uint8_t low,high;
    TRY(read8(io,s,1,0x1000e,&s->clock_csr));REQUIRE(s->clock_csr!=255 && (s->clock_csr&0xc0)==0xc0,BLOCK_NOT_READY);
    TRY(read8(io,s,0,2,&s->enable));REQUIRE((s->enable&6)==2,BLOCK_NOT_READY);
    s->attempted=true;s->phase=CONTROL_SETUP;
    TRY(write8(io,s,1,0x1000e,0x12)); /* HT request + force HT; no save/restore. */
    TRY(read8(io,s,1,0x1000e,&s->clock_csr));REQUIRE((s->clock_csr&0xf3)==0xd2,BLOCK_IO);
    TRY(write8(io,s,0,0x210,0));TRY(write8(io,s,0,0x211,2)); /* F2 block size 512. */
    TRY(read8(io,s,0,0x210,&low));TRY(read8(io,s,0,0x211,&high));s->block_size=(uint16_t)(low|(unsigned)high<<8);
    REQUIRE(s->block_size==512,BLOCK_IO);
    TRY(write32(io,s,0x18004048,4u<<16));
    TRY(write8(io,s,0,2,s->enable|4));s->enable|=4;s->phase=CONTROL_F2;
    for (unsigned i=0;i<500;++i) {
        TRY(read8(io,s,0,3,&s->io_ready));if ((s->io_ready&6)==6) { s->f2_ready=true;break; }
        TRY(io->delay_us(io->context,10000));
    }REQUIRE(s->f2_ready,BLOCK_TIMEOUT);
    /* The observed SDIO device ID is 0x4345, not 0xa9bf (43455).
     * Use brcmfmac's default watermark for this ID, without the 43455 tuning. */
    TRY(write8(io,s,1,0x10008,8));
    TRY(write32(io,s,0x18004024,0xf0)); /* Poll only; CPU IRQs remain masked. */
    s->phase=CONTROL_MAILBOX;
    for (unsigned i=0;i<500;++i) {
        ++s->polls;TRY(pump(io,s));if (s->device_ready) { break; }TRY(io->delay_us(io->context,10000));
    }REQUIRE(s->device_ready,BLOCK_TIMEOUT);
    s->phase=CONTROL_CREDIT;TRY(wait_for(io,s,false));
    s->phase=CONTROL_ABI;TRY(query(io,s,1,4,NULL));
    REQUIRE(s->response_length==4,BLOCK_RANGE);s->ioctl_version=le32(s->response);
    REQUIRE(s->ioctl_version==1 || s->ioctl_version==2,BLOCK_UNSUPPORTED);
    s->phase=CONTROL_VERSION;TRY(query(io,s,262,256,"ver"));
    bool terminated=false;
    for (unsigned i=0;i<s->response_length;++i) {
        unsigned ch=s->response[i];if (!ch) { s->version[i]=0;terminated=i!=0;break; }
        REQUIRE(ch>=32 || ch=='\r' || ch=='\n' || ch=='\t',BLOCK_IO);REQUIRE(ch<=126,BLOCK_IO);
        s->version[i]=(char)(ch<32 ? ' ' : ch);
    }REQUIRE(terminated,BLOCK_RANGE);
    s->ready=true;s->phase=CONTROL_READY;s->error=BLOCK_OK;return BLOCK_OK;
fail:s->error=r;s->waiting=false;return r;
}
