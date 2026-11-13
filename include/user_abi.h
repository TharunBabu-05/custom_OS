#ifndef OUR_OS_USER_ABI_H
#define OUR_OS_USER_ABI_H
/* Version 1 of the original OurOS AArch64 syscall ABI. x8 selects the call;
 * x0..x5 hold arguments. The return value is a signed x0. No libc or Linux
 * syscall compatibility is implied. All pointers are checked by the kernel. */
#include <stddef.h>
#include <stdint.h>
enum uos_syscall {
    UOS_SYS_EXIT=0,UOS_SYS_WRITE=1,UOS_SYS_YIELD=2,UOS_SYS_TIME_MS=3,
    UOS_SYS_OPEN=4,UOS_SYS_READ=5,UOS_SYS_WRITE_FILE=6,UOS_SYS_SEEK=7,UOS_SYS_CLOSE=8,
    UOS_SYS_ARGC=9,UOS_SYS_ARG=10,UOS_SYS_GETCWD=11,UOS_SYS_CHDIR=12,
    UOS_SYS_STAT=13,UOS_SYS_READDIR=14,UOS_SYS_SLEEP_MS=15,UOS_SYS_GETPID=16
};
/* Fixed 48-byte result; unused/reserved bytes are always zero. Directory
 * index is a fresh traversal, not a stable cursor across filesystem edits. */
struct uos_entry { uint64_t bytes;uint32_t type,reserved;char name[32]; };
enum { UOS_FILE=1,UOS_DIRECTORY=2 };
_Static_assert(sizeof(struct uos_entry)==48,"directory ABI size");
enum { UOS_READ=1,UOS_WRITE=2,UOS_CREATE=4,UOS_TRUNC=8 };
enum { UOS_SEEK_SET=0,UOS_SEEK_CUR=1,UOS_SEEK_END=2 };
#define UOS_ERROR (-1LL)
#define UOS_EFAULT (-2LL)
#endif
