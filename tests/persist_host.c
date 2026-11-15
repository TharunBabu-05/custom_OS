#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "host_assert.h"
#include "persist.h"
#include "sha256.h"
enum { SECTORS=6144,START=4096 };
static unsigned char media[SECTORS*512],durable[SECTORS*512],original[SECTORS*512];
static struct persist_volume v,reopened;
static unsigned events,fail_event,corrupt_event,writes,flushes;
static bool torn;
static unsigned model_min_sectors=DATA_MIN_SECTORS;
static void p32(unsigned char *p,uint32_t x) { for (unsigned i=0;i<4;++i) { p[i]=(unsigned char)(x>>(i*8)); } }
static void digest(const void *p,size_t n,unsigned char out[32]) { struct sha256 s;sha256_init(&s);sha256_update(&s,p,n);sha256_final(&s,out); }
static bool fail(void) { return ++events==fail_event; }
static enum block_result read_sector(void *ctx,uint64_t lba,unsigned char *p)
{ (void)ctx;assert(lba<SECTORS);if (fail()) { return BLOCK_IO; }memcpy(p,media+lba*512,512);if (events==corrupt_event) { p[0]^=1; }return BLOCK_OK; }
static enum block_result write_sector(void *ctx,uint64_t lba,const unsigned char *p)
{
    (void)ctx;assert(lba>=START+1 && lba<START+model_min_sectors);++writes;
    if (fail()) { if (torn) { memcpy(media+lba*512,p,137);memcpy(durable+lba*512,p,137); }return BLOCK_IO; }
    memcpy(media+lba*512,p,512);return BLOCK_OK;
}
static enum block_result flush(void *ctx)
{
    (void)ctx;++flushes;bool bad=fail();if (!bad || torn) { memcpy(durable,media,sizeof(media)); }return bad ? BLOCK_IO : BLOCK_OK;
}
static struct block_device device={.name="volatile-cache-model",.sectors=SECTORS,.capacity_known=true,.read=read_sector,.write=write_sector,.flush=flush};
static void reset_counters(void) { events=fail_event=corrupt_event=writes=flushes=0;torn=false; }
static void header(unsigned char *p,unsigned bank,uint64_t generation)
{
    memset(p,0,512);memcpy(p,"OUROSBK1",8);p32(p+8,1);p32(p+12,bank);p32(p+16,(uint32_t)generation);p32(p+20,(uint32_t)(generation>>32));p32(p+24,DATA_PAYLOAD_BYTES);
    digest(p+512,DATA_PAYLOAD_BYTES,p+32);digest(p,64,p+64);
}
static void genesis(void)
{
    model_min_sectors=DATA_MIN_SECTORS;
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
static void update_diagnostics(unsigned point)
{
    unsigned p=v.payload_bytes/512;const char *phase;uint64_t base=(uint64_t)v.start+1+(v.bank^1)*v.bank_sectors,lba=base;
    if (point==1) { phase="invalidate-header"; }
    else if (point==2) { phase="invalidate-flush"; }
    else if (point<=p+2) { phase="payload-write";lba=base+point-2; }
    else if (point==p+3) { phase="payload-flush";lba=base+p; }
    else if (point<=2*p+3) { phase="payload-readback";lba=base+point-p-3; }
    else if (point==2*p+4) { phase="commit-header"; }
    else if (point==2*p+5) { phase="commit-flush"; }
    else { assert(point==2*p+6);phase="commit-readback"; }
    assert(v.write_phase && !strcmp(v.write_phase,phase) && v.write_io==BLOCK_IO && v.write_lba==lba && !v.readback_mismatch);
}
static void cuts(void)
{
    genesis();assert(persist_mount(&v,&device)==FS_OK);assert(persist_write(&v,"/note","old-version",11,false)==FS_OK);
    memcpy(original,durable,sizeof(original));reset_counters();assert(persist_write(&v,"/note","new-version",11,false)==FS_OK);unsigned count=events;
    for (unsigned mode=0;mode<2;++mode) { for (unsigned point=1;point<=count;++point) {
        memcpy(media,original,sizeof(media));memcpy(durable,original,sizeof(durable));reset_counters();assert(persist_mount(&v,&device)==FS_OK);events=0;fail_event=point;torn=mode!=0;
        assert(persist_write(&v,"/note","new-version",11,false)==FS_IO && v.failed);read_equals(&v,"/note","old-version");unsigned before=events;
        update_diagnostics(point);
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
static void genesis_v2(void)
{
    genesis();model_min_sectors=DATA_V2_MIN_SECTORS;unsigned char *p=media+START*512;
    p32(p+8,2);p32(p+20,DATA_V2_BANK_SECTORS);p32(p+24,DATA_V2_FILES);p32(p+28,DATA_V2_FILE_BYTES);digest(p,64,p+64);
    p+=512;memset(p,0,512);memcpy(p,"OUROSBK2",8);p32(p+8,2);p32(p+16,1);p32(p+24,DATA_V2_PAYLOAD_BYTES);
    digest(p+512,DATA_V2_PAYLOAD_BYTES,p+32);digest(p,64,p+64);memcpy(durable,media,sizeof(media));reset_counters();
}
static void genesis_v3(void)
{
    genesis_v2();unsigned char *p=media+START*512;p32(p+8,3);digest(p,64,p+64);
    p+=512;memcpy(p,"OUROSBK3",8);p32(p+8,3);digest(p,64,p+64);memcpy(durable,media,sizeof(media));reset_counters();
}
static void genesis_v4(void)
{
    genesis();model_min_sectors=DATA_V4_MIN_SECTORS;unsigned char *p=media+START*512;
    p32(p+8,4);p32(p+20,DATA_V4_BANK_SECTORS);p32(p+24,DATA_V4_FILES);p32(p+28,DATA_V4_FILE_BYTES);digest(p,64,p+64);
    p+=512;memset(p,0,512);memcpy(p,"OUROSBK4",8);p32(p+8,4);p32(p+16,1);p32(p+24,DATA_V4_PAYLOAD_BYTES);
    digest(p+512,DATA_V4_PAYLOAD_BYTES,p+32);digest(p,64,p+64);memcpy(durable,media,sizeof(media));reset_counters();
}
static void capacity_v2(void)
{
    static unsigned char bytes[8193],out[8192];for (unsigned i=0;i<sizeof(bytes);++i) { bytes[i]=(unsigned char)i; }
    genesis_v2();assert(persist_mount(&v,&device)==FS_OK && v.version==2 && v.files==16 && v.file_bytes==8192 && v.directory_bytes==1024 && !writes);
    assert(persist_write(&v,"/large",bytes,8192,false)==FS_OK);size_t n;
    assert(persist_read(&v,"/large",0,out,sizeof(out),&n)==FS_OK && n==8192 && !memcmp(out,bytes,n));
    unsigned before=writes;assert(persist_write(&v,"/large",bytes,8193,false)==FS_FILE_TOO_LARGE && writes==before);
    assert(persist_read(&v,"/large",8191,out,2,&n)==FS_OK && n==1 && out[0]==bytes[8191]);
    assert(persist_read(&v,"/large",8192,out,1,&n)==FS_OK && !n);
    for (unsigned i=0;i<15;++i) { char path[24];snprintf(path,sizeof(path),"/file%u",i);assert(persist_write(&v,path,"x",1,false)==FS_OK); }
    before=writes;assert(persist_write(&v,"/overflow","x",1,false)==FS_NO_NODES && writes==before);
    memcpy(media,durable,sizeof(media));assert(persist_mount(&reopened,&device)==FS_OK && reopened.version==2 && reopened.generation==17);
    assert(persist_read(&reopened,"/large",0,out,sizeof(out),&n)==FS_OK && n==8192 && !memcmp(out,bytes,n));
    media[(START+1+reopened.bank*DATA_V2_BANK_SECTORS+1)*512]^=1;
    assert(persist_mount(&reopened,&device)==FS_OK && reopened.generation==16 && reopened.valid_banks==1);
    genesis_v2();unsigned char *super=media+START*512;p32(super+24,17);digest(super,64,super+64);
    assert(persist_mount(&v,&device)==FS_CORRUPT && !writes);
    genesis_v2();unsigned char *bank=media+(START+1)*512;memcpy(bank,"OUROSBK1",8);digest(bank,64,bank+64);
    assert(persist_mount(&v,&device)==FS_CORRUPT && !writes);
    genesis_v2();bank=media+(START+1)*512;memcpy(bank+512,"dup",4);memcpy(bank+576,"dup",4);
    digest(bank+512,DATA_V2_PAYLOAD_BYTES,bank+32);digest(bank,64,bank+64);
    assert(persist_mount(&v,&device)==FS_CORRUPT && !writes);
    puts("PASS: v2 16-slot/8192-byte binary capacity, EOF, full directory, oversize preservation, remount, corrupt-newest fallback and strict geometry/magic/duplicate rejection.");
}
static void cuts_v2(void)
{
    genesis_v2();assert(persist_mount(&v,&device)==FS_OK && persist_write(&v,"/note","old",3,false)==FS_OK);
    memcpy(original,durable,sizeof(original));reset_counters();assert(persist_write(&v,"/note","new",3,false)==FS_OK);unsigned count=events;
    for (unsigned mode=0;mode<2;++mode) { for (unsigned point=1;point<=count;++point) {
        memcpy(media,original,sizeof(media));memcpy(durable,original,sizeof(durable));reset_counters();assert(persist_mount(&v,&device)==FS_OK);
        events=0;fail_event=point;torn=mode!=0;assert(persist_write(&v,"/note","new",3,false)==FS_IO && v.failed);read_equals(&v,"/note","old");
        update_diagnostics(point);
        unsigned before=events;assert(persist_write(&v,"/retry","x",1,false)==FS_IO && events==before);
        memcpy(media,durable,sizeof(media));reset_counters();assert(persist_mount(&reopened,&device)==FS_OK && reopened.version==2);
        unsigned char out[3];size_t n;assert(persist_read(&reopened,"/note",0,out,3,&n)==FS_OK && n==3 && (!memcmp(out,"old",3) || !memcmp(out,"new",3)));
        assert(!memcmp(durable,original,(START+1)*512) && !memcmp(durable+(START+DATA_V2_MIN_SECTORS)*512,original+(START+DATA_V2_MIN_SECTORS)*512,(SECTORS-START-DATA_V2_MIN_SECTORS)*512));
    } }
    printf("PASS: all %u v2 failure points (no completion/torn/ambiguous flush); reboot selects complete old/new data with unchanged MBR/superblock/outside sectors.\n",2*count);
}
static void readback_diagnostics(void)
{
    for (unsigned version=1;version<=2;++version) { for (unsigned header_read=0;header_read<2;++header_read) {
        if (version==1) { genesis(); }else { genesis_v2(); }
        assert(persist_mount(&v,&device)==FS_OK);events=0;unsigned p=v.payload_bytes/512;
        corrupt_event=header_read ? 2*p+6 : p+4;
        assert(persist_write(&v,"/bad","x",1,false)==FS_IO && v.failed && v.readback_mismatch && v.write_io==BLOCK_OK);
        assert(!strcmp(v.write_phase,header_read ? "commit-readback" : "payload-readback"));
        assert(v.write_lba==(uint64_t)START+1+v.bank_sectors+(header_read ? 0 : 1));
        unsigned before=events;assert(persist_sync(&v)==FS_IO && events==before && v.readback_mismatch);
    } }
    genesis();assert(persist_mount(&v,&device)==FS_OK);events=0;fail_event=1;
    assert(persist_sync(&v)==FS_IO && v.failed && v.write_io==BLOCK_IO && !strcmp(v.write_phase,"sync-flush") && !v.write_lba);
    puts("PASS: v1/v2 payload/header readback mismatch distinguished from transport errors; sync failure latched without retry.");
}
static unsigned char archive[512+DATA_V4_PAYLOAD_BYTES],saved_archive[512+DATA_V4_PAYLOAD_BYTES];
struct archive_io { size_t offset,length;unsigned calls,fail_call; };
static bool export_bytes(const void *data,size_t count,void *context)
{
    struct archive_io *io=context;if (++io->calls==io->fail_call) { return false; }
    assert(io->offset+count<=sizeof(archive));memcpy(archive+io->offset,data,count);io->offset+=count;return true;
}
static bool import_bytes(void *data,size_t count,void *context)
{
    struct archive_io *io=context;if (++io->calls==io->fail_call || io->offset+count>io->length) { return false; }
    memcpy(data,archive+io->offset,count);io->offset+=count;return true;
}
static void backup_roundtrip(void)
{
    for (unsigned version=1;version<=2;++version) {
        if (version==1) { genesis(); }else { genesis_v2(); }assert(persist_mount(&v,&device)==FS_OK);
        unsigned char pattern[8192];for (unsigned i=0;i<sizeof(pattern);++i) { pattern[i]=(unsigned char)i; }
        assert(persist_write(&v,"/pattern.bin",pattern,v.file_bytes,false)==FS_OK && persist_write(&v,"/empty",NULL,0,true)==FS_OK);
        unsigned before=events;struct archive_io out={0};assert(persist_export(&v,export_bytes,&out)==FS_OK && out.offset==512+v.payload_bytes && before==events && !v.busy);
        memcpy(saved_archive,archive,out.offset);size_t length=out.offset;
        out=(struct archive_io){.fail_call=2};assert(persist_export(&v,export_bytes,&out)==FS_IO && !v.busy && !v.failed && before==events);
        memcpy(archive,saved_archive,length);
        struct archive_io in={.length=length};assert(persist_restore(&v,length,import_bytes,&in)==FS_BUSY && !in.calls && events==before);
        if (version==1) { genesis(); }else { genesis_v2(); }assert(persist_mount(&v,&device)==FS_OK);
        before=events;in=(struct archive_io){.length=length};assert(persist_restore(&v,length-1,import_bytes,&in)==FS_UNSUPPORTED && !in.calls && before==events);
        for (unsigned part=0;part<2;++part) {
            archive[part ? 512 : 16]^=1;in=(struct archive_io){.length=length};
            assert(persist_restore(&v,length,import_bytes,&in)==FS_CORRUPT && !v.busy && !v.failed && events==before);archive[part ? 512 : 16]^=1;
        }
        /* A checksummed archive must also have a valid directory. */
        archive[512]='/';digest(archive+512,length-512,archive+32);digest(archive,64,archive+64);in=(struct archive_io){.length=length};
        assert(persist_restore(&v,length,import_bytes,&in)==FS_CORRUPT && events==before);memcpy(archive,saved_archive,length);
        for (unsigned call=1;call<=length/512;++call) {
            in=(struct archive_io){.length=length,.fail_call=call};assert(persist_restore(&v,length,import_bytes,&in)==FS_IO && !v.failed && !v.busy && events==before);
        }
        in=(struct archive_io){.length=length};assert(persist_restore(&v,length,import_bytes,&in)==FS_OK && v.generation==2);
        assert(!memcmp(v.live,archive+512,v.payload_bytes));memcpy(media,durable,sizeof(media));assert(persist_mount(&reopened,&device)==FS_OK && !memcmp(reopened.live,archive+512,reopened.payload_bytes));
        /* Every commit failure after complete validation is still atomic. */
        unsigned count=2*(unsigned)(length-512)/512+6;
        for (unsigned point=1;point<=count;++point) {
            if (version==1) { genesis(); }else { genesis_v2(); }assert(persist_mount(&v,&device)==FS_OK);events=0;fail_event=point;torn=true;
            in=(struct archive_io){.length=length};assert(persist_restore(&v,length,import_bytes,&in)==FS_IO && v.failed && !v.busy);
            memcpy(media,durable,sizeof(media));reset_counters();assert(persist_mount(&reopened,&device)==FS_OK);
            if (reopened.generation==2) { assert(!memcmp(reopened.live,archive+512,reopened.payload_bytes)); }
            else { assert(reopened.generation==1 && !reopened.live[0]); }
        }
    }
    puts("PASS: v1/v2 backup export without block I/O; empty-target atomic restore, binary/empty files, remount, bad/truncated inputs, every reader failure and all interrupted restore commits.");
}
static void rename_checks(void)
{
    for (unsigned version=1;version<=2;++version) {
        if (version==1) { genesis(); }else { genesis_v2(); }assert(persist_mount(&v,&device)==FS_OK);
        assert(persist_write(&v,"/old-name","rename-binary\0tail",18,false)==FS_OK && persist_write(&v,"/keep","keep",4,false)==FS_OK);
        unsigned before=events;assert(persist_rename(&v,"/old-name","/old-name")==FS_OK && events==before);
        assert(persist_rename(&v,"/old-name","/keep")==FS_EXISTS && events==before);
        assert(persist_rename(&v,"/missing","/new-name")==FS_NOT_FOUND && events==before);
        assert(persist_rename(&v,"/old-name","/bad/path")==FS_INVALID_PATH && events==before);
        memcpy(original,durable,sizeof(original));events=0;assert(persist_rename(&v,"/old-name","/new-name")==FS_OK);unsigned count=events;
        for (unsigned mode=0;mode<2;++mode) { for (unsigned point=1;point<=count;++point) {
            memcpy(media,original,sizeof(media));memcpy(durable,original,sizeof(durable));reset_counters();assert(persist_mount(&v,&device)==FS_OK);
            events=0;fail_event=point;torn=mode!=0;assert(persist_rename(&v,"/old-name","/new-name")==FS_IO && v.failed);
            bool dir;assert(persist_stat(&v,"/old-name",&dir)==FS_OK && persist_stat(&v,"/new-name",&dir)==FS_NOT_FOUND);
            before=events;assert(persist_rename(&v,"/old-name","/other")==FS_IO && events==before);
            memcpy(media,durable,sizeof(media));reset_counters();assert(persist_mount(&reopened,&device)==FS_OK);
            bool old=persist_stat(&reopened,"/old-name",&dir)==FS_OK,next=persist_stat(&reopened,"/new-name",&dir)==FS_OK;assert(old!=next);
            unsigned char out[32];size_t n;assert(persist_read(&reopened,old ? "/old-name" : "/new-name",0,out,sizeof(out),&n)==FS_OK && n==18 && !memcmp(out,"rename-binary\0tail",18));
            read_equals(&reopened,"/keep","keep");
            assert(!memcmp(durable,original,(START+1)*512) && !memcmp(durable+(START+model_min_sectors)*512,original+(START+model_min_sectors)*512,(SECTORS-START-model_min_sectors)*512));
        } }
        /* Rename reuses the original slot, even when the directory is full. */
        if (version==1) { genesis(); }else { genesis_v2(); }assert(persist_mount(&v,&device)==FS_OK);
        for (unsigned i=0;i<v.files;++i) { char path[32];snprintf(path,sizeof(path),"/slot%u",i);assert(persist_write(&v,path,"x",1,false)==FS_OK); }
        assert(persist_rename(&v,"/slot0","/renamed")==FS_OK);read_equals(&v,"/renamed","x");
        memcpy(media,durable,sizeof(media));reset_counters();assert(persist_mount(&reopened,&device)==FS_OK);read_equals(&reopened,"/renamed","x");
    }
    puts("PASS: v1/v2 atomic rename, binary contents, no-op/no overwrite, full-directory slot reuse and all 272/1044 interrupted-I/O cases; reboot exposes exactly one complete name.");
}
struct listed { unsigned count,dirs;char names[16][32]; };
static void list_name(const char *key,bool dir,size_t n,void *context)
{ (void)n;struct listed *l=context;assert(l->count<16);strcpy(l->names[l->count++],key);l->dirs+=dir; }
static void directories(void)
{
    genesis_v3();assert(persist_mount(&v,&device)==FS_OK && v.version==3);
    assert(persist_mkdir(&v,"/alpha")==FS_OK && persist_mkdir(&v,"/alpha/beta")==FS_OK && persist_mkdir(&v,"/other")==FS_OK);
    unsigned char data[8192],out[8192];for (unsigned i=0;i<sizeof(data);++i) { data[i]=(unsigned char)i; }
    assert(persist_write(&v,"/alpha/note",data,sizeof(data),false)==FS_OK && persist_write(&v,"/other/note","other",5,false)==FS_OK);
    bool dir;assert(persist_stat(&v,"/alpha/beta",&dir)==FS_OK && dir);size_t n;
    assert(persist_read(&v,"/alpha",0,out,sizeof(out),&n)==FS_IS_DIR);
    struct listed list={0};assert(persist_list(&v,"/alpha",list_name,&list)==FS_OK && list.count==2 && list.dirs==1);
    assert(persist_rename(&v,"/alpha/note","/alpha/beta/moved")==FS_OK && persist_read(&v,"/alpha/beta/moved",0,out,sizeof(out),&n)==FS_OK && n==8192 && !memcmp(data,out,n));
    assert(persist_rename(&v,"/other/note","/alpha/beta/moved")==FS_EXISTS);
    assert(persist_rename(&v,"/alpha","/changed")==FS_IS_DIR && persist_remove(&v,"/alpha",true)==FS_NOT_EMPTY);
    assert(persist_mkdir(&v,"/alpha/beta")==FS_EXISTS && persist_mkdir(&v,"/missing/child")==FS_NOT_FOUND);
    assert(persist_write(&v,"/alpha","x",1,false)==FS_IS_DIR && persist_write(&v,"/other/note/child","x",1,false)==FS_NOT_DIR);
    assert(persist_remove(&v,"/alpha/beta",false)==FS_IS_DIR && persist_remove(&v,"/other/note",true)==FS_NOT_DIR);
    struct archive_io exported={0};assert(persist_export(&v,export_bytes,&exported)==FS_OK);memcpy(saved_archive,archive,exported.offset);
    genesis_v3();assert(persist_mount(&v,&device)==FS_OK);struct archive_io input={.length=exported.offset};
    assert(persist_restore(&v,input.length,import_bytes,&input)==FS_OK);read_equals(&v,"/other/note","other");
    memcpy(media,durable,sizeof(media));reset_counters();assert(persist_mount(&reopened,&device)==FS_OK && persist_stat(&reopened,"/alpha/beta",&dir)==FS_OK && dir);
    for (unsigned bad=0;bad<10;++bad) {
        memcpy(archive,saved_archive,exported.offset);
        switch (bad) {
        case 0:p32(archive+512+40,1);break; /* self-cycle */
        case 1:p32(archive+512+40,2);break; /* alpha <-> beta */
        case 2:p32(archive+512+40,17);break;
        case 3:p32(archive+512+40,16);break; /* unused parent */
        case 4:p32(archive+512+4*64+40,4);break; /* parent is file slot 3 */
        case 5:memset(archive+512+4*64,0,32);memcpy(archive+512+4*64,"moved",5);p32(archive+512+4*64+40,2);break;
        case 6:p32(archive+512+36,99);break;
        case 7:archive[512+DATA_V2_DIRECTORY_BYTES]=1;break; /* directory data */
        case 8:archive[512+44]=1;break;
        default:
            memset(archive+512,0,DATA_V2_PAYLOAD_BYTES);
            for (unsigned i=0;i<16;++i) { unsigned char *e=archive+512+i*64;memset(e,'a',31);e[30]=(unsigned char)('A'+i);p32(e+36,2);p32(e+40,i); }
            break;
        }
        digest(archive+512,DATA_V2_PAYLOAD_BYTES,archive+32);digest(archive,64,archive+64);
        genesis_v3();assert(persist_mount(&v,&device)==FS_OK);unsigned before=events;input=(struct archive_io){.length=exported.offset};
        assert(persist_restore(&v,input.length,import_bytes,&input)==FS_CORRUPT && events==before && !v.failed && !v.busy);
        memset(media+(START+1)*512,0,512);unsigned char *bank=media+(START+1+DATA_V2_BANK_SECTORS)*512;
        memcpy(bank,archive,exported.offset);p32(bank+12,1);digest(bank,64,bank+64);
        assert(persist_mount(&reopened,&device)==FS_CORRUPT && !reopened.ready);
    }
    genesis_v3();assert(persist_mount(&v,&device)==FS_OK);char path[80]="";
    for (unsigned i=0;i<16;++i) { strcat(path,"/d");assert(persist_mkdir(&v,path)==FS_OK); }
    assert(persist_mkdir(&v,"/extra")==FS_NO_NODES && persist_remove(&v,"/",true)==FS_BUSY);
    puts("PASS: v3 nested mkdir/list/read/move/rmdir, sibling names, full/deep tree, archive roundtrip, binary bytes, and checksummed cycles/orphans/duplicate/non-directory-parent/type/reserved-data rejection at restore and mount.");
}
static void directory_cuts(void)
{
    for (unsigned operation=0;operation<3;++operation) {
        genesis_v3();assert(persist_mount(&v,&device)==FS_OK && persist_mkdir(&v,"/a")==FS_OK && persist_mkdir(&v,"/b")==FS_OK && persist_write(&v,"/a/file","body",4,false)==FS_OK);
        memcpy(original,durable,sizeof(original));memcpy(saved_archive+512,v.live,v.payload_bytes);events=0;
        enum fs_result r=operation==0 ? persist_mkdir(&v,"/a/new") : operation==1 ? persist_rename(&v,"/a/file","/b/file") : persist_remove(&v,"/b",true);
        assert(r==FS_OK);unsigned count=events;memcpy(archive+512,v.live,v.payload_bytes);
        for (unsigned mode=0;mode<2;++mode) { for (unsigned point=1;point<=count;++point) {
            memcpy(media,original,sizeof(media));memcpy(durable,original,sizeof(durable));reset_counters();assert(persist_mount(&v,&device)==FS_OK);events=0;fail_event=point;torn=mode!=0;
            r=operation==0 ? persist_mkdir(&v,"/a/new") : operation==1 ? persist_rename(&v,"/a/file","/b/file") : persist_remove(&v,"/b",true);
            assert(r==FS_IO && v.failed && !v.busy && !memcmp(v.live,saved_archive+512,v.payload_bytes));
            memcpy(media,durable,sizeof(media));reset_counters();assert(persist_mount(&reopened,&device)==FS_OK);
            assert(!memcmp(reopened.live,saved_archive+512,reopened.payload_bytes) || !memcmp(reopened.live,archive+512,reopened.payload_bytes));
        } }
    }
    puts("PASS: all 3132 v3 mkdir/cross-directory rename/rmdir interrupted-I/O outcomes remount exactly one complete old/new tree.");
}
static void replacement_checks(void)
{
    unsigned outcomes=0;
    for (unsigned version=1;version<=3;++version) { for (unsigned exists=0;exists<2;++exists) {
        if (version==1) { genesis(); }else if (version==2) { genesis_v2(); }else { genesis_v3(); }
        assert(persist_mount(&v,&device)==FS_OK);
        const char *src=version==3 ? "/stage/new" : "/new",*dst=version==3 ? "/live/current" : "/current";
        if (version==3) { assert(persist_mkdir(&v,"/stage")==FS_OK && persist_mkdir(&v,"/live")==FS_OK); }
        unsigned char data[8192];for (unsigned i=0;i<v.file_bytes;++i) { data[i]=(unsigned char)(i*17); }
        assert(persist_write(&v,src,data,v.file_bytes,false)==FS_OK && persist_write(&v,"/keep","keep",4,false)==FS_OK);
        if (exists) { assert(persist_write(&v,dst,"old-target",10,false)==FS_OK); }
        unsigned before=events;
        assert(persist_publish(&v,src,src)==FS_OK && persist_publish(&v,"/missing",dst)==FS_NOT_FOUND && events==before);
        if (version==3) { assert(persist_publish(&v,src,"/live")==FS_IS_DIR && persist_publish(&v,"/stage",dst)==FS_IS_DIR && events==before); }
        memcpy(original,durable,sizeof(original));memcpy(saved_archive+512,v.live,v.payload_bytes);uint64_t generation=v.generation;
        events=0;assert(persist_publish(&v,src,dst)==FS_OK && v.generation==generation+1);unsigned count=events;
        memcpy(archive+512,v.live,v.payload_bytes);bool dir;unsigned char out[8192];size_t n;
        assert(persist_stat(&v,src,&dir)==FS_NOT_FOUND && persist_read(&v,dst,0,out,sizeof(out),&n)==FS_OK && n==v.file_bytes && !memcmp(out,data,n));
        assert(persist_mount(&reopened,&device)==FS_OK && !memcmp(reopened.live,v.live,v.payload_bytes));
        for (unsigned mode=0;mode<2;++mode) { for (unsigned point=1;point<=count;++point) {
            memcpy(media,original,sizeof(media));memcpy(durable,original,sizeof(durable));reset_counters();assert(persist_mount(&v,&device)==FS_OK);
            events=0;fail_event=point;torn=mode!=0;
            assert(persist_publish(&v,src,dst)==FS_IO && v.failed && !v.busy && !memcmp(v.live,saved_archive+512,v.payload_bytes));
            update_diagnostics(point);before=events;
            assert(persist_publish(&v,src,src)==FS_IO && persist_publish(&v,src,dst)==FS_IO && events==before);
            memcpy(media,durable,sizeof(media));reset_counters();assert(persist_mount(&reopened,&device)==FS_OK);
            assert(!memcmp(reopened.live,saved_archive+512,reopened.payload_bytes) || !memcmp(reopened.live,archive+512,reopened.payload_bytes));
            assert(!memcmp(durable,original,(START+1)*512) && !memcmp(durable+(START+model_min_sectors)*512,original+(START+model_min_sectors)*512,(SECTORS-START-model_min_sectors)*512));
            ++outcomes;
        } }
    } }
    for (unsigned version=1;version<=3;++version) {
        if (version==1) { genesis(); }else if (version==2) { genesis_v2(); }else { genesis_v3(); }assert(persist_mount(&v,&device)==FS_OK);
        for (unsigned i=0;i<v.files;++i) { char path[32];snprintf(path,sizeof(path),"/slot%u",i);assert(persist_write(&v,path,i ? "old" : "new",3,false)==FS_OK); }
        assert(persist_publish(&v,"/slot0","/slot1")==FS_OK);read_equals(&v,"/slot1","new");
        assert(persist_write(&v,"/reused","x",1,false)==FS_OK && persist_write(&v,"/full","x",1,false)==FS_NO_NODES);
        assert(persist_write(&v,"/reused",NULL,0,false)==FS_OK && persist_publish(&v,"/reused","/slot1")==FS_OK);read_equals(&v,"/slot1","");
        assert(persist_mount(&reopened,&device)==FS_OK);read_equals(&reopened,"/slot1","");
    }
    printf("PASS: v1/v2/v3 atomic publication, full-size binary/empty files, full-table replacement and slot reuse; all %u interrupted-I/O outcomes recover the exact old/new payload, including both names and target bytes.\n",outcomes);
}
int main(int argc,char **argv)
{
    if (argc==1 || argc==2) { behavior();cuts();rejection();mount_diagnostics();capacity_v2();cuts_v2();readback_diagnostics();backup_roundtrip();rename_checks();directories();directory_cuts();replacement_checks(); }
    if (argc==3 && !strcmp(argv[1],"--export-prefix")) {
        for (unsigned version=1;version<=4;++version) {
            if (version==1) { genesis(); }else if (version==2) { genesis_v2(); }else if (version==3) { genesis_v3(); }else { genesis_v4(); }assert(persist_mount(&v,&device)==FS_OK);
            if (version>=3) { assert(persist_mkdir(&v,"/docs")==FS_OK); }
            assert(persist_write(&v,version>=3 ? "/docs/native.txt" : "/native.txt","native-backup",13,false)==FS_OK);struct archive_io io={0};assert(persist_export(&v,export_bytes,&io)==FS_OK);
            char path[512];assert(snprintf(path,sizeof(path),"%s.v%u.bak",argv[2],version)<(int)sizeof(path));FILE *f=fopen(path,"wb");assert(f && fwrite(archive,1,io.offset,f)==io.offset && !fclose(f));
        }return 0;
    }
    if (argc==3 && !strcmp(argv[1],"--backup")) {
        FILE *f=fopen(argv[2],"rb");assert(f);struct archive_io io={.length=fread(archive,1,sizeof(archive),f)};assert(feof(f) || io.length==sizeof(archive));assert(!fclose(f));
        unsigned version=archive[8];assert(version>=1 && version<=4);if (version==1) { genesis(); }else if (version==2) { genesis_v2(); }else if (version==3) { genesis_v3(); }else { genesis_v4(); }
        assert(persist_mount(&v,&device)==FS_OK && persist_restore(&v,io.length,import_bytes,&io)==FS_OK && !memcmp(v.live,archive+512,v.payload_bytes));
        puts("PASS: independently restored actual host-generated backup with all payload bytes identical.");return 0;
    }
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
