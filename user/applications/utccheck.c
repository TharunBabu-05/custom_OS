#include "user.h"

static void number(uint64_t number)
{
    char text[20];unsigned i=sizeof(text);
    do { text[--i]=(char)('0'+number%10);number/=10; } while (number);
    uos_write(text+i,sizeof(text)-i);
}
int app_main(void)
{
    struct uos_utc_time before={1,2,3},after;
    if (uos_utc_time((void *)(uintptr_t)1)!=UOS_EFAULT) { return 2; }
    int64_t r=uos_utc_time(&before);
    if (r==UOS_ENOTIME) {
        if (before.unix_seconds!=1 || before.age_seconds!=2 || before.remaining_seconds!=3) { return 3; }
        uos_puts("PASS: UTC unavailable and output preserved.\n");return 0;
    }
    if (r) { return 4; }
    int64_t start=uos_time_ms();
    if (uos_sleep_ms(1000) || uos_time_ms()-start<1000 || uos_utc_time(&after) ||
        after.age_seconds<=before.age_seconds || after.unix_seconds-before.unix_seconds!=after.age_seconds-before.age_seconds ||
        after.age_seconds+after.remaining_seconds!=86400) { return 5; }
    uos_puts("PASS: UTC advanced with the counter. Unix seconds=");number(after.unix_seconds);uos_puts("\n");
    return 0;
}
