#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <windows.h>
#include "host_assert.h"
#include "netprobe.h"
#include "dns_reply_fixture.h"
static struct netprobe s;static unsigned char entropy[32],server[4],query[DNS_QUERY_MAX],reply[DNS_MESSAGE_MAX],wire[600],response[600];static size_t qn,rn,wn,sn;
static struct { uint64_t ms;unsigned writes,mode,arp,calls,fail_at;bool callback; } m;
static size_t unhex(const char *p,unsigned char *out,size_t cap)
{ size_t n=strlen(p)/2;assert(n<=cap);for (size_t i=0;i<n;++i) { unsigned v;assert(sscanf(p+i*2,"%2x",&v)==1);out[i]=(unsigned char)v; }return n; }
static void seed(void)
{
    const unsigned char mac[6]={2,0,0,0,0,1};struct dhcp_lease l={.address={192,0,2,10},.mask={255,255,255,0},.router={192,0,2,1},.seconds=3600,.have_mask=true,.have_router=true,.have_dns=true};memcpy(l.dns,server,4);
    memset(&s,0,sizeof(s));memset(&m,0,sizeof(m));assert(netprobe_init(&s,mac,&l,entropy,0)==BLOCK_OK);s.live=s.active=s.gateway_known=true;s.phase=NP_READY;const unsigned char gw[6]={2,0,0,0,1,1};memcpy(s.gateway_mac,gw,6);
}
static enum block_result touch(void) { assert(!m.callback);return ++m.calls==m.fail_at ? BLOCK_IO : BLOCK_OK; }
static void receive(const void *p,size_t n) { m.callback=true;netprobe_receive(&s,p,n);m.callback=false; }
static enum block_result tx(void *ctx,enum netprobe_packet kind,unsigned sequence,const void *p,size_t n)
{
    assert(ctx==&m && netprobe_can_tx(&s,m.ms) && netprobe_packet_valid(&s,kind,sequence,p,n));enum block_result r=touch();if (r!=BLOCK_OK) { return r; }
    if (kind==NP_ARP_REPLY) { ++m.arp;return BLOCK_OK; }assert(kind==NP_DNS_QUERY && !sequence && n==wn && !memcmp(p,wire,n));++m.writes;
    if (m.mode==4) { return BLOCK_NOT_READY; }
    if (m.mode!=1 && m.mode!=3 && m.mode!=5) { receive(response,sn);assert(s.dns.seen && s.dns.status==DNS_PENDING); }
    if (m.mode==2) { assert(!s.dns.written);return BLOCK_IO; }return BLOCK_OK;
}
static enum block_result poll(void *ctx) { assert(ctx==&m);return touch(); }
static enum block_result delay(void *ctx,unsigned us) { assert(ctx==&m && us==10000);if (m.mode!=5) { m.ms+=10; }return touch(); }
static uint64_t clock_ms(void *ctx) { assert(ctx==&m);return m.ms; }
static bool ready(void *ctx) { assert(ctx==&m);return true; }
static const struct netprobe_io io={&m,tx,poll,delay,clock_ms,ready};
static enum block_result run(void)
{ for (unsigned i=0;i<1200;++i) { enum block_result r=netprobe_service(&io,&s);if (r!=BLOCK_OK || s.phase==NP_READY) { return r; }r=delay(&m,10000);if (r!=BLOCK_OK) { return r; } }return BLOCK_TIMEOUT; }
static void parser_cases(const char *name,enum dns_status expected,const unsigned char answer[4],unsigned hops,uint32_t ttl)
{
    struct dns_query d={0};assert(dns_begin(&d,name,entropy)==BLOCK_OK);unsigned char encoded[DNS_QUERY_MAX];size_t bytes;
    assert(dns_encode(&d,encoded,sizeof(encoded),&bytes)==BLOCK_OK && bytes==qn && !memcmp(encoded,query,qn));
    for (size_t cap=0;cap<qn;++cap) { memset(encoded,0xa5,sizeof(encoded));assert(dns_encode(&d,encoded,cap,&bytes)!=BLOCK_OK && !bytes && encoded[0]==0xa5); }
    dns_receive(&d,reply,rn);assert(!d.received && !d.seen);d.issued=true;dns_receive(&d,reply,rn);assert(d.seen && d.status==DNS_PENDING && d.observed_status==expected && d.ttl==ttl && d.cnames==hops && !memcmp(d.address,answer,4));
    struct dns_query saved=d;dns_receive(&d,reply,rn);assert(!memcmp(&d,&saved,sizeof(d)));assert(dns_begin(&d,name,entropy)!=BLOCK_OK);d.status=d.observed_status;assert(dns_begin(&d,name,entropy)!=BLOCK_OK);
    for (size_t length=0;length<rn;++length) { d=(struct dns_query){0};assert(dns_begin(&d,name,entropy)==BLOCK_OK);d.issued=true;dns_receive(&d,reply,length);assert(!d.seen); }
}
static void guarded(const char *name)
{
    SYSTEM_INFO info;GetSystemInfo(&info);unsigned char *pages=VirtualAlloc(NULL,info.dwPageSize*2,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);assert(pages);DWORD old;assert(VirtualProtect(pages+info.dwPageSize,info.dwPageSize,PAGE_NOACCESS,&old));
    uint32_t random=19;
    for (unsigned i=0;i<12000;++i) {
        random=random*1664525u+1013904223u;size_t bytes=i<rn ? i : rn;unsigned char *p=pages+info.dwPageSize-bytes;memcpy(p,reply,bytes);
        if (i>=rn) { p[random%bytes]^=(unsigned char)(1u<<(i%8)); }
        struct dns_query d={0};assert(dns_begin(&d,name,entropy)==BLOCK_OK);d.issued=true;dns_receive(&d,p,bytes);
        assert(d.status==DNS_PENDING);if (d.seen && d.observed_status==DNS_SUCCESS) { assert(d.address[0] && d.address[0]!=127 && d.address[0]<224); }
    }
    assert(VirtualFree(pages,0,MEM_RELEASE));
}
static void adversarial(const char *name)
{
    unsigned char independent[600];size_t model_bytes=dns_reply_fixture(wire,independent,0);assert(model_bytes==sn && !memcmp(independent,response,sn));
    for (unsigned mode=0;mode<8;++mode) {
        unsigned char p[600]={0};memcpy(p,query,qn);p[2]=0x81;p[3]=0x80;p[7]=1;
        const unsigned char rr[]={0xc0,12,0,5,0,1,0,0,0,60,0,2,0xc0,12};memcpy(p+qn,rr,sizeof(rr));
        if (!mode) { fixture_put16(p+qn,0xc000u+(unsigned)qn); } /* owner self pointer */
        if (mode==2) { p[qn+13]=0; } /* pointer into header */
        if (mode==3) { fixture_put16(p+qn+12,0xc000u+(unsigned)qn+12); }
        if (mode==4) { p[qn+11]=3; } /* RDATA overruns packet */
        if (mode==5) { p[qn-3]=2; } /* wrong question type */
        if (mode==6) { p[1]^=1; }
        if (mode==7) { p[qn+11]=1;p[qn+12]=0; }
        struct dns_query d={0};assert(dns_begin(&d,name,entropy)==BLOCK_OK);d.issued=true;dns_receive(&d,p,qn+sizeof(rr)-(mode==7 ? 1 : 0));assert(!d.seen);
        if (mode==5 || mode==6) { assert(d.ignored==1); }else { assert(d.malformed==1); }
    }
    /* Uppercase question echoes are equivalent; forged endpoint fields with
     * repaired checksums must still fail before DNS response acceptance. */
    struct dns_query d={0};assert(dns_begin(&d,name,entropy)==BLOCK_OK);d.issued=true;unsigned char p[600];memcpy(p,reply,rn);for (unsigned i=12;i<qn-4;++i) { if (p[i]>='a' && p[i]<='z') { p[i]-=32; } }dns_receive(&d,p,rn);assert(d.seen && d.observed_status==DNS_SUCCESS);
    const unsigned offsets[]={26,30,34,36,42,54};
    for (unsigned i=0;i<sizeof(offsets)/sizeof(offsets[0]);++i) {
        seed();assert(netprobe_dns_start(&s,name,entropy,0)==BLOCK_OK);s.dns.issued=true;memcpy(p,response,sn);p[offsets[i]]^=1;p[24]=p[25]=p[40]=p[41]=0;
        fixture_put16(p+24,fixture_sum(p+14,20));unsigned char pseudo[600];memcpy(pseudo,p+26,8);pseudo[8]=0;pseudo[9]=17;memcpy(pseudo+10,p+38,2);memcpy(pseudo+12,p+34,sn-34);unsigned sum=fixture_sum(pseudo,12+sn-34);fixture_put16(p+40,sum ? sum : 65535);
        receive(p,sn);assert(!s.dns.seen);
    }
    seed();assert(netprobe_dns_start(&s,name,entropy,0)==BLOCK_OK);s.dns.issued=true;memcpy(p,response,sn);p[40]=p[41]=0;receive(p,sn);assert(s.dns.seen); /* IPv4 UDP checksum zero is permitted. */
    unsigned char reject[32];memset(reject,255,sizeof(reject));d=(struct dns_query){0};assert(dns_begin(&d,name,reject)==BLOCK_ARGUMENT);reject[2]=0;reject[3]=1;assert(dns_begin(&d,name,reject)==BLOCK_OK && d.id==65535 && d.port==1025);
}
int main(int argc,char **argv)
{
    assert(argc==2 && dns_selftest());FILE *file=fopen(argv[1],"r");assert(file);char line[9000];unsigned rows=0;
    while (fgets(line,sizeof(line),file)) {
        char *v[12];unsigned fields=0;for (char *p=strtok(line," \r\n");p;p=strtok(NULL," \r\n")) { assert(fields<12);v[fields++]=p; }assert(fields==12 && !strcmp(v[0],"D"));
        assert(unhex(v[2],entropy,32)==32 && unhex(v[3],server,4)==4);qn=unhex(v[4],query,sizeof(query));rn=unhex(v[5],reply,sizeof(reply));unsigned char answer[4];assert(unhex(v[7],answer,4)==4);
        enum dns_status expected=(enum dns_status)atoi(v[6]);unsigned hops=(unsigned)atoi(v[8]);uint32_t ttl=(uint32_t)atoi(v[9]);wn=unhex(v[10],wire,sizeof(wire));sn=unhex(v[11],response,sizeof(response));
        parser_cases(v[1],expected,answer,hops,ttl);seed();assert(netprobe_dns_start(&s,v[1],entropy,0)==BLOCK_OK);unsigned char encoded[NETPROBE_FRAME_BYTES];size_t bytes=0;
        assert(netprobe_encode(&s,NP_DNS_QUERY,0,encoded,sizeof(encoded),&bytes)==BLOCK_OK && bytes==wn && !memcmp(encoded,wire,wn));
        /* Early replies have no pending issued request. Wrong endpoints,
         * checksums, fragmentation and transaction are never accepted. */
        receive(response,sn);assert(!s.dns.seen);s.dns.issued=true;
        unsigned offsets[]={0,6,14,16,20,22,24,26,30,34,36,38,40,42,44,54};
        for (unsigned i=0;i<sizeof(offsets)/sizeof(offsets[0]);++i) { unsigned char bad[600];memcpy(bad,response,sn);bad[offsets[i]]^=1;s.dns.seen=false;receive(bad,sn);assert(!s.dns.seen); }
        seed();assert(netprobe_dns_start(&s,v[1],entropy,0)==BLOCK_OK && run()==BLOCK_OK && s.dns_finished==1 && s.dns.status==expected && s.dns.sent==1 && s.active && s.live);unsigned calls=m.calls;
        if (!rows) {
            guarded(v[1]);
            adversarial(v[1]);
            for (unsigned mode=1;mode<=5;++mode) {
                seed();m.mode=mode;assert(netprobe_dns_start(&s,v[1],entropy,0)==BLOCK_OK);if (mode==3) { s.lease.seconds=1; }
                enum block_result r=run();if (mode==1) { assert(r==BLOCK_OK && s.dns.status==DNS_TIMEOUT && s.dns.sent==3 && m.ms==10000 && s.active && s.live); }
                else if (mode==2) { assert(r==BLOCK_IO && !s.dns.written && !s.dns_finished && s.dns.status==DNS_PENDING); }
                else if (mode==3) { assert(r==BLOCK_TIMEOUT && s.dns.status==DNS_PENDING && m.ms==1000 && s.dns.sent==1); }
                else if (mode==4) { assert(r==BLOCK_NOT_READY && !s.dns.written); }
                else { assert(r==BLOCK_OK && s.dns.status==DNS_TIMEOUT && !m.ms && s.dns.sent==1 && s.dns_polls==1001); }netprobe_close(&s);assert(!s.active && !s.armed);
            }
            for (unsigned fail=1;fail<=calls;++fail) { seed();m.fail_at=fail;assert(netprobe_dns_start(&s,v[1],entropy,0)==BLOCK_OK && run()!=BLOCK_OK && s.dns.status!=DNS_SUCCESS);netprobe_close(&s); }
        }
        ++rows;
    }fclose(file);assert(rows==13);
    char out[DNS_NAME_MAX+1];memset(out,0xa5,sizeof(out));const char *bad[]={"",".","-a.com","a-.com","a..com","a b.com","a/com","a.com.."};for (unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);++i) { assert(!dns_name_normalize(bad[i],out) && (unsigned char)out[0]==0xa5); }
    assert(dns_name_normalize("WWW.Example.Com.",out) && !strcmp(out,"www.example.com"));
    seed();s.lease.have_dns=false;assert(netprobe_dns_start(&s,"example.com",entropy,0)==BLOCK_UNSUPPORTED && !s.dns.attempted);s.lease.have_dns=true;s.lease.dns[3]=20;assert(netprobe_dns_start(&s,"example.com",entropy,0)==BLOCK_UNSUPPORTED && !s.dns.attempted);
    puts("PASS: 13 independent exact DNS/UDP/IPv4 fixtures; A/multiple A/compressed ordered-unordered CNAMEs, hop limit/loops/forward pointers, NXDOMAIN/NODATA/SERVFAIL/truncation/additional isolation, maximum name, matched question/transaction/endpoints/checksums, unbiased random port selection, bounded retries, failed writes/credit wait/lease expiry, every truncation and 12000 guard-page mutations; no I/O in RX");return 0;
}
