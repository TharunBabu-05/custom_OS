#include "user.h"
#include "user_mmu.h"
static int same(const char *a,const char *b) { while (*a && *a==*b) { ++a;++b; }return *a==*b; }
static void word(struct app_args *a,const char *s)
{ a->offset[a->count++]=a->used;do { a->bytes[a->used++]=*s; }while (*s++); }
static void make(struct app_args *a,const char *path)
{ *a=(struct app_args){0};word(a,path); }
static void number(char text[24],uint64_t v)
{
    unsigned n=0;do { text[n++]=(char)('0'+v%10);v/=10; }while (v);
    for (unsigned i=0;i<n/2;++i) { char c=text[i];text[i]=text[n-i-1];text[n-i-1]=c; }text[n]=0;
}
static uint64_t value(const char *s) { uint64_t v=0;while (*s>='0' && *s<='9') { v=v*10+(unsigned)(*s++-'0'); }return v; }
static int chain(uint64_t pid,uint64_t handle,unsigned remaining,unsigned code)
{
    struct app_args args;char digits[24];make(&args,"/boot/EXECCHK.ELF");word(&args,"after");
    number(digits,pid);word(&args,digits);number(digits,handle);word(&args,digits);
    number(digits,remaining);word(&args,digits);word(&args,"hello world");word(&args,"");number(digits,code);word(&args,digits);
    /* A quiet bottom-stack location must be freshly zeroed in each image. */
    *(volatile unsigned char *)(uintptr_t)(USER_STACK_BASE+128)=0x5a;
    (void)uos_exec(&args);return 99;
}
int app_main(void)
{
    char mode[24],text[256],digits[24];int64_t argc=uos_argc();struct app_args args;
    if (argc>1 && uos_arg(1,mode,sizeof(mode))<0) { return 1; }
    if (argc==2 && same(mode,"parent")) {
        for (unsigned i=0;i<4;++i) {
            make(&args,"/boot/EXECCHK.ELF");word(&args,"child");int64_t pid=uos_spawn(&args,5);
            if (pid<0 || uos_wait((uint64_t)pid)!=37 || uos_wait((uint64_t)pid)!=UOS_ERROR) { return 2; }
        }
        make(&args,"/boot/EXEC.ELF");word(&args,"/boot/FAULT.ELF");int64_t pid=uos_spawn(&args,2);
        if (pid<0 || uos_wait((uint64_t)pid)!=125) { return 3; }
        make(&args,"/boot/EXECCHK.ELF");word(&args,"loop");pid=uos_spawn(&args,1);
        if (pid<0 || uos_wait((uint64_t)pid)!=124) { return 4; }
        uos_puts("PASS: exec preserves child identity/wait, repeated slot reuse, fault containment and original timeout across image changes.\n");return 0;
    }
    if (argc==2 && same(mode,"loop")) {
        make(&args,"/boot/EXECCHK.ELF");word(&args,"loop");(void)uos_exec(&args);return 98;
    }
    if (argc==8 && same(mode,"after")) {
        if (uos_arg(2,digits,sizeof(digits))<0 || value(digits)!=(uint64_t)uos_getpid()) { return 5; }
        if (uos_arg(3,digits,sizeof(digits))<0) { return 6; }uint64_t handle=value(digits);
        if (uos_arg(4,digits,sizeof(digits))<0) { return 7; }unsigned remaining=(unsigned)value(digits);
        if (uos_arg(5,text,sizeof(text))!=11 || !same(text,"hello world") || uos_arg(6,text,sizeof(text))!=0) { return 8; }
        if (uos_arg(7,digits,sizeof(digits))<0) { return 9; }unsigned code=(unsigned)value(digits);
        if (*(volatile unsigned char *)(uintptr_t)(USER_STACK_BASE+128)!=0 || uos_getcwd(text,sizeof(text))!=4 || !same(text,"/etc")) { return 10; }
        if (uos_read(handle,text,4)!=4 || text[0]!='r' || text[1]!='o' || text[2]!='s' || text[3]!='\n' || uos_seek(handle,2,UOS_SEEK_SET)!=2) { return 11; }
        if (remaining>1) { if (uos_sleep_ms(1)) { return 12; }return chain((uint64_t)uos_getpid(),handle,remaining-1,code); }
        if (uos_close(handle) || uos_chdir("/")) { return 13; }
        uos_puts("PASS: exec retained PID/cwd/handle cursor and arguments; eight images used fresh stacks and preserved resources.\n");return (int)code;
    }
    if (argc!=1 && !(argc==2 && same(mode,"child"))) { return 14; }
    unsigned code=argc==2 ? 37 : 0;uint64_t pid=(uint64_t)uos_getpid();volatile unsigned char canary[64];
    for (unsigned i=0;i<sizeof(canary);++i) { canary[i]=(unsigned char)(i^0xa5); }
    if (uos_chdir("/etc")) { return 15; }int64_t h=uos_open("hostname",UOS_READ);
    if (h<0 || uos_read((uint64_t)h,text,2)!=2 || text[0]!='o' || text[1]!='u') { return 16; }
    if (uos_exec((const struct app_args *)0)!=UOS_EFAULT || uos_exec((const struct app_args *)(uintptr_t)UINT64_MAX)!=UOS_EFAULT) { return 17; }
    make(&args,"/boot/HELLO.ELF");args.count=9;if (uos_exec(&args)!=UOS_ERROR) { return 18; }
    make(&args,"/boot/HELLO.ELF");args.offset[0]=1;if (uos_exec(&args)!=UOS_ERROR) { return 19; }
    make(&args,"/boot/HELLO.ELF");args.bytes[args.used-1]='x';if (uos_exec(&args)!=UOS_ERROR) { return 20; }
    const char *bad[]={"/boot/CONFIG.TXT","/boot/exec-absent.elf","/etc"};
    for (unsigned i=0;i<3;++i) { make(&args,bad[i]);if (uos_exec(&args)!=UOS_ERROR) { return 21; } }
    if (code==0) {
        make(&args,"/boot/HELLO.ELF");int64_t child=uos_spawn(&args,2);
        if (child>=0 && (uos_exec(&args)!=UOS_EBUSY || uos_wait((uint64_t)child))) { return 22; }
    }
    if ((uint64_t)uos_getpid()!=pid || uos_getcwd(text,sizeof(text))!=4 || !same(text,"/etc")) { return 23; }
    for (unsigned i=0;i<sizeof(canary);++i) { if (canary[i]!=(unsigned char)(i^0xa5)) { return 24; } }
    uos_puts("PASS: failed exec retained the original process, stack and open handle; invalid buffers/argv/ELF/paths refused.\n");
    return chain(pid,(uint64_t)h,8,code);
}
