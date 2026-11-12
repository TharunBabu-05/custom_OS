#include "host_assert.h"
#include <string.h>
#include "usb_bot.h"
struct model {
    unsigned mode,stage,commands,query,clears,csw_stalls,data_stalls,write_cbws,write_data;
    bool requery;
    uint32_t tag,length;unsigned char op,cdb[16];bool halted;
};
static uint32_t le(const unsigned char *p) { return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static void put(unsigned char *p,uint32_t n) { for (unsigned i=0;i<4;++i) { p[i]=(unsigned char)(n>>(8*i)); } }
static bool mode_command(const struct model *m) { return m->op==0x1a || m->op==0x5a; }
static unsigned query_form(const struct model *m)
{ return m->op==0x5a ? (m->cdb[2]==8 ? 4 : 5) : m->cdb[1] ? 1 : m->cdb[2]==8 ? 2 : 3; }
static bool rejected(const struct model *m)
{
    unsigned form=query_form(m);
    return ((m->mode==1 || m->mode==24) && form==1) || ((m->mode==2 || m->mode>=16) && m->mode!=24 && form<4) || (m->mode==3 && form<5);
}
static enum block_result clear(void *context,bool input)
{
    struct model *m=context;assert(m->halted);assert(input || m->op==0x2a);++m->clears;
    if (m->mode==7) { return BLOCK_TIMEOUT; }m->halted=false;return BLOCK_OK;
}
static enum block_result exchange(void *context,bool input,void *buffer,unsigned length,unsigned *actual)
{
    struct model *m=context;unsigned char *p=buffer;assert(!m->halted);*actual=length;
    if (!m->stage) {
        assert(!input && length==31 && le(p)==0x43425355);m->tag=le(p+4);assert(m->tag==++m->commands);
        m->op=p[15];memcpy(m->cdb,p+15,p[14]);m->length=le(p+8);m->stage=m->length ? 1 : 2;
        if (m->op==0x2a) { ++m->write_cbws; }
        if (mode_command(m)) {
            unsigned form=query_form(m);if (!m->requery) { assert(form==++m->query); }
            else { assert(form==m->query); }
            assert(p[12]==0x80 && (form<=2 ? length==31 && m->length==64 : m->length==192));
            if (m->mode==9) { m->halted=true;*actual=0;return BLOCK_STALL; }
            if (m->mode==12) { return BLOCK_TIMEOUT; }
        }return BLOCK_OK;
    }
    if (m->stage==1) {
        m->stage=2;assert(length==m->length);
        if (m->op==0x2a) {
            assert(!input && length==512);++m->write_data;
            if (m->mode==10) { m->halted=true;*actual=0;++m->data_stalls;return BLOCK_STALL; }
            return BLOCK_OK;
        }
        assert(input);memset(p,0,length);
        if (m->op==0x12) { memcpy(p+8,"SanDisk ",8);memcpy(p+16,"Cruzer Blade    ",16); }
        else if (m->op==0x25) { p[2]=3;p[3]=0xff;p[6]=2; }
        else if (m->op==3) { p[0]=0x70;p[2]=5;p[7]=10;p[12]=0x20; }
        else if (mode_command(m)) {
            if (rejected(m)) {
                *actual=0;
                if (m->mode==1 && m->query==1) { m->halted=true;++m->data_stalls;return BLOCK_STALL; }
                return BLOCK_OK;
            }
            bool ten=m->op==0x5a;unsigned header=ten ? 8 : 4,at=header;
            if (m->mode==22 || m->mode==24) { p[ten ? 7 : 3]=8;at+=8; }
            if (m->mode==3) { p[at]=0x0a;p[at+1]=2;at+=4; }
            p[at]=8;p[at+1]=18;p[ten ? 1 : 0]=(unsigned char)(at+20-(ten ? 2 : 1));
            if (m->mode==4) { p[ten ? 1 : 0]=(unsigned char)(header-(ten ? 2 : 1)); }
            if (m->mode==5) { p[at+2]=4; }
            if (m->mode==15) { p[at+1]=255; }
            if (m->mode==16) { p[at+2]=4; }
            if (m->mode==17) { p[ten ? 3 : 2]=0x80; }
            if (m->mode==18) { p[0]=255; }
            if (m->mode==19) { p[at]|=0x40; }
            if (m->mode==20) { p[ten ? 7 : 3]=1; }
            if (m->mode==21) { p[at+1]=255; }
            if (m->mode==23) { p[ten ? 1 : 0]=(unsigned char)(header-(ten ? 2 : 1)); }
        }else { assert(0); }return BLOCK_OK;
    }
    assert(input && length==13 && m->stage==2);
    if (mode_command(m) && !rejected(m) && m->mode!=4 && m->mode!=5 && m->mode!=15 &&
        (m->mode==6 || m->csw_stalls==0)) {
        m->halted=true;*actual=0;++m->csw_stalls;return BLOCK_STALL;
    }
    m->stage=0;memset(p,0,length);put(p,0x53425355);put(p+4,m->tag+(m->mode==11 && mode_command(m) ? 1 : 0));
    if (m->op==0x35 || (mode_command(m) && rejected(m)) || (m->op==0x2a && m->mode==10)) { p[12]=1; }
    if ((mode_command(m) && rejected(m)) || (m->op==0x2a && m->mode==10)) { put(p+8,m->length); }
    if (m->mode==13 && mode_command(m)) { put(p+8,40); }
    return BLOCK_OK;
}
int main(void)
{
    for (unsigned mode=0;mode<=24;++mode) {
        struct model m={.mode=mode};struct usb_bot b;struct block_device d;
        struct usb_bot_io io={.transfer=exchange,.context=&m,.clear_halt=mode==8 ? NULL : clear};
        assert(usb_bot_attach(&b,&io,&d));
        enum block_result r=block_flush(&d);
        bool ok=mode<=3 || mode==10 || mode==13 || mode==14 || mode==22 || mode==24;
        assert((r==BLOCK_OK)==ok && b.ready==ok && !m.write_cbws);
        if (ok) {
            assert(b.write_through && !m.halted);
            if (mode==1) { assert(m.query==2 && m.data_stalls==1 && m.csw_stalls==1 && m.clears==2); }
            if (mode==2) { assert(m.query==4); }
            if (mode==3) { assert(m.query==5); }
            m.requery=true;assert(block_flush(&d)==BLOCK_OK && b.write_through && !m.halted);
            if (mode==10) {
                unsigned char sector[512]={0};assert(block_write(&d,7,sector)==BLOCK_IO && !b.ready);
                assert(m.write_cbws==1 && m.write_data==1 && m.data_stalls==1 && !m.halted);
            }
        } else {
            unsigned before=m.commands;unsigned char sector[512];assert(block_write(&d,0,sector)==BLOCK_NOT_READY && m.commands==before);
            if (mode==6) { assert(m.csw_stalls==2 && m.clears==1); }
            if (mode==7) { assert(r==BLOCK_TIMEOUT && m.clears==1); }
            if (mode==8 || mode==9) { assert(!m.clears); }
            if (mode==4) { assert(m.query==5 && !m.clears); }
        }
    }
    puts("PASS: BOT data/status STALL recovery, exactly one CSW retry, failed/missing clear, double stall, CBW stall, malformed status/timeout refusal, no WRITE replay, padded data, MODE6 DBD/all-pages and MODE10 fallbacks; no inferred WCE");
    return 0;
}
