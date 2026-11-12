#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "host_assert.h"
#include "persist.h"
#include "sha256.h"
enum { SECTORS=6144,START=4096 };
static unsigned char media[SECTORS*512],durable[SECTORS*512],original[SECTORS*512];
static struct persist_volume v,reopened;
static unsigned events,fail_event,writes,flushes;
static bool torn;
static void p32(unsigned char *p,uint32_t x) { for (unsigned i=0;i<4;++i) { p[i]=(unsigned char)(x>>(i*8)); } }
static void digest(const void *p,size_t n,unsigned char out[32]) { struct sha256 s;sha256_init(&s);sha256_update(&s,p,n);sha256_final(&s,out); }
static bool fail(void) { return ++events==fail_event; }
static enum block_result read_sector(void *ctx,uint64_t lba,unsigned char *p)
{ (void)ctx;assert(lba<SECTORS);if (fail()) { return BLOCK_IO; }memcpy(p,media+lba*512,512);return BLOCK_OK; }
static enum block_result write_sector(void *ctx,uint64_t lba,const unsigned char *p)
{
    (void)ctx;assert(lba>=START+1 && lba<START+DATA_MIN_SECTORS);++writes;
    if (fail()) { if (torn) { memcpy(media+lba*512,p,137);memcpy(durable+lba*512,p,137); }return BLOCK_IO; }
    memcpy(media+lba*512,p,512);return BLOCK_OK;
}
static enum block_result flush(void *ctx)
{
    (void)ctx;++flushes;bool bad=fail();if (!bad || torn) { memcpy(durable,media,sizeof(media)); }return bad ? BLOCK_IO : BLOCK_OK;
}
static struct block_device device={.name="volatile-cache-model",.sectors=SECTORS,.capacity_known=true,.read=read_sector,.write=write_sector,.flush=flush};
static void reset_counters(void) { events=fail_event=writes=flushes=0;torn=false; }
static void header(unsigned char *p,unsigned bank,uint64_t generation)
{
    memset(p,0,512);memcpy(p,"OUROSBK1",8);p32(p+8,1);p32(p+12,bank);p32(p+16,(uint32_t)generation);p32(p+20,(uint32_t)(generation>>32));p32(p+24,DATA_PAYLOAD_BYTES);
    digest(p+512,DATA_PAYLOAD_BYTES,p+32);digest(p,64,p+64);
}
static void genesis(void)
{
    memset(media,0,sizeof(media));media[510]=85;media[511]=170;media[450]=12;p32(media+454,2048);p32(media+458,1024);
    media[466]=0xda;p32(media+470,START);p32(media+474,2048);unsigned char *p=media+START*512;
    memcpy(p,"OUROSDAT",8);p32(p+8,1);p32(p+12,512);p32(p+16,2048);p32(p+20,DATA_BANK_SECTORS);p32(p+24,DATA_FILES);p32(p+28,DATA_FILE_BYTES);digest(p,64,p+64);
    header(p+512,0,1);memcpy(durable,media,sizeof(media));reset_counters();
}
static void read_equals(struct persist_volume *fs,const char *path,const char *expected)
{
    unsigned char out[4096];size_t n=999;assert(persist_read(fs,path,0,out,sizeof(out),&n)==FS_OK && n==strlen(expected) && !memcmp(out,expected,n));
}
static void behavior(void)
{
    genesis();assert(persist_mount(&v,&device)==FS_OK && v.generation==1 && v.bank==0 && v.valid_banks==1 && writes==0);
    bool dir;assert(persist_stat(&v,"/",&dir)==FS_OK && dir);assert(persist_read(&v,"/",0,media,1,(size_t[]){0})==FS_IS_DIR);
    assert(persist_write(&v,"/note.txt","hello",5,false)==FS_OK);read_equals(&v,"/note.txt","hello");assert(v.generation==2 && v.bank==1);
    assert(persist_write(&v,"/note.txt",NULL,0,true)==FS_OK && v.generation==2);read_equals(&v,"/note.txt","hello");
    unsigned char out[20];size_t n;assert(persist_read(&v,"/note.txt",2,out,2,&n)==FS_OK && n==2 && !memcmp(out,"ll",2));
    assert(persist_read(&v,"/note.txt",UINT32_MAX,out,20,&n)==FS_OK && !n);
    assert(persist_write(&v,"/empty",NULL,0,true)==FS_OK);read_equals(&v,"/empty","");
    unsigned before=writes;assert(persist_write(&v,"/bad/name",NULL,0,true)==FS_INVALID_PATH && persist_write(&v,"/../bad",NULL,0,true)==FS_INVALID_PATH);
    assert(persist_write(&v,"/large",out,4097,false)==FS_FILE_TOO_LARGE && writes==before);
    for (unsigned i=2;i<8;++i) { char key[20];snprintf(key,sizeof(key),"/file%u",i);assert(persist_write(&v,key,"x",1,false)==FS_OK); }
    before=writes;assert(persist_write(&v,"/full",NULL,0,true)==FS_NO_NODES && writes==before);
    assert(persist_remove(&v,"/note.txt",true)==FS_NOT_DIR && persist_remove(&v,"/",true)==FS_BUSY);
    assert(persist_remove(&v,"/empty",false)==FS_OK && persist_write(&v,"/replacement","new",3,false)==FS_OK);
    assert(persist_sync(&v)==FS_OK);memcpy(media,durable,sizeof(media));assert(persist_mount(&reopened,&device)==FS_OK);read_equals(&reopened,"/note.txt","hello");read_equals(&reopened,"/replacement","new");
    assert(persist_stat(&reopened,"/empty",&dir)==FS_NOT_FOUND);
    assert(!memcmp(media,durable,START*512) && !memcmp(media+(START+DATA_MIN_SECTORS)*512,durable+(START+DATA_MIN_SECTORS)*512,(SECTORS-START-DATA_MIN_SECTORS)*512));
    puts("PASS: persistent create/replace/touch/read/seek-to-EOF/remove/full-directory/reuse, path/size bounds, synchronous flush and remount survive volatile-cache loss");
}
static void cuts(void)
{
    genesis();assert(persist_mount(&v,&device)==FS_OK);assert(persist_write(&v,"/note","old-version",11,false)==FS_OK);
    memcpy(original,durable,sizeof(original));reset_counters();assert(persist_write(&v,"/note","new-version",11,false)==FS_OK);unsigned count=events;
    for (unsigned mode=0;mode<2;++mode) { for (unsigned point=1;point<=count;++point) {
        memcpy(media,original,sizeof(media));memcpy(durable,original,sizeof(durable));reset_counters();assert(persist_mount(&v,&device)==FS_OK);events=0;fail_event=point;torn=mode!=0;
        assert(persist_write(&v,"/note","new-version",11,false)==FS_IO && v.failed);read_equals(&v,"/note","old-version");unsigned before=events;
        assert(persist_write(&v,"/no-retry","x",1,false)==FS_IO && events==before);
        memcpy(media,durable,sizeof(media));reset_counters();assert(persist_mount(&reopened,&device)==FS_OK);
        unsigned char out[20];size_t n;assert(persist_read(&reopened,"/note",0,out,sizeof(out),&n)==FS_OK && n==11 && (!memcmp(out,"old-version",11) || !memcmp(out,"new-version",11)));
        assert(!memcmp(durable,original,START*512) && !memcmp(durable+(START+DATA_MIN_SECTORS)*512,original+(START+DATA_MIN_SECTORS)*512,(SECTORS-START-DATA_MIN_SECTORS)*512));
    } }
    printf("PASS: all %u update read/write/flush failure points, both no-completion and torn-write/ambiguous-flush models; reboot selects complete old/new snapshot, no out-of-data writes\n",count*2);
}
static void rejection(void)
{
    genesis();memcpy(original,media,sizeof(media));struct block_device d=device;d.write=NULL;assert(persist_mount(&v,&d)==FS_UNSUPPORTED && !writes);
    d=device;d.flush=NULL;assert(persist_mount(&v,&d)==FS_UNSUPPORTED && !writes);
    p32(media+470,2500);assert(persist_mount(&v,&device)==FS_CORRUPT && !writes);memcpy(media,original,sizeof(media));
    p32(media+474,UINT32_MAX);assert(persist_mount(&v,&device)==FS_CORRUPT && !writes);memcpy(media,original,sizeof(media));
    media[482]=0xda;p32(media+486,5120);p32(media+490,512);assert(persist_mount(&v,&device)!=FS_OK && !writes);memcpy(media,original,sizeof(media));
    media[START*512+65]^=1;assert(persist_mount(&v,&device)==FS_CORRUPT && !writes);memcpy(media,original,sizeof(media));
    unsigned char *bank=media+(START+1)*512;memcpy(bank+512,"bad/name",9);header(bank,0,1);assert(persist_mount(&v,&device)==FS_CORRUPT && !writes);memcpy(media,original,sizeof(media));
    assert(persist_mount(&v,&device)==FS_OK && persist_write(&v,"/note","good",4,false)==FS_OK);media[(START+1+DATA_BANK_SECTORS+1)*512]^=1;
    assert(persist_mount(&reopened,&device)==FS_OK && reopened.generation==1 && reopened.valid_banks==1);
    media[(START+2)*512]^=1;assert(persist_mount(&reopened,&device)==FS_CORRUPT);
    genesis();assert(persist_mount(&v,&device)==FS_OK);v.generation=UINT64_MAX;unsigned before=writes;assert(persist_write(&v,"/overflow","x",1,false)==FS_UNSUPPORTED && writes==before);
    puts("PASS: missing write/flush, overlapping/out-of-bounds/duplicate partitions, corrupt superblock/data, invalid names and generation overflow refused; damaged newest snapshot falls back to older complete bank");
}
static void mount_diagnostics(void)
{
    genesis();assert(persist_mount(&v,&device)==FS_OK);unsigned count=events;assert(count==70);
    for (unsigned point=1;point<=count;++point) {
        genesis();fail_event=point;assert(persist_mount(&v,&device)==FS_IO && !v.ready && !writes && v.mount_io==BLOCK_IO);
        const char *phase=point==1 ? "mbr" : point==2 ? "superblock" : point==3 ? "bank0-header" : point<69 ? "bank0-payload" : point==69 ? "bank1-header" : "cache-policy/flush";
        assert(!strcmp(v.mount_phase,phase));
        uint64_t lba=point==1 || point==70 ? 0 : point==69 ? START+67 : START+point-2;
        assert(v.mount_lba==lba);
    }
    puts("PASS: all 70 mount I/O failures retain exact phase/result/LBA; failed mounts accept no writes");
}
int main(int argc,char **argv)
{
    behavior();cuts();rejection();mount_diagnostics();
    if (argc==2 || argc==3) {
        FILE *f=fopen(argv[argc-1],"rb");assert(f);
        if (argc==2) {
            unsigned char m[512];assert(fread(m,1,512,f)==512);uint32_t start=(uint32_t)m[470]|(uint32_t)m[471]<<8|(uint32_t)m[472]<<16|(uint32_t)m[473]<<24;
            assert(m[466]==0xda && start==260096);assert(fseek(f,(long)start*512,SEEK_SET)==0);
        } else { assert(!strcmp(argv[1],"--partition")); }
        genesis();assert(fread(media+START*512,1,2048*512,f)==2048*512);fclose(f);
        reset_counters();assert(persist_mount(&v,&device)==FS_OK && v.generation==1 && v.bank==0 && v.valid_banks==1 && !writes);
        puts("PASS: independently mounted actual PowerShell-generated blank image data partition and initial generation");
    }return 0;
}
