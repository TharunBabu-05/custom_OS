#include "app_args.h"
#include "app_env.h"
static void clear(struct app_args *a)
{ unsigned char *p=(unsigned char *)a;for (size_t i=0;i<sizeof(*a);++i) { p[i]=0; } }
static bool identifier(char c,bool first)
{ return (c>='a' && c<='z') || (c>='A' && c<='Z') || c=='_' || (!first && c>='0' && c<='9'); }
static bool expand(const char *line,size_t length,size_t *i,app_args_lookup lookup,void *context,struct app_args *out)
{
    char key[APP_ENV_NAME_BYTES]={0},value[APP_ENV_VALUE_BYTES];size_t used=0;bool braces=false;
    if (*i<length && line[*i]=='{') { braces=true;++*i; }
    if (*i<length && line[*i]=='?') { key[used++]=line[(*i)++]; }
    else {
        while (*i<length && identifier(line[*i],used==0)) {
            if (used==sizeof(key)-1) { return false; }key[used++]=line[(*i)++];
        }
    }
    if (braces) { if (!used || *i==length || line[*i]!='}') { return false; }++*i; }
    if (!used) { if (out->used>=APP_ARG_BYTES-1) { return false; }out->bytes[out->used++]='$';return true; }
    if (!lookup(key,value,sizeof(value),context)) { return false; }
    size_t n=0;while (n<sizeof(value) && value[n]) {
        if ((unsigned char)value[n]<32 || (unsigned char)value[n]>126) { return false; }++n;
    }
    if (n==sizeof(value) || out->used>APP_ARG_BYTES-1 || n>APP_ARG_BYTES-1-out->used) { return false; }
    for (size_t j=0;j<n;++j) { out->bytes[out->used++]=value[j]; }return true;
}
static bool parse(const char *line,struct app_args *out,app_args_lookup lookup,void *context)
{
    if (!out) { return false; }clear(out);if (!line) { return false; }
    size_t length=0;while (length<APP_ARG_BYTES && line[length]) { ++length; }
    if (length==APP_ARG_BYTES) { return false; }
    size_t i=0;
    while (i<length) {
        while (line[i]==' ' || line[i]=='\t') { ++i; }if (i==length) { break; }
        if (out->count==APP_ARG_LIMIT) { goto bad; }
        out->offset[out->count++]=out->used;char quote=0;
        while (i<length) {
            char c=line[i++];
            if (!quote && (c==' ' || c=='\t')) { break; }
            if (c=='\\') { if (i==length) { goto bad; }c=line[i++]; }
            else if (c=='\'' || c=='"') {
                if (!quote) { quote=c;continue; }if (c==quote) { quote=0;continue; }
            }
            else if (c=='$' && lookup && quote!='\'') {
                if (!expand(line,length,&i,lookup,context,out)) { goto bad; }continue;
            }
            if ((unsigned char)c<32 || (unsigned char)c>126 || out->used>=APP_ARG_BYTES-1) { goto bad; }
            out->bytes[out->used++]=c;
        }
        if (quote || out->used==APP_ARG_BYTES) { goto bad; }out->bytes[out->used++]=0;
    }
    if (!out->count || !out->bytes[0]) { goto bad; }return true;
bad:clear(out);return false;
}
bool app_args_parse(const char *line,struct app_args *out)
{ return parse(line,out,NULL,NULL); }
bool app_args_expand(const char *line,app_args_lookup lookup,void *context,struct app_args *out)
{ if (!lookup) { if (out) { clear(out); }return false; }return parse(line,out,lookup,context); }
