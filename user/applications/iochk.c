#include "user.h"
static void copy(char *d,const char *s) { do { *d++=*s; }while (*s++); }
static void args_make(struct app_args *a,const char *path,const char *mode)
{
    unsigned char *p=(unsigned char *)a;for (size_t i=0;i<sizeof(*a);++i) { p[i]=0; }
    do { a->bytes[a->used++]=*path; }while (*path++);a->count=1;
    if (mode) { a->offset[a->count++]=a->used;do { a->bytes[a->used++]=*mode; }while (*mode++); }
}
static int nofile(const char *path) { struct uos_entry e;return uos_stat(path,&e)==UOS_ERROR; }
static int samehash(const char *a,const char *b)
{
    unsigned char x[32],y[32];if (uos_filehash(a,x) || uos_filehash(b,y)) { return 0; }
    for (unsigned i=0;i<32;++i) { if (x[i]!=y[i]) { return 0; } }return 1;
}
int app_main(void)
{
    const char *source="/tmp/.io-source",*target="/tmp/.io-target";struct app_args args;struct uos_stdio io={0};
    unsigned char bytes[512];struct uos_entry e;int64_t pid,h;uint64_t held[15];
    if (!nofile(source) || !nofile(target) || uos_stdin(bytes,1)!=UOS_ENOTTY) { return 1; }
    args_make(&args,"/boot/CAT.ELF",0);copy(io.input,source);copy(io.output,target);
    if (uos_call(UOS_SYS_SPAWN_IO,(uintptr_t)&args,2,0,0,0,0)!=UOS_EFAULT) { return 2; }
    io.input[255]='x';for (unsigned i=0;i<255;++i) { io.input[i]='x'; }
    if (uos_spawn_io(&args,2,&io)!=UOS_ERROR || !nofile(target)) { return 3; }copy(io.input,source);
    for (unsigned i=0;i<256;++i) { io.output[i]='x'; }
    if (uos_spawn_io(&args,2,&io)!=UOS_ERROR) { return 27; }copy(io.output,target);
    if (uos_spawn_io(&args,2,&io)!=UOS_ERROR || !nofile(target)) { return 4; }
    if (uos_replace(source,"abc",3)!=3) { return 5; }
    copy(io.output,source);if (uos_spawn_io(&args,2,&io)!=UOS_ERROR) { return 6; }copy(io.output,target);
    args_make(&args,"/boot/CONFIG.TXT",0);if (uos_spawn_io(&args,2,&io)!=UOS_ERROR || !nofile(target)) { return 7; }
    args_make(&args,"/boot/CAT.ELF",0);copy(io.output,"/boot/.io-target");if (uos_spawn_io(&args,2,&io)!=UOS_ERROR) { return 8; }copy(io.output,target);
    h=uos_open(source,UOS_WRITE);if (h<0 || uos_spawn_io(&args,2,&io)!=UOS_ERROR || !nofile(target) || uos_close((uint64_t)h)) { return 9; }
    for (unsigned i=0;i<15;++i) { h=uos_open(source,UOS_READ);if (h<0) { return 28; }held[i]=(uint64_t)h; }
    if (uos_spawn_io(&args,2,&io)!=UOS_ERROR || !nofile(target)) { return 29; }
    for (unsigned i=0;i<15;++i) { if (uos_close(held[i])) { return 30; } }
    pid=uos_spawn_io(&args,2,&io);if (pid<0 || uos_wait((uint64_t)pid) || !samehash(source,target)) { return 10; }
    if (uos_spawn_io(&args,2,&io)!=UOS_ERROR || uos_remove(target,0)) { return 11; }
    if (uos_replace(source,"",0)) { return 12; }
    pid=uos_spawn_io(&args,2,&io);if (pid<0 || uos_wait((uint64_t)pid) || uos_stat(target,&e) || e.bytes || uos_remove(target,0)) { return 13; }
    for (unsigned i=0;i<512;++i) { bytes[i]=(unsigned char)i; }
    h=uos_open(source,UOS_WRITE);if (h<0) { return 14; }
    for (unsigned i=0;i<8;++i) { if (uos_write_file((uint64_t)h,bytes,512)!=512) { return 15; } }
    if (uos_close((uint64_t)h)) { return 16; }
    pid=uos_spawn_io(&args,2,&io);if (pid<0 || uos_wait((uint64_t)pid) || !samehash(source,target) || uos_remove(target,0)) { return 17; }
    args_make(&args,"/boot/IORAW.ELF","overflow");io.input[0]=0;
    pid=uos_spawn_io(&args,2,&io);if (pid<0 || uos_wait((uint64_t)pid)!=74 || !samehash(source,target) || uos_remove(target,0)) { return 18; }
    if (uos_replace(source,"abc",3)!=3) { return 19; }copy(io.input,source);args_make(&args,"/boot/IORAW.ELF","bounds");
    pid=uos_spawn_io(&args,2,&io);if (pid<0 || uos_wait((uint64_t)pid) || uos_stat(target,&e) || e.bytes!=512) { return 20; }
    h=uos_open(target,UOS_READ);if (h<0 || uos_read((uint64_t)h,bytes,512)!=512 || uos_close((uint64_t)h)) { return 21; }
    for (unsigned i=0;i<512;++i) { if (bytes[i]!=(unsigned char)i) { return 22; } }if (uos_remove(target,0)) { return 23; }
    args_make(&args,"/boot/IORAW.ELF","hold");pid=uos_spawn_io(&args,2,&io);
    if (pid<0 || uos_remove(source,0)!=UOS_ERROR || uos_remove(target,0)!=UOS_ERROR || uos_wait((uint64_t)pid) || uos_remove(target,0)) { return 24; }
    args_make(&args,"/boot/IORAW.ELF","fault");pid=uos_spawn_io(&args,2,&io);
    if (pid<0 || uos_wait((uint64_t)pid)!=125 || uos_stat(target,&e) || e.bytes!=512 || uos_remove(target,0)) { return 25; }
    args_make(&args,"/boot/SPIN.ELF",0);pid=uos_spawn_io(&args,1,&io);
    if (pid<0 || uos_wait((uint64_t)pid)!=124 || uos_remove(target,0) || uos_remove(source,0)) { return 26; }
    uos_puts("PASS: stdio binary/EOF/bounds, no overwrite, leases, output limit, fault/timeout and stream cleanup.\n");return 0;
}
