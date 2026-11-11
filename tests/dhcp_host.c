#include <stdio.h>
#include <string.h>
#include "host_assert.h"
#include "dhcp.h"
static unsigned char mac[6],entropy[10],discover[600],offer[600],request[600],ack[600];static size_t dn,on,rn,an;
static size_t unhex(const char *s,unsigned char *p,size_t cap)
{ size_t n=strlen(s)/2;assert(n<=cap);for (size_t i=0;i<n;++i) { unsigned v;assert(sscanf(s+2*i,"%2x",&v)==1);p[i]=(unsigned char)v; }return n; }
static size_t option(unsigned char *p,size_t n,unsigned tag)
{ for (size_t at=282;at<n;) { unsigned t=p[at++];if (t==tag) { return at; }if (!t) { continue; }if (t==255) { break; }at+=1+p[at]; }return 0; }
static void parser_negatives(void)
{
    for (unsigned fault=0;fault<9;++fault) {
        unsigned char bad[600];memcpy(bad,offer,on);bad[40]=bad[41]=0; /* legal zero UDP checksum: reach option/BOOTP parser */
        size_t at=0;
        if (!fault) { at=option(bad,on,61);assert(at);bad[at+2]^=1; }
        if (fault==1) { at=option(bad,on,54);bad[at]=3; }
        if (fault==2) { at=option(bad,on,51);memset(bad+at+1,0,4); }
        if (fault==3) { at=option(bad,on,1);bad[at+2]=0; }
        if (fault==4) { at=option(bad,on,6);bad[at]=5; }
        if (fault==5) { at=option(bad,on,255)-1;assert(at+4<on);bad[at++]=53;bad[at++]=1;bad[at++]=2;bad[at]=255; }
        if (fault==6) { at=option(bad,on,255)-1;bad[at++]=52;bad[at++]=1;bad[at++]=1;bad[at]=255; }
        if (fault==7) { bad[42+24]=1; } /* relay not supported */
        if (fault==8) { at=option(bad,on,53);bad[at+1]=5; } /* unsolicited ACK */
        struct dhcp_client neg={0};assert(dhcp_client_init(&neg,mac,entropy)==BLOCK_OK);neg.discover_issued=true;dhcp_receive(&neg,bad,on);assert(!neg.have_offer);
    }
    unsigned char nak[600];memcpy(nak,ack,an);nak[40]=nak[41]=0;size_t at=option(nak,an,53);nak[at+1]=6;
    struct dhcp_client neg={0};assert(dhcp_client_init(&neg,mac,entropy)==BLOCK_OK);neg.discover_issued=true;dhcp_receive(&neg,offer,on);neg.phase=DHCP_REQUESTING;neg.request_issued=true;
    neg.offer.server[3]^=1;dhcp_receive(&neg,nak,an);assert(!neg.nak_seen);neg.offer.server[3]^=1;dhcp_receive(&neg,nak,an);assert(neg.nak_seen && neg.phase==DHCP_FAILED);
}
static struct dhcp_client s;
static struct { uint64_t ms;unsigned calls,tx,polls,delays,mode;bool ready,in_rx; } m;
static void receive(const void *p,size_t n) { m.in_rx=true;dhcp_receive(&s,p,n);m.in_rx=false; }
static bool ready(void *ctx) { assert(ctx==&m && !m.in_rx);return m.ready; }
static enum block_result tx(void *ctx,const void *p,size_t n)
{
    assert(ctx==&m && !m.in_rx && dhcp_wire_request_valid(p,n));++m.calls;++m.tx;const unsigned char *frame=p;unsigned type=frame[282+2];
    if (m.mode==7) { return BLOCK_NOT_READY; }
    if (type==1 && m.mode!=1) { receive(offer,on);assert(s.have_offer);if (m.mode==8) { return BLOCK_IO; } }
    if (type==3 && m.mode!=2) { receive(ack,an);assert(s.ack_seen);if (m.mode==9) { return BLOCK_IO; } }
    return BLOCK_OK;
}
static enum block_result poll(void *ctx) { assert(ctx==&m && !m.in_rx);++m.calls;++m.polls;if (m.mode==3) { return BLOCK_IO; }return BLOCK_OK; }
static enum block_result delay(void *ctx,unsigned us) { assert(ctx==&m && us==10000 && !m.in_rx);++m.calls;++m.delays;if (m.mode==4) { return BLOCK_IO; }if (m.mode!=5) { m.ms+=10; }return BLOCK_OK; }
static uint64_t time_ms(void *ctx) { assert(ctx==&m);return m.ms; }
static const struct dhcp_io io={&m,tx,poll,delay,time_ms,ready};
static void seed(unsigned mode) { memset(&m,0,sizeof(m));memset(&s,0,sizeof(s));m.ready=mode!=6;m.mode=mode; }
int main(int argc,char **argv)
{
    assert(dhcp_selftest());
    assert(argc==2);FILE *f=fopen(argv[1],"r");assert(f);char line[10000];unsigned count=0;
    while (fgets(line,sizeof(line),f)) {
        char *v[8];unsigned fields=0;for (char *p=strtok(line," \r\n");p;p=strtok(NULL," \r\n")) { assert(fields<8);v[fields++]=p; }assert(fields==8);
        unhex(v[1],mac,6);unhex(v[2],entropy,10);dn=unhex(v[3],discover,600);on=unhex(v[4],offer,600);rn=unhex(v[5],request,600);an=unhex(v[6],ack,600);
        unsigned char out[343];size_t n=0;memset(out,0xa5,sizeof(out));memset(&s,0,sizeof(s));assert(dhcp_client_init(&s,mac,entropy)==BLOCK_OK);
        assert(dhcp_encode(&s,false,0,out,sizeof(out),&n)==BLOCK_OK && n==dn && !memcmp(out,discover,n) && out[342]==0xa5 && dhcp_wire_request_valid(out,n));
        s.discover_issued=true;dhcp_receive(&s,offer,on);assert(s.have_offer && !s.discover_written);assert(dhcp_encode(&s,true,0,out,sizeof(out),&n)==BLOCK_NOT_READY);
        s.discover_written=true;s.phase=DHCP_REQUESTING;assert(dhcp_encode(&s,true,0,out,sizeof(out),&n)==BLOCK_OK && n==rn && !memcmp(out,request,n) && dhcp_wire_request_valid(out,n));
        dhcp_receive(&s,ack,an);assert(!s.ack_seen);s.request_issued=true;dhcp_receive(&s,ack,an);assert(s.ack_seen && !s.request_written && s.lease.seconds==3600 && s.lease.have_dns && s.lease.have_router && s.lease.have_mask);
        assert(s.lease.address[3]==10+count && s.lease.server[3]==1 && s.lease.dns[0]==8);
        assert(dhcp_encode_decline(&s,out,sizeof(out),&n)==BLOCK_NOT_READY);
        s.phase=DHCP_ACKED;s.request_written=true;unsigned char decline[600];size_t declined=unhex(v[7],decline,sizeof(decline));
        assert(dhcp_encode_decline(&s,out,sizeof(out),&n)==BLOCK_OK && n==declined && !memcmp(out,decline,n) && out[342]==0xa5 && dhcp_wire_request_valid(out,n));
        parser_negatives();
        for (size_t cut=0;cut<on;++cut) { struct dhcp_client cut_state={0};assert(dhcp_client_init(&cut_state,mac,entropy)==BLOCK_OK);cut_state.discover_issued=true;dhcp_receive(&cut_state,offer,cut);assert(!cut_state.have_offer); }
        for (unsigned fault=0;fault<10;++fault) {
            unsigned char bad[600];memcpy(bad,offer,on);
            static const unsigned positions[]={0,12,14,16,20,24,34,42,46,70};bad[positions[fault]]^=1;
            struct dhcp_client negative={0};assert(dhcp_client_init(&negative,mac,entropy)==BLOCK_OK);negative.discover_issued=true;dhcp_receive(&negative,bad,on);assert(!negative.have_offer);
        }
        seed(0);assert(dhcp_probe_run(&io,mac,entropy,&s)==BLOCK_OK && s.phase==DHCP_ACKED && !s.armed && s.discover_sent==1 && s.request_sent==1 && s.ack_seen);
        unsigned calls=m.calls;assert(dhcp_probe_run(&io,mac,entropy,&s)==BLOCK_NOT_READY && m.calls==calls);
        for (unsigned mode=1;mode<=9;++mode) {
            seed(mode);enum block_result r=dhcp_probe_run(&io,mac,entropy,&s);assert(r!=BLOCK_OK && !s.armed && s.phase==DHCP_FAILED);
            assert(s.discover_sent<=3 && s.request_sent<=3 && s.polls<=4500);
            if (mode==1) { assert(s.discover_sent==3 && !s.have_offer && !s.request_sent && r==BLOCK_TIMEOUT); }
            if (mode==2) { assert(s.request_sent==3 && s.have_offer && !s.ack_seen && r==BLOCK_TIMEOUT); }
            if (mode==7) { assert(s.deferrals==9 && !s.discover_written); }
            if (mode==8) { assert(s.have_offer && !s.discover_written && !s.request_sent); }
            if (mode==9) { assert(s.ack_seen && !s.request_written && r==BLOCK_IO); }
        }
        ++count;
    }fclose(f);assert(count==16);uint32_t random=0x1234;
    for (unsigned i=0;i<10000;++i) {
        struct { unsigned char before[8];struct dhcp_client state;unsigned char after[8]; } guard;memset(&guard,0xa5,sizeof(guard));memset(&guard.state,0,sizeof(guard.state));
        assert(dhcp_client_init(&guard.state,mac,entropy)==BLOCK_OK);guard.state.discover_issued=true;unsigned char bad[1600]={0};memcpy(bad,offer,on);random=random*1664525+1013904223;bad[random%1600]^=(unsigned char)(random>>24);
        dhcp_receive(&guard.state,bad,i%3 ? on : random%1601);for (unsigned j=0;j<8;++j) { assert(guard.before[j]==0xa5 && guard.after[j]==0xa5); }
    }
    puts("PASS: 16 independent exact DHCP client packets and server DORA fixtures, checksum/identity/transaction gates, early reply vs failed write, lost OFFER/ACK, bounded retries/deferrals/stalled clocks, 10000 receive mutations and output guards; no configured IP");return 0;
}
