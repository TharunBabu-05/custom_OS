#ifndef OS_APP_ENV_H
#define OS_APP_ENV_H
#include <stddef.h>
enum { APP_ENV_LIMIT=8,APP_ENV_NAME_BYTES=32,APP_ENV_VALUE_BYTES=128 };
struct app_env_entry { char name[APP_ENV_NAME_BYTES],value[APP_ENV_VALUE_BYTES]; };
_Static_assert(sizeof(struct app_env_entry)==160,"environment entry ABI size");
struct app_environment { unsigned count;struct app_env_entry entries[APP_ENV_LIMIT]; };
enum app_env_result { APP_ENV_OK,APP_ENV_MISSING,APP_ENV_INVALID,APP_ENV_FULL,APP_ENV_SMALL };
/* Private fixed-capacity environment. Names match [A-Za-z_][A-Za-z0-9_]*;
 * values contain printable ASCII, including spaces and the empty string.
 * Failed mutations/reads leave storage/output unchanged. No heap allocation. */
void app_env_init(struct app_environment *);
enum app_env_result app_env_get(const struct app_environment *,const char *,char *,size_t,size_t *);
enum app_env_result app_env_set(struct app_environment *,const char *,const char *);
enum app_env_result app_env_unset(struct app_environment *,const char *);
#endif
