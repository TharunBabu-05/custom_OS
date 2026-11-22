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
    bool request=type==3,decline=type==4,renewal=s->renewal;
    const struct dhcp_lease *lease=decline ? &s->lease : &s->offer;
    if (renewal && (!request || !s->armed || s->phase!=DHCP_REQUESTING ||
        !ip(lease->address) || (!s->rebinding && (!ip(lease->server) || !unicast(lease->server_mac))))) { return BLOCK_NOT_READY; }
    if (!renewal && (decline ? s->phase!=DHCP_ACKED || !s->ack_seen || !s->request_written || !ip(lease->address) || !ip(lease->server) : request ? !s->have_offer || !s->discover_written || !ip(lease->address) || !ip(lease->server) : s->phase!=DHCP_SELECTING)) { return BLOCK_NOT_READY; }
    unsigned char p[DHCP_FRAME_BYTES]={0};
    bool directed=renewal && !s->rebinding;
    for (unsigned i=0;i<6;++i) { p[i]=directed ? lease->server_mac[i] : 255;p[6+i]=s->mac[i]; }put16(p+12,0x800);
    unsigned char *v=p+14,*u=v+20,*b=u+8;v[0]=0x45;put16(v+2,DHCP_FRAME_BYTES-14);put16(v+6,0x4000);v[8]=64;v[9]=17;put32(v+16,UINT32_MAX);
    put16(u,68);put16(u+2,67);put16(u+4,DHCP_FRAME_BYTES-34);
    b[0]=1;b[1]=1;b[2]=6;put32(b+4,s->xid);put16(b+8,decline ? 0 : seconds>65535 ? 65535 : seconds);put16(b+10,decline || renewal ? 0 : 0x8000);
    if (renewal) {
        put32(v+12,be32(lease->address));put32(b+12,be32(lease->address));
        if (directed) { put32(v+16,be32(lease->server)); }
    }
    for (unsigned i=0;i<6;++i) { b[28+i]=s->mac[i]; }put32(b+236,0x63825363);
    size_t at=240;b[at++]=53;b[at++]=1;b[at++]=(unsigned char)type;b[at++]=61;b[at++]=7;b[at++]=1;
    for (unsigned i=0;i<6;++i) { b[at++]=s->mac[i]; }
    if ((request && !renewal) || decline) { b[at++]=50;b[at++]=4;for (unsigned i=0;i<4;++i) { b[at++]=lease->address[i]; }b[at++]=54;b[at++]=4;for (unsigned i=0;i<4;++i) { b[at++]=lease->server[i]; } }
    if (!decline) {
        b[at++]=55;b[at++]=renewal ? 6 : 4;b[at++]=1;b[at++]=3;b[at++]=6;b[at++]=51;
        if (renewal) { b[at++]=58;b[at++]=59; }
        b[at++]=57;b[at++]=2;b[at++]=2;b[at++]=64;
    }
    b[at]=255;
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
    bool renewal=!zero(b+12,4),rebinding=eq(p,broadcast,6);
    bool route=renewal ?
        b[242]==3 && ip(v+12) && eq(v+12,b+12,4) && (rebinding ? be32(v+16)==UINT32_MAX : unicast(p) && ip(v+16)) :
        rebinding && zero(v+12,4) && be32(v+16)==UINT32_MAX;
    bool valid=route && unicast(p+6) && be16(p+12)==0x800 && v[0]==0x45 && be16(v+2)==n-14 && be16(v+6)==0x4000 && v[8] && v[9]==17 &&
        !finish(sum(0,v,20)) && be16(u)==68 && be16(u+2)==67 && be16(u+4)==n-34 && be16(u+6) && !udp_sum(v,n-34) &&
        b[0]==1 && b[1]==1 && b[2]==6 && !b[3] && be32(b+4) && be16(b+10)==(renewal || b[242]==4 ? 0 : 0x8000) && zero(b+16,12) && eq(b+28,p+6,6) && zero(b+34,202) && be32(b+236)==0x63825363 &&
        b[240]==53 && b[241]==1 && (b[242]==1 || b[242]==3 || b[242]==4);
    if (!valid) { return false; }
    /* Typed transport accepts only our exact DISCOVER/REQUEST layout, including
     * client identifier, options, zero padding and checksums. No generic IP TX. */
    struct dhcp_client state={0};unsigned char entropy[10]={0},expected[DHCP_FRAME_BYTES];size_t bytes=0;
    for (unsigned i=0;i<4;++i) { entropy[i]=b[4+i]; }
    if (dhcp_client_init(&state,p+6,entropy)!=BLOCK_OK) { return false; }bool request=b[242]==3;
    if (renewal) {
        state.phase=DHCP_REQUESTING;state.renewal=true;state.rebinding=rebinding;
        put32(state.offer.address,be32(b+12));put32(state.offer.server,be32(v+16));
        for (unsigned i=0;i<6;++i) { state.offer.server_mac[i]=p[i]; }
    } else if (request) {
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
static bool same_configuration(const struct dhcp_lease *bound,struct dhcp_lease *ack)
{
    if (!eq(bound->address,ack->address,4) ||
        (ack->have_mask && (!bound->have_mask || !eq(bound->mask,ack->mask,4))) ||
        (ack->have_router && (!bound->have_router || !eq(bound->router,ack->router,4))) ||
        (ack->have_dns && (!bound->have_dns || !eq(bound->dns,ack->dns,4)))) { return false; }
    put32(ack->mask,be32(bound->mask));ack->have_mask=bound->have_mask;
    put32(ack->router,be32(bound->router));ack->have_router=bound->have_router;
    put32(ack->dns,be32(bound->dns));ack->have_dns=bound->have_dns;
    return true;
}
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
    if ((!zero(b+12,4) && (!s->renewal || !eq(b+12,s->offer.address,4))) || !zero(b+24,4) || be32(b+236)!=0x63825363 || !ip(v+12)) { goto malformed; }
    struct dhcp_lease value={0};for (unsigned i=0;i<4;++i) { value.address[i]=b[16+i]; }
    for (unsigned i=0;i<6;++i) { value.server_mac[i]=p[6+i]; }
    unsigned seen=0,type=0;bool end=false;
    for (size_t at=240;at<bytes;) {
        unsigned tag=b[at++];if (!tag) { continue; }if (tag==255) { end=true;break; }
        if (at==bytes) { goto malformed; }unsigned len=b[at++];if (len>bytes-at) { goto malformed; }const unsigned char *data=b+at;at+=len;
        unsigned bit=tag==53 ? 1 : tag==54 ? 2 : tag==51 ? 4 : tag==1 ? 8 : tag==3 ? 16 : tag==6 ? 32 : tag==61 ? 64 : tag==58 ? 128 : tag==59 ? 256 : 0;
        if (bit && (seen&bit)) { goto malformed; }seen|=bit;
        if (tag==52) { goto malformed; } /* option overload requires separate field parsing */
        if (tag==53) { if (len!=1) { goto malformed; }type=data[0]; }
        else if (tag==61) { if (len!=7 || data[0]!=1 || !eq(data+1,s->mac,6)) { goto malformed; } }
        else if (tag==54) { if (len!=4 || !ip(data)) { goto malformed; }for (unsigned i=0;i<4;++i) { value.server[i]=data[i]; } }
        else if (tag==51) { if (len!=4 || !be32(data)) { goto malformed; }value.seconds=be32(data); }
        else if (tag==58 || tag==59) {
            if (len!=4 || !be32(data)) { goto malformed; }
            if (tag==58) { value.t1_seconds=be32(data); }else { value.t2_seconds=be32(data); }
        }
        else if (tag==1) { if (len!=4 || !mask_valid(data)) { goto malformed; }for (unsigned i=0;i<4;++i) { value.mask[i]=data[i]; }value.have_mask=true; }
        else if (tag==3 || tag==6) {
            if (!len || len%4 || len>32) { goto malformed; }for (unsigned i=0;i<len;i+=4) { if (!ip(data+i)) { goto malformed; } }
            unsigned char *out=tag==3 ? value.router : value.dns;for (unsigned i=0;i<4;++i) { out[i]=data[i]; }
            if (tag==3) { value.have_router=true; }else { value.have_dns=true; }
        }
    }
    if (!end || (seen&3)!=3 || !eq(value.server,v+12,4)) { goto malformed; } /* direct server only, no relay */
    if (s->renewal) {
        bool original=eq(value.server,s->offer.server,4) && eq(value.server_mac,s->offer.server_mac,6);
        if (!s->request_issued || s->ack_seen || s->nak_seen ||
            (!original && (!s->rebinding || type==6)) ||
            (be32(v+16)!=UINT32_MAX && !eq(v+16,s->offer.address,4))) { ++s->ignored;return; }
        if (type==6) { s->nak_seen=true;return; }
        if (type==5 && value.seconds && same_configuration(&s->offer,&value)) {
            s->lease=value;s->ack_seen=true;return;
        }
        ++s->ignored;return;
    }
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

enum block_result dhcp_renew_service(const struct dhcp_renew_io *io,struct dhcp_renewal *r,
    const unsigned char mac[6],struct dhcp_lease *lease,uint64_t *origin)
{
    if (!io || !io->tx || !io->random || !io->milliseconds || !r || !mac || !lease || !origin) { return BLOCK_ARGUMENT; }
    struct dhcp_client *exchange=&r->exchange;
    uint64_t now=io->milliseconds(io->context),age=now-*origin,life=(uint64_t)lease->seconds*1000;
    if (age>=life) { exchange->armed=false;r->queued=false;return BLOCK_OK; }
    if (exchange->request_written && exchange->nak_seen) {
        r->rejected=true;exchange->armed=false;return BLOCK_IO;
    }
    if (exchange->request_written && exchange->ack_seen) {
        if (now-r->first_sent<(uint64_t)exchange->lease.seconds*1000) {
            *lease=exchange->lease;*origin=r->first_sent;++r->completed;
            exchange->armed=false;exchange->request_written=false;r->queued=false;
            return BLOCK_OK;
        }
        exchange->ack_seen=false;
    }
    uint64_t t1=lease->t1_seconds ? (uint64_t)lease->t1_seconds*1000 : life/2;
    uint64_t t2=lease->t2_seconds ? (uint64_t)lease->t2_seconds*1000 : life*7/8;
    if (t1>=t2 || t2>=life) { t1=life/2;t2=life*7/8; }
    bool rebinding=age>=t2;
    if (!exchange->armed || rebinding!=exchange->rebinding) {
        if (age<t1 && !r->queued) { return BLOCK_OK; }
        unsigned char entropy[32]={0};
        if (!io->random(io->context,entropy) || be32(entropy)==r->xid ||
            dhcp_client_init(exchange,mac,entropy)!=BLOCK_OK) { return BLOCK_IO; }
        r->xid=exchange->xid;r->queued=false;r->next=now;
        exchange->renewal=true;exchange->rebinding=rebinding;exchange->phase=DHCP_REQUESTING;exchange->offer=*lease;
    }
    if (now<r->next) { return BLOCK_OK; }
    unsigned char frame[DHCP_FRAME_BYTES];size_t bytes;
    enum block_result result=encode(exchange,3,0,frame,sizeof(frame),&bytes);
    if (result!=BLOCK_OK) { return result; }
    exchange->request_issued=true;
    result=io->tx(io->context,frame,bytes);
    if (result!=BLOCK_OK && !exchange->request_written) { exchange->ack_seen=exchange->nak_seen=false;exchange->request_issued=false; }
    if (result==BLOCK_NOT_READY && ++exchange->deferrals<=8) { r->next=now+100;return BLOCK_OK; }
    if (result!=BLOCK_OK && result!=BLOCK_NOT_READY) { return result; }
    if (result==BLOCK_OK) {
        if (!exchange->request_written) { r->first_sent=now;exchange->request_written=true; }
        ++r->requests;if (rebinding) { ++r->rebroadcasts; }
    }
    exchange->deferrals=0;
    uint64_t remain=(rebinding ? life : t2)-age;
    r->next=now+(remain/2>60000 ? remain/2 : 60000);
    return BLOCK_OK;
}
