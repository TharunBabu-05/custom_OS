#include "app_args.h"
static void clear(struct app_args *a)
{ unsigned char *p=(unsigned char *)a;for (size_t i=0;i<sizeof(*a);++i) { p[i]=0; } }
bool app_args_parse(const char *line,struct app_args *out)
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
            if ((unsigned char)c<32 || (unsigned char)c>126 || out->used>=APP_ARG_BYTES-1) { goto bad; }
            out->bytes[out->used++]=c;
        }
        if (quote || out->used==APP_ARG_BYTES) { goto bad; }out->bytes[out->used++]=0;
    }
    if (!out->count || !out->bytes[0]) { goto bad; }return true;
bad:clear(out);return false;
}
