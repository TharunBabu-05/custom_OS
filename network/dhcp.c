/* Original bounded IPv4/UDP/DHCP bootstrap probe. Wire references:
 * RFC 791, RFC 768, RFC 2131 and RFC 2132; see docs/wifi-dhcp.md. */
#include "dhcp.h"
static unsigned be16(const unsigned char *p) { return (unsigned)p[0]<<8|p[1]; }
static uint32_t be32(const unsigned char *p) { return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3]; }
static void put16(unsigned char *p,unsigned x) { p[0]=(unsigned char)(x>>8);p[1]=(unsigned char)x; }
static void put32(unsigned char *p,uint32_t x) { for (unsigned i=0;i<4;++i) { p[i]=(unsigned char)(x>>(24-8*i)); } }
static bool eq(const unsigned char *a,const unsigned char *b,size_t n) { unsigned x=0;for (size_t i=0;i<n;++i) { x|=a[i]^b[i]; }return !x; }
static bool zero(const unsigned char *p,size_t n) { unsigned x=0;for (size_t i=0;i<n;++i) { x|=p[i]; }return !x; }
static bool unicast(const unsigned char *p) { return !zero(p,6) && !(p[0]&1); }
static bool ip(const unsigned char *p) { return p[0] && p[0]!=127 && p[0]<224 && be32(p)!=UINT32_MAX; }
static uint32_t sum(uint32_t value,const unsigned char *p,size_t n)
{ for (size_t i=0;i<n;i+=2) { value+=(unsigned)p[i]<<8|(i+1<n ? p[i+1] : 0); }return value; }
static unsigned finish(uint32_t value) { while (value>>16) { value=(value&65535)+(value>>16); }return (~value)&65535; }
static unsigned udp_sum(const unsigned char *p,size_t n) { return finish(sum(sum(17+(uint32_t)n,p+12,8),p+20,n)); }
enum block_result dhcp_client_init(struct dhcp_client *s,const unsigned char mac[6],const unsigned char entropy[10])
{
    if (!s || !mac || !entropy || !unicast(mac) || !be32(entropy)) { return BLOCK_ARGUMENT; }
    *s=(struct dhcp_client){.phase=DHCP_SELECTING,.error=BLOCK_NOT_READY,.attempted=true,.armed=true,.xid=be32(entropy)};
    for (unsigned i=0;i<6;++i) { s->mac[i]=mac[i]; }
    s->initial_delay_ms=1000+be16(entropy+4)%9001;s->retry_ms[0]=3000+be16(entropy+6)%2001;s->retry_ms[1]=7000+be16(entropy+8)%2001;return BLOCK_OK;
}
static enum block_result encode(const struct dhcp_client *s,unsigned type,unsigned seconds,void *out,size_t cap,size_t *actual)
{
    if (actual) { *actual=0; }if (!s || !out || !actual || !unicast(s->mac) || !s->xid || cap<DHCP_FRAME_BYTES) { return BLOCK_ARGUMENT; }
    bool request=type==3,decline=type==4;const struct dhcp_lease *lease=decline ? &s->lease : &s->offer;
    if (decline ? s->phase!=DHCP_ACKED || !s->ack_seen || !s->request_written || !ip(lease->address) || !ip(lease->server) : request ? !s->have_offer || !s->discover_written || !ip(lease->address) || !ip(lease->server) : s->phase!=DHCP_SELECTING) { return BLOCK_NOT_READY; }
    unsigned char p[DHCP_FRAME_BYTES]={0};for (unsigned i=0;i<6;++i) { p[i]=255;p[6+i]=s->mac[i]; }put16(p+12,0x800);
    unsigned char *v=p+14,*u=v+20,*b=u+8;v[0]=0x45;put16(v+2,DHCP_FRAME_BYTES-14);put16(v+6,0x4000);v[8]=64;v[9]=17;put32(v+16,UINT32_MAX);
    put16(u,68);put16(u+2,67);put16(u+4,DHCP_FRAME_BYTES-34);
    b[0]=1;b[1]=1;b[2]=6;put32(b+4,s->xid);put16(b+8,decline ? 0 : seconds>65535 ? 65535 : seconds);put16(b+10,decline ? 0 : 0x8000);
    for (unsigned i=0;i<6;++i) { b[28+i]=s->mac[i]; }put32(b+236,0x63825363);
    size_t at=240;b[at++]=53;b[at++]=1;b[at++]=(unsigned char)type;b[at++]=61;b[at++]=7;b[at++]=1;
    for (unsigned i=0;i<6;++i) { b[at++]=s->mac[i]; }
    if (request || decline) { b[at++]=50;b[at++]=4;for (unsigned i=0;i<4;++i) { b[at++]=lease->address[i]; }b[at++]=54;b[at++]=4;for (unsigned i=0;i<4;++i) { b[at++]=lease->server[i]; } }
    if (!decline) { b[at++]=55;b[at++]=4;b[at++]=1;b[at++]=3;b[at++]=6;b[at++]=51;b[at++]=57;b[at++]=2;b[at++]=2;b[at++]=64; }b[at]=255;
    put16(v+10,finish(sum(0,v,20)));unsigned checksum=udp_sum(v,DHCP_FRAME_BYTES-34);put16(u+6,checksum ? checksum : 65535);
    for (unsigned i=0;i<DHCP_FRAME_BYTES;++i) { ((unsigned char *)out)[i]=p[i]; }*actual=sizeof(p);return BLOCK_OK;
}
enum block_result dhcp_encode(const struct dhcp_client *s,bool request,unsigned seconds,void *p,size_t cap,size_t *n)
{ return encode(s,request ? 3 : 1,seconds,p,cap,n); }
enum block_result dhcp_encode_decline(const struct dhcp_client *s,void *p,size_t cap,size_t *n)
{ return encode(s,4,0,p,cap,n); }
bool dhcp_wire_request_valid(const void *buffer,size_t n)
{
    if (!buffer || n!=DHCP_FRAME_BYTES) { return false; }const unsigned char *p=buffer,*v=p+14,*u=v+20,*b=u+8;
    static const unsigned char broadcast[6]={255,255,255,255,255,255};
    bool valid=eq(p,broadcast,6) && unicast(p+6) && be16(p+12)==0x800 && v[0]==0x45 && be16(v+2)==n-14 && be16(v+6)==0x4000 && v[8] && v[9]==17 &&
        zero(v+12,4) && be32(v+16)==UINT32_MAX && !finish(sum(0,v,20)) && be16(u)==68 && be16(u+2)==67 && be16(u+4)==n-34 && be16(u+6) && !udp_sum(v,n-34) &&
        b[0]==1 && b[1]==1 && b[2]==6 && !b[3] && be32(b+4) && be16(b+10)==(b[242]==4 ? 0 : 0x8000) && zero(b+12,16) && eq(b+28,p+6,6) && zero(b+34,202) && be32(b+236)==0x63825363 &&
        b[240]==53 && b[241]==1 && (b[242]==1 || b[242]==3 || b[242]==4);
    if (!valid) { return false; }
    /* Typed transport accepts only our exact DISCOVER/REQUEST layout, including
     * client identifier, options, zero padding and checksums. No generic IP TX. */
    struct dhcp_client state={0};unsigned char entropy[10]={0},expected[DHCP_FRAME_BYTES];size_t bytes=0;
    for (unsigned i=0;i<4;++i) { entropy[i]=b[4+i]; }
    if (dhcp_client_init(&state,p+6,entropy)!=BLOCK_OK) { return false; }bool request=b[242]==3;
    if (request) {
        state.phase=DHCP_REQUESTING;state.have_offer=state.discover_written=true;
        for (unsigned i=0;i<4;++i) { state.offer.address[i]=b[254+i];state.offer.server[i]=b[260+i]; }
    }
    if (b[242]==4) {
        state.phase=DHCP_ACKED;state.ack_seen=state.request_written=true;
        for (unsigned i=0;i<4;++i) { state.lease.address[i]=b[254+i];state.lease.server[i]=b[260+i]; }
    }
    return encode(&state,b[242],be16(b+8),expected,sizeof(expected),&bytes)==BLOCK_OK && bytes==n && eq(expected,p,n);
}
static bool mask_valid(const unsigned char *p)
{ uint32_t x=be32(p),inverse=~x;return x && inverse && !(inverse&(inverse+1)); }
void dhcp_receive(struct dhcp_client *s,const void *buffer,size_t n)
{
    if (!s || !s->armed || (s->phase!=DHCP_SELECTING && s->phase!=DHCP_REQUESTING) || !buffer) { return; }++s->received;
    const unsigned char *p=buffer;static const unsigned char broadcast[6]={255,255,255,255,255,255};
    if (n<14 || be16(p+12)!=0x800 || !unicast(p+6) || (!eq(p,s->mac,6) && !eq(p,broadcast,6))) { ++s->ignored;return; }
    if (n<42 || n>1514) { goto malformed; }const unsigned char *v=p+14,*u=v+20;
    if (v[0]!=0x45 || be16(v+2)>n-14 || be16(v+2)<268 || (be16(v+6)&~0x4000) || !v[8] || v[9]!=17 || finish(sum(0,v,20))) { goto malformed; }
    if (be16(u)!=67 || be16(u+2)!=68) { ++s->ignored;return; }
    size_t length=be16(u+4);if (length<248 || length!=be16(v+2)-20u || (be16(u+6) && udp_sum(v,length))) { goto malformed; }
    const unsigned char *b=u+8;size_t bytes=length-8;
    if (b[0]!=2 || b[1]!=1 || b[2]!=6 || b[3] || be32(b+4)!=s->xid || !eq(b+28,s->mac,6)) { ++s->ignored;return; }
    if (!zero(b+12,4) || !zero(b+24,4) || be32(b+236)!=0x63825363 || !ip(v+12)) { goto malformed; }
    struct dhcp_lease value={0};for (unsigned i=0;i<4;++i) { value.address[i]=b[16+i]; }
    unsigned seen=0,type=0;bool end=false;
    for (size_t at=240;at<bytes;) {
        unsigned tag=b[at++];if (!tag) { continue; }if (tag==255) { end=true;break; }
        if (at==bytes) { goto malformed; }unsigned len=b[at++];if (len>bytes-at) { goto malformed; }const unsigned char *data=b+at;at+=len;
        unsigned bit=tag==53 ? 1 : tag==54 ? 2 : tag==51 ? 4 : tag==1 ? 8 : tag==3 ? 16 : tag==6 ? 32 : tag==61 ? 64 : 0;
        if (bit && (seen&bit)) { goto malformed; }seen|=bit;
        if (tag==52) { goto malformed; } /* option overload requires separate field parsing */
        if (tag==53) { if (len!=1) { goto malformed; }type=data[0]; }
        else if (tag==61) { if (len!=7 || data[0]!=1 || !eq(data+1,s->mac,6)) { goto malformed; } }
        else if (tag==54) { if (len!=4 || !ip(data)) { goto malformed; }for (unsigned i=0;i<4;++i) { value.server[i]=data[i]; } }
        else if (tag==51) { if (len!=4 || !be32(data)) { goto malformed; }value.seconds=be32(data); }
        else if (tag==1) { if (len!=4 || !mask_valid(data)) { goto malformed; }for (unsigned i=0;i<4;++i) { value.mask[i]=data[i]; }value.have_mask=true; }
        else if (tag==3 || tag==6) {
            if (!len || len%4 || len>32) { goto malformed; }for (unsigned i=0;i<len;i+=4) { if (!ip(data+i)) { goto malformed; } }
            unsigned char *out=tag==3 ? value.router : value.dns;for (unsigned i=0;i<4;++i) { out[i]=data[i]; }
            if (tag==3) { value.have_router=true; }else { value.have_dns=true; }
        }
    }
    if (!end || (seen&3)!=3 || !eq(value.server,v+12,4)) { goto malformed; } /* direct server only, no relay */
    if (type==6 && s->phase==DHCP_REQUESTING && s->request_issued && eq(value.server,s->offer.server,4)) { s->nak_seen=true;s->phase=DHCP_FAILED;s->error=BLOCK_IO;return; }
    if (!ip(value.address) || !value.seconds || (be32(v+16)!=UINT32_MAX && !eq(v+16,value.address,4))) { goto malformed; }
    if (s->phase==DHCP_SELECTING && type==2 && s->discover_issued) { if (!s->have_offer) { s->offer=value;s->have_offer=true; }else { ++s->ignored; }return; }
    if (s->phase==DHCP_REQUESTING && type==5 && s->request_issued && eq(value.server,s->offer.server,4) && eq(value.address,s->offer.address,4)) { if (!s->ack_seen) { s->lease=value;s->ack_seen=true; }else { ++s->ignored; }return; }
    ++s->ignored;return;
malformed:++s->malformed;
}
enum block_result dhcp_probe_run(const struct dhcp_io *io,const unsigned char mac[6],const unsigned char entropy[10],struct dhcp_client *s)
{
    if (!io || !io->tx || !io->poll || !io->ready || !io->delay_us || !io->milliseconds || !s) { return BLOCK_ARGUMENT; }
    if (s->attempted) { return BLOCK_NOT_READY; }enum block_result r=dhcp_client_init(s,mac,entropy);if (r!=BLOCK_OK) { return r; }
    uint64_t start=io->milliseconds(io->context),next=s->initial_delay_ms;
    for (unsigned poll=0;poll<4500;++poll) {
        ++s->polls;r=io->poll(io->context);if (r!=BLOCK_OK) { break; }if (!io->ready(io->context)) { r=BLOCK_NOT_READY;break; }
        if (s->phase==DHCP_FAILED) { r=s->error;break; }
        if (s->ack_seen && s->request_written) { s->phase=DHCP_ACKED;r=BLOCK_OK;break; }
        uint64_t elapsed=io->milliseconds(io->context)-start;if (elapsed>=45000) { r=BLOCK_TIMEOUT;break; }
        if (s->phase==DHCP_SELECTING && s->have_offer && s->discover_written) { s->phase=DHCP_REQUESTING;next=elapsed; }
        unsigned sent=s->phase==DHCP_REQUESTING ? s->request_sent : s->discover_sent;
        if (elapsed>=next && sent<3) {
            bool request=s->phase==DHCP_REQUESTING;unsigned char frame[DHCP_FRAME_BYTES];size_t bytes=0;
            r=dhcp_encode(s,request,(unsigned)(elapsed/1000),frame,sizeof(frame),&bytes);if (r!=BLOCK_OK) { break; }
            if (request) { s->request_issued=true; }else { s->discover_issued=true; }
            r=io->tx(io->context,frame,bytes);
            if (r==BLOCK_NOT_READY && ++s->deferrals<=8) { continue; }if (r!=BLOCK_OK) { break; }
            if (request) { s->request_written=true;++s->request_sent; }else { s->discover_written=true;++s->discover_sent; }
            next=elapsed+s->retry_ms[sent ? 1 : 0];
        }
        r=io->delay_us(io->context,10000);if (r!=BLOCK_OK) { break; }r=BLOCK_TIMEOUT;
    }
    s->armed=false;s->error=r;if (r!=BLOCK_OK) { s->phase=DHCP_FAILED; }return r;
}
