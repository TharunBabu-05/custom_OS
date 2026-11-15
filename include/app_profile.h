#ifndef OS_APP_PROFILE_H
#define OS_APP_PROFILE_H
#include <stdbool.h>
#include "app_env.h"
enum { APP_PROFILE_BYTES=1536 };
/* Versioned data-only environment snapshot. Encode LF; decode LF or CRLF.
 * Failed encode/decode leave output unchanged. No filesystem access here. */
bool app_profile_encode(const struct app_environment *,void *,size_t,size_t *);
bool app_profile_decode(const void *,size_t,struct app_environment *);
#endif
