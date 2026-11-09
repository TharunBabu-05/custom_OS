#include <stdio.h>
#include <string.h>
#include "brcm_control.h"
#include "brcm_scan.h"
#include "host_assert.h"
static struct brcm_control_state control;
static struct brcm_scan_state scan;
static struct { unsigned char packet[24][4096],mask[16];unsigned length[24],head,tail,at,sequence,tx,max_rx;uint64_t ms;bool up; } m;
static uint32_t g32(const unsigned char *p) { return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static void p16(unsigned char *p,unsigned v) { p[0]=(unsigned char)v;p[1]=(unsigned char)(v>>8); }
static void p32(unsigned char *p,uint32_t v) { for (unsigned i=0;i<4;++i) { p[i]=(unsigned char)(v>>(8*i)); } }
static void b16(unsigned char *p,unsigned v) { p[0]=(unsigned char)(v>>8);p[1]=(unsigned char)v; }
static void b32(unsigned char *p,uint32_t v) { for (unsigned i=0;i<4;++i) { p[i]=(unsigned char)(v>>(24-8*i)); } }
static unsigned char *enqueue(unsigned length,unsigned channel)
{
    assert(m.tail<24 && length<=4096);unsigned char *p=m.packet[m.tail];m.length[m.tail++]=(length+3)&~3u;memset(p,0,4096);
    p32(p,((uint32_t)(uint16_t)~length<<16)|length);p[4]=(unsigned char)m.sequence++;p[5]=(unsigned char)channel;p[7]=12;p[9]=(uint8_t)(control.tx_seq+8);return p+12;
}
static void event(bool partial)
{
    unsigned bss_length=partial ? 126+9*252 : 0;unsigned payload=partial ? 12+bss_length : 0;
    unsigned char *p=enqueue(12+4+72+payload,1);p[0]=0x20;unsigned char *eth=p+4;
    b16(eth+12,0x886c);b16(eth+14,0x8001);eth[20]=16;eth[21]=24;b16(eth+22,1);b16(eth+24,2);b32(eth+28,69);b32(eth+32,partial ? 8 : 0);b32(eth+44,payload);
    if (partial) {
        unsigned char *result=eth+72;p32(result,payload);p32(result+4,109);p16(result+8,scan.sync_id);p16(result+10,1);
        unsigned char *bss=result+12;p32(bss,109);p32(bss+4,bss_length);bss[8]=2;bss[13]=42;bss[18]=9;memcpy(bss+19,"transport",9);
        p16(bss+72,0x100b);bss[88]=11;p16(bss+78,(uint16_t)-55);p16(bss+116,126);p32(bss+120,bss_length-126);
        for (unsigned at=126;at<bss_length;at+=252) { bss[at]=222;bss[at+1]=250; }
    }
}
static enum block_result read32(void *ctx,uint32_t at,uint32_t *v)
{ (void)ctx;assert(at==0x18004020 || at==0x1800404c);*v=at==0x18004020 ? (m.head<m.tail ? 0x40 : 0) : 0x40002;return BLOCK_OK; }
static enum block_result write32(void *ctx,uint32_t at,uint32_t v)
{ (void)ctx;assert(at==0x18004020 && v==0x40);return BLOCK_OK; }
static enum block_result packet(void *ctx,bool write,void *buffer,size_t n)
{
    (void)ctx;unsigned char *p=buffer;
    if (!write) {
        assert(n<=512 && !(n&3));if (n>m.max_rx) { m.max_rx=(unsigned)n; }memset(p,0,n);
        if (m.head==m.tail) { assert(n==64);return BLOCK_OK; }
        unsigned remain=m.length[m.head]-m.at;assert((!m.at && n==64) || (m.at && n==(remain>512 ? 512 : remain)));
        unsigned amount=remain<n ? remain : (unsigned)n;memcpy(p,m.packet[m.head]+m.at,amount);m.at+=amount;
        if (m.at==m.length[m.head]) { ++m.head;m.at=0; }return BLOCK_OK;
    }
    assert(n>=32 && n<=2048 && !(n&3));++m.tx;unsigned command=g32(p+12),bytes=g32(p+16),flags=g32(p+20);bool set=!!(flags&2);const unsigned char *input=p+28;
    unsigned char reply[128]={0};unsigned reply_bytes=0;uint32_t error=0;bool scanning=false;
    if (command==2 || command==3) { assert(set && bytes==4 && !g32(input));m.up=command==2; }
    else {
        assert(command==(set ? 263u : 262u));const char *name=(const char *)input;const unsigned char *data=input+strlen(name)+1;
        if (!strcmp(name,"country")) { assert(!set);memcpy(reply,"IN\0\0",4);memcpy(reply+8,"IN\0\0",4);reply_bytes=12; }
        else if (!strcmp(name,"scan_ver")) { assert(!set);error=0xffffffe9u; }
        else if (!strcmp(name,"event_msgs")) { if (set) { memcpy(m.mask,data,16); }else { memcpy(reply,m.mask,16);reply_bytes=16; } }
        else { assert(!strcmp(name,"escan") && set && m.up && (m.mask[8]&32) && g32(data)==1);scanning=true;event(true); }
    }
    unsigned char *cdc=enqueue(28+reply_bytes,0);p32(cdc,command);p32(cdc+4,reply_bytes);p32(cdc+8,(flags&0xffff0000u)|(error ? 1u : 0));p32(cdc+12,error);memcpy(cdc+16,reply,reply_bytes);
    if (scanning) { event(false); }return BLOCK_OK;
}
static enum block_result delay(void *ctx,unsigned us) { (void)ctx;m.ms+=us/1000;return BLOCK_OK; }
static uint64_t now(void *ctx) { (void)ctx;return m.ms; }
static struct brcm_control_io control_io={.read32=read32,.write32=write32,.packet=packet,.delay_us=delay};
static enum block_result iovar(void *ctx,bool set,const char *name,const void *input,size_t n,void *out,size_t capacity,size_t *actual,uint32_t *status)
{ (void)ctx;return brcm_control_iovar(&control_io,&control,set,name,input,n,out,capacity,actual,status); }
static enum block_result radio(void *ctx,bool up,uint32_t *status) { (void)ctx;return brcm_control_radio(&control_io,&control,up,status); }
static enum block_result poll(void *ctx) { (void)ctx;return brcm_control_poll(&control_io,&control); }
int main(void)
{
    control=(struct brcm_control_state){.ready=true,.f2_ready=true,.device_ready=true,.error=BLOCK_OK,.phase=CONTROL_READY,.tx_seq=12,.tx_max=52,.request_id=12,.event=brcm_scan_event,.event_context=&scan,.rx_limit=1024};
    struct brcm_prepare_state prep={.ready=true,.clm_complete=true,.country="IN",.ccode="IN",.country_revision=0};
    struct brcm_scan_io io={NULL,iovar,radio,poll,delay,now};
    assert(brcm_scan_run(&io,&prep,&scan)==BLOCK_OK && scan.ready && scan.count==1 && scan.ap[0].channel==11 && scan.ap[0].rssi==-55 && scan.interface_down && scan.mask_restored && !m.up && !m.mask[8]);
    assert(control.event_frames==2 && m.max_rx==512 && control.frame_length<=4096 && m.tx==10);
    puts("PASS: real SDPCM/CDC exchange and scan engine together: scan event before SET acknowledgement, completion afterward, event larger than 2 KiB in <=512-byte reads, UP/DOWN and mask restore");return 0;
}
