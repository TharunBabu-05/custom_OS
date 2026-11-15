#ifndef OS_APP_ARGS_H
#define OS_APP_ARGS_H
#include <stdbool.h>
#include <stddef.h>
enum { APP_ARG_LIMIT=8, APP_ARG_BYTES=512 };
struct app_args { unsigned count,used;unsigned offset[APP_ARG_LIMIT];char bytes[APP_ARG_BYTES]; };
/* Whitespace-separated words, single/double quotes and backslash escapes.
 * Includes executable as argument zero. No expansions. Failure clears out. */
bool app_args_parse(const char *line,struct app_args *out);
/* Original shell expansion, without field splitting or recursive parsing.
 * Callback writes a terminated printable value; missing variables become an
 * empty value. Single quotes suppress expansion. Failure clears out. */
typedef bool (*app_args_lookup)(const char *,char *,size_t,void *);
bool app_args_expand(const char *,app_args_lookup,void *,struct app_args *);
#endif
