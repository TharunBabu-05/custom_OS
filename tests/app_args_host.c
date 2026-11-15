#include "app_args.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"argument check failed at %d\n",__LINE__);exit(1); } } while (0)
static bool lookup(const char *key,char *out,size_t capacity,void *context)
{
    (void)context;const char *value="";
    if (!strcmp(key,"GREETING")) { value="hello world"; }else if (!strcmp(key,"?")) { value="42"; }
    else if (!strcmp(key,"VALUE")) { value="$GREETING; 'quoted' \\ literal"; }
    else if (!strcmp(key,"FAIL")) { return false; }
    else if (!strcmp(key,"CONTROL")) { value="bad\nvalue"; }
    else if (!strcmp(key,"LONG")) { if (capacity<128) { return false; }memset(out,'L',127);out[127]=0;return true; }
    if (strlen(value)>=capacity) { return false; }strcpy(out,value);return true;
}
static void expanded(void)
{
    struct app_args a;
    CHECK(app_args_expand("echo \"$GREETING\" '$GREETING' \\$GREETING pre${EMPTY}post ${MISSING} $? \"${VALUE}\"",lookup,NULL,&a));
    const char *words[]={"echo","hello world","$GREETING","$GREETING","prepost","","42","$GREETING; 'quoted' \\ literal"};
    CHECK(a.count==8);for (unsigned i=0;i<8;++i) { CHECK(!strcmp(a.bytes+a.offset[i],words[i])); }
    CHECK(app_args_parse("echo $GREETING ${MISSING} $?",&a) && !strcmp(a.bytes+a.offset[1],"$GREETING"));
    CHECK(app_args_expand("x $LONG$LONG$LONG$LONG $EMPTY",lookup,NULL,&a) && a.used==512 && a.count==3);
    CHECK(app_args_expand("echo $$ $(literal) ${?}",lookup,NULL,&a) && a.count==4 && !strcmp(a.bytes+a.offset[1],"$$") && !strcmp(a.bytes+a.offset[2],"$(literal)"));
    const char *bad[]={"echo ${}","echo ${GREETING","echo ${1BAD}","echo ${GREETING:-default}","echo $FAIL","echo $CONTROL",
        "x $LONG$LONG$LONG$LONG x","x $LONG$LONG$LONG$LONG $EMPTY $EMPTY","$MISSING","echo \"$GREETING", "echo $ABCDEFGHIJKLMNOPQRSTUVWXYZ_12345"};
    for (unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);++i) {
        memset(&a,0xa5,sizeof(a));CHECK(!app_args_expand(bad[i],lookup,NULL,&a));
        for (size_t j=0;j<sizeof(a);++j) { CHECK(!((unsigned char *)&a)[j]); }
    }
    puts("PASS: shell expansion quotes/escapes/braces/status, unset/empty words, non-recursive literal values, unchanged raw parser and exact 512-byte expansion bounds; failed output cleared.");
}
int main(void)
{
    struct app_args a;
    CHECK(app_args_parse("/boot/ECHO.ELF \"hello world\" \"Jack's\" '' one\\ two x\"y\"",&a));
    CHECK(a.count==6);CHECK(!strcmp(a.bytes+a.offset[1],"hello world"));CHECK(!strcmp(a.bytes+a.offset[2],"Jack's"));
    CHECK(!strcmp(a.bytes+a.offset[3],""));CHECK(!strcmp(a.bytes+a.offset[4],"one two"));CHECK(!strcmp(a.bytes+a.offset[5],"xy"));
    CHECK(app_args_parse("a b c d e f g h",&a) && a.count==8);
    const char *bad[]={"", "  ","''", "a b c d e f g h i", "a \\","a 'b", "a \"b", "a\n",NULL};
    for (unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);++i) {
        memset(&a,0xa5,sizeof(a));CHECK(!app_args_parse(bad[i],&a));
        for (size_t j=0;j<sizeof(a);++j) { CHECK(!((unsigned char *)&a)[j]); }
    }
    char boundary[513];memset(boundary,'x',sizeof(boundary));boundary[511]=0;
    CHECK(app_args_parse(boundary,&a) && a.used==512);boundary[511]='x';boundary[512]=0;CHECK(!app_args_parse(boundary,&a));
    /* Deterministic parser mutations: all successful offsets/strings remain
     * bounded, contiguous and terminated; all failed outputs are cleared. */
    unsigned seed=0x12345;
    for (unsigned n=0;n<20000;++n) {
        size_t len=n%513;for (size_t i=0;i<len;++i) { seed=seed*1664525+1013904223;boundary[i]=(char)(1+(seed>>24)%126); }boundary[len]=0;
        if (app_args_parse(boundary,&a)) {
            CHECK(a.count>=1 && a.count<=8 && a.used<=512 && a.used>0 && a.bytes[0]);unsigned end=0;
            for (unsigned i=0;i<a.count;++i) { CHECK(a.offset[i]==end);while (end<a.used && a.bytes[end]) { ++end; }CHECK(end<a.used);++end; }CHECK(end==a.used);
        } else { for (size_t i=0;i<sizeof(a);++i) { CHECK(!((unsigned char *)&a)[i]); } }
        if (app_args_expand(boundary,lookup,NULL,&a)) {
            CHECK(a.count>=1 && a.count<=8 && a.used<=512 && a.used>0 && a.bytes[0]);unsigned end=0;
            for (unsigned i=0;i<a.count;++i) { CHECK(a.offset[i]==end);while (end<a.used && a.bytes[end]) { CHECK(a.bytes[end]>=32 && a.bytes[end]<=126);++end; }CHECK(end<a.used);++end; }CHECK(end==a.used);
        }else { for (size_t i=0;i<sizeof(a);++i) { CHECK(!((unsigned char *)&a)[i]); } }
    }
    puts("PASS: quoted argument parsing, empty words, escapes, bounds, rejection clearing and 20000 mutations for both raw/expanded paths.");expanded();return 0;
}
