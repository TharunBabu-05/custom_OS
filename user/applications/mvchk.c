#include "user.h"
static int hash_same(const char *a,const unsigned char *expected)
{
    unsigned char h[32];if (uos_filehash(a,h)) { return 0; }
    for (unsigned i=0;i<32;++i) { if (h[i]!=expected[i]) { return 0; } }return 1;
}
int app_main(void)
{
    const char *a="/tmp/.mv-src",*b="/tmp/.mv-dst",*d="/data/.mv-src",*e="/data/.mv-dst";
    struct uos_entry entry;unsigned char hash[32],bytes[512];
    if (uos_stat(a,&entry)!=UOS_ERROR || uos_stat(b,&entry)!=UOS_ERROR || uos_stat(d,&entry)!=UOS_ERROR || uos_stat(e,&entry)!=UOS_ERROR) { return 1; }
    /* Reserve two scratch slots up front; refuse capacity exhaustion without
     * altering existing files, including on an old eight-slot v1 volume. */
    if (uos_replace(d,"",0)!=0) { return 20; }
    if (uos_replace(e,"",0)!=0) { (void)uos_remove(d,0);return 20; }
    if (uos_remove(e,0)) { return 20; }
    uos_puts("MVCHK: regular-file rename, no overwrite and leases; wait for PASS.\n");
    for (unsigned i=0;i<512;++i) { bytes[i]=(unsigned char)i; }
    int64_t h=uos_open(a,UOS_READ|UOS_WRITE|UOS_CREATE);if (h<0) { return 2; }
    if (uos_write_file((uint64_t)h,bytes,sizeof(bytes))!=512 || uos_rename(a,b)!=UOS_EBUSY || uos_rename(a,a)) { return 3; }
    if (uos_close((uint64_t)h) || uos_filehash(a,hash) || uos_chdir("/tmp") || uos_rename("./.mv-src","../tmp//.mv-dst") ||
        uos_stat(a,&entry)!=UOS_ERROR || !hash_same(b,hash)) { return 4; }
    if (uos_replace(a,"keep",4)!=4 || uos_rename(b,a)!=UOS_EEXIST || !hash_same(b,hash) ||
        uos_rename(b,"/data/.mv-dst")!=UOS_EXDEV || uos_rename(b,"/boot/MOVE.TXT")!=UOS_EROFS ||
        uos_rename("/tmp","/tmp/else")!=UOS_EISDIR || uos_rename("/tmp/.missing-mv",a)!=UOS_ENOENT) { return 5; }
    if (uos_call(UOS_SYS_RENAME,0,1,(uintptr_t)a,uos_length(a),0,0)!=UOS_EFAULT ||
        uos_call(UOS_SYS_RENAME,(uintptr_t)a,uos_length(a),0,1,0,0)!=UOS_EFAULT ||
        uos_call(UOS_SYS_RENAME,(uintptr_t)a,0,(uintptr_t)b,uos_length(b),0,0)!=UOS_ERROR) { return 6; }
    char bad[3]={'x',0,'y'};
    if (uos_call(UOS_SYS_RENAME,(uintptr_t)bad,3,(uintptr_t)a,uos_length(a),0,0)!=UOS_ERROR || uos_remove(a,0) || uos_remove(b,0)) { return 7; }
    if (uos_replace(d,bytes,sizeof(bytes))!=512 || uos_filehash(d,hash)) { return 8; }
    h=uos_open(d,UOS_READ);if (h<0 || uos_rename(d,e)!=UOS_EBUSY || uos_close((uint64_t)h) || uos_rename(d,e) || !hash_same(e,hash)) { return 9; }
    if (uos_replace(d,"keep",4)!=4 || uos_rename(e,d)!=UOS_EEXIST || !hash_same(e,hash) || uos_remove(d,0) || uos_remove(e,0) || uos_chdir("/")) { return 10; }
    uos_puts("PASS: RAM/data binary rename, relative aliases, no overwrite, open-handle leases, readonly/cross-mount/directory refusal and syscall pointer checks.\n");return 0;
}
