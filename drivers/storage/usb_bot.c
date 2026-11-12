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
static enum block_result command(struct usb_bot *b,const unsigned char *cdb,unsigned cdb_length,
                                 unsigned char *data,unsigned length,unsigned *received,bool *check)
{
    if (!cdb || cdb_length<6 || cdb_length>16 || length>512 || (length && !data)) { return BLOCK_ARGUMENT; }
    if (cdb[0]!=0x00 && cdb[0]!=0x03 && cdb[0]!=0x12 && cdb[0]!=0x25 && cdb[0]!=0x28 && cdb[0]!=0x2a && cdb[0]!=0x35) { return BLOCK_UNSUPPORTED; }
    if ((cdb[0]==0x2a && (cdb_length!=10 || length!=512)) || (cdb[0]==0x35 && (cdb_length!=10 || length))) { return BLOCK_ARGUMENT; }
    bool input=cdb[0]!=0x2a;
    unsigned char cbw[31]={0},csw[13]={0};
    if (!++b->tag) { ++b->tag; }
    le32(cbw,0x43425355); le32(cbw+4,b->tag); le32(cbw+8,length);
    cbw[12]=length && input ? 0x80 : 0; cbw[14]=(unsigned char)cdb_length;
    for (unsigned i=0;i<cdb_length;++i) { cbw[15+i]=cdb[i]; }
    unsigned actual=0,n=0; *check=false; if (received) { *received=0; }
    enum block_result r=transfer(b,false,cbw,sizeof(cbw),&actual);
    if (r!=BLOCK_OK || actual!=sizeof(cbw)) { return r==BLOCK_OK ? BLOCK_IO : r; }
    if (length) {
        r=transfer(b,input,data,length,&n); if (r!=BLOCK_OK || n>length) { return r==BLOCK_OK ? BLOCK_IO : r; }
    }
    r=transfer(b,true,csw,sizeof(csw),&actual);
    if (r!=BLOCK_OK || actual!=sizeof(csw)) { return r==BLOCK_OK ? BLOCK_IO : r; }
    if (getle(csw)!=0x53425355 || getle(csw+4)!=b->tag || getle(csw+8)>length || csw[12]>2) { return BLOCK_IO; }
    if (getle(csw+8)!=length-n || csw[12]==2) { return BLOCK_IO; }
    if (received) { *received=n; }
    *check=csw[12]==1; return BLOCK_OK;
}
static enum block_result sense(struct usb_bot *b)
{
    const unsigned char cdb[6]={3,0,0,0,18,0}; unsigned char data[18]={0}; unsigned n; bool check;
    enum block_result r=command(b,cdb,6,data,18,&n,&check);
    if (r!=BLOCK_OK || check || n<14 || ((data[0]&0x7f)!=0x70 && (data[0]&0x7f)!=0x71)) { return r==BLOCK_OK ? BLOCK_IO : r; }
    b->sense_key=data[2]&15; b->asc=data[12]; b->ascq=data[13]; return BLOCK_OK;
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
    const unsigned char cdb[10]={0x35};unsigned n;bool check;
    enum block_result r=command(b,cdb,10,NULL,0,&n,&check);
    if (r==BLOCK_OK && check) { (void)sense(b); }
    if (r==BLOCK_OK && check) { r=BLOCK_IO; }
    b->error=r;if (r!=BLOCK_OK) { b->ready=false; }return r;
}
