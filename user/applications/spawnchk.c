#include "user.h"
static void make(struct app_args *a,const char *path)
{
    unsigned char *p=(unsigned char *)a;for (size_t i=0;i<sizeof(*a);++i) { p[i]=0; }
    do { a->bytes[a->used++]=*path; }while (*path++);a->count=1;
}
static void word(struct app_args *a,const char *s)
{ a->offset[a->count++]=a->used;do { a->bytes[a->used++]=*s; }while (*s++); }
static int same(const char *a,const char *b) { while (*a && *a==*b) { ++a;++b; }return *a==*b; }
int app_main(void)
{
    struct app_args args;char text[256],digits[24];
    if (uos_call(UOS_SYS_SPAWN,0,1,0,0,0,0)!=UOS_EFAULT || uos_wait((uint64_t)uos_getpid())!=UOS_ERROR) { return 1; }
    make(&args,"/boot/HELLO.ELF");args.offset[0]=1;
    if (uos_spawn(&args,1)!=UOS_ERROR) { return 2; }args.offset[0]=0;
    args.count=9;if (uos_spawn(&args,1)!=UOS_ERROR) { return 3; }
    make(&args,"/boot/HELLO.ELF");args.bytes[--args.used]='x';if (uos_spawn(&args,1)!=UOS_ERROR) { return 4; }
    make(&args,"/boot/HELLO.ELF");if (uos_spawn(&args,0)!=UOS_ERROR || uos_spawn(&args,31)!=UOS_ERROR) { return 5; }
    make(&args,"/boot/CONFIG.TXT");if (uos_spawn(&args,1)!=UOS_ERROR) { return 6; }
    make(&args,"/boot/ABSENT.ELF");if (uos_spawn(&args,1)!=UOS_ERROR) { return 7; }
    if (uos_chdir("/etc")) { return 8; }
    int64_t handle=uos_open("hostname",UOS_READ);if (handle<0 || uos_read((uint64_t)handle,text,2)!=2) { return 9; }
    uint64_t v=(uint64_t)handle;unsigned n=0;do { digits[n++]=(char)('0'+v%10);v/=10; }while (v);
    for (unsigned i=0;i<n/2;++i) { char c=digits[i];digits[i]=digits[n-i-1];digits[n-i-1]=c; }digits[n]=0;
    make(&args,"../boot/CHILDCHK.ELF");word(&args,digits);word(&args,"hello world");word(&args,"");
    int64_t pid=uos_spawn(&args,2);if (pid<0 || uos_spawn(&args,2)!=UOS_ERROR || uos_wait((uint64_t)pid+1)!=UOS_ERROR || uos_wait((uint64_t)pid)!=37) { return 10; }
    if (uos_wait((uint64_t)pid)!=UOS_ERROR || uos_getcwd(text,sizeof(text))!=4 || !same(text,"/etc") || uos_read((uint64_t)handle,text,4)!=4 || uos_close((uint64_t)handle)) { return 11; }
    int64_t previous=pid;
    for (unsigned i=0;i<8;++i) {
        make(&args,"/boot/LEAK.ELF");pid=uos_spawn(&args,2);
        if (i==0 && uos_sleep_ms(50)) { return 15; }
        if (pid<=previous || uos_wait((uint64_t)previous)!=UOS_ERROR || uos_wait((uint64_t)pid)!=0) { return 12; }previous=pid;
    }
    make(&args,"/boot/FAULT.ELF");pid=uos_spawn(&args,2);if (pid<0 || uos_wait((uint64_t)pid)!=125) { return 13; }
    make(&args,"/boot/SPIN.ELF");pid=uos_spawn(&args,1);if (pid<0 || uos_wait((uint64_t)pid)!=124) { return 14; }
    uos_puts("PASS: spawn/wait args, inherited private cwd, handle isolation, stale IDs, slot reuse, fault/timeout and child cleanup.\n");return 0;
}
