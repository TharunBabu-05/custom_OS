#include "app_profile.h"
static const char header[]="OUROSENV1";
static size_t length(const char *s,size_t bound)
{ size_t n=0;while (n<bound && s[n]) { ++n; }return n; }
bool app_profile_encode(const struct app_environment *env,void *output,size_t capacity,size_t *bytes)
{
    if (!env || !output || !bytes || env->count>APP_ENV_LIMIT) { return false; }
    struct app_environment checked={0};size_t needed=sizeof(header);
    for (unsigned i=0;i<env->count;++i) {
        const struct app_env_entry *e=&env->entries[i];
        if (app_env_set(&checked,e->name,e->value)!=APP_ENV_OK || checked.count!=i+1) { return false; }
        needed+=length(e->name,APP_ENV_NAME_BYTES)+length(e->value,APP_ENV_VALUE_BYTES)+2;
    }
    if (needed>capacity || needed>APP_PROFILE_BYTES) { return false; }
    char *out=output;size_t at=0;
    for (size_t i=0;i<sizeof(header)-1;++i) { out[at++]=header[i]; }out[at++]='\n';
    for (unsigned i=0;i<checked.count;++i) {
        const char *s=checked.entries[i].name;while (*s) { out[at++]=*s++; }out[at++]='=';
        s=checked.entries[i].value;while (*s) { out[at++]=*s++; }out[at++]='\n';
    }
    *bytes=at;return true;
}
static bool line(const unsigned char *data,size_t bytes,size_t *at,size_t *first,size_t *end)
{
    *first=*at;while (*at<bytes && data[*at]!='\n') { ++*at; }if (*at==bytes) { return false; }
    *end=*at; ++*at;if (*end>*first && data[*end-1]=='\r') { --*end; }return true;
}
bool app_profile_decode(const void *input,size_t bytes,struct app_environment *out)
{
    if (!input || !out || !bytes || bytes>APP_PROFILE_BYTES) { return false; }
    const unsigned char *data=input;size_t at=0,first,end;
    if (!line(data,bytes,&at,&first,&end) || end!=sizeof(header)-1) { return false; }
    for (size_t i=0;i<end;++i) { if (data[i]!=(unsigned char)header[i]) { return false; } }
    struct app_environment next={0};
    while (at<bytes) {
        if (!line(data,bytes,&at,&first,&end)) { return false; }
        size_t split=first;while (split<end && data[split]!='=') { ++split; }
        if (split==end || split-first>=APP_ENV_NAME_BYTES || end-split-1>=APP_ENV_VALUE_BYTES) { return false; }
        char name[APP_ENV_NAME_BYTES]={0},value[APP_ENV_VALUE_BYTES]={0};
        for (size_t i=first;i<end;++i) { if (data[i]<32 || data[i]>126) { return false; } }
        for (size_t i=first;i<split;++i) { name[i-first]=(char)data[i]; }
        for (size_t i=split+1;i<end;++i) { value[i-split-1]=(char)data[i]; }
        unsigned old=next.count;
        if (app_env_set(&next,name,value)!=APP_ENV_OK || next.count!=old+1) { return false; }
    }
    *out=next;return true;
}
