#include "host_assert.h"
#include <stdio.h>
#include <string.h>
#include "usb_bot.h"
struct mock {
    unsigned stage,commands,reads,mode,tur,writes,flushes;
    uint32_t tag,length,lba;
    unsigned char opcode,cdb[16],saved[512];
};
static uint32_t le(const unsigned char *p)
{ return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static void putle(unsigned char *p,uint32_t x)
{ for (unsigned i=0;i<4;++i) { p[i]=(unsigned char)(x>>(i*8)); } }
static void putbe(unsigned char *p,uint32_t x)
{ for (unsigned i=0;i<4;++i) { p[i]=(unsigned char)(x>>(24-i*8)); } }
static enum block_result transport(void *context,bool input,void *buffer,unsigned length,unsigned *actual)
{
    struct mock *m=context; unsigned char *p=buffer; *actual=length;
    if (!m->stage) {
        assert(!input && length==31 && le(p)==0x43425355);
        assert(!p[13] && !(p[14]&0xe0));
        m->tag=le(p+4); m->length=le(p+8); m->opcode=p[15];
        assert(m->tag==m->commands+1); ++m->commands;
        assert(m->opcode==0 || m->opcode==3 || m->opcode==0x12 || m->opcode==0x25 || m->opcode==0x28 || m->opcode==0x2a || m->opcode==0x35);
        assert(p[12]==(m->length && m->opcode!=0x2a ? 0x80 : 0));
        assert(p[14]==((m->opcode==0x25 || m->opcode==0x28 || m->opcode==0x2a || m->opcode==0x35) ? 10 : 6));
        if (m->opcode==0x35) { ++m->flushes;for (unsigned i=1;i<10;++i) { assert(!p[15+i]); } }
        memcpy(m->cdb,p+15,p[14]);
        if (m->opcode==0) { ++m->tur; }
        m->stage=m->length ? 1 : 2;
        if (m->mode==1 && m->opcode==0x28) { return BLOCK_TIMEOUT; }
        if (m->mode==2 && m->opcode==0x28) { *actual=30; }
        return BLOCK_OK;
    }
    if (m->stage==1 && m->opcode==0x2a) {
        assert(!input && length==512 && m->cdb[7]==0 && m->cdb[8]==1);++m->writes;memcpy(m->saved,p,512);m->stage=2;
        if (m->mode==20) { return BLOCK_TIMEOUT; }if (m->mode==21) { *actual=511; }return BLOCK_OK;
    }
    assert(input);
    if (m->stage==1) {
        assert(length==m->length); memset(p,0,length); m->stage=2;
        switch (m->opcode) {
        case 0x12:
            assert(length==36 && m->cdb[4]==36);
            memcpy(p+8,"VENDOR!\033",8); memcpy(p+16,"Test USB storage",16); break;
        case 3:
            assert(length==18); p[0]=0x70; p[2]=6; p[7]=10; p[12]=0x29; break;
        case 0x25:
            assert(length==8); putbe(p,m->mode==10 ? UINT32_MAX : 1023);
            putbe(p+4,m->mode==11 ? 4096 : 512); break;
        case 0x28:
            assert(length==512 && m->cdb[7]==0 && m->cdb[8]==1);
            m->lba=(uint32_t)m->cdb[2]<<24|(uint32_t)m->cdb[3]<<16|(uint32_t)m->cdb[4]<<8|m->cdb[5];
            assert(m->lba<1024); ++m->reads;
            for (unsigned i=0;i<512;++i) { p[i]=(unsigned char)(m->lba+i); }
            if (m->writes) { memcpy(p,m->saved,512); }
            if (m->mode==3 || m->mode==4) { *actual=256; }
            break;
        default: assert(0);
        }
        return BLOCK_OK;
    }
    assert(m->stage==2 && length==13); m->stage=0;
    memset(p,0,length); putle(p,0x53425355); putle(p+4,m->tag);
    if (m->mode==12 && m->opcode==0 && m->tur==1) { p[12]=1; }
    if (m->opcode==0x28) {
        if (m->mode==3) { putle(p+8,256); }
        if (m->mode==5) { p[0]^=1; }
        if (m->mode==6) { putle(p+4,m->tag+1); }
        if (m->mode==7) { putle(p+8,513); }
        if (m->mode==8) { p[12]=2; }
        if (m->mode==9) { p[12]=1; }
        if (m->mode==13) { *actual=12; }
        if (m->mode==14) { p[12]=3; }
    }
    if (m->opcode==0x2a) {
        if (m->mode==22) { p[0]^=1; }if (m->mode==23) { putle(p+4,m->tag+1); }
        if (m->mode==24) { putle(p+8,1); }if (m->mode==25) { p[12]=1; }if (m->mode==26) { p[12]=2; }
    }
    if (m->opcode==0x35 && m->mode==27) { p[12]=1; }
    return BLOCK_OK;
}
int main(void)
{
    struct usb_bot bot; struct block_device device; unsigned char sector[512];
    assert(!usb_bot_attach(NULL,NULL,NULL));
    for (unsigned mode=0;mode<=14;++mode) {
        struct mock m={.mode=mode}; struct usb_bot_io io={transport,&m};
        bool ok=usb_bot_attach(&bot,&io,&device);
        if (mode==10 || mode==11) { assert(!ok && !bot.ready && bot.error==BLOCK_UNSUPPORTED); continue; }
        assert(ok && bot.ready && device.capacity_known && device.sectors==1024);
        assert(!strcmp(bot.vendor,"VENDOR!.") && !strcmp(bot.product,"Test USB storage"));
        if (mode==12) { assert(m.tur==2 && bot.sense_key==6 && bot.asc==0x29); }
        unsigned before=m.commands;
        assert(block_read(&device,1024,sector)==BLOCK_RANGE);
        assert(block_read(&device,0,NULL)==BLOCK_ARGUMENT && m.commands==before);
        enum block_result r=block_read(&device,1023,sector);
        if (mode==0 || mode==12) {
            assert(r==BLOCK_OK && m.lba==1023 && bot.ready);
            for (unsigned i=0;i<512;++i) { assert(sector[i]==(unsigned char)(1023+i)); }
            for (unsigned i=0;i<600;++i) { assert(block_read(&device,i,sector)==BLOCK_OK); }
            assert(m.reads==601);
        } else {
            assert(r==(mode==1 ? BLOCK_TIMEOUT : BLOCK_IO) && !bot.ready);
            before=m.commands; assert(block_read(&device,0,sector)==BLOCK_NOT_READY && m.commands==before);
        }
    }
    puts("PASS: BOT read framing/opcode allowlist, SCSI identity/capacity/sense retry, read bounds, 601 reads, malformed/short CSW, tag/residue/status failures and no reuse after I/O errors");
    for (unsigned mode=19;mode<=27;++mode) {
        struct mock m={.mode=mode};struct usb_bot_io io={transport,&m};assert(usb_bot_attach(&bot,&io,&device));
        unsigned before=m.commands;assert(block_write(&device,1024,sector)==BLOCK_RANGE && block_write(&device,0,NULL)==BLOCK_ARGUMENT && m.commands==before);
        for (unsigned i=0;i<512;++i) { sector[i]=(unsigned char)(i^0xa5); }
        enum block_result r=block_write(&device,7,sector);
        if (mode==19 || mode==27) {
            assert(r==BLOCK_OK && m.writes==1 && !memcmp(m.saved,sector,512));
            unsigned char readback[512];assert(block_read(&device,7,readback)==BLOCK_OK && !memcmp(readback,sector,512));
            r=block_flush(&device);assert(r==(mode==27 ? BLOCK_IO : BLOCK_OK) && m.flushes==1);
        } else { assert(r==(mode==20 ? BLOCK_TIMEOUT : BLOCK_IO)); }
        if (mode!=19) { before=m.commands;assert(!bot.ready && block_write(&device,7,sector)==BLOCK_NOT_READY && block_flush(&device)==BLOCK_NOT_READY && m.commands==before); }
    }
    struct block_device read_only=device;read_only.write=NULL;unsigned before=bot.tag;
    assert(block_write(&read_only,0,sector)==BLOCK_UNSUPPORTED && block_flush(&read_only)==BLOCK_UNSUPPORTED && bot.tag==before);
    puts("PASS: exact WRITE10 bulk-OUT direction/data, READ10 readback, synchronous CACHE10 with IMMED clear, bounds/readonly, short/tag/residue/check/phase errors and failed flush latch");
    return 0;
}
