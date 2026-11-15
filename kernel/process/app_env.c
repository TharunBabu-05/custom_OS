#include "app_env.h"
#include <stdbool.h>
static void wipe(void *p,size_t n) { volatile unsigned char *b=p;while (n--) { *b++=0; } }
static bool letter(unsigned char c) { return (c>='a' && c<='z') || (c>='A' && c<='Z') || c=='_'; }
static bool name_length(const char *s,size_t *bytes)
{
    if (!s || !letter((unsigned char)s[0])) { return false; }
    size_t n=1;while (n<APP_ENV_NAME_BYTES && s[n]) {
        unsigned char c=(unsigned char)s[n];if (!letter(c) && !(c>='0' && c<='9')) { return false; }++n;
    }
    if (n==APP_ENV_NAME_BYTES) { return false; }*bytes=n;return true;
}
static bool value_length(const char *s,size_t *bytes)
{
    if (!s) { return false; }size_t n=0;
    while (n<APP_ENV_VALUE_BYTES && s[n]) { unsigned char c=(unsigned char)s[n];if (c<32 || c>126) { return false; }++n; }
    if (n==APP_ENV_VALUE_BYTES) { return false; }*bytes=n;return true;
}
static int find(const struct app_environment *env,const char *key)
{
    for (unsigned i=0;i<env->count;++i) {
        const char *a=env->entries[i].name,*b=key;while (*a && *a==*b) { ++a;++b; }if (*a==*b) { return (int)i; }
    }return -1;
}
enum app_env_result app_env_set(struct app_environment *env,const char *key,const char *value)
{
    size_t a,b;if (!env || env->count>APP_ENV_LIMIT || !name_length(key,&a) || !value_length(value,&b)) { return APP_ENV_INVALID; }
    int id=find(env,key);if (id<0 && env->count==APP_ENV_LIMIT) { return APP_ENV_FULL; }
    struct app_env_entry entry={0};
    for (size_t i=0;i<a;++i) { entry.name[i]=key[i]; }for (size_t i=0;i<b;++i) { entry.value[i]=value[i]; }
    if (id<0) { id=(int)env->count++; }env->entries[id]=entry;return APP_ENV_OK;
}
enum app_env_result app_env_get(const struct app_environment *env,const char *key,char *out,size_t capacity,size_t *bytes)
{
    size_t n;if (!env || env->count>APP_ENV_LIMIT || !out || !bytes || !name_length(key,&n)) { return APP_ENV_INVALID; }
    int id=find(env,key);if (id<0) { return APP_ENV_MISSING; }const char *value=env->entries[id].value;
    if (!value_length(value,&n)) { return APP_ENV_INVALID; }if (capacity<=n) { return APP_ENV_SMALL; }
    for (size_t i=0;i<=n;++i) { out[i]=value[i]; }*bytes=n;return APP_ENV_OK;
}
enum app_env_result app_env_unset(struct app_environment *env,const char *key)
{
    size_t n;if (!env || env->count>APP_ENV_LIMIT || !name_length(key,&n)) { return APP_ENV_INVALID; }
    int id=find(env,key);if (id<0) { return APP_ENV_OK; }
    for (unsigned i=(unsigned)id;i+1<env->count;++i) { env->entries[i]=env->entries[i+1]; }
    --env->count;wipe(&env->entries[env->count],sizeof(env->entries[0]));return APP_ENV_OK;
}
void app_env_init(struct app_environment *env)
{
    if (!env) { return; }wipe(env,sizeof(*env));
    (void)app_env_set(env,"PATH","/data/bin:/boot");(void)app_env_set(env,"HOME","/home/user");(void)app_env_set(env,"TERM","serial");
}
