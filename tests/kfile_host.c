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
static unsigned data_min_sectors=DATA_MIN_SECTORS;
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
    (void)context;assert(lba>=260097 && lba<260096+data_min_sectors);++writes;
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
    uint64_t before=writes;fail_write=true;
    if (media[260096*512+8]==5) { assert(kfile_truncate(31,h,10)==FS_IO); }
    else { assert(kfile_write(31,h,"changed",7,&n)==FS_IO && !n); }
    fail_write=false;
    assert(writes==before+1 && position(31,h)==2);content("/data/owned.txt","persisted");
    before=writes;assert(kfile_write(31,h,"retry",5,&n)==FS_IO && !n && writes==before);
    assert(kfile_close_owner(31)==1 && kfile_count()==0);
    assert(vfs_data_failed() && vfs_rename("/","/data/owned.txt","/data/owned.txt")==FS_IO &&
        vfs_publish("/","/data/owned.txt","/data/owned.txt")==FS_IO && writes==before);
    puts("PASS: boot read-only handles/read-error cursor preservation, persistent handle write/read/close and independent remount, aliases pinned; failed commit preserves cursor/old RAM view and cleanup still releases owner");
}

static void extended_handles(void)
{
    uint64_t file,other;
    size_t n;
    unsigned char bytes[16];
    const unsigned create=KFILE_WRITE|KFILE_CREATE|KFILE_EXCL;
    const unsigned flags=KFILE_READ|KFILE_WRITE|KFILE_APPEND;
    for (unsigned mount=0;mount<2;++mount) {
        const char *path=mount ? "/data/extended.txt" : "/tmp/extended.txt";
        const char *alias=mount ? "/data//./extended.txt" : "/tmp//./extended.txt";
        assert(kfile_open(1,"/",path,KFILE_WRITE|KFILE_EXCL,&file)==FS_BAD_ARGUMENT && !file);
        assert(kfile_open(1,"/",path,KFILE_READ|KFILE_APPEND,&file)==FS_BAD_ARGUMENT && !file);
        assert(kfile_open(1,"/",path,create|flags,&file)==FS_OK);
        assert(kfile_write(1,file,"abcdef",6,&n)==FS_OK && n==6);
        uint32_t offset;
        assert(kfile_seek(1,file,2,KFILE_SET,&offset)==FS_OK && offset==2);
        assert(kfile_open(2,"/",alias,create|KFILE_TRUNC,&other)==FS_EXISTS && !other);
        assert(kfile_open(2,"/",alias,KFILE_WRITE|KFILE_APPEND,&other)==FS_BUSY && !other);
        assert(kfile_pread(1,file,bytes,sizeof(bytes),4,&n)==FS_OK && n==2 && !memcmp(bytes,"ef",2));
        assert(kfile_pread(1,file,bytes,sizeof(bytes),UINT64_MAX,&n)==FS_OK && !n);
        assert(kfile_pread(2,file,bytes,1,0,&n)==FS_BAD_HANDLE && !n);
        struct kfile_status info={.bytes=99,.offset=9,.flags=7};
        assert(kfile_stat(2,file,&info)==FS_BAD_HANDLE && info.bytes==99 && info.offset==9 && info.flags==7);
        assert(kfile_stat(1,file,&info)==FS_OK && info.bytes==6 && info.offset==2 && info.flags==flags);
        assert(kfile_pwrite(1,file,"XY",2,1,&n)==FS_OK && n==2 && position(1,file)==2);
        assert(kfile_pwrite(1,file,"!",1,6,&n)==FS_OK && n==1 && position(1,file)==2);
        assert(kfile_pwrite(1,file,"!",1,8,&n)==FS_BAD_ARGUMENT && !n && position(1,file)==2);
        assert(kfile_write(1,file,"Z",1,&n)==FS_OK && n==1 && position(1,file)==8);
        assert(kfile_write(1,file,NULL,0,&n)==FS_OK && !n && position(1,file)==8);
        content(path,"aXYdef!Z");
        if (!mount) {
            allocation_budget=0;
            assert(kfile_truncate(1,file,4096)==FS_NO_MEMORY && position(1,file)==8);
            allocation_budget=-1;
            content(path,"aXYdef!Z");
        }
        assert(kfile_truncate(1,file,3)==FS_OK && position(1,file)==3);
        assert(kfile_truncate(1,file,8)==FS_OK && position(1,file)==3);
        assert(kfile_pread(1,file,bytes,8,0,&n)==FS_OK && n==8 && !memcmp(bytes,"aXY\0\0\0\0\0",8));
        uint64_t before=writes;
        assert(kfile_truncate(1,file,8)==FS_OK && writes==before);
        assert(kfile_truncate(1,file,UINT64_MAX)==FS_FILE_TOO_LARGE && position(1,file)==3);
        assert(kfile_close(1,file)==FS_OK && kfile_stat(1,file,&info)==FS_BAD_HANDLE);
        assert(kfile_open(1,"/",path,KFILE_READ,&file)==FS_OK);
        assert(kfile_truncate(1,file,0)==FS_ACCESS && kfile_pwrite(1,file,"x",1,0,&n)==FS_ACCESS && !n);
        assert(kfile_close(1,file)==FS_OK && vfs_remove(path,false)==FS_OK);
    }
    puts("PASS: exclusive create, append, metadata and positioned I/O preserve ownership/cursors; truncate clamps, zero-fills and rolls back on allocation failure.");
}
static void descriptor_lifecycle(void)
{
    const char *path="/tmp/descriptor";
    uint64_t base,reader,peer,aliases[KFILE_LIMIT-3],refused;
    unsigned flags=KFILE_READ|KFILE_WRITE|KFILE_CREATE|KFILE_EXCL|KFILE_CLOEXEC;
    assert(kfile_open(41,"/",path,flags,&base)==FS_OK);
    assert(kfile_open(42,"/","/tmp/base.txt",KFILE_READ|KFILE_CLOEXEC,&peer)==FS_OK);
    assert(kfile_dup(42,base,0,0,&refused)==FS_BAD_HANDLE && !refused);
    assert(kfile_dup(41,base,KFILE_READ,0,&reader)==FS_OK && reader!=base);
    assert(kfile_dup(41,reader,KFILE_WRITE,0,&refused)==FS_ACCESS && !refused);
    assert(kfile_set_append(41,reader,true)==FS_ACCESS);
    assert(kfile_set_append(42,base,true)==FS_BAD_HANDLE);
    size_t n;
    assert(kfile_write(41,base,"abc",3,&n)==FS_OK && n==3);
    uint32_t offset;
    assert(kfile_seek(41,reader,0,KFILE_SET,&offset)==FS_OK && !offset && !position(41,base));
    assert(kfile_set_append(41,base,true)==FS_OK);
    assert(kfile_write(41,base,"z",1,&n)==FS_OK && n==1 && position(41,reader)==4);
    for (unsigned i=0;i<KFILE_LIMIT-3;++i) {
        assert(kfile_dup(41,base,0,KFILE_CLOEXEC,&aliases[i])==FS_OK);
    }
    assert(kfile_dup(41,base,0,0,&refused)==FS_NO_HANDLES && !refused);
    struct kfile_entry entries[KFILE_LIMIT];
    assert(kfile_list_owner(41,base,reader,entries)==KFILE_LIMIT-3);
    assert(kfile_list_owner(42,0,0,entries)==1 && entries[0].token==peer);
    assert(kfile_close_on_exec(41)==KFILE_LIMIT-2 && kfile_count()==2);
    assert(kfile_close(41,base)==FS_BAD_HANDLE);
    assert(kfile_close(41,aliases[0])==FS_BAD_HANDLE);
    assert(kfile_list_owner(41,0,0,entries)==1 && entries[0].token==reader);
    assert(entries[0].status.offset==4 && entries[0].status.flags==(KFILE_READ|KFILE_APPEND) &&
           !entries[0].descriptor_flags);
    assert(vfs_write(path,"x",1)==FS_BUSY && vfs_remove(path,false)==FS_BUSY);
    assert(kfile_close_owner(41)==1 && kfile_close_owner(42)==1 && !kfile_count());
    content(path,"abcz");
    assert(vfs_remove(path,false)==FS_OK);
    puts("PASS: shared descriptors, restricted rights, append, exec cleanup, owner visibility and slot exhaustion.");
}
static void rename_leases(void)
{
    assert(vfs_mkdir("/tmp/mv-dir")==FS_OK && vfs_write("/tmp/mv-src","unchanged",9)==FS_OK);
    const char *pointer;size_t length;assert(ramfs_read(0,"/tmp/mv-src",&pointer,&length)==FS_OK);struct heap_stats before=heap_get_stats();
    uint64_t h;assert(kfile_open(1,"/","/tmp/mv-src",KFILE_READ,&h)==FS_OK);
    assert(vfs_rename("/tmp","./mv-src","mv-dir/dest")==FS_BUSY);
    assert(vfs_rename("/tmp","./mv-src","../tmp//mv-src")==FS_OK);
    assert(kfile_close(1,h)==FS_OK && vfs_rename("/tmp","mv-src","mv-dir/dest")==FS_OK);
    const char *after;assert(ramfs_read(0,"/tmp/mv-dir/dest",&after,&length)==FS_OK && pointer==after && length==9);content("/tmp/mv-dir/dest","unchanged");
    struct heap_stats now=heap_get_stats();assert(before.objects==now.objects && before.pages==now.pages && before.capacity_bytes==now.capacity_bytes);
    assert(vfs_write("/tmp/mv-src","keep",4)==FS_OK && vfs_rename("/","/tmp/mv-dir/dest","/tmp/mv-src")==FS_EXISTS);content("/tmp/mv-src","keep");
    assert(vfs_rename("/","/tmp/mv-dir","/tmp/other")==FS_IS_DIR);
    assert(vfs_rename("/","/tmp/mv-src","/data/mv-dest")==FS_CROSS_DEVICE);
    assert(vfs_rename("/","/tmp/mv-src","/boot/MOVED.TXT")==FS_READ_ONLY);
    assert(vfs_rename("/","/boot/CONFIG.TXT","/boot/MOVED.TXT")==FS_READ_ONLY);
    assert(vfs_rename("/","/tmp/mv-src","/tmp/missing/dest")==FS_NOT_FOUND);
    assert(vfs_rename("/","/tmp/mv-src","/tmp/next/")==FS_NOT_DIR);
    assert(vfs_remove("/tmp/mv-dir/dest",false)==FS_OK && vfs_remove("/tmp/mv-src",false)==FS_OK && vfs_remove("/tmp/mv-dir",true)==FS_OK);
    assert(vfs_write("/data/mv-src","persistent",10)==FS_OK && kfile_open(1,"/","/data/mv-src",KFILE_READ,&h)==FS_OK);
    assert(vfs_rename("/data","./mv-src","next")==FS_BUSY && kfile_close(1,h)==FS_OK);
    assert(vfs_rename("/data","./mv-src","next")==FS_OK);content("/data/next","persistent");
    assert(vfs_rename("/data","next","/tmp/next")==FS_CROSS_DEVICE && vfs_remove("/data/next",false)==FS_OK);
    assert(!kfile_count() && heap_check());
    puts("PASS: RAM rename preserves buffer/allocation; canonical source leases, same-path no-op, no overwrite, directory/readonly/cross-mount/missing-parent refusal and persistent leases.");
}
static void directory_leases(void)
{
    if (media[260096*512+8]<3) { return; }
    assert(vfs_mkdir("/data/dir-a")==FS_OK && vfs_mkdir("/data/dir-b")==FS_OK && vfs_write("/data/dir-a/file","nested",6)==FS_OK);
    uint64_t h;assert(kfile_open(1,"/data/dir-a","./file",KFILE_READ,&h)==FS_OK);
    assert(vfs_rename("/","/data/dir-a/../dir-a/file","/data/dir-b/file")==FS_BUSY && vfs_remove("/data/dir-a",true)==FS_BUSY);
    assert(kfile_close(1,h)==FS_OK && vfs_rename("/data/dir-a","file","../dir-b/file")==FS_OK && vfs_remove("/data/dir-a",true)==FS_OK);
    content("/data/dir-b/file","nested");assert(kfile_open(1,"/","/data/dir-b/file",KFILE_READ,&h)==FS_OK);
    assert(vfs_remove("/data/dir-b/file",false)==FS_BUSY && vfs_mkdir("/data/dir-b/file/child")==FS_NOT_DIR);
    assert(kfile_close(1,h)==FS_OK && vfs_remove("/data/dir-b/file",false)==FS_OK && vfs_remove("/data/dir-b",true)==FS_OK);
    assert(!kfile_count());puts("PASS: persistent directory ancestor leases, nested canonical aliases, cross-directory file moves and type checks; all temporary nodes/handles removed.");
}
static void replacement_leases(void)
{
    struct heap_stats base=heap_get_stats();uint64_t h;bool dir;char canonical[256];
    for (unsigned mount=0;mount<2;++mount) {
        const char *a=mount ? "/data/.pub-src" : "/tmp/.pub-src",*b=mount ? "/data/.pub-dst" : "/tmp/.pub-dst";
        assert(vfs_write(a,"new\0body",8)==FS_OK && vfs_write(b,"old body",8)==FS_OK);
        assert(kfile_open(1,"/",a,KFILE_READ,&h)==FS_OK && vfs_publish("/",a,b)==FS_BUSY && kfile_close(1,h)==FS_OK);
        assert(kfile_open(2,"/",b,KFILE_READ|KFILE_WRITE,&h)==FS_OK && vfs_publish("/",a,b)==FS_BUSY);
        assert(vfs_publish("/",b,b)==FS_OK && kfile_close(2,h)==FS_OK);
        assert(vfs_rename("/",a,b)==FS_EXISTS);content(b,"old body");
        const char *pointer=NULL;size_t n=0;if (!mount) { assert(ramfs_read(0,a,&pointer,&n)==FS_OK); }
        assert(vfs_publish("/",a,b)==FS_OK && vfs_resolve("/",a,canonical,&dir)==FS_NOT_FOUND);
        unsigned char out[16];assert(vfs_read(b,0,out,sizeof(out),&n)==FS_OK && n==8 && !memcmp(out,"new\0body",8));
        if (!mount) { const char *after;assert(ramfs_read(0,b,&after,&n)==FS_OK && after==pointer); }
        assert(vfs_write(a,NULL,0)==FS_OK && vfs_publish("/",a,b)==FS_OK);content(b,"");
        assert(vfs_remove(b,false)==FS_OK);
    }
    assert(vfs_write("/tmp/.pub-src","keep",4)==FS_OK);
    assert(vfs_publish("/","/tmp/.pub-src","/data/new")==FS_CROSS_DEVICE);
    assert(vfs_publish("/","/tmp/.pub-src","/boot/CONFIG.TXT")==FS_READ_ONLY);
    assert(vfs_publish("/","/boot/CONFIG.TXT","/tmp/.pub-src")==FS_READ_ONLY);
    assert(vfs_publish("/","/tmp/.pub-src","/tmp")==FS_IS_DIR);
    assert(vfs_publish("/","/tmp","/tmp/.pub-src")==FS_IS_DIR);
    assert(vfs_publish("/","/tmp/.pub-src","/tmp/missing/target")==FS_NOT_FOUND);
    assert(vfs_publish("/","/tmp/.pub-src","/tmp/target/")==FS_NOT_DIR);content("/tmp/.pub-src","keep");
    assert(vfs_write("/tmp/.pub-dst","old",3)==FS_OK);
    unsigned filled=0;
    for (;filled<RAMFS_NODES;++filled) {
        char path[40];snprintf(path,sizeof(path),"/tmp/.pub-fill%u",filled);
        enum fs_result r=vfs_touch(path);if (r==FS_NO_NODES) { break; }assert(r==FS_OK);
    }
    assert(filled<RAMFS_NODES && vfs_publish("/tmp","./.pub-src","../tmp//.pub-dst")==FS_OK);content("/tmp/.pub-dst","keep");
    for (unsigned i=0;i<filled;++i) { char path[40];snprintf(path,sizeof(path),"/tmp/.pub-fill%u",i);assert(vfs_remove(path,false)==FS_OK); }
    assert(vfs_remove("/tmp/.pub-dst",false)==FS_OK && !kfile_count() && heap_check());
    struct heap_stats end=heap_get_stats();assert(base.objects==end.objects && base.pages==end.pages && base.capacity_bytes==end.capacity_bytes);
    puts("PASS: atomic replacement retains RAM buffer, frees old target, refuses both source/target leases and readonly/cross-mount/directory/bad paths; no handle or heap leak.");
}
static void mounted_capacity(void)
{
    struct vfs_volume_info info,unchanged;assert(vfs_volume_info("/data",&info)==FS_OK);
    unsigned version=media[260096*512+8],limit=version==5 ? DATA_V5_FILE_BYTES : version==4 ? 16384 : version==1 ? 4096 : 8192;
    assert(info.type==VFS_SNAPSHOT && info.version==version && info.max_write_bytes==limit && !info.flags && !info.used);
    unchanged=info;assert(vfs_volume_info("/missing",&info)==FS_NOT_FOUND && !memcmp(&info,&unchanged,sizeof(info)));
    assert(vfs_volume_info("/tmp",&info)==FS_OK && info.type==VFS_RAM && info.flags==VFS_VOLATILE && info.max_write_bytes==4096);
    assert(vfs_volume_info("/boot/CONFIG.TXT",&info)==FS_OK && info.type==VFS_FAT && info.flags==VFS_READONLY && !info.max_write_bytes);
    static unsigned char bytes[VFS_FILE_MAX+1],actual[VFS_FILE_MAX];for (unsigned i=0;i<limit;++i) { bytes[i]=(unsigned char)(i*29+7); }
    assert(vfs_write("/data/capacity",bytes,limit)==FS_OK && vfs_write("/data/capacity",bytes,limit+1)==FS_FILE_TOO_LARGE);
    size_t n;assert(vfs_read("/data/capacity",0,actual,sizeof(actual),&n)==FS_OK && n==limit && !memcmp(actual,bytes,n));
    uint64_t h;uint32_t offset;assert(kfile_open(1,"/","/data/capacity",KFILE_READ|KFILE_WRITE,&h)==FS_OK);
    assert(kfile_seek(1,h,limit-4,KFILE_SET,&offset)==FS_OK && offset==limit-4 && kfile_write(1,h,"tail",4,&n)==FS_OK && n==4);
    assert(kfile_write(1,h,"x",1,&n)==FS_FILE_TOO_LARGE && !n && position(1,h)==limit);
    if (limit==16384) {
        assert(kfile_seek(1,h,8188,KFILE_SET,&offset)==FS_OK && kfile_write(1,h,"boundary",8,&n)==FS_OK && n==8);
        assert(kfile_seek(1,h,8188,KFILE_SET,&offset)==FS_OK && kfile_read(1,h,actual,8,&n)==FS_OK && n==8 && !memcmp(actual,"boundary",8));
    }
    assert(kfile_close(1,h)==FS_OK && vfs_remove("/data/capacity",false)==FS_OK);
    assert(vfs_volume_info("/data",&info)==FS_OK && !info.used && !kfile_count());
    puts("PASS: mount query/output preservation, per-format whole-file bounds, owned-handle tail/boundary writes and overflow cursor preservation.");
}
int main(int argc,char **argv)
{
    assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f && !fseek(f,0,SEEK_END));long bytes=ftell(f);assert(bytes==134217728);rewind(f);
    media_bytes=(size_t)bytes;media=malloc(media_bytes);assert(media && fread(media,1,media_bytes,f)==media_bytes);fclose(f);
    const unsigned char *super=media+260096*512;
    unsigned bank=(unsigned)super[20]|(unsigned)super[21]<<8|(unsigned)super[22]<<16|(unsigned)super[23]<<24;
    if (super[8]==5) { assert(bank==DATA_V5_BANK_SECTORS);data_min_sectors=DATA_V5_SECTORS; }
    else {
        assert(bank==DATA_BANK_SECTORS || bank==DATA_V2_BANK_SECTORS || bank==DATA_V4_BANK_SECTORS);
        data_min_sectors=1+2*bank;
    }
    disk=(struct block_device){.name="memory-copy",.sectors=media_bytes/512,.capacity_known=true,.read=sector_read,.write=sector_write,.flush=flush};
    assert(heap_init() && ramfs_init("host-test")==FS_OK && vfs_mount_boot(&disk)==FS_OK && vfs_mount_data(&disk)==FS_OK);
    mounted_capacity();cursors_and_modes();leases();limits_and_rollback();rename_leases();directory_leases();replacement_leases();
    extended_handles();descriptor_lifecycle();boot_and_data();
    assert(vfs_remove("/tmp/base.txt",false)==FS_OK && !kfile_count() && heap_check());
    struct heap_stats heap=heap_get_stats();assert(heap.objects==3 && heap.pages==2 && heap.capacity_bytes==544 && live_pages==2);
    printf("PASS: final handle/lease count zero, RAMFS/heap/page baseline restored; reads=%llu writes=%llu flushes=%llu; only in-memory data-bank sectors changed\n",(unsigned long long)reads,(unsigned long long)writes,(unsigned long long)flushes);
    free(media);return 0;
}
