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
/* All named calls below consume at most x0..x3. Leave unused argument
 * registers alone; the kernel preserves them. Keep uos_call for the full
 * six-argument ABI and any future call that needs additional registers. */
static inline int64_t uos_call4(unsigned number,uint64_t a,uint64_t b,uint64_t c,uint64_t d)
{
    register uint64_t x0 __asm__("x0")=a;
    register uint64_t x1 __asm__("x1")=b;
    register uint64_t x2 __asm__("x2")=c;
    register uint64_t x3 __asm__("x3")=d;
    register uint64_t x8 __asm__("x8")=number;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x1),"r"(x2),"r"(x3),"r"(x8) : "memory","cc");
    return (int64_t)x0;
}
static inline int64_t uos_call2(unsigned number,uint64_t a,uint64_t b)
{
    register uint64_t x0 __asm__("x0")=a;
    register uint64_t x1 __asm__("x1")=b;
    register uint64_t x8 __asm__("x8")=number;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x1),"r"(x8) : "memory","cc");
    return (int64_t)x0;
}
static inline int64_t uos_write(const void *p,size_t n)
{ return uos_call2(UOS_SYS_WRITE,(uintptr_t)p,n); }
static inline size_t uos_length(const char *p) { size_t n=0;while (p[n]) { ++n; }return n; }
static inline int64_t uos_puts(const char *p) { return uos_write(p,uos_length(p)); }
static inline int64_t uos_yield(void) { return uos_call2(UOS_SYS_YIELD,0,0); }
static inline int64_t uos_time_ms(void) { return uos_call2(UOS_SYS_TIME_MS,0,0); }
static inline int64_t uos_utc_time(struct uos_utc_time *out)
{ return uos_call2(UOS_SYS_UTC_TIME,(uintptr_t)out,0); }
static inline int64_t uos_trust_check(const unsigned char digest[32],uint64_t not_before,uint64_t not_after)
{ return uos_call4(UOS_SYS_TRUST_CHECK,(uintptr_t)digest,not_before,not_after,0); }
static inline int64_t uos_random(void *out,size_t bytes)
{ return uos_call2(UOS_SYS_RANDOM,(uintptr_t)out,bytes); }
static inline int64_t uos_open(const char *path,unsigned flags)
{ return uos_call4(UOS_SYS_OPEN,(uintptr_t)path,uos_length(path),flags,0); }
static inline int64_t uos_read(uint64_t h,void *p,size_t n)
{ return uos_call4(UOS_SYS_READ,h,(uintptr_t)p,n,0); }
static inline int64_t uos_write_file(uint64_t h,const void *p,size_t n)
{ return uos_call4(UOS_SYS_WRITE_FILE,h,(uintptr_t)p,n,0); }
static inline int64_t uos_fstat(uint64_t h,struct uos_file_info *out)
{ return uos_call2(UOS_SYS_FSTAT,h,(uintptr_t)out); }
static inline int64_t uos_pread(uint64_t h,void *p,size_t n,uint64_t offset)
{ return uos_call4(UOS_SYS_PREAD,h,(uintptr_t)p,n,offset); }
static inline int64_t uos_pwrite(uint64_t h,const void *p,size_t n,uint64_t offset)
{ return uos_call4(UOS_SYS_PWRITE,h,(uintptr_t)p,n,offset); }
static inline int64_t uos_ftruncate(uint64_t h,uint64_t bytes)
{ return uos_call2(UOS_SYS_FTRUNCATE,h,bytes); }
static inline int64_t uos_seek(uint64_t h,int64_t n,unsigned base)
{ return uos_call4(UOS_SYS_SEEK,h,(uint64_t)n,base,0); }
static inline int64_t uos_close(uint64_t h) { return uos_call2(UOS_SYS_CLOSE,h,0); }
static inline int64_t uos_argc(void) { return uos_call2(UOS_SYS_ARGC,0,0); }
static inline int64_t uos_arg(unsigned i,char *p,size_t n) { return uos_call4(UOS_SYS_ARG,i,(uintptr_t)p,n,0); }
static inline int64_t uos_getcwd(char *p,size_t n) { return uos_call2(UOS_SYS_GETCWD,(uintptr_t)p,n); }
static inline int64_t uos_chdir(const char *p) { return uos_call2(UOS_SYS_CHDIR,(uintptr_t)p,uos_length(p)); }
static inline int64_t uos_stat(const char *p,struct uos_entry *e) { return uos_call4(UOS_SYS_STAT,(uintptr_t)p,uos_length(p),(uintptr_t)e,0); }
static inline int64_t uos_readdir(const char *p,unsigned index,struct uos_entry *e) { return uos_call4(UOS_SYS_READDIR,(uintptr_t)p,uos_length(p),index,(uintptr_t)e); }
static inline int64_t uos_sleep_ms(unsigned ms) { return uos_call2(UOS_SYS_SLEEP_MS,ms,0); }
static inline int64_t uos_getpid(void) { return uos_call2(UOS_SYS_GETPID,0,0); }
static inline int64_t uos_readline(char *p,size_t n) { return uos_call2(UOS_SYS_READLINE,(uintptr_t)p,n); }
static inline int64_t uos_mkdir(const char *p) { return uos_call2(UOS_SYS_MKDIR,(uintptr_t)p,uos_length(p)); }
static inline int64_t uos_remove(const char *p,int directory) { return uos_call4(UOS_SYS_REMOVE,(uintptr_t)p,uos_length(p),(uint64_t)directory,0); }
static inline int64_t uos_replace(const char *p,const void *data,size_t n) { return uos_call4(UOS_SYS_REPLACE,(uintptr_t)p,uos_length(p),(uintptr_t)data,n); }
int app_main(void);
static inline int64_t uos_copy(const char *a,const char *b) { return uos_call4(UOS_SYS_COPY,(uintptr_t)a,uos_length(a),(uintptr_t)b,uos_length(b)); }
static inline int64_t uos_filehash(const char *p,unsigned char digest[32]) { return uos_call4(UOS_SYS_FILEHASH,(uintptr_t)p,uos_length(p),(uintptr_t)digest,0); }
static inline int64_t uos_sync(void) { return uos_call2(UOS_SYS_SYNC,0,0); }
static inline int64_t uos_rename(const char *source,const char *target)
{ return uos_call4(UOS_SYS_RENAME,(uintptr_t)source,uos_length(source),(uintptr_t)target,uos_length(target)); }
/* Consume a prepared regular file, atomically replacing a regular target. */
static inline int64_t uos_publish(const char *source,const char *target)
{ return uos_call4(UOS_SYS_PUBLISH,(uintptr_t)source,uos_length(source),(uintptr_t)target,uos_length(target)); }
/* Copy a validated packed argv into a free slot; direct children, 1..30 seconds. */
static inline int64_t uos_spawn(const struct app_args *args,unsigned seconds) { return uos_call2(UOS_SYS_SPAWN,(uintptr_t)args,seconds); }
static inline int64_t uos_wait(uint64_t pid) { return uos_call2(UOS_SYS_WAIT,pid,0); }
/* Replace this process with a checked packed argv. Success never returns;
 * PID, cwd, owned handles/cursors, streams and original deadlines survive. */
static inline int64_t uos_exec(const struct app_args *args) { return uos_call2(UOS_SYS_EXEC,(uintptr_t)args,0); }
/* Environment values are copied; no kernel pointer is exposed. */
static inline int64_t uos_getenv(const char *name,char *value,size_t capacity)
{ return uos_call4(UOS_SYS_ENV_GET,(uintptr_t)name,uos_length(name),(uintptr_t)value,capacity); }
static inline int64_t uos_setenv(const char *name,const char *value)
{ return uos_call4(UOS_SYS_ENV_SET,(uintptr_t)name,uos_length(name),(uintptr_t)value,uos_length(value)); }
static inline int64_t uos_unsetenv(const char *name)
{ return uos_call2(UOS_SYS_ENV_UNSET,(uintptr_t)name,uos_length(name)); }
static inline int64_t uos_env_entry(unsigned index,struct app_env_entry *entry)
{ return uos_call2(UOS_SYS_ENV_ENTRY,index,(uintptr_t)entry); }
static inline int64_t uos_profile(unsigned action,const char *path)
{ return uos_call4(UOS_SYS_ENV_PROFILE,action,(uintptr_t)path,uos_length(path),0); }
/* 1..16 zeroed 4096-byte pages; result is an address or a negative error. */
static inline int64_t uos_alloc_pages(unsigned pages) { return uos_call2(UOS_SYS_ALLOC_PAGES,pages,0); }
static inline int64_t uos_free_pages(void *base) { return uos_call2(UOS_SYS_FREE_PAGES,(uintptr_t)base,0); }
static inline int64_t uos_memory_info(struct uos_memory_info *out) { return uos_call2(UOS_SYS_MEMORY_INFO,(uintptr_t)out,0); }
static inline int64_t uos_resize_pages(void *base,unsigned pages)
{ return uos_call2(UOS_SYS_RESIZE_PAGES,(uintptr_t)base,pages); }
static inline int64_t uos_memory_pressure(struct uos_memory_pressure *out)
{ return uos_call2(UOS_SYS_MEMORY_PRESSURE,(uintptr_t)out,0); }
static inline int64_t uos_processes(struct uos_process_table *out)
{ return uos_call2(UOS_SYS_PROCESSES,(uintptr_t)out,sizeof(*out)); }
/* Poll/cancel never reap. Only a live direct-child relation grants authority;
 * finished children remain queryable until ordinary wait collects them. */
static inline int64_t uos_child_poll(uint64_t pid,struct uos_child_info *out)
{ return uos_call2(UOS_SYS_CHILD_POLL,pid,(uintptr_t)out); }
static inline int64_t uos_child_cancel(uint64_t pid)
{ return uos_call2(UOS_SYS_CHILD_CANCEL,pid,0); }
static inline int64_t uos_statfs(const char *path,struct uos_fs_info *out)
{ return uos_call4(UOS_SYS_STATFS,(uintptr_t)path,uos_length(path),(uintptr_t)out,0); }
static inline int64_t uos_udp_open(const struct uos_udp_peer *peer)
{ return uos_call2(UOS_SYS_UDP_OPEN,(uintptr_t)peer,0); }
static inline int64_t uos_udp_send(uint64_t handle,const void *data,size_t bytes)
{ return uos_call4(UOS_SYS_UDP_SEND,handle,(uintptr_t)data,bytes,0); }
static inline int64_t uos_udp_recv(uint64_t handle,void *data,size_t capacity,unsigned wait_ms)
{ return uos_call4(UOS_SYS_UDP_RECV,handle,(uintptr_t)data,capacity,wait_ms); }
static inline int64_t uos_udp_close(uint64_t handle)
{ return uos_call2(UOS_SYS_UDP_CLOSE,handle,0); }
static inline int64_t uos_tcp_open(const struct uos_tcp_peer *peer)
{ return uos_call2(UOS_SYS_TCP_OPEN,(uintptr_t)peer,0); }
static inline int64_t uos_tcp_status(uint64_t handle)
{ return uos_call2(UOS_SYS_TCP_STATUS,handle,0); }
static inline int64_t uos_tcp_write(uint64_t handle,const void *data,size_t bytes)
{ return uos_call4(UOS_SYS_TCP_WRITE,handle,(uintptr_t)data,bytes,0); }
static inline int64_t uos_tcp_read(uint64_t handle,void *data,size_t bytes)
{ return uos_call4(UOS_SYS_TCP_READ,handle,(uintptr_t)data,bytes,0); }
static inline int64_t uos_tcp_close(uint64_t handle)
{ return uos_call2(UOS_SYS_TCP_CLOSE,handle,0); }
static inline int64_t uos_spawn_io(const struct app_args *args,unsigned seconds,const struct uos_stdio *io)
{ return uos_call4(UOS_SYS_SPAWN_IO,(uintptr_t)args,seconds,(uintptr_t)io,0); }
static inline int64_t uos_spawn_append(const struct app_args *args,unsigned seconds,const struct uos_stdio *io)
{ return uos_call4(UOS_SYS_SPAWN_APPEND,(uintptr_t)args,seconds,(uintptr_t)io,0); }
/* Binary file stdin only; terminal input uses readline. Up to 512 bytes/call. */
static inline int64_t uos_stdin(void *buffer,size_t capacity) { return uos_call2(UOS_SYS_STDIN,(uintptr_t)buffer,capacity); }
/* Start two children concurrently, connect stdout -> stdin, wait for both.
 * io may be NULL for terminal output/no input file. Returns pipeline status. */
static inline int64_t uos_pipeline(const struct app_args *producer,const struct app_args *consumer,unsigned seconds,const struct uos_stdio *io)
{ return uos_call4(UOS_SYS_PIPE_RUN,(uintptr_t)producer,(uintptr_t)consumer,seconds,(uintptr_t)io); }
static inline int64_t uos_pipeline_append(const struct app_args *producer,const struct app_args *consumer,unsigned seconds,const struct uos_stdio *io)
{ return uos_call4(UOS_SYS_PIPE_APPEND,(uintptr_t)producer,(uintptr_t)consumer,seconds,(uintptr_t)io); }
#endif
