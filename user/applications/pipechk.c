#include "user.h"
static void make(struct app_args *a,const char *path,const char *mode)
{
    unsigned char *p=(unsigned char *)a;for (size_t i=0;i<sizeof(*a);++i) { p[i]=0; }
    do { a->bytes[a->used++]=*path; }while (*path++);a->count=1;
    if (mode) { a->offset[a->count++]=a->used;do { a->bytes[a->used++]=*mode; }while (*mode++); }
}
static void text(char *out,const char *in) { do { *out++=*in; }while (*in++); }
static int missing(const char *path) { struct uos_entry e;return uos_stat(path,&e)==UOS_ERROR; }
static int job(struct app_args *left,struct app_args *right,struct uos_stdio *io,unsigned seconds,int expected,const char *hash)
{
    if (uos_pipeline(left,right,seconds,io)!=expected) { return 0; }
    if (hash) {
        char out[66];int64_t h=uos_open(io->output,UOS_READ);
        if (h<0 || uos_read((uint64_t)h,out,66)!=65 || out[64]!='\n' || uos_close((uint64_t)h)) { return 0; }
        for (unsigned i=0;i<64;++i) { if (out[i]!=hash[i]) { return 0; } }
    }
    return uos_remove(io->output,0)==0;
}
int app_main(void)
{
    struct app_args left,right;struct uos_stdio io={0};const char *output="/tmp/.pipe-out";text(io.output,output);
    if (!missing(output) || !missing("/tmp/.pipe-in")) { return 1; }
    make(&left,"/boot/STREAM.ELF",0);make(&right,"/boot/HASH.ELF",0);
    if (uos_pipeline(0,&right,1,0)!=UOS_EFAULT || uos_pipeline(&left,0,1,0)!=UOS_EFAULT ||
        uos_pipeline(&left,&right,0,0)!=UOS_ERROR || uos_pipeline(&left,&right,31,0)!=UOS_ERROR ||
        uos_pipeline(&left,&right,1,(struct uos_stdio *)1)!=UOS_EFAULT) { return 2; }
    right.count=9;if (uos_pipeline(&left,&right,1,&io)!=UOS_ERROR || !missing(output)) { return 3; }
    make(&right,"/boot/CONFIG.TXT",0);if (uos_pipeline(&left,&right,1,&io)!=UOS_ERROR || !missing(output)) { return 4; }
    make(&left,"\033bad",0);make(&right,"/boot/HASH.ELF",0);
    if (uos_pipeline(&left,&right,1,&io)!=UOS_ERROR || !missing(output)) { return 21; }make(&left,"/boot/STREAM.ELF",0);
    make(&right,"/boot/HASH.ELF",0);text(io.input,"/tmp/.pipe-absent");
    if (uos_pipeline(&left,&right,1,&io)!=UOS_ERROR || !missing(output)) { return 5; }io.input[0]=0;
    if (uos_replace(output,"keep",4)!=4 || uos_pipeline(&left,&right,1,&io)!=UOS_ERROR || uos_remove(output,0)) { return 6; }
    struct app_args hello;make(&hello,"/boot/HELLO.ELF",0);int64_t pid=uos_spawn(&hello,2);
    if (pid<0 || uos_pipeline(&left,&right,1,&io)!=UOS_ERROR || uos_wait((uint64_t)pid)) { return 7; }
    make(&left,"/boot/STREAM.ELF","empty");
    if (!job(&left,&right,&io,2,0,"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855")) { return 8; }
    make(&left,"/boot/STREAM.ELF","small");
    if (!job(&left,&right,&io,2,0,"3e5718fea51a8f3f5baca61c77afab473c1810f8b9db330273b4011ce92c787e")) { return 9; }
    make(&left,"/boot/STREAM.ELF",0);
    for (unsigned i=0;i<3;++i) {
        if (!job(&left,&right,&io,2,0,"a1f259d4365ed4320c377ce26f5c8c56dcdc9a89e7b641bfd8eabfbbeac86654")) { return 10; }
    }
    make(&right,"/boot/STREAM.ELF","drain");if (!job(&left,&right,&io,2,0,0)) { return 11; }
    make(&left,"/boot/STREAM.ELF","late");make(&right,"/boot/HASH.ELF",0);
    if (!job(&left,&right,&io,2,0,"3e5718fea51a8f3f5baca61c77afab473c1810f8b9db330273b4011ce92c787e")) { return 12; }
    make(&left,"/boot/STREAM.ELF","nested");
    if (!job(&left,&right,&io,2,0,"3e5718fea51a8f3f5baca61c77afab473c1810f8b9db330273b4011ce92c787e")) { return 13; }
    if (uos_replace("/tmp/.pipe-in","abc",3)!=3) { return 14; }text(io.input,"/tmp/.pipe-in");make(&left,"/boot/CAT.ELF",0);
    if (!job(&left,&right,&io,2,0,"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") || uos_remove(io.input,0)) { return 15; }io.input[0]=0;
    make(&left,"/boot/STREAM.ELF",0);make(&right,"/boot/STREAM.ELF","exit");if (!job(&left,&right,&io,2,74,0)) { return 16; }
    make(&left,"/boot/IORAW.ELF","fault");make(&right,"/boot/HASH.ELF",0);
    if (!job(&left,&right,&io,2,125,"110009dcee21620b166f3abfecb5eff7a873be729d1c2d53822e7acc5f34eb9b")) { return 17; }
    make(&left,"/boot/STREAM.ELF",0);make(&right,"/boot/FAULT.ELF",0);if (!job(&left,&right,&io,2,125,0)) { return 18; }
    make(&left,"/boot/SPIN.ELF",0);make(&right,"/boot/HASH.ELF",0);if (!job(&left,&right,&io,1,124,0)) { return 19; }
    make(&left,"/boot/STREAM.ELF",0);make(&right,"/boot/SPIN.ELF",0);if (!job(&left,&right,&io,1,124,0)) { return 20; }
    uos_puts("PASS: pipelines binary FIFO/EOF, blocking/wakeup, reuse, invalid setup, broken pipe, faults/timeouts and cleanup.\n");return 0;
}
