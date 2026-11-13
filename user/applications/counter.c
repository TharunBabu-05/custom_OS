#include "user.h"
static size_t decimal(char *p,uint64_t n)
{
    char reverse[20];size_t count=0;do { reverse[count++]=(char)('0'+n%10);n/=10; }while (n);
    for (size_t i=0;i<count;++i) { p[i]=reverse[count-i-1]; }return count;
}
int app_main(void)
{
    for (unsigned step=1;step<=3;++step) {
        char line[64];const char *prefix="EL0 counter step=";size_t n=0;
        while (*prefix) { line[n++]=*prefix++; }
        line[n++]=(char)('0'+step);const char *middle=" time_ms=";
        while (*middle) { line[n++]=*middle++; }
        int64_t now=uos_time_ms();if (now<0) { return 2; }n+=decimal(line+n,(uint64_t)now);line[n++]='\n';
        if (uos_write(line,n)!=(int64_t)n || uos_yield()!=0) { return 1; }
    }return 0;
}
