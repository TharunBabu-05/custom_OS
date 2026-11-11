/* Original polling SDPCM/BCDC control bring-up. Register/packet definitions:
 * brcmfmac sdio.c, bcmsdh.c, bcdc.c and fwil.h (see docs/wifi-control.md). */
#include "brcm_control.h"
#include "brcm_keys.h"
#include "dhcp.h"
static void scrub(void *buffer,size_t n) { volatile unsigned char *p=buffer;while (n--) { *p++=0; } }
static uint32_t le32(const unsigned char *p)
{ return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static void put32(unsigned char *p,uint32_t v)
{ for (unsigned i=0;i<4;++i) { p[i]=(unsigned char)(v>>(8*i)); } }
static enum block_result header(const unsigned char *p,size_t n,unsigned maximum,struct brcm_sdpcm_header *h)
{
    if (!p || !h || n<12) { return BLOCK_ARGUMENT; }
    *h=(struct brcm_sdpcm_header){0};uint32_t hardware=le32(p);
    if (!hardware) { return BLOCK_NOT_READY; }
    uint16_t len=(uint16_t)hardware,complement=(uint16_t)(hardware>>16);
    if ((uint16_t)(len^complement)!=65535) { return BLOCK_IO; }
    if (len<12 || len>maximum || p[7]<12 || p[7]>len) { return BLOCK_RANGE; }
    unsigned channel=p[5]&15;bool descriptor=!!(p[5]&0x80);
    if ((p[5]&0x70) || channel>3 || (descriptor && channel!=3)) { return BLOCK_UNSUPPORTED; }
    *h=(struct brcm_sdpcm_header){len,p[4],(uint8_t)channel,p[7],p[9],descriptor};return BLOCK_OK;
}
enum block_result brcm_sdpcm_header(const unsigned char *p,size_t n,struct brcm_sdpcm_header *h)
{ return header(p,n,BRCM_CONTROL_FRAME_MAX,h); }
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
static enum block_result glom_fail(struct brcm_control_state *s,enum block_result r)
{ ++s->glom_failures;s->glom_error=r;s->glom_count=0;return r; }
static enum block_result glom_descriptor(struct brcm_control_state *s,const struct brcm_sdpcm_header *h)
{
    s->glom_phase=1;unsigned n=h->length-h->offset;s->glom_bytes=0;s->glom_wire_bytes=0;s->glom_last_count=n/2;s->glom_bad_index=0;
    if (!h->descriptor || !n || (n&1) || n/2>BRCM_GLOM_FRAMES) { return glom_fail(s,BLOCK_RANGE); }
    unsigned count=n/2;const unsigned char *p=s->frame+h->offset;
    for (unsigned i=0;i<count;++i) {
        s->glom_bad_index=i;unsigned len=p[2*i]|(unsigned)p[2*i+1]<<8;s->glom_lengths[i]=(uint16_t)len;
        if (len<(i ? 12u : 24u) || len>(i ? BRCM_CONTROL_FRAME_MAX : BRCM_CONTROL_FRAME_MAX+255u) || len>BRCM_GLOM_MAX-s->glom_bytes) { return glom_fail(s,BLOCK_RANGE); }
        s->glom_bytes+=len;
    }
    s->glom_wire_bytes=(s->glom_bytes+511u)&~511u;
    if (s->glom_wire_bytes>BRCM_GLOM_MAX) { return glom_fail(s,BLOCK_RANGE); }
    s->glom_count=count;++s->glom_descriptors;s->glom_error=BLOCK_OK;return BLOCK_OK;
}
static enum block_result glom_receive(const struct brcm_control_io *io,struct brcm_control_state *s)
{
    unsigned count=s->glom_count;s->glom_phase=2;
    /* No 64-byte speculative header read: the descriptor supplies the whole
     * block-rounded wire size. Each FIFO request remains <=512 bytes. */
    for (unsigned at=0;at<s->glom_wire_bytes;at+=512) {
        enum block_result r=packet(io,s,false,s->glom+at,512);if (r!=BLOCK_OK) { return glom_fail(s,r); }
    }
    for (unsigned i=0;i<3;++i) { s->glom_header[i]=le32(s->glom+4*i); }
    struct brcm_sdpcm_header outer;s->glom_phase=3;
    enum block_result r=header(s->glom,s->glom_wire_bytes,BRCM_GLOM_MAX,&outer);
    if (r!=BLOCK_OK) { return glom_fail(s,r==BLOCK_NOT_READY ? BLOCK_RANGE : r); }
    if (outer.channel!=3 || outer.descriptor || outer.length>s->glom_bytes || ((outer.length+511u)&~511u)!=s->glom_wire_bytes || outer.offset>s->glom_lengths[0]-12) { return glom_fail(s,BLOCK_RANGE); }
    if ((uint8_t)(outer.window-s->tx_seq)>64) { return glom_fail(s,BLOCK_IO); }
    /* Validate EVERY subframe before dispatching any callback. Control,
     * nested aggregates, unknown flags and descriptor payloads are refused. */
    struct brcm_sdpcm_header sub[BRCM_GLOM_FRAMES];unsigned starts[BRCM_GLOM_FRAMES],at=0;s->glom_phase=4;
    for (unsigned i=0;i<count;++i) {
        s->glom_bad_index=i;unsigned skip=i ? 0 : outer.offset;starts[i]=at+skip;unsigned available=s->glom_lengths[i]-skip;
        for (unsigned word=0;word<3;++word) { s->glom_subheader[word]=le32(s->glom+starts[i]+4*word); }
        r=header(s->glom+starts[i],available,BRCM_CONTROL_FRAME_MAX,&sub[i]);
        if (r!=BLOCK_OK) { return glom_fail(s,r==BLOCK_NOT_READY ? BLOCK_RANGE : r); }
        if (sub[i].descriptor || (sub[i].channel!=1 && sub[i].channel!=2) || sub[i].length>available || starts[i]+sub[i].length>outer.length) { return glom_fail(s,BLOCK_RANGE); }
        /* The aggregate header and first subframe use the same RX sequence.
         * Subsequent subframes consume consecutive sequence numbers. */
        if (sub[i].sequence!=(uint8_t)(outer.sequence+i)) { return glom_fail(s,BLOCK_IO); }
        at+=s->glom_lengths[i];
    }
    unsigned limit=s->rx_limit ? s->rx_limit : 64;
    if (s->rx_frames>=limit || count+1>limit-s->rx_frames) { return glom_fail(s,BLOCK_RANGE); }
    if (outer.sequence!=s->rx_seq) { ++s->seq_resync; }s->rx_seq=(uint8_t)(outer.sequence+count);s->tx_max=outer.window;
    s->rx_frames+=count+1;++s->glom_frames;s->glom_subframes+=count;s->glom_count=0;s->glom_phase=5;s->glom_error=BLOCK_OK;
    for (unsigned i=0;i<count;++i) {
        const unsigned char *data=s->glom+starts[i]+sub[i].offset;size_t n=sub[i].length-sub[i].offset;
        if (!n) { continue; }
        if (sub[i].channel==1) { ++s->event_frames;if (s->event) { s->event(s->event_context,data,n); } }
        else { ++s->data_frames;if (s->data_observer) { s->data_observer(s->data_context,data,n); } }
    }
    return BLOCK_OK;
}
static enum block_result receive(const struct brcm_control_io *io,struct brcm_control_state *s)
{
    if (s->glom_count) { return glom_receive(io,s); }
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
    if (h.channel==3) { return glom_descriptor(s,&h); }
    if (h.channel==1) { ++s->event_frames;if (s->event) { s->event(s->event_context,s->frame+h.offset,h.length-h.offset); }return BLOCK_OK; }
    if (h.channel==2) { ++s->data_frames;if (s->data_observer) { s->data_observer(s->data_context,s->frame+h.offset,h.length-h.offset); }return BLOCK_OK; }
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
static enum block_result exchange_guarded(const struct brcm_control_io *io,struct brcm_control_state *s,uint32_t command,bool set,const unsigned char *input,unsigned capacity,bool (*permitted)(void *),void *ctx)
{
    s->waiting=false;s->firmware_rejected=false;s->firmware_status=0;
    enum block_result r=wait_for(io,s,false);if (r!=BLOCK_OK) { return r; }
    if (permitted && !permitted(ctx)) { return BLOCK_NOT_READY; }
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
static enum block_result exchange(const struct brcm_control_io *io,struct brcm_control_state *s,uint32_t command,bool set,const unsigned char *input,unsigned capacity)
{ return exchange_guarded(io,s,command,set,input,capacity,NULL,NULL); }
static enum block_result query(const struct brcm_control_io *io,struct brcm_control_state *s,uint32_t command,unsigned capacity,const char *name)
{
    unsigned char input[256]={0};if (capacity>sizeof(input)) { return BLOCK_ARGUMENT; }
    if (name) { unsigned i=0;while (name[i] && i+1<capacity) { input[i]=(unsigned char)name[i];++i; } }
    return exchange(io,s,command,false,input,capacity);
}
static bool same(const char *a,const char *b)
{ while (*a && *a==*b) { ++a;++b; }return *a==*b; }
static enum block_result eapol_tx(const struct brcm_control_io *io,struct brcm_control_state *s,const void *ethernet,size_t n,bool (*permitted)(void *),void *ctx,bool fourth)
{
    if (!io || !s || !ethernet || !permitted || !io->packet || !io->read32 || !io->write32 || !io->delay_us) { return BLOCK_ARGUMENT; }
    if (!s->ready || !s->f2_ready || s->error!=BLOCK_OK || s->waiting) { return BLOCK_NOT_READY; }
    const unsigned char *p=ethernet;
    if (n<113 || n>370 || (p[0]&1) || (p[6]&1) || p[12]!=0x88 || p[13]!=0x8e || p[14]<1 || p[14]>3 || p[15]!=3 ||
        ((unsigned)p[16]<<8|p[17])!=n-18 || p[18]!=2 || p[19]!=(fourth ? 3 : 1) || p[20]!=8 || p[21] || p[22] ||
        ((unsigned)p[111]<<8|p[112])!=n-113) { return BLOCK_ARGUMENT; }
    if (fourth && (n!=113 || !brcm_key_iszero(p+31,64))) { return BLOCK_ARGUMENT; }
    unsigned dest=0,src=0,different=0;for (unsigned i=0;i<6;++i) { dest|=p[i];src|=p[6+i];different|=p[i]^p[6+i]; }
    if (!dest || !src || !different) { return BLOCK_ARGUMENT; }
    /* Stage before pumping RX: callers may be borrowing s->frame. */
    unsigned char frame[388]={0};unsigned length=16+(unsigned)n,wire=(length+3)&~3u;
    for (size_t i=0;i<n;++i) { frame[16+i]=p[i]; }
    enum block_result r=wait_for(io,s,false);
    if (r==BLOCK_OK) {
        /* Credit pumping can deliver DEAUTH or a failed exchange. Recheck
         * the owner AFTER callbacks and immediately before the FIFO write. */
        if (!permitted(ctx)) { return BLOCK_NOT_READY; }
        put32(frame,((uint32_t)(uint16_t)~length<<16)|length);frame[4]=s->tx_seq;frame[5]=2;frame[7]=12;frame[12]=0x20;
        r=packet(io,s,true,frame,wire);
        if (r==BLOCK_OK) { ++s->tx_seq;++s->tx_frames; }
    }
    if (r!=BLOCK_OK) { s->ready=false;s->error=r; }return r;
}
enum block_result brcm_control_eapol_m2(const struct brcm_control_io *io,struct brcm_control_state *s,const void *p,size_t n,bool (*guard)(void *),void *ctx)
{ return eapol_tx(io,s,p,n,guard,ctx,false); }
static bool peer_mac(const unsigned char *p)
{ unsigned v=0;if (!p || (p[0]&1)) { return false; }for (unsigned i=0;i<6;++i) { v|=p[i]; }return v!=0; }
enum block_result brcm_control_port(const struct brcm_control_io *io,struct brcm_control_state *s,const unsigned char peer[6],bool authorize,uint32_t *status,bool (*guard)(void *),void *ctx)
{
    if (!io || !s || !status || !guard || !peer_mac(peer) || !io->packet || !io->read32 || !io->write32 || !io->delay_us) { return BLOCK_ARGUMENT; }
    *status=0;if (!s->ready || !s->f2_ready || s->waiting || s->error!=BLOCK_OK) { return BLOCK_NOT_READY; }
    unsigned char mac[6];for (unsigned i=0;i<6;++i) { mac[i]=peer[i]; }
    enum block_result r=exchange_guarded(io,s,authorize ? 121 : 122,true,mac,6,guard,ctx);*status=s->firmware_status;
    if (r!=BLOCK_OK && r!=BLOCK_NOT_READY && !s->firmware_rejected) { s->ready=false;s->error=r; }return r;
}
enum block_result brcm_station_info_decode(const void *data,size_t n,const unsigned char peer[6],struct brcm_station_info *out)
{
    if (!out) { return BLOCK_ARGUMENT; }*out=(struct brcm_station_info){0};
    if (!data || !peer_mac(peer)) { return BLOCK_ARGUMENT; }const unsigned char *p=data;
    if (n<4) { return BLOCK_RANGE; }out->version=p[0]|(unsigned)p[1]<<8;out->length=p[2]|(unsigned)p[3]<<8;
    if (n>=12) { out->flags=le32(p+8); }
    if (out->length<22 || out->length>n || n>512) { return BLOCK_RANGE; }
    if (out->version<3 || out->version>7) { return BLOCK_UNSUPPORTED; }
    unsigned different=0;for (unsigned i=0;i<6;++i) { out->peer[i]=p[16+i];different|=peer[i]^p[16+i]; }
    if (different) { return BLOCK_UNSUPPORTED; }out->valid=true;
    if ((out->flags&0x4000) && out->length>=84) {
        out->statistics=true;out->tx_packets=le32(p+52);out->tx_failures=le32(p+56);out->rx_unicast=le32(p+60);out->rx_multicast=le32(p+64);
        out->decrypt_ok=le32(p+76);out->decrypt_fail=le32(p+80);
    }return BLOCK_OK;
}
enum block_result brcm_control_station_info(const struct brcm_control_io *io,struct brcm_control_state *s,const unsigned char peer[6],struct brcm_station_info *out,uint32_t *status)
{
    if (!out || !status || !peer_mac(peer)) { return BLOCK_ARGUMENT; }*out=(struct brcm_station_info){0};
    unsigned char reply[512]={0};size_t n=0;enum block_result r=brcm_control_iovar(io,s,false,"sta_info",peer,6,reply,sizeof(reply),&n,status);
    return r==BLOCK_OK ? brcm_station_info_decode(reply,n,peer,out) : r;
}
enum block_result brcm_control_eapol_m4(const struct brcm_control_io *io,struct brcm_control_state *s,const void *p,size_t n,bool (*guard)(void *),void *ctx)
{ return eapol_tx(io,s,p,n,guard,ctx,true); }
enum block_result brcm_control_dhcp(const struct brcm_control_io *io,struct brcm_control_state *s,const void *p,size_t n,bool (*guard)(void *),void *ctx)
{
    if (!io || !s || !guard || !io->read32 || !io->write32 || !io->packet || !io->delay_us || !dhcp_wire_request_valid(p,n)) { return BLOCK_ARGUMENT; }
    if (!s->ready || !s->f2_ready || s->waiting || s->error!=BLOCK_OK) { return BLOCK_NOT_READY; }
    unsigned char frame[360]={0};for (size_t i=0;i<n;++i) { frame[16+i]=((const unsigned char *)p)[i]; }
    enum block_result r=wait_for(io,s,false);if (r==BLOCK_OK) {
        if (!guard(ctx)) { return BLOCK_NOT_READY; }
        unsigned length=16+(unsigned)n;put32(frame,((uint32_t)(uint16_t)~length<<16)|length);frame[4]=s->tx_seq;frame[5]=2;frame[7]=12;frame[12]=0x20;
        r=packet(io,s,true,frame,(length+3u)&~3u);if (r==BLOCK_OK) { ++s->tx_seq;++s->tx_frames; }
    }
    if (r!=BLOCK_OK) { s->ready=false;s->error=r; }return r;
}
enum block_result brcm_control_netprobe(const struct brcm_control_io *io,struct brcm_control_state *s,const struct netprobe *owner,enum netprobe_packet kind,unsigned sequence,const void *p,size_t n,bool (*guard)(void *),void *ctx)
{
    if (!io || !s || !guard || !io->read32 || !io->write32 || !io->packet || !io->delay_us || !netprobe_packet_valid(owner,kind,sequence,p,n)) { return BLOCK_ARGUMENT; }
    if (!s->ready || !s->f2_ready || s->waiting || s->error!=BLOCK_OK) { return BLOCK_NOT_READY; }
    unsigned char frame[92]={0};for (size_t i=0;i<n;++i) { frame[16+i]=((const unsigned char *)p)[i]; }
    enum block_result r=wait_for(io,s,false);if (r==BLOCK_OK) {
        if (!guard(ctx)) { return BLOCK_NOT_READY; }
        unsigned length=16+(unsigned)n;put32(frame,((uint32_t)(uint16_t)~length<<16)|length);frame[4]=s->tx_seq;frame[5]=2;frame[7]=12;frame[12]=0x20;
        r=packet(io,s,true,frame,(length+3u)&~3u);if (r==BLOCK_OK) { ++s->tx_seq;++s->tx_frames; }
    }
    if (r!=BLOCK_OK) { s->ready=false;s->error=r; }return r;
}
enum block_result brcm_control_key(const struct brcm_control_io *io,struct brcm_control_state *s,const void *wire,size_t n,uint32_t *status,bool (*guard)(void *),void *ctx)
{
    if (!io || !s || !status || !guard || !io->packet || !io->read32 || !io->write32 || !io->delay_us || !brcm_key_wire_valid(wire,n)) { return BLOCK_ARGUMENT; }
    *status=0;if (!s->ready || !s->f2_ready || s->waiting || s->error!=BLOCK_OK) { return BLOCK_NOT_READY; }
    unsigned char payload[9+BRCM_KEY_BYTES]={ 'w','s','e','c','_','k','e','y',0 };
    for (unsigned i=0;i<BRCM_KEY_BYTES;++i) { payload[9+i]=((const unsigned char *)wire)[i]; }
    enum block_result r=exchange_guarded(io,s,263,true,payload,sizeof(payload),guard,ctx);*status=s->firmware_status;
    scrub(payload,sizeof(payload));scrub(s->frame,sizeof(s->frame));scrub(s->response,sizeof(s->response));s->response_length=0;
    /* A malformed echoed reply could also enter aggregate/header diagnostic
     * storage. Do not retain raw words from a sensitive exchange there. */
    scrub(s->glom,sizeof(s->glom));scrub(s->last_header,sizeof(s->last_header));
    scrub(s->glom_header,sizeof(s->glom_header));scrub(s->glom_subheader,sizeof(s->glom_subheader));
    if (r!=BLOCK_OK && r!=BLOCK_NOT_READY && !s->firmware_rejected) { s->ready=false;s->error=r; }return r;
}
enum block_result brcm_control_iovar(const struct brcm_control_io *io,struct brcm_control_state *s,bool set,const char *name,const void *input,size_t n,void *output,size_t capacity,size_t *actual,uint32_t *status)
{
    if (!io || !s || !name || !actual || !status || !io->packet || !io->read32 || !io->write32 || !io->delay_us || (n && !input) || (capacity && !output)) { return BLOCK_ARGUMENT; }
    *actual=0;*status=0;
    if (!s->ready || s->error!=BLOCK_OK || !s->f2_ready) { return BLOCK_NOT_READY; }
    unsigned name_length=0;while (name_length<32 && name[name_length]) { ++name_length; }
    if (!name_length || name_length==32) { return BLOCK_ARGUMENT; }++name_length;
    bool auth_policy=same(name,"auth") || same(name,"wpa_auth") || same(name,"wsec") || same(name,"mfp");
    bool allowed=set ? same(name,"clmload") || same(name,"country") || same(name,"event_msgs") || same(name,"escan") || auth_policy || same(name,"event_msgs_ext") || same(name,"mgmt_frame") || same(name,"join") || same(name,"wpaie") || same(name,"auth_status") :
        same(name,"cap") || auth_policy || same(name,"sup_wpa") || same(name,"cur_etheraddr") || same(name,"clmver") || same(name,"clmload_status") || same(name,"country") || same(name,"event_msgs") || same(name,"scan_ver") || same(name,"event_msgs_ext") || same(name,"wpaie") || same(name,"assoc_info") || same(name,"assoc_req_ies");
    if (!set && same(name,"sta_info")) { allowed=true;if (n!=6 || capacity!=512 || !peer_mac(input)) { return BLOCK_ARGUMENT; } }
    if (!allowed) { return BLOCK_UNSUPPORTED; }
    if (same(name,"join") && (n!=70 || capacity)) { return BLOCK_ARGUMENT; }
    if (same(name,"auth_status") && (n!=60 || capacity)) { return BLOCK_ARGUMENT; }
    if (same(name,"wpaie") && (set ? n>256 || capacity : n || capacity!=256)) { return BLOCK_ARGUMENT; }
    if ((same(name,"assoc_info") || same(name,"assoc_req_ies")) && (set || n || capacity!=512)) { return BLOCK_ARGUMENT; }
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
enum block_result brcm_control_station(const struct brcm_control_io *io,struct brcm_control_state *s,uint32_t *status)
{
    if (!io || !s || !status || !io->read32 || !io->write32 || !io->packet || !io->delay_us) { return BLOCK_ARGUMENT; }
    *status=0;if (!s->ready || !s->f2_ready || s->error!=BLOCK_OK) { return BLOCK_NOT_READY; }
    s->station_mode_known=false;enum block_result r=query(io,s,19,4,NULL);*status=s->firmware_status;
    if (r!=BLOCK_OK) { if (!s->firmware_rejected) { s->ready=false;s->error=r; }return r; }
    if (s->response_length!=4) { return BLOCK_RANGE; }
    s->station_mode=le32(s->response);s->station_mode_known=true;
    return s->station_mode==1 ? BLOCK_OK : BLOCK_UNSUPPORTED;
}
enum block_result brcm_control_identity(const struct brcm_control_io *io,struct brcm_control_state *s,struct brcm_link_identity *out,uint32_t *status)
{
    if (!io || !s || !out || !status || !io->read32 || !io->write32 || !io->packet || !io->delay_us) { return BLOCK_ARGUMENT; }
    *out=(struct brcm_link_identity){0};*status=0;
    if (!s->ready || !s->f2_ready || s->error!=BLOCK_OK) { return BLOCK_NOT_READY; }
    struct brcm_link_identity value={0};enum block_result r=query(io,s,23,6,NULL);*status=s->firmware_status;
    if (r!=BLOCK_OK) { goto fail; }if (s->response_length!=6) { return BLOCK_RANGE; }
    for (unsigned i=0;i<6;++i) { value.bssid[i]=s->response[i]; }
    r=query(io,s,25,36,NULL);*status=s->firmware_status;if (r!=BLOCK_OK) { goto fail; }
    if (s->response_length!=36 || le32(s->response)>32) { return BLOCK_RANGE; }
    value.ssid_length=le32(s->response);for (unsigned i=0;i<32;++i) { value.ssid[i]=s->response[4+i]; }
    *out=value;return BLOCK_OK;
fail:
    if (!s->firmware_rejected) { s->ready=false;s->error=r; }return r;
}
static enum block_result station_query(const struct brcm_control_io *io,struct brcm_control_state *s,uint32_t command,uint32_t *value,uint32_t *status)
{
    enum block_result r=query(io,s,command,4,NULL);*status=s->firmware_status;
    if (r!=BLOCK_OK) { if (!s->firmware_rejected) { s->ready=false;s->error=r; }return r; }
    if (s->response_length!=4) { return BLOCK_RANGE; }*value=le32(s->response);return BLOCK_OK;
}
enum block_result brcm_control_station_prepare(const struct brcm_control_io *io,struct brcm_control_state *s,uint32_t *status)
{
    if (!io || !s || !status || !io->read32 || !io->write32 || !io->packet || !io->delay_us) { return BLOCK_ARGUMENT; }
    *status=0;if (!s->ready || !s->f2_ready || s->error!=BLOCK_OK) { return BLOCK_NOT_READY; }
    s->station_prepare_phase=1;s->station_ap_known=false;s->station_up_known=false;s->station_initial_known=false;
    s->station_change_attempted=false;s->station_mode_changed=false;s->station_mode_known=false;
    enum block_result r=station_query(io,s,117,&s->station_ap_mode,status);if (r!=BLOCK_OK) { return r; }
    s->station_ap_known=true;if (s->station_ap_mode!=0) { return BLOCK_UNSUPPORTED; }
    s->station_prepare_phase=2;r=station_query(io,s,162,&s->station_up_state,status);if (r!=BLOCK_OK) { return r; }
    s->station_up_known=true;if (s->station_up_state!=0) { return BLOCK_NOT_READY; }
    s->station_prepare_phase=3;r=station_query(io,s,19,&s->station_mode,status);if (r!=BLOCK_OK) { return r; }
    s->station_mode_known=true;s->station_initial_known=true;s->station_initial_mode=s->station_mode;
    if (s->station_mode>1) { return BLOCK_UNSUPPORTED; }
    if (s->station_mode==0) {
        s->station_prepare_phase=4;s->station_change_attempted=true;s->station_mode_known=false;
        const unsigned char infrastructure[4]={1,0,0,0};r=exchange(io,s,20,true,infrastructure,4);*status=s->firmware_status;
        if (r!=BLOCK_OK) { if (!s->firmware_rejected) { s->ready=false;s->error=r; }return r; }
        s->station_prepare_phase=5;r=station_query(io,s,19,&s->station_mode,status);if (r!=BLOCK_OK) { return r; }
        s->station_mode_known=true;if (s->station_mode!=1) { return BLOCK_IO; }
        s->station_mode_changed=true;
    }
    s->station_prepare_phase=6;return BLOCK_OK;
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
