#include "user.h"
static int same_hash(const char *p,const unsigned char h[32])
{
    unsigned char actual[32];if (uos_filehash(p,actual)) { return 0; }
    for (unsigned i=0;i<32;++i) { if (h[i]!=actual[i]) { return 0; } }return 1;
}
int app_main(void)
{
    const char *paths[]={"/tmp/.replace-src","/tmp/.replace-dst","/data/.replace-src","/data/.replace-dst"};
    struct uos_entry entry;unsigned char bytes[512],hash[32];
    for (unsigned i=0;i<4;++i) { if (uos_stat(paths[i],&entry)!=UOS_ERROR) { return 1; } }
    /* Refuse unavailable capacity before the test starts changing contents. */
    if (uos_replace(paths[2],"",0)) { return 20; }
    if (uos_replace(paths[3],"",0)) { (void)uos_remove(paths[2],0);return 20; }
    uos_puts("REPLCHK: atomic publication, consumed source and target leases; wait for PASS.\n");
    for (unsigned i=0;i<512;++i) { bytes[i]=(unsigned char)(i*17); }
    for (unsigned mount=0;mount<2;++mount) {
        const char *a=paths[mount*2],*b=paths[mount*2+1];
        if (uos_replace(a,bytes,sizeof(bytes))!=512 || uos_replace(b,"old",3)!=3 || uos_filehash(a,hash)) { return 2; }
        int64_t h=uos_open(a,UOS_READ);if (h<0 || uos_publish(a,b)!=UOS_EBUSY || uos_close((uint64_t)h)) { return 3; }
        h=uos_open(b,UOS_READ|UOS_WRITE);if (h<0 || uos_publish(a,b)!=UOS_EBUSY || uos_publish(b,b) || uos_close((uint64_t)h)) { return 4; }
        if (uos_rename(a,b)!=UOS_EEXIST || uos_publish(a,b) || uos_stat(a,&entry)!=UOS_ERROR || !same_hash(b,hash)) { return 5; }
        if (uos_replace(a,"",0) || uos_publish(a,b) || uos_stat(b,&entry) || entry.bytes || uos_stat(a,&entry)!=UOS_ERROR || uos_remove(b,0)) { return 6; }
    }
    const char *a=paths[0],*b=paths[1];
    if (uos_replace(a,"preserved",9)!=9 || uos_publish(a,paths[2])!=UOS_EXDEV || uos_publish(a,"/boot/CONFIG.TXT")!=UOS_EROFS ||
        uos_publish("/boot/CONFIG.TXT",a)!=UOS_EROFS || uos_publish(a,"/tmp")!=UOS_EISDIR || uos_publish("/tmp",a)!=UOS_EISDIR ||
        uos_publish("/tmp/.replace-missing",a)!=UOS_ENOENT) { return 7; }
    if (uos_call(UOS_SYS_PUBLISH,0,1,(uintptr_t)a,uos_length(a),0,0)!=UOS_EFAULT ||
        uos_call(UOS_SYS_PUBLISH,(uintptr_t)a,uos_length(a),0,1,0,0)!=UOS_EFAULT ||
        uos_call(UOS_SYS_PUBLISH,(uintptr_t)a,0,(uintptr_t)b,uos_length(b),0,0)!=UOS_ERROR) { return 8; }
    char bad[3]={'x',0,'y'};
    if (uos_call(UOS_SYS_PUBLISH,(uintptr_t)bad,3,(uintptr_t)a,uos_length(a),0,0)!=UOS_ERROR ||
        uos_chdir("/tmp") || uos_publish("./.replace-src","../tmp//.replace-dst") || uos_remove(b,0) || uos_chdir("/")) { return 9; }
    uos_puts("PASS: RAM/data atomic replacement, binary/empty contents, consumed source, both leases, no-overwrite mv, aliases, readonly/cross-mount/directory refusal and syscall pointer checks.\n");return 0;
}
