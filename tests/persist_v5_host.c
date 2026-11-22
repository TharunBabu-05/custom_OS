#include <stdio.h>
#include <string.h>
#include "host_assert.h"
#include "persist.h"
#include "sha256.h"

enum { START=4096, DISK_BYTES=2048*512, META_BYTES=4096 };
static unsigned char mbr[512],disk[DISK_BYTES],cache[DISK_BYTES],baseline[DISK_BYTES];
static unsigned char archive[DATA_V5_ARCHIVE_BYTES],fixture[32768];
static struct persist_volume volume,reopened;
static unsigned event,fault,writes;
static bool armed,torn,flush_commits;

static void put(unsigned char *p,uint32_t n)
{ for (unsigned i=0;i<4;++i) { p[i]=(unsigned char)(n>>(8*i)); } }
static unsigned get(const unsigned char *p)
{ return p[0]|(unsigned)p[1]<<8|(unsigned)p[2]<<16|(unsigned)p[3]<<24; }
static void sha(const void *p,size_t n,unsigned char out[32])
{ struct sha256 s;sha256_init(&s);sha256_update(&s,p,n);sha256_final(&s,out); }
static bool fail(void) { return armed && ++event==fault; }
static enum block_result read(void *context,uint64_t lba,unsigned char *out)
{
    (void)context;
    assert(lba==0 || (lba>=START && lba<START+2048));
    if (fail()) { return BLOCK_IO; }
    memcpy(out,lba ? cache+(lba-START)*512 : mbr,512);return BLOCK_OK;
}
static enum block_result write(void *context,uint64_t lba,const unsigned char *sector)
{
    (void)context;
    assert(lba>START && lba<START+2048);++writes;
    size_t at=(size_t)(lba-START)*512;
    if (fail()) {
        if (torn) { memcpy(disk+at,sector,256);memcpy(cache+at,sector,256); }
        return BLOCK_IO;
    }
    memcpy(cache+at,sector,512);return BLOCK_OK;
}
static enum block_result flush(void *context)
{
    (void)context;
    bool bad=fail();
    if (!bad || flush_commits) { memcpy(disk,cache,sizeof(disk)); }
    return bad ? BLOCK_IO : BLOCK_OK;
}
static const struct block_device device={.name="v5-memory",.sectors=START+2048,.capacity_known=true,.read=read,.write=write,.flush=flush};
static void identity(unsigned char *header,unsigned bank)
{
    put(header+12,bank);sha(header+512,META_BYTES,header+32);sha(header,64,header+64);
}
static void blank(void)
{
    memset(mbr,0,sizeof(mbr));memset(disk,0,sizeof(disk));
    mbr[450]=0xda;put(mbr+454,START);put(mbr+458,2048);mbr[510]=0x55;mbr[511]=0xaa;
    memcpy(disk,"OUROSDAT",8);put(disk+8,5);put(disk+12,512);put(disk+16,2048);
    put(disk+20,9);put(disk+24,32);put(disk+28,32768);sha(disk,64,disk+64);
    unsigned char *header=disk+512;
    memcpy(header,"OUROSEX5",8);put(header+8,5);put(header+16,1);put(header+24,4096);identity(header,0);
    memcpy(cache,disk,sizeof(cache));armed=torn=flush_commits=false;writes=0;
    assert(persist_mount(&volume,&device)==FS_OK && volume.version==5 && volume.generation==1);
}
static void value(const struct persist_volume *v,const char *path,const void *expected,size_t wanted)
{
    unsigned char out[32768];size_t count=0;
    assert(persist_read(v,path,0,out,sizeof(out),&count)==FS_OK && count==wanted && !memcmp(out,expected,count));
}
static uint32_t used_header(const struct persist_volume *v) { return (1+v->bank*9)*512; }
static void crashes(void)
{
    blank();assert(persist_write(&volume,"/keep","keeper",6,false)==FS_OK);
    assert(persist_write(&volume,"/change","before",6,false)==FS_OK);
    memcpy(baseline,disk,sizeof(disk));
    event=0;fault=0;armed=true;writes=0;
    assert(persist_write(&volume,"/change","after",5,false)==FS_OK && writes==11);
    unsigned operations=event;assert(operations>20);
    for (unsigned mode=0;mode<3;++mode) {
        for (unsigned boundary=1;boundary<=operations;++boundary) {
            armed=false;memcpy(disk,baseline,sizeof(disk));memcpy(cache,disk,sizeof(cache));
            assert(persist_mount(&volume,&device)==FS_OK && volume.generation==3);
            fault=boundary;event=0;armed=true;torn=mode==1;flush_commits=mode==2;
            assert(persist_write(&volume,"/change","after",5,false)==FS_IO && volume.failed && volume.generation==3);
            armed=false;memcpy(cache,disk,sizeof(cache));
            assert(persist_mount(&reopened,&device)==FS_OK);
            bool newer=reopened.generation==4;
            assert(newer || reopened.generation==3);
            value(&reopened,"/change",newer ? "after" : "before",newer ? 5 : 6);
            value(&reopened,"/keep","keeper",6);
        }
    }
    puts("PASS: every v5 read/write/flush cut point, torn writes and ambiguous flush preserve old or new committed data; one small change writes 11 sectors");
}
static void metadata(void)
{
    blank();
    assert(persist_write(&volume,"/a","old",3,false)==FS_OK && persist_write(&volume,"/b","unchanged",9,false)==FS_OK);
    assert(persist_write(&volume,"/a","new",3,false)==FS_OK);
    unsigned offset=used_header(&volume);
    memcpy(baseline,disk,sizeof(disk));
    for (unsigned attack=0;attack<6;++attack) {
        memcpy(disk,baseline,sizeof(disk));
        unsigned char *h=disk+offset,*extent=h+512+2048;
        if (attack==0) { disk[get(extent)*512]^=1; }
        if (attack==1) { put(extent,18); }
        if (attack==2) { put(extent,get(extent+64)); }
        if (attack==3) { put(extent,2048); }
        if (attack==4) { extent[36]=1; }
        if (attack==5) { put(h+512+36,2); }
        identity(h,volume.bank);
        memcpy(cache,disk,sizeof(cache));
        struct persist_report report;
        assert(persist_check(&volume,&report)==FS_OK && report.banks[volume.bank].state==PERSIST_BANK_BAD_PAYLOAD);
        assert(persist_mount(&reopened,&device)==FS_OK && reopened.generation==3 && reopened.valid_banks==1);
        value(&reopened,"/a","old",3);value(&reopened,"/b","unchanged",9);
    }
    memcpy(disk,baseline,sizeof(disk));put(disk+offset+16,3);identity(disk+offset,volume.bank);
    memcpy(cache,disk,sizeof(cache));assert(persist_mount(&reopened,&device)==FS_CORRUPT);
    puts("PASS: rejects corrupt data, metadata extents outside the pool, overlap, reserved bytes, invalid node kinds and equal generations");
}
static void capacity(void)
{
    blank();
    for (unsigned i=0;i<sizeof(fixture);++i) { fixture[i]=(unsigned char)(i*13+5); }
    for (unsigned i=0;i<31;++i) {
        char path[8];snprintf(path,sizeof(path),"/f%u",i);
        assert(persist_write(&volume,path,fixture,sizeof(fixture),false)==FS_OK);
    }
    assert(writes==31*(64+10));
    unsigned before=writes;
    assert(persist_write(&volume,"/large",fixture,sizeof(fixture),false)==FS_NO_SPACE && writes==before && !volume.failed);
    assert(persist_write(&volume,"/tiny","tin",3,false)==FS_OK);
    assert(persist_mkdir(&volume,"/full")==FS_NO_NODES);
    assert(persist_remove(&volume,"/f0",false)==FS_OK);
    assert(persist_publish(&volume,"/f1","/tiny")==FS_OK);
    assert(persist_mount(&reopened,&device)==FS_OK);
    value(&reopened,"/tiny",fixture,sizeof(fixture));
    bool directory=false;assert(persist_stat(&reopened,"/f1",&directory)==FS_NOT_FOUND);
    unsigned char cross[20];size_t n=0;
    assert(persist_read(&reopened,"/f2",503,cross,sizeof(cross),&n)==FS_OK && n==sizeof(cross) && !memcmp(cross,fixture+503,n));
    assert(persist_read(&reopened,"/f2",32767,cross,sizeof(cross),&n)==FS_OK && n==1 && *cross==fixture[32767]);
    puts("PASS: 32 nodes, 992 KiB file contents, pool exhaustion before writes, extent reuse, 32 KiB readback and metadata-only publish");
}
struct input { size_t length,emitted;bool mutate; };
static bool archive_read(uint32_t offset,void *p,size_t n,void *context)
{
    struct input *io=context;if (offset>io->length || n>io->length-offset) { return false; }
    memcpy(p,archive+offset,n);
    if (io->mutate && volume.valid_banks==1 && writes && n>512 && offset>512) { ((unsigned char *)p)[0]^=1; }
    return true;
}
static bool archive_emit(const void *p,size_t n,void *context)
{
    struct input *io=context;if (n>sizeof(archive)-io->emitted) { return false; }
    memcpy(archive+io->emitted,p,n);io->emitted+=n;return true;
}
static void restore_crashes(struct input *in)
{
    blank();memcpy(baseline,disk,sizeof(disk));
    event=0;fault=0;armed=true;
    assert(persist_restore_extents(&volume,in->length,archive_read,in)==FS_OK);
    unsigned operations=event;
    for (unsigned mode=0;mode<3;++mode) {
        for (unsigned boundary=1;boundary<=operations;++boundary) {
            armed=false;memcpy(disk,baseline,sizeof(disk));memcpy(cache,disk,sizeof(cache));
            assert(persist_mount(&volume,&device)==FS_OK && volume.generation==1);
            fault=boundary;event=0;armed=true;torn=mode==1;flush_commits=mode==2;
            assert(persist_restore_extents(&volume,in->length,archive_read,in)==FS_IO && volume.generation==1);
            armed=false;memcpy(cache,disk,sizeof(cache));
            assert(persist_mount(&reopened,&device)==FS_OK);
            if (reopened.generation==2) {
                value(&reopened,"/nested/file",fixture,16384);
            } else {
                bool directory;
                assert(reopened.generation==1 && persist_stat(&reopened,"/nested",&directory)==FS_NOT_FOUND);
            }
        }
    }
    printf("PASS: %u v4 import interruption outcomes preserve an empty volume or the complete import\n",3*operations);
}
static void backups(void)
{
    blank();memset(archive,0,sizeof(archive));
    size_t old_size=512+DATA_V4_PAYLOAD_BYTES;
    memcpy(archive,"OUROSBK4",8);put(archive+8,4);put(archive+16,14);put(archive+24,DATA_V4_PAYLOAD_BYTES);
    memcpy(archive+512,"nested",6);put(archive+512+36,2);
    memcpy(archive+576,"file",4);put(archive+576+32,16384);put(archive+576+36,1);put(archive+576+40,1);
    memcpy(archive+512+1024+16384,fixture,16384);
    sha(archive+512,DATA_V4_PAYLOAD_BYTES,archive+32);sha(archive,64,archive+64);
    struct input in={.length=old_size};
    archive[old_size-1]^=1;
    assert(persist_restore_extents(&volume,old_size,archive_read,&in)==FS_CORRUPT && writes==0 && !volume.failed);
    archive[old_size-1]^=1;
    restore_crashes(&in);blank();
    assert(persist_restore_extents(&volume,old_size,archive_read,&in)==FS_OK && volume.generation==2);
    value(&volume,"/nested/file",fixture,16384);
    assert(persist_restore_extents(&volume,old_size,archive_read,&in)==FS_BUSY);
    assert(persist_write(&volume,"/nested/file",fixture,sizeof(fixture),false)==FS_OK);
    unsigned before=writes;
    assert(persist_export_previous(&volume,archive_emit,&in)==FS_OK && writes==before && in.emitted==sizeof(archive));
    assert(!memcmp(archive,"OUROSBK5",8) && get(archive+16)==2);
    blank();in.length=in.emitted;in.mutate=true;
    assert(persist_restore_extents(&volume,in.length,archive_read,&in)==FS_IO && volume.failed);
    memcpy(cache,disk,sizeof(cache));assert(persist_mount(&reopened,&device)==FS_OK && reopened.generation==1);
    blank();in.mutate=false;
    assert(persist_restore_extents(&volume,in.length,archive_read,&in)==FS_OK);
    value(&volume,"/nested/file",fixture,16384);
    puts("PASS: explicit v4 archive migration, no writes for malformed archive, v5 previous export/reimport, and changed archive refusal before publish");
}
int main(void) { crashes();metadata();capacity();backups();return 0; }
