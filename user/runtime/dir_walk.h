#ifndef OUR_OS_DIR_WALK_H
#define OUR_OS_DIR_WALK_H
#include "user.h"
#include <stdbool.h>

enum { DIR_WALK_PATH_BYTES=256 };
enum dir_walk_result { DIR_WALK_OK,DIR_WALK_MISSING,DIR_WALK_READ,DIR_WALK_LIMIT,DIR_WALK_STOP };
typedef bool (*dir_walk_visit)(const char *path,const char *name,const struct uos_entry *,void *);

/* Visit the starting entry, then each descendant in directory order. */
enum dir_walk_result dir_walk(char path[DIR_WALK_PATH_BYTES],dir_walk_visit,void *);
const char *dir_walk_error(enum dir_walk_result);
#endif
