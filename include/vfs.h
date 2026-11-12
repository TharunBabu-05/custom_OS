#ifndef OS_VFS_H
#define OS_VFS_H
#include "fat32.h"
/* Mount routes: volatile RAM root, read-only /boot FAT and snapshot /data. */
enum fs_result vfs_mount_boot(const struct block_device *device);
bool vfs_boot_mounted(void);
enum fs_result vfs_boot_open(const char *absolute,struct fat32_file *file);
void vfs_boot_info(void);
enum fs_result vfs_resolve(const char *cwd,const char *path,char *absolute,bool *directory);
enum fs_result vfs_list(const char *absolute,ramfs_emit emit,void *context);
enum fs_result vfs_read(const char *absolute,uint32_t offset,void *buffer,size_t capacity,size_t *count);
bool vfs_read_only(const char *absolute);
enum fs_result vfs_prepare_write(const char *cwd,const char *path,char *absolute);
enum fs_result vfs_mount_data(const struct block_device *device);
void vfs_data_info(void);
enum fs_result vfs_data_sync(void);
enum fs_result vfs_write(const char *,const void *,size_t);
enum fs_result vfs_touch(const char *);
enum fs_result vfs_mkdir(const char *);
enum fs_result vfs_remove(const char *,bool directory);
enum fs_result vfs_file_size(const char *canonical,uint32_t *bytes);
/* Trusted handle backend: caller holds an exclusive canonical-path lease.
 * Public mutations below the handle layer must use vfs_write instead. */
enum fs_result vfs_replace_open(const char *canonical,const void *,size_t);
#endif
