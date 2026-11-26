#ifndef OS_KFILE_H
#define OS_KFILE_H
#include "ramfs.h"
#include <stdint.h>

enum { KFILE_LIMIT=16, KFILE_READ=1, KFILE_WRITE=2, KFILE_CREATE=4, KFILE_TRUNC=8,
       KFILE_EXCL=16, KFILE_APPEND=32, KFILE_CLOEXEC=64 };
enum kfile_whence { KFILE_SET, KFILE_CUR, KFILE_END };
struct kfile_status { uint32_t bytes,offset;unsigned flags; };
struct kfile_entry { uint64_t token;struct kfile_status status;unsigned descriptor_flags; };
/* Single-core cooperative kernel API. Operations never yield and cannot be
 * called from IRQ context. Owners identify kernel tasks, not EL0 principals.
 * Tokens are opaque, generation checked, and owned by one nonzero owner.
 * Shared readers or one writer pin a canonical file until close/owner cleanup.
 * Writable files follow their mount's bound; reads <=65536. Seek stays within
 * [0,length]; no sparse files. Persistent writes commit synchronously; close
 * only releases the handle and performs no additional device I/O. */
enum fs_result kfile_open(uint64_t owner,const char *cwd,const char *path,unsigned flags,uint64_t *handle);
enum fs_result kfile_read(uint64_t owner,uint64_t handle,void *buffer,size_t capacity,size_t *count);
enum fs_result kfile_write(uint64_t owner,uint64_t handle,const void *buffer,size_t length,size_t *count);
enum fs_result kfile_pread(uint64_t owner,uint64_t handle,void *buffer,size_t capacity,uint64_t offset,size_t *count);
enum fs_result kfile_pwrite(uint64_t owner,uint64_t handle,const void *buffer,size_t length,uint64_t offset,size_t *count);
enum fs_result kfile_stat(uint64_t owner,uint64_t handle,struct kfile_status *status);
enum fs_result kfile_truncate(uint64_t owner,uint64_t handle,uint64_t bytes);
enum fs_result kfile_seek(uint64_t owner,uint64_t handle,int64_t displacement,enum kfile_whence whence,uint32_t *position);
enum fs_result kfile_close(uint64_t owner,uint64_t handle);
unsigned kfile_close_owner(uint64_t owner);
unsigned kfile_close_writers(uint64_t owner);
/* Zero access keeps the source rights. Duplicates share the cursor. */
enum fs_result kfile_dup(uint64_t owner,uint64_t handle,unsigned access,unsigned flags,uint64_t *duplicate);
enum fs_result kfile_dup_to(uint64_t owner,uint64_t handle,uint64_t recipient,unsigned access,unsigned flags,uint64_t *duplicate);
enum fs_result kfile_receive(uint64_t escrow,uint64_t handle,uint64_t recipient);
unsigned kfile_owner_count(uint64_t owner);
enum fs_result kfile_set_cloexec(uint64_t owner,uint64_t handle,bool enabled);
enum fs_result kfile_set_append(uint64_t owner,uint64_t handle,bool enabled);
size_t kfile_list_owner(uint64_t owner,uint64_t input,uint64_t output,struct kfile_entry out[KFILE_LIMIT]);
unsigned kfile_close_on_exec(uint64_t owner);
unsigned kfile_count(void);
/* VFS calls this only after canonicalizing a mutation's target. */
bool kfile_path_busy(const char *canonical,bool removing_directory);
void kfile_info(void);
bool file_command(const char *line);
#endif
