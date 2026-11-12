/* Exercise the public handle/VFS API against real RAMFS, FAT and persistent
 * backends. The delivery image is copied to memory; no host file is written. */
#include "host_assert.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kfile.h"
#include "vfs.h"
#include "persist.h"
#include "heap.h"
#include "memory.h"
#include "console.h"

_Alignas(4096) static unsigned char page_memory[32][4096];
static bool used_pages[32];
static int allocation_budget=-1;
static unsigned live_pages;
struct page_stats pages_stats(void)
{ return (struct page_stats){.ready=true,.allocated_pages=live_pages,.free_pages=32-live_pages}; }
bool page_alloc(uint64_t *physical)
{
    if (!physical || allocation_budget==0) { return false; }
    for (unsigned i=0;i<32;++i) { if (!used_pages[i]) {
        used_pages[i]=true;++live_pages;if (allocation_budget>0) { --allocation_budget; }
        *physical=(uintptr_t)page_memory[i];return true;
    } }return false;
}
bool page_free(uint64_t physical)
{
    for (unsigned i=0;i<32;++i) { if (physical==(uintptr_t)page_memory[i] && used_pages[i]) {
        used_pages[i]=false;--live_pages;return true;
    } }return false;
}
void console_write(const char *text) { (void)text; }
void console_u64(uint64_t value) { (void)value; }
void console_hex(uint64_t value) { (void)value; }

static unsigned char *media;
static size_t media_bytes;
static uint64_t reads,writes,flushes;
static bool fail_read,fail_write;
static enum block_result sector_read(void *context,uint64_t lba,unsigned char *out)
{
    (void)context;assert(lba<media_bytes/512);++reads;
    if (fail_read) { return BLOCK_IO; }memcpy(out,media+lba*512,512);return BLOCK_OK;
}
static enum block_result sector_write(void *context,uint64_t lba,const unsigned char *data)
{
    (void)context;assert(lba>=260097 && lba<260096+DATA_MIN_SECTORS);++writes;
    if (fail_write) { return BLOCK_IO; }memcpy(media+lba*512,data,512);return BLOCK_OK;
}
static enum block_result flush(void *context) { (void)context;++flushes;return BLOCK_OK; }
static struct block_device disk;

static uint32_t position(uint64_t owner,uint64_t handle)
{
    uint32_t offset=UINT32_MAX;assert(kfile_seek(owner,handle,0,KFILE_CUR,&offset)==FS_OK);return offset;
}
static void content(const char *path,const char *expected)
{
    unsigned char buffer[4096];size_t n=999;
    assert(vfs_read(path,0,buffer,sizeof(buffer),&n)==FS_OK && n==strlen(expected) && !memcmp(buffer,expected,n));
}
static void cursors_and_modes(void)
{
    uint64_t h,a,b;size_t n;unsigned char bytes[64];uint32_t offset;
    assert(vfs_write("/tmp/base.txt","abcdef",6)==FS_OK);
    assert(kfile_open(1,"/","/tmp/base.txt",KFILE_READ|KFILE_WRITE,&h)==FS_OK);
    assert(kfile_read(2,h,bytes,1,&n)==FS_BAD_HANDLE && n==0);
    assert(kfile_close(2,h)==FS_BAD_HANDLE && kfile_count()==1);
    assert(kfile_read(1,h,bytes,2,&n)==FS_OK && n==2 && !memcmp(bytes,"ab",2) && position(1,h)==2);
    assert(kfile_write(1,h,"XY",2,&n)==FS_OK && n==2 && position(1,h)==4);
    content("/tmp/base.txt","abXYef");
    assert(kfile_seek(1,h,-2,KFILE_END,&offset)==FS_OK && offset==4);
    assert(kfile_read(1,h,bytes,sizeof(bytes),&n)==FS_OK && n==2 && !memcmp(bytes,"ef",2));
    assert(kfile_read(1,h,bytes,sizeof(bytes),&n)==FS_OK && !n && position(1,h)==6);
    assert(kfile_write(1,h,"!",1,&n)==FS_OK && n==1 && position(1,h)==7);
    assert(kfile_write(1,h,NULL,0,&n)==FS_OK && !n && position(1,h)==7);
    content("/tmp/base.txt","abXYef!");
    assert(kfile_seek(1,h,INT64_MIN,KFILE_CUR,&offset)!=FS_OK && position(1,h)==7);
    assert(kfile_seek(1,h,INT64_MAX,KFILE_END,&offset)!=FS_OK && position(1,h)==7);
    assert(kfile_seek(1,h,8,KFILE_SET,&offset)!=FS_OK && position(1,h)==7);
    assert(kfile_write(1,h,bytes,SIZE_MAX,&n)!=FS_OK && !n && position(1,h)==7);
    assert(kfile_close(1,h)==FS_OK && kfile_close(1,h)==FS_BAD_HANDLE);
    assert(kfile_open(1,"/","/tmp/base.txt",KFILE_READ,&a)==FS_OK && a!=h);
    assert(kfile_open(2,"/tmp","./base.txt",KFILE_READ,&b)==FS_OK);
    assert(kfile_read(1,h,bytes,1,&n)==FS_BAD_HANDLE && !n);
    assert(kfile_read(1,a,bytes,3,&n)==FS_OK && n==3 && !memcmp(bytes,"abX",3));
    assert(kfile_read(2,b,bytes,1,&n)==FS_OK && n==1 && bytes[0]=='a');
    assert(position(1,a)==3 && position(2,b)==1);
    assert(kfile_write(1,a,"bad",3,&n)==FS_ACCESS && !n && position(1,a)==3);
    assert(kfile_close_owner(1)==1 && kfile_count()==1);
    assert(kfile_read(1,a,bytes,1,&n)==FS_BAD_HANDLE && !n);
    assert(kfile_close_owner(1)==0 && kfile_close_owner(2)==1 && kfile_count()==0);
    puts("PASS: independent cursors, tail-preserving writes/extension, EOF, bounded seeks, zero/overflow writes, mode checks, cross-owner/stale tokens and owner cleanup");
}
static void leases(void)
{
    uint64_t a,b,other;assert(vfs_mkdir("/tmp/locks")==FS_OK);
    assert(vfs_write("/tmp/locks/x.txt","locked",6)==FS_OK);
    assert(kfile_open(11,"/","/tmp/locks/x.txt",KFILE_READ,&a)==FS_OK);
    assert(kfile_open(12,"/tmp/locks","../locks//./x.txt",KFILE_READ,&b)==FS_OK);
    assert(kfile_open(13,"/","/tmp/locks/x.txt",KFILE_WRITE,&other)==FS_BUSY);
    assert(vfs_write("/tmp//locks/./x.txt","bad",3)==FS_BUSY);
    assert(vfs_touch("/tmp/locks/../locks/x.txt")==FS_BUSY);
    assert(vfs_remove("/tmp//locks/x.txt",false)==FS_BUSY);
    assert(vfs_remove("/tmp/locks/.",true)==FS_BUSY);
    assert(vfs_mkdir("/tmp/locks-peer")==FS_OK && vfs_remove("/tmp/locks-peer",true)==FS_OK);
    assert(kfile_close(11,a)==FS_OK && vfs_write("/tmp/locks/x.txt","bad",3)==FS_BUSY);
    assert(kfile_close(12,b)==FS_OK);
    assert(kfile_open(13,"/","/tmp/locks/x.txt",KFILE_WRITE,&other)==FS_OK);
    assert(kfile_open(14,"/","/tmp/locks/x.txt",KFILE_READ,&a)==FS_BUSY);
    assert(kfile_open(13,"/","/tmp/locks/x.txt",KFILE_WRITE,&a)==FS_BUSY);
    assert(kfile_close_owner(13)==1 && vfs_remove("/tmp/locks/x.txt",false)==FS_OK && vfs_remove("/tmp/locks",true)==FS_OK);
    puts("PASS: shared readers/exclusive writer, canonical alias pins, ancestor/component boundaries and lease release after last close");
}
static void limits_and_rollback(void)
{
    uint64_t handles[KFILE_LIMIT],h;unsigned char data[4096];size_t n;uint32_t offset;
    assert(vfs_write("/tmp/victim.txt","keep",4)==FS_OK);
    for (unsigned i=0;i<KFILE_LIMIT;++i) { assert(kfile_open(20+i,"/","/tmp/base.txt",KFILE_READ,&handles[i])==FS_OK); }
    assert(kfile_count()==KFILE_LIMIT);
    assert(kfile_open(99,"/","/tmp/never.txt",KFILE_WRITE|KFILE_CREATE,&h)==FS_NO_HANDLES);
    assert(vfs_read("/tmp/never.txt",0,data,sizeof(data),&n)==FS_NOT_FOUND);
    assert(kfile_open(99,"/","/tmp/victim.txt",KFILE_WRITE|KFILE_TRUNC,&h)==FS_NO_HANDLES);content("/tmp/victim.txt","keep");
    for (unsigned i=0;i<KFILE_LIMIT;++i) { assert(kfile_close(20+i,handles[i])==FS_OK); }
    assert(kfile_open(0,"/","/tmp/invalid.txt",KFILE_WRITE|KFILE_CREATE,&h)!=FS_OK);
    assert(kfile_open(99,"/","/tmp/invalid.txt",0,&h)!=FS_OK);
    assert(kfile_open(99,"/","/tmp/invalid.txt",KFILE_READ|KFILE_TRUNC,&h)!=FS_OK);
    assert(vfs_read("/tmp/invalid.txt",0,data,sizeof(data),&n)==FS_NOT_FOUND);
    assert(kfile_open(99,"/","/tmp",KFILE_READ,&h)==FS_IS_DIR);
    assert(kfile_open(99,"/","/tmp/base.txt/../victim.txt",KFILE_READ,&h)==FS_NOT_DIR);
    assert(kfile_open(99,"/","/tmp/victim.txt",KFILE_WRITE|KFILE_TRUNC,&h)==FS_OK);content("/tmp/victim.txt","");
    assert(kfile_read(99,h,data,1,&n)==FS_ACCESS && !n);
    memset(data,0x5c,sizeof(data));assert(kfile_write(99,h,data,sizeof(data),&n)==FS_OK && n==sizeof(data));
    assert(kfile_write(99,h,data,1,&n)==FS_FILE_TOO_LARGE && !n && position(99,h)==4096);
    assert(kfile_seek(99,h,0,KFILE_SET,&offset)==FS_OK && offset==0);
    assert(kfile_close_owner(99)==1);
    assert(kfile_open(99,"/","/tmp/oom.txt",KFILE_READ|KFILE_WRITE|KFILE_CREATE,&h)==FS_OK);
    allocation_budget=0;assert(kfile_write(99,h,data,sizeof(data),&n)==FS_NO_MEMORY && !n && position(99,h)==0);allocation_budget=-1;
    content("/tmp/oom.txt","");assert(kfile_write(99,h,"ok",2,&n)==FS_OK && n==2 && kfile_close(99,h)==FS_OK);
    assert(vfs_remove("/tmp/oom.txt",false)==FS_OK && vfs_remove("/tmp/victim.txt",false)==FS_OK);
    puts("PASS: handle exhaustion refuses CREATE/TRUNC without mutation; invalid flags/owners/directories, 4KiB limit and heap-growth failure preserve contents/cursors");
}
static void boot_and_data(void)
{
    uint64_t h;unsigned char bytes[64];size_t n;uint32_t offset;
    assert(kfile_open(30,"/","/boot/CONFIG.TXT",KFILE_READ,&h)==FS_OK);
    assert(kfile_read(30,h,bytes,7,&n)==FS_OK && n==7 && !memcmp(bytes,"# OurOS",7));
    fail_read=true;assert(kfile_read(30,h,bytes,7,&n)==FS_IO && !n);fail_read=false;
    assert(position(30,h)==7 && kfile_close(30,h)==FS_OK);
    assert(kfile_open(30,"/","/boot/CONFIG.TXT",KFILE_WRITE,&h)==FS_READ_ONLY);
    assert(kfile_open(30,"/","/boot/new.txt",KFILE_WRITE|KFILE_CREATE,&h)==FS_READ_ONLY);
    assert(kfile_open(31,"/","/data/owned.txt",KFILE_READ|KFILE_WRITE|KFILE_CREATE,&h)==FS_OK);
    assert(kfile_write(31,h,"persisted",9,&n)==FS_OK && n==9);
    assert(kfile_seek(31,h,0,KFILE_SET,&offset)==FS_OK && offset==0);
    assert(kfile_read(31,h,bytes,9,&n)==FS_OK && n==9 && !memcmp(bytes,"persisted",9));
    assert(vfs_write("/data//owned.txt","bad",3)==FS_BUSY && vfs_remove("/data/owned.txt",false)==FS_BUSY);
    assert(kfile_close_owner(31)==1 && !kfile_count());content("/data/owned.txt","persisted");
    static struct persist_volume independently_mounted;
    assert(persist_mount(&independently_mounted,&disk)==FS_OK);
    assert(persist_read(&independently_mounted,"/owned.txt",0,bytes,sizeof(bytes),&n)==FS_OK && n==9 && !memcmp(bytes,"persisted",9));
    assert(kfile_open(31,"/","/data/owned.txt",KFILE_READ|KFILE_WRITE,&h)==FS_OK);
    assert(kfile_seek(31,h,2,KFILE_SET,&offset)==FS_OK && offset==2);
    uint64_t before=writes;fail_write=true;assert(kfile_write(31,h,"changed",7,&n)==FS_IO && !n);fail_write=false;
    assert(writes==before+1 && position(31,h)==2);content("/data/owned.txt","persisted");
    before=writes;assert(kfile_write(31,h,"retry",5,&n)==FS_IO && !n && writes==before);
    assert(kfile_close_owner(31)==1 && kfile_count()==0);
    puts("PASS: boot read-only handles/read-error cursor preservation, persistent handle write/read/close and independent remount, aliases pinned; failed commit preserves cursor/old RAM view and cleanup still releases owner");
}
int main(int argc,char **argv)
{
    assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f && !fseek(f,0,SEEK_END));long bytes=ftell(f);assert(bytes==134217728);rewind(f);
    media_bytes=(size_t)bytes;media=malloc(media_bytes);assert(media && fread(media,1,media_bytes,f)==media_bytes);fclose(f);
    disk=(struct block_device){.name="memory-copy",.sectors=media_bytes/512,.capacity_known=true,.read=sector_read,.write=sector_write,.flush=flush};
    assert(heap_init() && ramfs_init("host-test")==FS_OK && vfs_mount_boot(&disk)==FS_OK && vfs_mount_data(&disk)==FS_OK);
    cursors_and_modes();leases();limits_and_rollback();boot_and_data();
    assert(vfs_remove("/tmp/base.txt",false)==FS_OK && !kfile_count() && heap_check());
    struct heap_stats heap=heap_get_stats();assert(heap.objects==3 && heap.pages==2 && heap.capacity_bytes==544 && live_pages==2);
    printf("PASS: final handle/lease count zero, RAMFS/heap/page baseline restored; reads=%llu writes=%llu flushes=%llu; only in-memory data-bank sectors changed\n",(unsigned long long)reads,(unsigned long long)writes,(unsigned long long)flushes);
    free(media);return 0;
}
