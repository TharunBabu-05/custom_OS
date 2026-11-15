#include "user.h"
static int same(const char *a,const char *b) { while (*a && *a==*b) { ++a;++b; }return *a==*b; }
static int has(const char *name,const char *expected)
{ char value[128];return uos_getenv(name,value,sizeof(value))==(int64_t)uos_length(expected) && same(value,expected); }
static void word(struct app_args *a,const char *s)
{ a->offset[a->count++]=a->used;do { a->bytes[a->used++]=*s; }while (*s++); }
static void make(struct app_args *a,const char *mode)
{ *a=(struct app_args){0};word(a,"/boot/ENVCHK.ELF");word(a,mode); }
static int exec_mode(const char *mode)
{ struct app_args a;make(&a,mode);(void)uos_exec(&a);return 90; }
static int checks(void)
{
    char out[128],long_value[129],long_name[33];struct app_env_entry e;
    if (!has("PATH","/data/bin:/boot") || !has("HOME","/home/user") || !has("TERM","serial")) { return 1; }
    for (unsigned i=0;i<3;++i) { if (uos_env_entry(i,&e)!=1) { return 2; } }
    unsigned char *raw=(unsigned char *)&e;for (size_t i=0;i<sizeof(e);++i) { raw[i]=0xa5; }
    if (uos_env_entry(3,&e)!=0) { return 3; }for (size_t i=0;i<sizeof(e);++i) { if (raw[i]!=0xa5) { return 3; } }
    if (uos_env_entry(APP_ENV_LIMIT,&e)!=UOS_ERROR || uos_env_entry(0,0)!=UOS_EFAULT ||
        uos_env_entry(0,(struct app_env_entry *)(uintptr_t)"readonly")!=UOS_EFAULT) { return 4; }
    for (unsigned i=0;i<128;++i) { long_value[i]='v'; }long_value[128]=0;
    for (unsigned i=0;i<32;++i) { long_name[i]='N'; }long_name[32]=0;
    if (uos_setenv(long_name,"v")!=UOS_ERROR || uos_setenv("PATH",long_value)!=UOS_ERROR || uos_setenv("1BAD","v")!=UOS_ERROR ||
        uos_setenv("BAD=NAME","v")!=UOS_ERROR || uos_setenv("PATH","bad\033[2J")!=UOS_ERROR || !has("PATH","/data/bin:/boot")) { return 5; }
    long_value[127]=0;long_name[31]=0;
    if (uos_setenv(long_name,long_value) || !has(long_name,long_value) || uos_setenv("EMPTY","") || !has("EMPTY","")) { return 6; }
    out[0]='x';if (uos_getenv(long_name,out,127)!=UOS_ERROR || out[0]!='x' || uos_getenv("ABSENT",out,sizeof(out))!=UOS_ENOENT || out[0]!='x') { return 7; }
    if (uos_call(UOS_SYS_ENV_GET,0,1,(uintptr_t)out,sizeof(out),0,0)!=UOS_EFAULT ||
        uos_call(UOS_SYS_ENV_GET,(uintptr_t)"PATH",4,0,128,0,0)!=UOS_EFAULT ||
        uos_call(UOS_SYS_ENV_GET,(uintptr_t)"PATH",4,(uintptr_t)"readonly",8,0,0)!=UOS_EFAULT ||
        uos_call(UOS_SYS_ENV_SET,(uintptr_t)"PATH",4,0,1,0,0)!=UOS_EFAULT ||
        uos_call(UOS_SYS_ENV_UNSET,UINT64_MAX,4,0,0,0,0)!=UOS_EFAULT ||
        uos_call(UOS_SYS_ENV_SET,(uintptr_t)"NULLVALUE",9,0,0,0,0)) { return 8; }
    char nul[3]={'X',0,'Y'};
    if (uos_call(UOS_SYS_ENV_SET,(uintptr_t)nul,3,(uintptr_t)"v",1,0,0)!=UOS_ERROR ||
        uos_call(UOS_SYS_ENV_SET,(uintptr_t)"PATH",4,(uintptr_t)nul,3,0,0)!=UOS_ERROR || !has("PATH","/data/bin:/boot") ||
        uos_unsetenv(long_name) || uos_unsetenv("EMPTY") || uos_unsetenv("NULLVALUE") || uos_unsetenv("ABSENT")) { return 9; }
    for (unsigned i=0;i<5;++i) { char key[3]={'V',(char)('0'+i),0};if (uos_setenv(key,"old")) { return 10; } }
    if (uos_setenv("EXCESS","v")!=UOS_ENOSPC || uos_setenv("V1","new") || !has("V1","new") || uos_unsetenv("V1") ||
        uos_setenv("REUSED","slot") || uos_getenv("V1",out,sizeof(out))!=UOS_ENOENT) { return 11; }
    for (unsigned i=0;i<5;++i) { char key[3]={'V',(char)('0'+i),0};if (uos_unsetenv(key)) { return 12; } }
    if (uos_unsetenv("REUSED")) { return 13; }
    return 0;
}
int app_main(void)
{
    char mode[32],out[128];int64_t argc=uos_argc();if (argc>1 && uos_arg(1,mode,sizeof(mode))<0) { return 14; }
    if (argc==2 && same(mode,"child")) {
        if (!has("MARKER","snapshot") || uos_setenv("MARKER","child-only") || uos_unsetenv("HOME")) { return 15; }
        return exec_mode("after-child");
    }
    if (argc==2 && same(mode,"after-child")) {
        if (!has("MARKER","child-only") || uos_getenv("HOME",out,sizeof(out))!=UOS_ENOENT || !has("PATH","/data/bin:/boot")) { return 16; }
        return 37;
    }
    if (argc==2 && (same(mode,"left") || same(mode,"right"))) {
        if (!has("MARKER","pipe-parent") || uos_setenv("MARKER",mode)) { return 17; }
        return exec_mode(same(mode,"left") ? "produce" : "consume");
    }
    if (argc==2 && same(mode,"produce")) {
        if (!has("MARKER","left")) { return 18; }unsigned char bytes[512];
        for (unsigned i=0;i<sizeof(bytes);++i) { bytes[i]=(unsigned char)i; }
        for (unsigned i=0;i<3;++i) { if (uos_write(bytes,sizeof(bytes))!=512 || uos_yield() || !has("MARKER","left")) { return 19; } }return 0;
    }
    if (argc==2 && same(mode,"consume")) {
        if (!has("MARKER","right")) { return 20; }unsigned char bytes[512];size_t total=0;
        for (;;) { int64_t n=uos_stdin(bytes,sizeof(bytes));if (n<0) { return 21; }if (!n) { break; }
            for (int64_t i=0;i<n;++i) { if (bytes[i]!=(unsigned char)(total+(size_t)i)) { return 22; } }total+=(size_t)n;
        }
        if (total!=1536 || !has("MARKER","right")) { return 23; }
        uos_puts("PASS: both pipeline environments inherited privately and survived exec with binary streams.\n");return 0;
    }
    if (argc!=1) { return 24; }int result=checks();if (result) { return result; }
    struct app_args child;make(&child,"child");
    if (uos_setenv("MARKER","snapshot")) { return 25; }int64_t pid=uos_spawn(&child,3);
    if (pid<0 || uos_setenv("MARKER","parent-after-spawn") || uos_wait((uint64_t)pid)!=37 ||
        !has("MARKER","parent-after-spawn") || !has("HOME","/home/user")) { return 26; }
    make(&child,"not-a-mode");child.bytes[0]='?';
    if (uos_exec(&child)!=UOS_ERROR || !has("MARKER","parent-after-spawn")) { return 27; }
    struct app_args left,right;make(&left,"left");make(&right,"right");
    if (uos_setenv("MARKER","pipe-parent") || uos_pipeline(&left,&right,3,0) || !has("MARKER","pipe-parent") || uos_unsetenv("MARKER")) { return 28; }
    uos_puts("PASS: checked environment names/values/buffers, full-table recovery, private spawn snapshots, child exec preservation, failed-exec retention and cleanup.\n");return 0;
}
