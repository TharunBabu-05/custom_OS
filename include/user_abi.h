#ifndef OUR_OS_USER_ABI_H
#define OUR_OS_USER_ABI_H
/* Version 1 of the original OurOS AArch64 syscall ABI. x8 selects the call;
 * x0..x5 hold arguments. The return value is a signed x0. No libc or Linux
 * syscall compatibility is implied. All pointers are checked by the kernel. */
#include <stddef.h>
#include <stdint.h>
#include "app_env.h"
enum uos_syscall {
    UOS_SYS_EXIT=0,UOS_SYS_WRITE=1,UOS_SYS_YIELD=2,UOS_SYS_TIME_MS=3,
    UOS_SYS_OPEN=4,UOS_SYS_READ=5,UOS_SYS_WRITE_FILE=6,UOS_SYS_SEEK=7,UOS_SYS_CLOSE=8,
    UOS_SYS_ARGC=9,UOS_SYS_ARG=10,UOS_SYS_GETCWD=11,UOS_SYS_CHDIR=12,
    UOS_SYS_STAT=13,UOS_SYS_READDIR=14,UOS_SYS_SLEEP_MS=15,UOS_SYS_GETPID=16,
    UOS_SYS_READLINE=17,UOS_SYS_MKDIR=18,UOS_SYS_REMOVE=19,UOS_SYS_REPLACE=20,
    UOS_SYS_COPY=21,UOS_SYS_FILEHASH=22,UOS_SYS_SYNC=23,
    UOS_SYS_SPAWN=24,UOS_SYS_WAIT=25,UOS_SYS_SPAWN_IO=26,UOS_SYS_STDIN=27,
    UOS_SYS_PIPE_RUN=28,UOS_SYS_RENAME=29,UOS_SYS_PUBLISH=30,UOS_SYS_EXEC=31,
    UOS_SYS_ENV_GET=32,UOS_SYS_ENV_SET=33,UOS_SYS_ENV_UNSET=34,UOS_SYS_ENV_ENTRY=35,
    UOS_SYS_ENV_PROFILE=36,UOS_SYS_ALLOC_PAGES=37,UOS_SYS_FREE_PAGES=38,UOS_SYS_MEMORY_INFO=39,
    UOS_SYS_PROCESSES=40,UOS_SYS_CHILD_POLL=41,UOS_SYS_CHILD_CANCEL=42,UOS_SYS_STATFS=43,
    UOS_SYS_RESIZE_PAGES=44,UOS_SYS_MEMORY_PRESSURE=45,
    UOS_SYS_UDP_OPEN=46,UOS_SYS_UDP_SEND=47,UOS_SYS_UDP_RECV=48,UOS_SYS_UDP_CLOSE=49,
    UOS_SYS_TCP_OPEN=50,UOS_SYS_TCP_STATUS=51,UOS_SYS_TCP_WRITE=52,UOS_SYS_TCP_READ=53,UOS_SYS_TCP_CLOSE=54,
    UOS_SYS_UTC_TIME=55,UOS_SYS_TRUST_CHECK=56,UOS_SYS_RANDOM=57,
    UOS_SYS_FSTAT=58,UOS_SYS_PREAD=59,UOS_SYS_PWRITE=60,UOS_SYS_FTRUNCATE=61,
    UOS_SYS_SPAWN_APPEND=62,UOS_SYS_PIPE_APPEND=63,
    UOS_SYS_FD_DUP=64,UOS_SYS_FD_SET=65,UOS_SYS_FD_LIST=66,
    UOS_SYS_PIPE_CHAIN=67,
    UOS_SYS_IPC_SEND=68,UOS_SYS_IPC_RECV=69,UOS_SYS_SIGNAL_SEND=70,UOS_SYS_SIGNAL_WAIT=71,
    UOS_SYS_CHILD_FAULT=72,UOS_SYS_VM_CREATE=73,UOS_SYS_VM_MAP=74,UOS_SYS_VM_UNMAP=75
};
enum { UOS_RANDOM_MAX=64 };
/* Fixed 48-byte result; unused/reserved bytes are always zero. Directory
 * index is a fresh traversal, not a stable cursor across filesystem edits. */
struct uos_entry { uint64_t bytes;uint32_t type,reserved;char name[32]; };
/* Empty path keeps the terminal/default stream. Output creates a new file;
 * existing files are refused. Paths are relative to the spawning process. */
struct uos_stdio { char input[256],output[256]; };
_Static_assert(sizeof(struct uos_stdio)==512,"standard stream ABI size");
enum { UOS_FILE=1,UOS_DIRECTORY=2 };
_Static_assert(sizeof(struct uos_entry)==48,"directory ABI size");
/* Private dynamic page arena, separate from the executable and stack. */
struct uos_memory_info { uint64_t page_bytes,limit_pages,live_pages,peak_pages,allocations,free_calls; };
_Static_assert(sizeof(struct uos_memory_info)==48,"memory info ABI size");
/* Caller-local heap diagnostics, including unsuccessful resize attempts. */
struct uos_memory_pressure {
    uint64_t free_pages,largest_run;
    uint64_t quota_failures,fragmentation_failures,backing_failures,mapping_failures;
};
_Static_assert(sizeof(struct uos_memory_pressure)==48,"memory pressure ABI size");
/* One coherent snapshot of the active foreground batch. No kernel addresses,
 * arguments, credentials, cwd or environment are exported. Unused bytes zero. */
enum { UOS_PROCESS_MAX=3,UOS_PROCESS_VERSION=1,UOS_PROCESS_SELF=1,UOS_PROCESS_CHILD=2 };
enum { UOS_PROCESS_MAX_V2=5,UOS_PROCESS_VERSION_V2=2,UOS_PIPE_STAGES=4,UOS_PIPE_APPEND=1 };
enum uos_process_state {
    UOS_PROCESS_RUNNING=1,UOS_PROCESS_READY,UOS_PROCESS_SLEEPING,UOS_PROCESS_INPUT,
    UOS_PROCESS_WAIT_CHILD,UOS_PROCESS_PIPE_READ,UOS_PROCESS_PIPE_WRITE,
    UOS_PROCESS_EXITED,UOS_PROCESS_FAULTED,UOS_PROCESS_TIMEOUT,UOS_PROCESS_CANCELLED,
    UOS_PROCESS_UDP_WAIT,UOS_PROCESS_IPC_SEND,UOS_PROCESS_IPC_RECV,UOS_PROCESS_SIGNAL_WAIT
};
struct uos_process_entry {
    uint64_t pid,parent_pid,syscalls,dispatches,heap_pages;
    uint32_t state,flags;int32_t exit_code;uint32_t reserved;
    char path[256];
};
struct uos_process_table {
    uint32_t version,count;uint64_t caller_pid;
    struct uos_process_entry entries[UOS_PROCESS_MAX];
};
struct uos_process_table_v2 {
    uint32_t version,count;uint64_t caller_pid;
    struct uos_process_entry entries[UOS_PROCESS_MAX_V2];
};
struct uos_child_info { uint64_t pid;uint32_t state,finished;int32_t exit_code;uint32_t reserved; };
_Static_assert(sizeof(struct uos_process_entry)==312,"process entry ABI size");
_Static_assert(sizeof(struct uos_process_table)==952,"process table ABI size");
_Static_assert(sizeof(struct uos_process_table_v2)==1576,"process table v2 ABI size");
_Static_assert(sizeof(struct uos_child_info)==24,"child info ABI size");
enum { UOS_IPC_BYTES=96,UOS_IPC_DEPTH=4,UOS_WAIT_MAX_MS=10000 };
/* Send uses peer as the destination PID. Receive returns the sender in peer.
 * A nonzero handle requires explicit READ/WRITE rights and arrives CLOEXEC. */
struct uos_message { uint64_t peer,handle;uint32_t bytes,rights;unsigned char data[UOS_IPC_BYTES]; };
_Static_assert(sizeof(struct uos_message)==120,"IPC message ABI size");
enum { UOS_SIGINT=2,UOS_SIGUSR1=10,UOS_SIGTERM=15 };
enum { UOS_STOP_EXIT=1,UOS_STOP_FAULT,UOS_STOP_TIMEOUT,UOS_STOP_SIGNAL,UOS_STOP_KERNEL };
enum { UOS_FAULT_FAR_VALID=1 };
struct uos_fault_info {
    uint64_t pid,pc,esr,far;
    uint32_t reason,code,signal,flags;
};
_Static_assert(sizeof(struct uos_fault_info)==48,"process stop ABI size");
enum { UOS_VM_MAX_PAGES=32 };
/* Query an existing path's mount; zero write limit means read-only. */
enum { UOS_FS_RAM=1,UOS_FS_FAT=2,UOS_FS_SNAPSHOT=3,UOS_FS_READONLY=1,UOS_FS_VOLATILE=2 };
struct uos_fs_info { uint64_t max_write_bytes;uint32_t type,version,node_limit,used_nodes;uint64_t flags; };
_Static_assert(sizeof(struct uos_fs_info)==32,"filesystem info ABI size");
enum { UOS_READ=1,UOS_WRITE=2,UOS_CREATE=4,UOS_TRUNC=8,UOS_EXCL=16,UOS_APPEND=32,UOS_CLOEXEC=64 };
/* File length and offset in bytes. Flags contain only READ, WRITE and APPEND. */
struct uos_file_info { uint64_t bytes,offset;uint32_t flags,reserved; };
_Static_assert(sizeof(struct uos_file_info)==24,"file-handle info ABI size");
enum { UOS_FD_MAX=16 };
struct uos_fd_entry { uint64_t token;uint32_t bytes,offset,flags,descriptor_flags; };
struct uos_fd_table { uint32_t count,reserved;struct uos_fd_entry entries[UOS_FD_MAX]; };
_Static_assert(sizeof(struct uos_fd_table)==392,"file-handle table ABI size");
enum { UOS_PROFILE_SAVE=1,UOS_PROFILE_LOAD=2 };
enum { UOS_SEEK_SET=0,UOS_SEEK_CUR=1,UOS_SEEK_END=2 };
enum { UOS_UDP_MAX_BYTES=256,UOS_UDP_MAX_WAIT_MS=10000 };
/* Connected IPv4 for a single LAN peer. Port fields use host byte order.
 * Zero local_port selects an available ephemeral port. */
struct uos_udp_peer { uint8_t ipv4[4];uint16_t port,local_port; };
_Static_assert(sizeof(struct uos_udp_peer)==8,"UDP peer ABI size");
enum { UOS_TCP_MAX_BYTES=512 };
struct uos_tcp_peer { uint8_t ipv4[4];uint16_t port,reserved; };
_Static_assert(sizeof(struct uos_tcp_peer)==8,"TCP peer ABI size");
struct uos_utc_time { uint64_t unix_seconds;uint32_t age_seconds,remaining_seconds; };
_Static_assert(sizeof(struct uos_utc_time)==16,"UTC time ABI size");
#define UOS_ERROR (-1LL)
#define UOS_EFAULT (-2LL)
#define UOS_EOF (-3LL)
#define UOS_ENOTTY (-4LL)
#define UOS_EPIPE (-5LL)
#define UOS_ENOENT (-6LL)
#define UOS_EEXIST (-7LL)
#define UOS_EBUSY (-8LL)
#define UOS_EXDEV (-9LL)
#define UOS_EROFS (-10LL)
#define UOS_EISDIR (-11LL)
#define UOS_ENOSPC (-12LL)
#define UOS_ENOMEM (-13LL)
#define UOS_ECHILD (-14LL)
#define UOS_EAGAIN (-15LL)
#define UOS_ENETDOWN (-16LL)
#define UOS_ETIMEDOUT (-17LL)
#define UOS_EMSGSIZE (-18LL)
#define UOS_EBADF (-19LL)
#define UOS_EHOSTUNREACH (-20LL)
#define UOS_ECONNRESET (-21LL)
#define UOS_ENOTIME (-22LL)
#define UOS_ECERTTIME (-23LL)
#define UOS_ENORANDOM (-24LL)
#define UOS_EINTR (-25LL)
#endif
