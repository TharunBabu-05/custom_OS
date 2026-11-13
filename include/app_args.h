#ifndef OS_APP_ARGS_H
#define OS_APP_ARGS_H
#include <stdbool.h>
#include <stddef.h>
enum { APP_ARG_LIMIT=8, APP_ARG_BYTES=512 };
struct app_args { unsigned count,used;unsigned offset[APP_ARG_LIMIT];char bytes[APP_ARG_BYTES]; };
/* Whitespace-separated words, single/double quotes and backslash escapes.
 * Includes executable as argument zero. No expansions. Failure clears out. */
bool app_args_parse(const char *line,struct app_args *out);
#endif
