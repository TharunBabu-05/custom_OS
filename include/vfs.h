#ifndef OS_VFS_H
#define OS_VFS_H
#include "fat32.h"
/* Two mount routes: writable RAM root and one read-only FAT volume at /boot. */
enum fs_result vfs_mount_boot(const struct block_device *device);
bool vfs_boot_mounted(void);
void vfs_boot_info(void);
enum fs_result vfs_resolve(const char *cwd,const char *path,char *absolute,bool *directory);
enum fs_result vfs_list(const char *absolute,ramfs_emit emit,void *context);
enum fs_result vfs_read(const char *absolute,uint32_t offset,void *buffer,size_t capacity,size_t *count);
bool vfs_read_only(const char *absolute);
enum fs_result vfs_prepare_write(const char *cwd,const char *path,char *absolute);
#endif
