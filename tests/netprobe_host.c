#include <stdio.h>
#include <string.h>
#include "host_assert.h"
#include "netprobe.h"
static struct netprobe s;static unsigned char local[6],entropy[32],packets[8][100];static size_t lengths[8];static struct dhcp_lease lease;
static struct { unsigned calls,fail_at,mode,tx[5],polls,delays;uint64_t ms,last_probe,first_announce;bool in_rx,asked; } m;
static size_t unhex(const char *p,unsigned char *out,size_t cap)
{ size_t n=strlen(p)/2;assert(n<=cap);for (size_t i=0;i<n;++i) { unsigned v;assert(sscanf(p+2*i,"%2x",&v)==1);out[i]=(unsigned char)v; }return n; }
static unsigned sum(const unsigned char *p,size_t n) { unsigned s=0;for (size_t i=0;i<n;i+=2) { s+=(unsigned)p[i]<<8|(i+1<n ? p[i+1] : 0); }while (s>>16) { s=(s&65535)+(s>>16); }return (~s)&65535; }
static void put16(unsigned char *p,unsigned v) { p[0]=(unsigned char)(v>>8);p[1]=(unsigned char)v; }
static enum block_result touch(void) { assert(!m.in_rx);return ++m.calls==m.fail_at ? BLOCK_IO : BLOCK_OK; }
static void receive(const void *p,size_t n) { m.in_rx=true;netprobe_receive(&s,p,n);m.in_rx=false; }
static void conflict(bool probe)
{ unsigned char p[60];memcpy(p,packets[3],60);if (probe) { memset(p,255,6);put16(p+20,1);memset(p+28,0,4);memset(p+32,0,6); }else { memcpy(p+28,lease.address,4); }receive(p,60); }
static enum block_result tx(void *ctx,enum netprobe_packet kind,unsigned sequence,const void *data,size_t n)
{
    assert(ctx==&m && netprobe_packet_valid(&s,kind,sequence,data,n) && netprobe_can_tx(&s,m.ms));enum block_result r=touch();if (r!=BLOCK_OK) { return r; }++m.tx[kind];
    if (m.mode==11) { return BLOCK_NOT_READY; }
    if (kind==NP_ARP_PROBE) {
        assert(!s.active && !s.announcements);if (m.tx[kind]>1) { assert(m.ms-m.last_probe>=s.probe_delay[m.tx[kind]-2]); }m.last_probe=m.ms;
        if (m.mode==1 || m.mode==2) { conflict(m.mode==2); }
    }
    if (kind==NP_ARP_ANNOUNCE) {
        assert(s.probes==3 && m.ms-m.last_probe>=2000);if (m.tx[kind]==1) { m.first_announce=m.ms; }else { assert(m.ms-m.first_announce>=2000); }
        if (m.mode==14 && s.active) { conflict(false); }
    }
    if (kind==NP_ARP_RESOLVE && m.mode!=3) {
        assert(s.active && s.probes==3 && s.announcements==2);receive(packets[3],lengths[3]);assert(s.gateway_known);
        if (m.mode==12) { return BLOCK_IO; }
        if (m.mode==15) { unsigned char changed[60];memcpy(changed,packets[3],60);changed[11]^=1;changed[27]^=1;receive(changed,60);assert(s.gateway_changed); }
    }
    if (kind==NP_ECHO && m.mode!=4) {
        const unsigned char *p=data;assert(s.active && s.gateway_known && sequence>=1 && sequence<=3);unsigned char reply[74];memcpy(reply,packets[5],74);put16(reply+40,sequence);
        reply[36]=reply[37]=0;if (m.mode==6) { reply[42]^=1; }put16(reply+36,sum(reply+34,40));
        if (m.mode==5) { reply[36]^=1; }receive(reply,74);
        if (m.mode==13) { assert(s.seen_mask&(1u<<(sequence-1)));assert(!(s.written_mask&(1u<<(sequence-1))));return BLOCK_IO; }
        assert(p[34]==8);
    }
    if (kind==NP_ARP_REPLY) { assert(n==lengths[7] && !memcmp(data,packets[7],n)); }
    return BLOCK_OK;
}
static enum block_result poll(void *ctx)
{
    assert(ctx==&m);enum block_result r=touch();if (r!=BLOCK_OK) { return r; }++m.polls;if (m.mode==9) { return BLOCK_IO; }
    if (s.active && !m.asked) { receive(packets[6],lengths[6]);m.asked=true;assert(s.reply_pending); }
    return BLOCK_OK;
}
static enum block_result delay(void *ctx,unsigned us)
{ assert(ctx==&m && us==10000);enum block_result r=touch();if (r!=BLOCK_OK) { return r; }++m.delays;if (m.mode==10) { return BLOCK_IO; }if (m.mode!=7) { m.ms+=10; }return BLOCK_OK; }
static uint64_t milliseconds(void *ctx) { assert(ctx==&m);return m.ms; }
static bool ready(void *ctx) { assert(ctx==&m);return m.mode!=8; }
static const struct netprobe_io io={&m,tx,poll,delay,milliseconds,ready};
static void seed(unsigned mode) { memset(&m,0,sizeof(m));memset(&s,0,sizeof(s));m.mode=mode;if (mode==16) { m.ms=3600000; } }
static enum block_result run(void) { return netprobe_run(&io,local,&lease,entropy,0,&s); }
int main(int argc,char **argv)
{
    assert(argc==2 && netprobe_selftest());FILE *f=fopen(argv[1],"r");assert(f);char line[5000];unsigned rows=0;
    while (fgets(line,sizeof(line),f)) {
        char *v[14];unsigned fields=0;for (char *p=strtok(line," \r\n");p;p=strtok(NULL," \r\n")) { assert(fields<14);v[fields++]=p; }assert(fields==14);
        unhex(v[1],local,6);unhex(v[2],entropy,32);lease=(struct dhcp_lease){.have_mask=true,.have_router=true,.seconds=3600};unhex(v[3],lease.address,4);unhex(v[4],lease.router,4);unhex(v[5],lease.mask,4);
        for (unsigned i=0;i<8;++i) { lengths[i]=unhex(v[6+i],packets[i],100); }
        seed(0);assert(netprobe_init(&s,local,&lease,entropy,0)==BLOCK_OK);unsigned char out[75];size_t bytes=0;memset(out,0xa5,sizeof(out));
        assert(netprobe_encode(&s,NP_ARP_PROBE,0,out,sizeof(out),&bytes)==BLOCK_OK && bytes==60 && !memcmp(out,packets[0],bytes) && out[60]==0xa5);
        s.phase=NP_ANNOUNCING;assert(netprobe_encode(&s,NP_ARP_ANNOUNCE,0,out,sizeof(out),&bytes)==BLOCK_OK && !memcmp(out,packets[1],bytes));
        s.active=true;s.phase=NP_RESOLVING;assert(netprobe_encode(&s,NP_ARP_RESOLVE,0,out,sizeof(out),&bytes)==BLOCK_OK && !memcmp(out,packets[2],bytes));
        receive(packets[3],60);assert(!s.gateway_known);s.resolve_issued=true;receive(packets[3],60);assert(s.gateway_known);
        s.phase=NP_PING;assert(netprobe_encode(&s,NP_ECHO,1,out,sizeof(out),&bytes)==BLOCK_OK && bytes==74 && !memcmp(out,packets[4],74) && out[74]==0xa5 && netprobe_packet_valid(&s,NP_ECHO,1,out,bytes));
        receive(packets[5],74);assert(!s.seen_mask);s.issued_mask=1;receive(packets[5],74);assert(s.seen_mask==1 && !s.written_mask && !s.received_mask);
        receive(packets[5],74);assert(s.seen_mask==1);receive(packets[6],60);assert(s.reply_pending);
        assert(netprobe_encode(&s,NP_ARP_REPLY,0,out,sizeof(out),&bytes)==BLOCK_OK && bytes==60 && !memcmp(out,packets[7],bytes));
        for (unsigned cut=0;cut<74;++cut) { struct netprobe n=s;n.seen_mask=0;netprobe_receive(&n,packets[5],cut);assert(!n.seen_mask); }
        seed(0);assert(run()==BLOCK_OK && s.phase==NP_DONE && s.received_mask==7 && s.echoes==3 && s.probes==3 && s.announcements==2 && s.resolutions==1 && s.arp_replies==1 && !s.active && !s.armed && s.was_active);
        unsigned total=m.calls,calls=m.calls;assert(run()==BLOCK_NOT_READY && m.calls==calls);
        for (unsigned mode=1;mode<=16;++mode) {
            seed(mode);assert(run()!=BLOCK_OK && !s.active && !s.armed && s.phase==NP_FAILED && s.echoes<=3 && s.probes<=3 && s.announcements<=2 && s.resolutions<=3 && s.polls<=3000);
            if (mode==1 || mode==2 || mode==14) { assert(s.conflict && !s.echoes); }
            if (mode==3) { assert(s.resolutions==3 && !s.gateway_known); }
            if (mode==4 || mode==5 || mode==6) { assert(s.echoes==3 && !s.received_mask); }
            if (mode==11) { assert(s.deferrals==9 && !s.probes); }
            if (mode==12) { assert(s.gateway_known && !s.resolutions && !s.echoes); }
            if (mode==13) { assert(s.seen_mask && !s.written_mask && !s.received_mask); }
            if (mode==15) { assert(s.gateway_changed && !s.echoes); }
            if (mode==16) { assert(!s.probes); }
        }
        if (!rows) { for (unsigned fail=1;fail<=total;++fail) { seed(0);m.fail_at=fail;assert(run()!=BLOCK_OK && !s.armed && !s.active); } }
        ++rows;
    }
    fclose(f);assert(rows==8);uint32_t random=0x12345678;
    for (unsigned i=0;i<10000;++i) {
        struct { unsigned char before[8];struct netprobe state;unsigned char after[8]; } g;memset(&g,0xa5,sizeof(g));memset(&g.state,0,sizeof(g.state));assert(netprobe_init(&g.state,local,&lease,entropy,0)==BLOCK_OK);g.state.active=true;g.state.phase=NP_PING;g.state.gateway_known=true;memcpy(g.state.gateway_mac,packets[5]+6,6);g.state.issued_mask=7;
        unsigned char bad[1600]={0};memcpy(bad,packets[i%2 ? 5 : 3],i%2 ? 74 : 60);random=random*1664525+1013904223;bad[random%1600]^=(unsigned char)(random>>24);netprobe_receive(&g.state,bad,random%1601);
        for (unsigned j=0;j<8;++j) { assert(g.before[j]==0xa5 && g.after[j]==0xa5); }
    }
    puts("PASS: 8 independent ARP/IPv4/ICMP exact packet sets; ACD timing, conflicts before/after activation, ARP response queue, gateway/echo identity/checksums, early replies then failed writes, lease expiry, stopped clocks, missing replies, credit limits, every I/O failure and 10000 receive mutations; temporary IP removed");return 0;
}
