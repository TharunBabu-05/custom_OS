#ifndef OS_PACKAGE_H
#define OS_PACKAGE_H
#include "ramfs.h"

bool package_command(const char *verb,const char *arguments);
enum fs_result package_path(const char *name,char out[RAMFS_PATH_MAX]);
#endif
