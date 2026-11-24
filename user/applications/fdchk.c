#include "user.h"
#include <stdbool.h>

static bool info_is(uint64_t file,uint64_t bytes,uint64_t offset,unsigned flags)
{
    struct uos_file_info info={.flags=UINT32_MAX,.reserved=UINT32_MAX};
    return !uos_fstat(file,&info) && info.bytes==bytes && info.offset==offset &&
           info.flags==flags && !info.reserved;
}

static bool bytes_are(uint64_t file,uint64_t offset,const char *expected,size_t bytes)
{
    char actual[16];
    if (bytes>sizeof(actual) || uos_pread(file,actual,bytes,offset)!=(int64_t)bytes) { return false; }
    for (size_t i=0;i<bytes;++i) { if (actual[i]!=expected[i]) { return false; } }
    return true;
}

static bool exercise(uint64_t file,const char *path)
{
    const unsigned modes=UOS_READ|UOS_WRITE|UOS_APPEND;
    char scratch[8];
    if (!info_is(file,0,0,modes) || uos_write_file(file,"abcdef",6)!=6 ||
        uos_seek(file,2,UOS_SEEK_SET)!=2 ||
        uos_open(path,UOS_WRITE|UOS_CREATE|UOS_EXCL|UOS_TRUNC)!=UOS_EEXIST ||
        !bytes_are(file,1,"bcd",3) || !info_is(file,6,2,modes)) { return false; }

    if (uos_pwrite(file,"XY",2,1)!=2 || !info_is(file,6,2,modes) ||
        uos_write_file(file,"!",1)!=1 || !info_is(file,7,7,modes) ||
        !bytes_are(file,0,"aXYdef!",7)) { return false; }

    if (uos_ftruncate(file,3) || !info_is(file,3,3,modes) ||
        uos_ftruncate(file,8) || !info_is(file,8,3,modes) ||
        !bytes_are(file,0,"aXY\0\0\0\0\0",8) ||
        uos_pwrite(file,"end",3,8)!=3 || !info_is(file,11,3,modes) ||
        !bytes_are(file,8,"end",3)) { return false; }

    return uos_fstat(file,NULL)==UOS_EFAULT &&
           uos_pread(file,NULL,1,0)==UOS_EFAULT &&
           uos_pwrite(file,NULL,1,0)==UOS_EFAULT &&
           uos_pread(file,scratch,513,0)==UOS_ERROR &&
           uos_pwrite(file,scratch,513,0)==UOS_ERROR &&
           uos_pread(file,scratch,1,UINT64_MAX)==0 &&
           uos_pwrite(file,"x",1,12)==UOS_ERROR &&
           uos_ftruncate(file,UINT64_MAX)==UOS_ERROR &&
           info_is(file,11,3,modes) && bytes_are(file,8,"end",3);
}

int app_main(void)
{
    const char *dir="/tmp/fdchk",*path="/tmp/fdchk/sample";
    if (uos_argc()!=1 || uos_mkdir(dir)) {
        uos_puts("FDCHK: scratch directory unavailable\n");return 2;
    }
    int64_t file=uos_open(path,UOS_READ|UOS_WRITE|UOS_CREATE|UOS_EXCL|UOS_APPEND);
    bool good=file>0 && exercise((uint64_t)file,path);
    if (file>0) {
        struct uos_file_info sentinel={.bytes=99,.offset=77,.flags=66,.reserved=55};
        if (uos_close((uint64_t)file) ||
            uos_fstat((uint64_t)file,&sentinel)!=UOS_EBADF || sentinel.bytes!=99 ||
            sentinel.offset!=77 || sentinel.flags!=66 || sentinel.reserved!=55) { good=false; }
        int64_t reader=uos_open(path,UOS_READ);
        if (reader>0) {
            if (!info_is((uint64_t)reader,11,0,UOS_READ) ||
                uos_ftruncate((uint64_t)reader,0)!=UOS_ERROR ||
                uos_pwrite((uint64_t)reader,"x",1,0)!=UOS_ERROR) { good=false; }
            if (uos_close((uint64_t)reader)) { good=false; }
        } else { good=false; }
        if (uos_remove(path,0)) { good=false; }
    }
    if (uos_remove(dir,1)) { good=false; }
    uos_puts(good ? "PASS: exclusive create, append mode, fstat, pread, pwrite and ftruncate.\n" :
                    "FAIL: extended file-handle checks.\n");
    return good ? 0 : 1;
}
