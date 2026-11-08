#ifndef OS_RAMFS_H
#define OS_RAMFS_H
#include <stdbool.h>
#include <stddef.h>
enum { RAMFS_NODES=64, RAMFS_NAME_MAX=31, RAMFS_PATH_MAX=256, RAMFS_FILE_MAX=4096 };
enum fs_result {
    FS_OK, FS_NOT_FOUND, FS_NOT_DIR, FS_IS_DIR, FS_EXISTS, FS_INVALID_PATH,
    FS_NAME_TOO_LONG, FS_PATH_TOO_LONG, FS_NO_NODES, FS_NO_MEMORY,
    FS_FILE_TOO_LARGE, FS_NOT_EMPTY, FS_BUSY, FS_NOT_READY, FS_BAD_ARGUMENT
};
struct ramfs_stats { unsigned files, directories, pages; size_t bytes; bool ready; };
typedef void (*ramfs_emit)(const char *name, bool directory, size_t bytes, void *context);
/* A single volatile root filesystem. Node IDs are internal, not open handles. */
enum fs_result ramfs_init(const char *board);
enum fs_result ramfs_lookup(int cwd, const char *path, int *node);
enum fs_result ramfs_pwd(int node, char *buffer, size_t capacity);
enum fs_result ramfs_list(int cwd, const char *path, ramfs_emit emit, void *context);
enum fs_result ramfs_chdir(int cwd, const char *path, int *directory);
enum fs_result ramfs_read(int cwd, const char *path, const char **data, size_t *length);
enum fs_result ramfs_mkdir(int cwd, const char *path);
enum fs_result ramfs_touch(int cwd, const char *path);
enum fs_result ramfs_write(int cwd, const char *path, const char *data, size_t length);
enum fs_result ramfs_remove(int cwd, const char *path, bool directory);
struct ramfs_stats ramfs_get_stats(void);
const char *ramfs_error(enum fs_result result);
bool filesystem_command(const char *line);
#endif
