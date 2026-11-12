#include <stdio.h>
#include <string.h>
#include "host_assert.h"
#include "network_session.h"
#include "netprobe_fixture.h"
static struct netprobe n;static struct network_session s;
static struct { uint64_t ms;unsigned mode,calls,fail_at,input,randoms,checks,infos,tx,polls;bool in_rx,asked;char output[20000];size_t bytes; } m;
static enum block_result touch(void) { assert(!m.in_rx);return ++m.calls==m.fail_at ? BLOCK_IO : BLOCK_OK; }
static unsigned sum(const unsigned char *p,size_t bytes)
{ unsigned x=0;for (size_t i=0;i<bytes;i+=2) { x+=(unsigned)p[i]<<8|(i+1<bytes ? p[i+1] : 0); }while (x>>16) { x=(x&65535)+(x>>16); }return (~x)&65535; }
static void put16(unsigned char *p,unsigned x) { p[0]=(unsigned char)(x>>8);p[1]=(unsigned char)x; }
static void receive(const void *p,size_t bytes) { m.in_rx=true;netprobe_receive(&n,p,bytes);m.in_rx=false; }
static enum block_result tx(void *ctx,enum netprobe_packet kind,unsigned sequence,const void *p,size_t bytes)
{
    assert(ctx==&m && n.live && netprobe_can_tx(&n,m.ms) && netprobe_packet_valid(&n,kind,sequence,p,bytes));enum block_result r=touch();if (r!=BLOCK_OK) { return r; }++m.tx;
    if (kind==NP_ECHO && m.mode!=2) {
        unsigned char reply[74];memcpy(reply,p,74);memcpy(reply,n.mac,6);memcpy(reply+6,n.gateway_mac,6);memcpy(reply+26,n.target,4);memcpy(reply+30,n.lease.address,4);
        reply[34]=reply[36]=reply[37]=reply[24]=reply[25]=0;put16(reply+36,sum(reply+34,40));put16(reply+24,sum(reply+14,20));receive(reply,74);
    }return BLOCK_OK;
}
static enum block_result poll(void *ctx)
{
    assert(ctx==&m);enum block_result r=touch();if (r!=BLOCK_OK) { return r; }++m.polls;
    if (!m.asked) { receive(np_request_fixture,60);m.asked=true; }
    if (m.mode==3 && m.ms>=2000) { return BLOCK_IO; }
    if (m.mode==4 && m.ms>=2000) { unsigned char conflict[60];memcpy(conflict,np_gateway_fixture,60);memcpy(conflict+28,n.lease.address,4);receive(conflict,60); }
    return BLOCK_OK;
}
static enum block_result delay(void *ctx,unsigned us)
{ assert(ctx==&m && us==10000);enum block_result r=touch();if (r==BLOCK_OK) { m.ms+=10; }return r; }
static uint64_t time_ms(void *ctx) { assert(ctx==&m);return m.ms; }
static bool ready(void *ctx) { assert(ctx==&m);return true; }
static const char *commands="help\nping www.google.com\nping 1.1.1.1\n";
static int input(void *ctx)
{
    assert(ctx==&m && s.active && !m.in_rx);
    if (m.mode==7) {
        if (m.input<140) { ++m.input;return 'x'; }if (m.input++==140) { return '\n'; }
        static const char edits[]="garbage\025disconnecX\bt\n";unsigned at=m.input-142;return at<sizeof(edits)-1 ? edits[at] : -1;
    }
    if (m.mode==1) { return m.ms>=45000 ? 3 : -1; } /* stays open > old 30 s boundary */
    if (m.mode==5 || m.mode==3 || m.mode==4) { return -1; }
    if (m.input<strlen(commands)) { return commands[m.input++]; }
    if (n.ping_finished && m.ms>=16000) { static const char close[]="netinfo\ndisconnect\n";unsigned at=m.input++-(unsigned)strlen(commands);if (at<sizeof(close)-1) { return close[at]; } }
    return -1;
}
static void output(void *ctx,const char *text)
{ assert(ctx==&m && !m.in_rx);size_t bytes=strlen(text);assert(m.bytes+bytes<sizeof(m.output));memcpy(m.output+m.bytes,text,bytes+1);m.bytes+=bytes; }
static bool random_bytes(void *ctx,unsigned char out[32])
{ assert(ctx==&m && !m.in_rx);++m.randoms;for (unsigned i=0;i<32;++i) { out[i]=(unsigned char)(i+9+m.randoms); }return m.mode!=6; }
static enum block_result inspect(void *ctx)
{ assert(ctx==&m && n.live && n.active);++m.checks;return touch(); }
static void info(void *ctx) { assert(ctx==&m && n.active);++m.infos; }
static const struct network_session_io io={{&m,tx,poll,delay,time_ms,ready},input,output,random_bytes,inspect,info};
static void seed(unsigned mode)
{
    memset(&m,0,sizeof(m));memset(&s,0,sizeof(s));memset(&n,0,sizeof(n));m.mode=mode;
    const unsigned char mac[6]={2,0,0,0,0,1};unsigned char entropy[32];for (unsigned i=0;i<32;++i) { entropy[i]=(unsigned char)(i+1); }
    const struct dhcp_lease lease={.address={192,0,2,10},.mask={255,255,255,0},.router={192,0,2,1},.seconds=mode==5 ? 1 : 3600,.have_mask=true,.have_router=true};
    assert(netprobe_init(&n,mac,&lease,entropy,0)==BLOCK_OK);n.live=n.active=n.was_active=n.gateway_known=true;n.phase=NP_READY;memcpy(n.gateway_mac,np_gateway_fixture+6,6);
}
int main(void)
{
    const char *invalid[]={"","1.2.3","1.2.3.256","1.2.3.4x","1..2.3","1.2.3.0000"," 1.2.3.4","0.1.2.3","127.0.0.1","224.0.0.1","www.google.com"};
    unsigned char address[4]={0xa5,0xa5,0xa5,0xa5};for (unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);++i) { assert(!network_parse_ipv4(invalid[i],address) && address[0]==0xa5); }
    assert(network_parse_ipv4("1.1.1.1",address) && address[3]==1);
    seed(0);assert(network_session_run(&io,&n,&s)==BLOCK_OK && s.reason==NS_USER && s.ended && !s.active && !n.live && !n.active && !n.armed && s.pings_started==1 && s.pings_ok==1 && n.received_mask==7 && m.checks>=3 && m.infos==2);
    assert(m.ms>=16000 && strstr(m.output,"Wi-Fi session OPEN") && strstr(m.output,"DNS is not implemented") && strstr(m.output,"reply verified"));unsigned calls=m.calls;
    assert(network_session_run(&io,&n,&s)!=BLOCK_OK && m.calls==calls);
    for (unsigned mode=1;mode<=7;++mode) {
        seed(mode);enum block_result r=network_session_run(&io,&n,&s);assert(s.ended && !s.active && !n.live && !n.active && !n.armed);
        if (mode==1) { assert(r==BLOCK_OK && s.reason==NS_USER && m.ms>=45000); }
        if (mode==2) { assert(r==BLOCK_OK && s.pings_timeout==1 && !s.pings_ok && n.phase==NP_READY && s.reason==NS_USER); }
        if (mode==3) { assert(r==BLOCK_IO && s.reason==NS_LINK); }
        if (mode==4) { assert(r!=BLOCK_OK && n.conflict && s.reason==NS_CONFLICT); }
        if (mode==5) { assert(r==BLOCK_TIMEOUT && s.reason==NS_LEASE && m.ms==1000); }
        if (mode==6) { assert(r==BLOCK_IO && s.reason==NS_IO && !s.pings_started); }
        if (mode==7) { assert(r==BLOCK_OK && s.reason==NS_USER && !s.pings_started && strstr(m.output,"Input too long; command discarded")); }
    }
    for (unsigned fail=1;fail<=calls;fail+=17) { seed(0);m.fail_at=fail;assert(network_session_run(&io,&n,&s)!=BLOCK_OK && !n.active && !n.armed && !s.active); }
    seed(0);unsigned char entropy[32];memset(entropy,0x55,sizeof(entropy));unsigned char target[4]={192,0,2,20};assert(netprobe_ping_start(&n,target,entropy,0)==BLOCK_UNSUPPORTED);
    target[0]=1;target[1]=target[2]=target[3]=1;assert(netprobe_ping_start(&n,target,entropy,0)==BLOCK_OK);unsigned char stale[74];memcpy(stale,np_echo_fixture,74);receive(stale,74);assert(!n.seen_mask);
    netprobe_close(&n);assert(!netprobe_can_tx(&n,0));
    puts("PASS: foreground session stays active, responsive input/ARP and numeric routed ping, fresh-token stale-reply rejection, ping timeout keeps Wi-Fi open, periodic peer checks, Ctrl+C/disconnect, lease expiry, link/conflict/RNG/I/O failure cleanup; DNS refused");return 0;
}
