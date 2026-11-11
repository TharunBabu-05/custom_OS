/* Original bounded ARP address checking, gateway resolution and ICMP echo.
 * Wire/timing definitions: RFC 826, RFC 5227, RFC 791 and RFC 792. */
#include "netprobe.h"
static unsigned be16(const unsigned char *p) { return (unsigned)p[0]<<8|p[1]; }
static uint32_t be32(const unsigned char *p) { return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3]; }
static void put16(unsigned char *p,unsigned v) { p[0]=(unsigned char)(v>>8);p[1]=(unsigned char)v; }
static void copy(unsigned char *a,const unsigned char *b,size_t n) { for (size_t i=0;i<n;++i) { a[i]=b[i]; } }
static bool eq(const unsigned char *a,const unsigned char *b,size_t n) { unsigned x=0;for (size_t i=0;i<n;++i) { x|=a[i]^b[i]; }return !x; }
static bool zero(const unsigned char *p,size_t n) { unsigned x=0;for (size_t i=0;i<n;++i) { x|=p[i]; }return !x; }
static bool mac(const unsigned char *p) { return p && !(p[0]&1) && !zero(p,6); }
static bool ip(const unsigned char *p) { return p[0] && p[0]!=127 && p[0]<224; }
static unsigned checksum(const unsigned char *p,size_t n)
{ uint32_t s=0;for (size_t i=0;i<n;i+=2) { s+=(unsigned)p[i]<<8|(i+1<n ? p[i+1] : 0); }while (s>>16) { s=(s&65535)+(s>>16); }return (~s)&65535; }
static const unsigned char broadcast[6]={255,255,255,255,255,255};
static const unsigned char suffix[16]={'O','u','r','O','S',' ','I','C','M','P',' ','p','r','o','b','e'};
static bool onlink(const struct dhcp_lease *l,const unsigned char p[4])
{ uint32_t m=be32(l->mask),a=be32(p),host=a&~m;return ip(p) && host && host!=~m && (a&m)==(be32(l->address)&m); }
enum block_result netprobe_init(struct netprobe *s,const unsigned char local[6],const struct dhcp_lease *lease,const unsigned char entropy[32],uint64_t origin)
{
    if (!s || !mac(local) || !lease || !entropy || zero(entropy+8,16) || !be16(entropy+24)) { return BLOCK_ARGUMENT; }
    if (!lease->have_mask || !lease->have_router || !lease->seconds) { return BLOCK_UNSUPPORTED; }
    uint32_t mask=be32(lease->mask),inverse=~mask;
    if (!mask || !inverse || (inverse&(inverse+1)) || !onlink(lease,lease->address) || !onlink(lease,lease->router) || eq(lease->address,lease->router,4)) { return BLOCK_UNSUPPORTED; }
    *s=(struct netprobe){.attempted=true,.armed=true,.phase=NP_PROBING,.error=BLOCK_NOT_READY,.lease=*lease,.lease_origin=origin};copy(s->mac,local,6);copy(s->token,entropy+8,16);
    s->identifier=be16(entropy+24);s->initial_delay=be16(entropy)%1001;s->probe_delay[0]=1000+be16(entropy+2)%1001;s->probe_delay[1]=1000+be16(entropy+4)%1001;return BLOCK_OK;
}
enum block_result netprobe_encode(const struct netprobe *s,enum netprobe_packet kind,unsigned sequence,void *output,size_t cap,size_t *actual)
{
    if (actual) { *actual=0; }if (!s || !output || !actual || (unsigned)kind>NP_ECHO || cap<(kind==NP_ECHO ? 74u : 60u)) { return BLOCK_ARGUMENT; }
    if (!s->armed || s->conflict || !mac(s->mac) || (kind==NP_ARP_PROBE ? s->phase!=NP_PROBING || s->active : kind==NP_ARP_ANNOUNCE ? s->phase!=NP_ANNOUNCING : !s->active)) { return BLOCK_NOT_READY; }
    if (kind==NP_ARP_RESOLVE && (s->phase!=NP_RESOLVING || (s->gateway_known && s->resolutions))) { return BLOCK_NOT_READY; }
    if (kind==NP_ARP_REPLY && (!s->reply_pending || !mac(s->reply_mac))) { return BLOCK_NOT_READY; }
    if (kind==NP_ECHO && (s->phase!=NP_PING || !s->gateway_known || !mac(s->gateway_mac) || !sequence || sequence>3)) { return BLOCK_NOT_READY; }
    unsigned char p[74]={0};copy(p,kind==NP_ECHO ? s->gateway_mac : kind==NP_ARP_REPLY ? s->reply_mac : broadcast,6);copy(p+6,s->mac,6);
    if (kind==NP_ECHO) {
        put16(p+12,0x800);unsigned char *v=p+14,*e=v+20;v[0]=0x45;put16(v+2,60);put16(v+4,sequence);put16(v+6,0x4000);v[8]=64;v[9]=1;copy(v+12,s->lease.address,4);copy(v+16,s->lease.router,4);
        e[0]=8;put16(e+4,s->identifier);put16(e+6,sequence);copy(e+8,s->token,16);copy(e+24,suffix,16);put16(e+2,checksum(e,40));put16(v+10,checksum(v,20));*actual=74;
    }else {
        put16(p+12,0x806);unsigned char *a=p+14;put16(a,1);put16(a+2,0x800);a[4]=6;a[5]=4;put16(a+6,kind==NP_ARP_REPLY ? 2 : 1);copy(a+8,s->mac,6);
        if (kind!=NP_ARP_PROBE) { copy(a+14,s->lease.address,4); }
        if (kind==NP_ARP_REPLY) { copy(a+18,s->reply_mac,6);copy(a+24,s->reply_ip,4); }
        else { copy(a+24,kind==NP_ARP_RESOLVE ? s->lease.router : s->lease.address,4); }*actual=60;
    }
    copy(output,p,*actual);return BLOCK_OK;
}
bool netprobe_packet_valid(const struct netprobe *s,enum netprobe_packet kind,unsigned sequence,const void *p,size_t n)
{ unsigned char expected[74];size_t bytes=0;return p && netprobe_encode(s,kind,sequence,expected,sizeof(expected),&bytes)==BLOCK_OK && bytes==n && eq(p,expected,n); }
bool netprobe_can_tx(const struct netprobe *s,uint64_t now)
{ return s && s->armed && !s->conflict && !s->gateway_changed && now-s->start<30000 && now-s->lease_origin<(uint64_t)s->lease.seconds*1000; }
void netprobe_receive(struct netprobe *s,const void *buffer,size_t n)
{
    if (!s || !s->armed || s->conflict || !buffer) { return; }++s->received;const unsigned char *p=buffer;
    if (n<14 || (!eq(p,s->mac,6) && !eq(p,broadcast,6)) || !mac(p+6) || eq(p+6,s->mac,6)) { ++s->ignored;return; }
    unsigned type=be16(p+12);
    if (type==0x806) {
        ++s->arp_rx;if (n<42 || n>1514) { goto malformed; }const unsigned char *a=p+14;unsigned op=be16(a+6);
        if (be16(a)!=1 || be16(a+2)!=0x800 || a[4]!=6 || a[5]!=4 || (op!=1 && op!=2) || !eq(p+6,a+8,6)) { goto malformed; }
        if (eq(a+14,s->lease.address,4) || (!s->active && op==1 && zero(a+14,4) && eq(a+24,s->lease.address,4))) {
            s->conflict=true;s->active=false;copy(s->conflict_mac,a+8,6);return;
        }
        if (!s->active) { ++s->ignored;return; }
        if (op==2 && eq(p,s->mac,6) && eq(a+18,s->mac,6) && eq(a+24,s->lease.address,4) && eq(a+14,s->lease.router,4)) {
            if (s->gateway_known && !eq(a+8,s->gateway_mac,6)) { s->gateway_changed=true;return; }
            if (s->phase==NP_RESOLVING && s->resolve_issued) { copy(s->gateway_mac,a+8,6);s->gateway_known=true;return; }
        }
        if (op==1 && eq(a+24,s->lease.address,4) && (zero(a+14,4) || onlink(&s->lease,a+14))) {
            if (s->reply_pending) { ++s->reply_dropped;return; }copy(s->reply_mac,a+8,6);copy(s->reply_ip,a+14,4);s->reply_pending=true;++s->reply_queued;return;
        }
        ++s->ignored;return;
    }
    if (type!=0x800 || !s->active || s->phase!=NP_PING || !s->gateway_known || !eq(p,s->mac,6) || !eq(p+6,s->gateway_mac,6)) { ++s->ignored;return; }
    ++s->ipv4_rx;if (n<74 || n>1514) { goto malformed; }const unsigned char *v=p+14,*e=v+20;
    if (v[0]!=0x45 || be16(v+2)!=60 || (be16(v+6)&~0x4000) || !v[8] || v[9]!=1 || checksum(v,20) || !eq(v+12,s->lease.router,4) || !eq(v+16,s->lease.address,4) || checksum(e,40)) { goto malformed; }
    if (e[0] || e[1] || be16(e+4)!=s->identifier || !eq(e+8,s->token,16) || !eq(e+24,suffix,16)) { ++s->ignored;return; }
    unsigned sequence=be16(e+6);if (!sequence || sequence>3 || !(s->issued_mask&(1u<<(sequence-1))) || (s->seen_mask&(1u<<(sequence-1)))) { ++s->ignored;return; }
    s->seen_mask|=1u<<(sequence-1);return;
malformed:++s->malformed;
}
enum block_result netprobe_run(const struct netprobe_io *io,const unsigned char local[6],const struct dhcp_lease *lease,const unsigned char entropy[32],uint64_t origin,struct netprobe *s)
{
    if (!io || !io->tx || !io->poll || !io->delay_us || !io->milliseconds || !io->ready || !s) { return BLOCK_ARGUMENT; }
    if (s->attempted) { return BLOCK_NOT_READY; }enum block_result r=netprobe_init(s,local,lease,entropy,origin);if (r!=BLOCK_OK) { return r; }
    s->start=io->milliseconds(io->context);uint64_t next=s->initial_delay;
    for (unsigned i=0;i<3000;++i) {
        ++s->polls;r=io->poll(io->context);if (r!=BLOCK_OK) { break; }
        if (s->conflict || s->gateway_changed) { r=BLOCK_IO;break; }if (!io->ready(io->context)) { r=BLOCK_NOT_READY;break; }
        uint64_t now=io->milliseconds(io->context),elapsed=now-s->start;
        if (elapsed>=30000 || now-s->lease_origin>=(uint64_t)s->lease.seconds*1000) { r=BLOCK_TIMEOUT;break; }
        for (unsigned j=0;j<3;++j) { unsigned bit=1u<<j;if ((s->seen_mask&s->written_mask&bit) && !(s->received_mask&bit)) { s->received_mask|=bit;s->latency_ms[j]=now-s->echo_started[j]; } }
        if (s->phase==NP_RESOLVING && s->gateway_known && s->resolutions) { s->phase=NP_PING;next=elapsed; }
        if (s->phase==NP_PROBING && s->probes==3 && elapsed>=next) { s->phase=NP_ANNOUNCING; }
        if (s->phase==NP_ANNOUNCING && s->announcements==2 && elapsed>=next) { s->phase=NP_RESOLVING; }
        if (s->phase==NP_PING && ((s->received_mask==7 && s->written_mask==7) || (s->echoes==3 && elapsed>=next))) { r=s->received_mask ? BLOCK_OK : BLOCK_TIMEOUT;if (r==BLOCK_OK) { s->phase=NP_DONE; }break; }
        bool send=s->reply_pending || elapsed>=next;enum netprobe_packet kind=s->reply_pending ? NP_ARP_REPLY : s->phase==NP_PROBING ? NP_ARP_PROBE : s->phase==NP_ANNOUNCING ? NP_ARP_ANNOUNCE : s->phase==NP_RESOLVING ? NP_ARP_RESOLVE : NP_ECHO;
        if (kind==NP_ARP_PROBE && s->probes==3) { send=false; }if (kind==NP_ARP_RESOLVE && s->resolutions==3) { send=false; }if (kind==NP_ECHO && s->echoes==3) { send=false; }
        if (send) {
            if (s->arp_replies>=8 && kind==NP_ARP_REPLY) { r=BLOCK_RANGE;break; }
            unsigned char frame[74];size_t bytes=0;unsigned sequence=kind==NP_ECHO ? s->echoes+1 : 0;
            r=netprobe_encode(s,kind,sequence,frame,sizeof(frame),&bytes);if (r!=BLOCK_OK) { break; }
            if (kind==NP_ECHO) { s->issued_mask|=1u<<(sequence-1);s->echo_started[sequence-1]=now; }
            if (kind==NP_ARP_RESOLVE) { s->resolve_issued=true; }
            /* A reply during credit pumping may be observed before completion;
             * only successful writes commit counters and observed echoes. */
            r=io->tx(io->context,kind,sequence,frame,bytes);
            if (r==BLOCK_NOT_READY && !s->conflict && ++s->deferrals<=8) { continue; }if (r!=BLOCK_OK) { break; }
            if (s->conflict || s->gateway_changed) { r=BLOCK_IO;break; }
            now=io->milliseconds(io->context);elapsed=now-s->start;
            if (kind==NP_ARP_REPLY) { ++s->arp_replies;s->reply_pending=false; }
            else if (kind==NP_ARP_PROBE) { ++s->probes;next=elapsed+(s->probes==3 ? 2000 : s->probe_delay[s->probes-1]); }
            else if (kind==NP_ARP_ANNOUNCE) { ++s->announcements;s->active=s->was_active=true;next=elapsed+2000; }
            else if (kind==NP_ARP_RESOLVE) { ++s->resolutions;next=elapsed+1000; }
            else { ++s->echoes;s->written_mask|=1u<<(sequence-1);next=elapsed+(s->echoes==3 ? 2000 : 1000); }
        }
        r=io->delay_us(io->context,10000);if (r!=BLOCK_OK) { break; }r=BLOCK_TIMEOUT;
    }
    s->active=s->armed=false;s->reply_pending=false;s->error=r;if (r!=BLOCK_OK) { s->phase=NP_FAILED; }return r;
}
