#include "user.h"
static void fill(void *p,size_t n,unsigned char b) { unsigned char *s=p;while (n--) { *s++=b; } }
static int filled(const void *p,size_t n,unsigned char b) { const unsigned char *s=p;while (n--) { if (*s++!=b) { return 0; } }return 1; }
static int same_hash(const unsigned char *a,const unsigned char *b) { for (unsigned i=0;i<32;++i) { if (a[i]!=b[i]) { return 0; } }return 1; }
int app_main(void)
{
    struct uos_fs_info data,info;struct uos_entry e;unsigned char expected[32],actual[32],bytes[32];
    if (uos_statfs("/data",&data) || data.type!=UOS_FS_SNAPSHOT || data.version!=4 || data.max_write_bytes!=16384 || data.node_limit!=16 || data.flags || data.used_nodes>12 ||
        uos_stat("/data/.v4chk",&e)!=UOS_ERROR) {
        uos_puts("V4CHK refused: needs v4 /data, four free nodes and unused /data/.v4chk. Older volumes retain their old limits.\n");return 1;
    }
    uos_puts("V4CHK: 16 KiB binary files, mount limits and checked pointers; wait for PASS.\n");
    if (uos_statfs("/tmp",&info) || info.type!=UOS_FS_RAM || info.max_write_bytes!=4096 || info.flags!=UOS_FS_VOLATILE ||
        uos_statfs("/boot",&info) || info.type!=UOS_FS_FAT || info.max_write_bytes || info.flags!=UOS_FS_READONLY) { return 2; }
    fill(&info,sizeof(info),0xa7);
    if (uos_statfs("/data/.v4chk",&info)!=UOS_ENOENT || !filled(&info,sizeof(info),0xa7) ||
        uos_statfs("/data",NULL)!=UOS_EFAULT || uos_statfs("/data",(void *)(uintptr_t)UINT64_MAX)!=UOS_EFAULT ||
        uos_statfs("/data",(void *)(uintptr_t)"readonly")!=UOS_EFAULT ||
        uos_call4(UOS_SYS_STATFS,0,5,(uintptr_t)&info,0)!=UOS_EFAULT || !filled(&info,sizeof(info),0xa7) ||
        uos_call4(UOS_SYS_STATFS,(uintptr_t)"/data",0,(uintptr_t)&info,0)!=UOS_ERROR || !filled(&info,sizeof(info),0xa7) ||
        uos_call4(UOS_SYS_STATFS,(uintptr_t)"/data",6,(uintptr_t)&info,0)!=UOS_ERROR || !filled(&info,sizeof(info),0xa7)) { return 3; }
    int64_t allocation=uos_alloc_pages(1);if (allocation<0) { return 4; }unsigned char *page=(void *)(uintptr_t)allocation;
    fill(page+4080,16,0x7c);
    if (uos_statfs("/data",(void *)(page+4080))!=UOS_EFAULT || !filled(page+4080,16,0x7c) || uos_free_pages(page) ||
        uos_statfs("/data",(void *)page)!=UOS_EFAULT) { return 5; }
    if (uos_mkdir("/data/.v4chk") || uos_mkdir("/data/.v4chk/sub") ||
        uos_copy("/boot/LARGE.BIN","/data/.v4chk/large")!=16384 || uos_filehash("/boot/LARGE.BIN",expected) ||
        uos_filehash("/data/.v4chk/large",actual) || !same_hash(expected,actual) ||
        uos_copy("/boot/LARGE.BIN","/tmp/.v4chk")!=UOS_ERROR || uos_stat("/tmp/.v4chk",&e)!=UOS_ERROR) { return 6; }
    int64_t handle=uos_open("/data/.v4chk/large",UOS_READ|UOS_WRITE);if (handle<0) { return 7; }uint64_t h=(uint64_t)handle;
    if (uos_seek(h,8188,UOS_SEEK_SET)!=8188 || uos_read(h,bytes,8)!=8) { return 8; }
    for (unsigned i=0;i<8;++i) { if (bytes[i]!=(unsigned char)((8188+i)*29+7)) { return 9; } }
    if (uos_seek(h,8188,UOS_SEEK_SET)!=8188 || uos_write_file(h,"boundary",8)!=8 || uos_seek(h,8188,UOS_SEEK_SET)!=8188 ||
        uos_read(h,bytes,8)!=8) { return 10; }
    for (unsigned i=0;i<8;++i) { if (bytes[i]!=(unsigned char)"boundary"[i]) { return 11; } }
    if (uos_seek(h,-1,UOS_SEEK_END)!=16383 || uos_write_file(h,"xx",2)!=UOS_ERROR || uos_seek(h,0,UOS_SEEK_CUR)!=16383 ||
        uos_write_file(h,(void *)(uintptr_t)UINT64_MAX,1)!=UOS_EFAULT || uos_seek(h,0,UOS_SEEK_CUR)!=16383 ||
        uos_read(h,bytes,2)!=1 || bytes[0]!=(unsigned char)(16383*29+7) || uos_read(h,bytes,1)!=0 ||
        uos_close(h) || uos_rename("/data/.v4chk/large","/data/.v4chk/sub/large") ||
        uos_copy("/boot/LARGE.BIN","/data/.v4chk/stage")!=16384 ||
        uos_publish("/data/.v4chk/stage","/data/.v4chk/sub/large") || uos_stat("/data/.v4chk/stage",&e)!=UOS_ERROR ||
        uos_stat("/data/.v4chk/sub/large",&e) || e.bytes!=16384 ||
        uos_filehash("/data/.v4chk/sub/large",actual) || !same_hash(expected,actual)) { return 12; }
    if (uos_remove("/data/.v4chk/sub/large",0) || uos_remove("/data/.v4chk/sub",1) || uos_remove("/data/.v4chk",1) ||
        uos_statfs("/data",&info) || info.used_nodes!=data.used_nodes || uos_sync()) { return 13; }
    uos_puts("PASS: v4 16384-byte copy/hash, 8 KiB boundary write, EOF/overflow preservation, RAM limit, atomic replacement, mount queries/pointer checks and cleanup.\n");return 0;
}
