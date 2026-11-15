#include "user.h"
static int same(const char *a,const char *b) { while (*a && *a==*b) { ++a;++b; }return *a==*b; }
static int has(const char *key,const char *expected)
{ char value[128];return uos_getenv(key,value,sizeof(value))==(int64_t)uos_length(expected) && same(value,expected); }
static int hash_same(const char *path,const unsigned char expected[32])
{ unsigned char hash[32];if (uos_filehash(path,hash)) { return 0; }for (unsigned i=0;i<32;++i) { if (hash[i]!=expected[i]) { return 0; } }return 1; }
int app_main(void)
{
    const char *path="/data/.profile-check",*bad="/data/.profile-bad";struct uos_entry e;char out[128];unsigned char hash[32];
    if (uos_stat(path,&e)!=UOS_ERROR || uos_stat(bad,&e)!=UOS_ERROR) { return 1; }
    if (uos_setenv("MARKER","saved value") || uos_profile(UOS_PROFILE_SAVE,path) || uos_filehash(path,hash)) { return 2; }
    if (uos_setenv("MARKER","changed") || uos_unsetenv("HOME") || uos_setenv("EXTRA","remove-on-load") ||
        uos_profile(UOS_PROFILE_LOAD,path) || !has("MARKER","saved value") || !has("HOME","/home/user") || uos_getenv("EXTRA",out,sizeof(out))!=UOS_ENOENT) { return 3; }
    int64_t h=uos_open(path,UOS_READ);if (h<0 || uos_profile(UOS_PROFILE_SAVE,path)!=UOS_EBUSY || uos_close((uint64_t)h) || !hash_same(path,hash)) { return 4; }
    h=uos_open(path,UOS_WRITE);if (h<0 || uos_profile(UOS_PROFILE_LOAD,path)!=UOS_EBUSY || uos_close((uint64_t)h) || !has("MARKER","saved value")) { return 5; }
    if (uos_profile(UOS_PROFILE_SAVE,"/tmp/not-persistent")!=UOS_EXDEV || uos_stat("/tmp/not-persistent",&e)!=UOS_ERROR ||
        uos_profile(UOS_PROFILE_SAVE,"/boot/CONFIG.TXT")!=UOS_EROFS || uos_profile(UOS_PROFILE_LOAD,"/boot/CONFIG.TXT")!=UOS_ERROR ||
        uos_profile(UOS_PROFILE_LOAD,"/data")==0 || uos_profile(UOS_PROFILE_LOAD,"/data/.profile-absent")!=UOS_ENOENT) { return 6; }
    if (uos_call(UOS_SYS_ENV_PROFILE,UOS_PROFILE_LOAD,0,1,0,0,0)!=UOS_EFAULT ||
        uos_call(UOS_SYS_ENV_PROFILE,0,(uintptr_t)path,uos_length(path),0,0,0)!=UOS_ERROR ||
        uos_call(UOS_SYS_ENV_PROFILE,UOS_PROFILE_LOAD,(uintptr_t)path,0,0,0,0)!=UOS_ERROR) { return 7; }
    const char *fixtures[]={"OUROSENV2\n","OUROSENV1\nPATH=/boot\nBAD-NAME=bad\n","OUROSENV1\nPATH=/boot\nPATH=/tmp\n",
        "OUROSENV1\nPATH=/boot","OUROSENV1\nA=1\nB=2\nC=3\nD=4\nE=5\nF=6\nG=7\nH=8\nI=9\n"};
    for (unsigned i=0;i<sizeof(fixtures)/sizeof(fixtures[0]);++i) {
        size_t n=uos_length(fixtures[i]);if (uos_replace(bad,fixtures[i],n)!=(int64_t)n || uos_profile(UOS_PROFILE_LOAD,bad)!=UOS_ERROR ||
            !has("PATH","/data/bin:/boot") || !has("MARKER","saved value") || !hash_same(path,hash)) { return 8; }
    }
    char value[128];for (unsigned i=0;i<127;++i) { value[i]='v'; }value[127]=0;
    if (uos_setenv("MAX",value) || uos_setenv("EMPTY","") || uos_setenv("LITERAL","$PATH = 'quotes'") || uos_setenv("LAST","eighth") ||
        uos_profile(UOS_PROFILE_SAVE,path) || uos_unsetenv("MAX") || uos_setenv("MARKER","changed again") || uos_profile(UOS_PROFILE_LOAD,path) ||
        !has("MAX",value) || !has("EMPTY","") || !has("LITERAL","$PATH = 'quotes'") || !has("LAST","eighth") || !has("MARKER","saved value")) { return 9; }
    const char *crlf="OUROSENV1\r\nPATH=/boot\r\nEMPTY=\r\n";size_t n=uos_length(crlf);
    if (uos_replace(bad,crlf,n)!=(int64_t)n || uos_profile(UOS_PROFILE_LOAD,bad) || !has("PATH","/boot") || !has("EMPTY","") ||
        uos_getenv("MARKER",out,sizeof(out))!=UOS_ENOENT) { return 10; }
    if (uos_remove(path,0) || uos_remove(bad,0)) { return 11; }
    uos_puts("PASS: profile save/load/full replacement, literal/empty/max values, leases, read-only/cross-mount refusal, corrupt-input rollback, CRLF and checked syscall pointers.\n");return 0;
}
