#include "user.h"
static int absent(const char *path) { struct uos_entry e;return uos_stat(path,&e)==UOS_ERROR; }
int app_main(void)
{
    struct uos_entry e;unsigned char bytes[512],first[32],second[32];char path[32]="/data/slot00";
    if (uos_readdir("/data",0,&e)!=0 || !absent("/tmp/.data2-limit") || !absent("/tmp/.data2-copy")) {
        uos_puts("DATACAP refused: requires empty /data and unused /tmp fixtures.\n");return 1;
    }
    for (unsigned i=0;i<512;++i) { bytes[i]=(unsigned char)i; }
    uos_puts("DATACAP: checking 8 KiB file/copy and 16 persistent slots; wait for PASS.\n");
    int64_t h=uos_open("/data/limit.bin",UOS_READ|UOS_WRITE|UOS_CREATE);if (h<0) { return 2; }
    for (unsigned i=0;i<16;++i) { if (uos_write_file((uint64_t)h,bytes,512)!=512) { return 3; } }
    if (uos_write_file((uint64_t)h,bytes,1)!=UOS_ERROR || uos_seek((uint64_t)h,0,UOS_SEEK_SET)) { return 4; }
    for (unsigned i=0;i<16;++i) {
        if (uos_read((uint64_t)h,bytes,512)!=512) { return 5; }
        for (unsigned j=0;j<512;++j) { if (bytes[j]!=(unsigned char)j) { return 6; } }
    }
    if (uos_read((uint64_t)h,bytes,1)!=0 || uos_close((uint64_t)h) || uos_stat("/data/limit.bin",&e) || e.bytes!=8192) { return 7; }
    if (uos_copy("/data/limit.bin","/data/clone.bin")!=8192 || uos_filehash("/data/limit.bin",first) || uos_filehash("/data/clone.bin",second)) { return 8; }
    for (unsigned i=0;i<32;++i) { if (first[i]!=second[i]) { return 9; } }
    if (uos_remove("/data/clone.bin",0) || uos_copy("/data/limit.bin","/tmp/.data2-copy")!=UOS_ERROR || !absent("/tmp/.data2-copy")) { return 10; }
    for (unsigned i=0;i<15;++i) {
        path[10]=(char)('0'+i/10);path[11]=(char)('0'+i%10);path[12]=0;
        if (uos_replace(path,"",0)) { return 11; }
    }
    if (uos_replace("/data/overflow","x",1)!=UOS_ERROR || !absent("/data/overflow")) { return 12; }
    for (unsigned i=0;i<15;++i) { path[10]=(char)('0'+i/10);path[11]=(char)('0'+i%10);if (uos_remove(path,0)) { return 13; } }
    if (uos_remove("/data/limit.bin",0)) { return 14; }
    h=uos_open("/tmp/.data2-limit",UOS_WRITE|UOS_CREATE);if (h<0) { return 15; }
    for (unsigned i=0;i<8;++i) { if (uos_write_file((uint64_t)h,bytes,512)!=512) { return 16; } }
    if (uos_write_file((uint64_t)h,bytes,1)!=UOS_ERROR || uos_close((uint64_t)h) || uos_remove("/tmp/.data2-limit",0) || uos_sync()) { return 17; }
    uos_puts("PASS: v2 8192-byte binary file/copy/EOF, 16 slots, overflow preservation, RAM 4096-byte bound and cleanup.\n");return 0;
}
