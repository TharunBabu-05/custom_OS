/* Bounded SCSI BOT, LUN zero: single-sector read/write and synchronous flush.
 * Only the separately validated data volume calls the write callbacks. */
#include "usb_bot.h"
static void le32(unsigned char *p,uint32_t value)
{ for (unsigned i=0;i<4;++i) { p[i]=(unsigned char)(value>>(i*8)); } }
static uint32_t getle(const unsigned char *p)
{ return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static uint32_t getbe(const unsigned char *p)
{ return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3]; }
static void be32(unsigned char *p,uint32_t value)
{ for (unsigned i=0;i<4;++i) { p[i]=(unsigned char)(value>>(24-i*8)); } }
static enum block_result transfer(struct usb_bot *b,bool input,void *data,unsigned length,unsigned *actual)
{ return b->io.transfer(b->io.context,input,data,length,actual); }
static enum block_result unstall(struct usb_bot *b,bool input)
{
    if (!b->io.clear_halt) { return BLOCK_IO; }
    enum block_result r=b->io.clear_halt(b->io.context,input);
    if (r==BLOCK_OK) { ++b->stalls_recovered; }return r;
}
static enum block_result command(struct usb_bot *b,const unsigned char *cdb,unsigned cdb_length,
                                 unsigned char *data,unsigned length,unsigned *received,bool *check)
{
    if (!cdb || cdb_length<6 || cdb_length>16 || length>512 || (length && !data)) { return BLOCK_ARGUMENT; }
    if (cdb[0]!=0x00 && cdb[0]!=0x03 && cdb[0]!=0x12 && cdb[0]!=0x1a && cdb[0]!=0x5a && cdb[0]!=0x25 && cdb[0]!=0x28 && cdb[0]!=0x2a && cdb[0]!=0x35) { return BLOCK_UNSUPPORTED; }
    if (cdb[0]==0x1a && (cdb_length!=6 || (length!=64 && length!=192) || (cdb[1]!=0 && cdb[1]!=8) || (cdb[2]!=8 && cdb[2]!=0x3f) || cdb[3] || cdb[4]!=length || cdb[5])) { return BLOCK_ARGUMENT; }
    if (cdb[0]==0x5a && (cdb_length!=10 || length!=192 || cdb[1] || (cdb[2]!=8 && cdb[2]!=0x3f) || cdb[3] || cdb[4] || cdb[5] || cdb[6] || cdb[7] || cdb[8]!=length || cdb[9])) { return BLOCK_ARGUMENT; }
    if ((cdb[0]==0x2a && (cdb_length!=10 || length!=512)) || (cdb[0]==0x35 && (cdb_length!=10 || length))) { return BLOCK_ARGUMENT; }
    bool input=cdb[0]!=0x2a;
    b->trace=(struct usb_bot_trace){.opcode=cdb[0],.stage=1,.status=0xff,.expected=31};
    if (cdb[0]==0x28 || cdb[0]==0x2a) { b->trace.lba=getbe(cdb+2); }
    b->sense_valid=false;b->sense_current=false;
    unsigned char cbw[31]={0},csw[13]={0};
    if (!++b->tag) { ++b->tag; }
    le32(cbw,0x43425355); le32(cbw+4,b->tag); le32(cbw+8,length);
    cbw[12]=length && input ? 0x80 : 0; cbw[14]=(unsigned char)cdb_length;
    for (unsigned i=0;i<cdb_length;++i) { cbw[15+i]=cdb[i]; }
    unsigned actual=0,n=0; *check=false; if (received) { *received=0; }
    enum block_result r=transfer(b,false,cbw,sizeof(cbw),&actual);
    b->trace.actual=actual;
    if (r!=BLOCK_OK || actual!=sizeof(cbw)) { return r==BLOCK_OK ? BLOCK_IO : r; }
    if (length) {
        b->trace.stage=2;b->trace.expected=length;b->trace.actual=0;
        r=transfer(b,input,data,length,&n);b->trace.actual=n;
        if (r==BLOCK_STALL) { r=unstall(b,input); }
        if (r!=BLOCK_OK || n>length) { return r==BLOCK_OK ? BLOCK_IO : r; }
    }
    b->trace.stage=3;b->trace.expected=13;b->trace.actual=0;actual=0;
    r=transfer(b,true,csw,sizeof(csw),&actual);
    if (r==BLOCK_STALL) {
        r=unstall(b,true);
        if (r==BLOCK_OK) { actual=0;r=transfer(b,true,csw,sizeof(csw),&actual); }
    }
    b->trace.actual=actual;
    if (r!=BLOCK_OK || actual!=sizeof(csw)) { return r==BLOCK_OK ? BLOCK_IO : r; }
    b->trace.status=csw[12];b->trace.residue=getle(csw+8);
    if (getle(csw)!=0x53425355 || getle(csw+4)!=b->tag || getle(csw+8)>length || csw[12]>2) { return BLOCK_IO; }
    /* A device may pad data; residue identifies relevant bytes. It must not
     * claim more relevant data than actually transferred. Callers still
     * require all 512 bytes for sector reads/writes. */
    unsigned relevant=length-getle(csw+8);
    if (relevant>n || csw[12]==2) { return BLOCK_IO; }
    if (received) { *received=relevant; }
    *check=csw[12]==1;b->trace.stage=4; return BLOCK_OK;
}
static enum block_result sense(struct usb_bot *b)
{
    const unsigned char cdb[6]={3,0,0,0,18,0}; unsigned char data[18]={0}; unsigned n; bool check;
    struct usb_bot_trace saved=b->trace;
    enum block_result r=command(b,cdb,6,data,18,&n,&check);b->trace=saved;
    if (r!=BLOCK_OK || check || n<8) { return r==BLOCK_OK ? BLOCK_IO : r; }
    unsigned format=data[0]&0x7f;
    if (format==0x70 || format==0x71) {
        if (n<14 || data[7]<6) { return BLOCK_IO; }
        b->sense_key=data[2]&15;b->asc=data[12];b->ascq=data[13];
    } else if (format==0x72 || format==0x73) {
        b->sense_key=data[1]&15;b->asc=data[2];b->ascq=data[3];
    } else { return BLOCK_IO; }
    b->sense_valid=true;b->sense_current=format==0x70 || format==0x72;return BLOCK_OK;
}
/* A rejected flush is NOT success. Only a complete CURRENT caching page
 * explicitly reporting WCE=0 permits synchronous WRITE completion instead.
 * Never infer disabled caching from a missing page, VID/PID or an error. */
static bool illegal_request(const struct usb_bot *b)
{ return b->sense_valid && b->sense_current && b->sense_key==5 && b->ascq==0 && (b->asc==0x20 || b->asc==0x24); }
/* 1: explicit WCE=0, 0: no caching page, -1: malformed/protected/WCE=1. */
static int cache_page(const unsigned char *p,unsigned n,bool ten)
{
    unsigned header=ten ? 8 : 4;if (n<header) { return -1; }
    unsigned total=ten ? ((unsigned)p[0]<<8|p[1])+2 : (unsigned)p[0]+1;
    unsigned blocks=ten ? (unsigned)p[6]<<8|p[7] : p[3];
    if (total>n || total<header || p[ten ? 2 : 1] || (p[ten ? 3 : 2]&0x80) ||
        (blocks%8) || header+blocks>total || (ten && (p[4] || p[5]))) { return -1; }
    bool found=false;
    for (unsigned at=header+blocks;at<total;) {
        if (total-at<2) { return -1; }
        bool sub=p[at]&0x40;unsigned bytes=2u+p[at+1];
        if (sub) { if (total-at<4) { return -1; }bytes=4u+((unsigned)p[at+2]<<8|p[at+3]); }
        if (bytes>total-at) { return -1; }
        if ((p[at]&0x3f)==8) {
            if (sub || found || bytes<20 || (p[at+2]&4)) { return -1; }found=true;
        }
        at+=bytes;
    }
    return found ? 1 : 0;
}
static enum block_result confirm_write_through(struct usb_bot *b)
{
    /* Some USB bridges reject DBD, six-byte commands or page-specific
     * requests. Try only bounded read-only CURRENT mode-page queries. */
    unsigned first=b->cache_query ? b->cache_query-1 : 0;
    for (unsigned form=first;form<5;++form) {
        unsigned char cdb[10]={0},data[192]={0};unsigned n;bool check,ten=form>=3;
        unsigned length=form<2 ? 64 : 192;
        cdb[0]=ten ? 0x5a : 0x1a;cdb[1]=form==0 ? 8 : 0;
        cdb[2]=form==2 || form==4 ? 0x3f : 8;cdb[ten ? 8 : 4]=(unsigned char)length;
        b->cache_query=form+1;
        enum block_result r=command(b,cdb,ten ? 10 : 6,data,length,&n,&check);
        if (r!=BLOCK_OK) { return r; }
        if (check) {
            r=sense(b);if (r!=BLOCK_OK) { return r; }
            if (!illegal_request(b)) { return BLOCK_IO; }continue;
        }
        int policy=cache_page(data,n,ten);
        if (policy<0) { return BLOCK_UNSUPPORTED; }
        if (policy==1) { b->write_through=true;return BLOCK_OK; }
    }
    return BLOCK_UNSUPPORTED;
}
static char clean(unsigned char c) { return c>=32 && c<=126 ? (char)c : '.'; }
bool usb_bot_attach(struct usb_bot *b,const struct usb_bot_io *io,struct block_device *device)
{
    if (!b || !io || !io->transfer || !device) { return false; }
    *b=(struct usb_bot){.io=*io,.error=BLOCK_IO};
    const unsigned char inquiry[6]={0x12,0,0,0,36,0}; unsigned char data[36]={0}; unsigned n; bool check;
    enum block_result r=command(b,inquiry,6,data,36,&n,&check);
    if (r!=BLOCK_OK || check || n<32 || (data[0]&31)!=0 || (data[0]>>5)!=0) { b->error=r==BLOCK_OK ? BLOCK_UNSUPPORTED : r; return false; }
    for (unsigned i=0;i<8;++i) { b->vendor[i]=clean(data[8+i]); }
    for (unsigned i=0;i<16;++i) { b->product[i]=clean(data[16+i]); }
    const unsigned char tur[6]={0}; bool ready=false;
    for (unsigned attempt=0;attempt<4;++attempt) {
        r=command(b,tur,6,NULL,0,&n,&check); if (r!=BLOCK_OK) { b->error=r; return false; }
        if (!check) { ready=true; break; }
        r=sense(b); if (r!=BLOCK_OK) { b->error=r; return false; }
        if (b->sense_key!=6 && b->sense_key!=2) { break; }
    }
    if (!ready) { b->error=BLOCK_NOT_READY; return false; }
    const unsigned char capacity[10]={0x25};
    r=command(b,capacity,10,data,8,&n,&check);
    if (r!=BLOCK_OK || check || n!=8) { b->error=r==BLOCK_OK ? BLOCK_IO : r; return false; }
    uint32_t last=getbe(data),size=getbe(data+4);
    if (last==UINT32_MAX || size!=512) { b->error=BLOCK_UNSUPPORTED; return false; }
    b->sectors=(uint64_t)last+1; b->ready=true; b->error=BLOCK_OK;
    *device=(struct block_device){.name="usb-bot",.sectors=b->sectors,.capacity_known=true,.read=usb_bot_read,.context=b,.write=usb_bot_write,.flush=usb_bot_flush}; return true;
}
enum block_result usb_bot_read(void *context,uint64_t lba,unsigned char *sector)
{
    struct usb_bot *b=context;
    if (!b || !b->ready) { return BLOCK_NOT_READY; }
    if (!sector) { return BLOCK_ARGUMENT; }
    if (lba>=b->sectors || lba>UINT32_MAX) { return BLOCK_RANGE; }
    unsigned char cdb[10]={0x28}; be32(cdb+2,(uint32_t)lba); cdb[8]=1;
    unsigned n; bool check;
    enum block_result r=command(b,cdb,10,sector,512,&n,&check);
    if (r==BLOCK_OK && check) { (void)sense(b); }
    if (r==BLOCK_OK && (check || n!=512)) { r=BLOCK_IO; }
    b->error=r; if (r!=BLOCK_OK) { b->ready=false; } return r;
}
enum block_result usb_bot_write(void *context,uint64_t lba,const unsigned char *sector)
{
    struct usb_bot *b=context;if (!b || !b->ready) { return BLOCK_NOT_READY; }
    if (!sector) { return BLOCK_ARGUMENT; }if (lba>=b->sectors || lba>UINT32_MAX) { return BLOCK_RANGE; }
    unsigned char cdb[10]={0x2a};be32(cdb+2,(uint32_t)lba);cdb[8]=1;unsigned n;bool check;
    enum block_result r=command(b,cdb,10,(unsigned char *)sector,512,&n,&check);
    if (r==BLOCK_OK && check) { (void)sense(b); }
    if (r==BLOCK_OK && (check || n!=512)) { r=BLOCK_IO; }
    b->error=r;if (r!=BLOCK_OK) { b->ready=false; }return r;
}
enum block_result usb_bot_flush(void *context)
{
    struct usb_bot *b=context;if (!b || !b->ready) { return BLOCK_NOT_READY; }
    /* Re-read current WCE each time; never reuse stale cache policy. */
    if (b->write_through) {
        b->write_through=false;enum block_result r=confirm_write_through(b);
        b->error=r;if (r!=BLOCK_OK) { b->ready=false; }return r;
    }
    const unsigned char cdb[10]={0x35};unsigned n;bool check;
    enum block_result r=command(b,cdb,10,NULL,0,&n,&check);
    if (r==BLOCK_OK && check) {
        enum block_result sr=sense(b);r=BLOCK_IO;
        if (sr==BLOCK_OK && illegal_request(b)) { r=confirm_write_through(b); }
    }
    b->error=r;if (r!=BLOCK_OK) { b->ready=false; }return r;
}
