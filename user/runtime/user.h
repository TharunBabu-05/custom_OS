#ifndef OUR_OS_USER_RUNTIME_H
#define OUR_OS_USER_RUNTIME_H
#include "user_abi.h"
#include "app_args.h"
static inline int64_t uos_call(unsigned number,uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f)
{
    register uint64_t x0 __asm__("x0")=a;
    register uint64_t x1 __asm__("x1")=b;
    register uint64_t x2 __asm__("x2")=c;
    register uint64_t x3 __asm__("x3")=d;
    register uint64_t x4 __asm__("x4")=e;
    register uint64_t x5 __asm__("x5")=f;
    register uint64_t x8 __asm__("x8")=number;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x1),"r"(x2),"r"(x3),"r"(x4),"r"(x5),"r"(x8) : "memory","cc");
    return (int64_t)x0;
}
static inline int64_t uos_write(const void *p,size_t n)
{ return uos_call(UOS_SYS_WRITE,(uintptr_t)p,n,0,0,0,0); }
static inline size_t uos_length(const char *p) { size_t n=0;while (p[n]) { ++n; }return n; }
static inline int64_t uos_puts(const char *p) { return uos_write(p,uos_length(p)); }
static inline int64_t uos_yield(void) { return uos_call(UOS_SYS_YIELD,0,0,0,0,0,0); }
static inline int64_t uos_time_ms(void) { return uos_call(UOS_SYS_TIME_MS,0,0,0,0,0,0); }
static inline int64_t uos_open(const char *path,unsigned flags)
{ return uos_call(UOS_SYS_OPEN,(uintptr_t)path,uos_length(path),flags,0,0,0); }
static inline int64_t uos_read(uint64_t h,void *p,size_t n)
{ return uos_call(UOS_SYS_READ,h,(uintptr_t)p,n,0,0,0); }
static inline int64_t uos_write_file(uint64_t h,const void *p,size_t n)
{ return uos_call(UOS_SYS_WRITE_FILE,h,(uintptr_t)p,n,0,0,0); }
static inline int64_t uos_seek(uint64_t h,int64_t n,unsigned base)
{ return uos_call(UOS_SYS_SEEK,h,(uint64_t)n,base,0,0,0); }
static inline int64_t uos_close(uint64_t h) { return uos_call(UOS_SYS_CLOSE,h,0,0,0,0,0); }
static inline int64_t uos_argc(void) { return uos_call(UOS_SYS_ARGC,0,0,0,0,0,0); }
static inline int64_t uos_arg(unsigned i,char *p,size_t n) { return uos_call(UOS_SYS_ARG,i,(uintptr_t)p,n,0,0,0); }
static inline int64_t uos_getcwd(char *p,size_t n) { return uos_call(UOS_SYS_GETCWD,(uintptr_t)p,n,0,0,0,0); }
static inline int64_t uos_chdir(const char *p) { return uos_call(UOS_SYS_CHDIR,(uintptr_t)p,uos_length(p),0,0,0,0); }
static inline int64_t uos_stat(const char *p,struct uos_entry *e) { return uos_call(UOS_SYS_STAT,(uintptr_t)p,uos_length(p),(uintptr_t)e,0,0,0); }
static inline int64_t uos_readdir(const char *p,unsigned index,struct uos_entry *e) { return uos_call(UOS_SYS_READDIR,(uintptr_t)p,uos_length(p),index,(uintptr_t)e,0,0); }
static inline int64_t uos_sleep_ms(unsigned ms) { return uos_call(UOS_SYS_SLEEP_MS,ms,0,0,0,0,0); }
static inline int64_t uos_getpid(void) { return uos_call(UOS_SYS_GETPID,0,0,0,0,0,0); }
static inline int64_t uos_readline(char *p,size_t n) { return uos_call(UOS_SYS_READLINE,(uintptr_t)p,n,0,0,0,0); }
static inline int64_t uos_mkdir(const char *p) { return uos_call(UOS_SYS_MKDIR,(uintptr_t)p,uos_length(p),0,0,0,0); }
static inline int64_t uos_remove(const char *p,int directory) { return uos_call(UOS_SYS_REMOVE,(uintptr_t)p,uos_length(p),(uint64_t)directory,0,0,0); }
static inline int64_t uos_replace(const char *p,const void *data,size_t n) { return uos_call(UOS_SYS_REPLACE,(uintptr_t)p,uos_length(p),(uintptr_t)data,n,0,0); }
int app_main(void);
static inline int64_t uos_copy(const char *a,const char *b) { return uos_call(UOS_SYS_COPY,(uintptr_t)a,uos_length(a),(uintptr_t)b,uos_length(b),0,0); }
static inline int64_t uos_filehash(const char *p,unsigned char digest[32]) { return uos_call(UOS_SYS_FILEHASH,(uintptr_t)p,uos_length(p),(uintptr_t)digest,0,0,0); }
static inline int64_t uos_sync(void) { return uos_call(UOS_SYS_SYNC,0,0,0,0,0,0); }
/* Copy a validated packed argv; one direct child, 1..30 seconds, no handles inherited. */
static inline int64_t uos_spawn(const struct app_args *args,unsigned seconds) { return uos_call(UOS_SYS_SPAWN,(uintptr_t)args,seconds,0,0,0,0); }
static inline int64_t uos_wait(uint64_t pid) { return uos_call(UOS_SYS_WAIT,pid,0,0,0,0,0); }
static inline int64_t uos_spawn_io(const struct app_args *args,unsigned seconds,const struct uos_stdio *io)
{ return uos_call(UOS_SYS_SPAWN_IO,(uintptr_t)args,seconds,(uintptr_t)io,0,0,0); }
/* Binary file stdin only; terminal input uses readline. Up to 512 bytes/call. */
static inline int64_t uos_stdin(void *buffer,size_t capacity) { return uos_call(UOS_SYS_STDIN,(uintptr_t)buffer,capacity,0,0,0,0); }
#endif
