#include "trust_time.h"
#include "host_assert.h"
#include <stdio.h>
#include <string.h>

static uint64_t tick;
static char output[200];
void console_write(const char *text)
{
    assert(strlen(output)+strlen(text)<sizeof(output));
    strcat(output,text);
}
void console_putc(char c) { const char text[]={c,0}; console_write(text); }
void console_u64(uint64_t value)
{
    char text[21];
    snprintf(text,sizeof(text),"%llu",(unsigned long long)value);
    console_write(text);
}
uint64_t timer_ticks(void) { return tick; }
uint64_t timer_frequency(void) { return 100; }

static void system_clock_test(void)
{
    struct utc_sample sample;
    assert(system_utc(&sample)==UTC_UNSET);
    assert(!system_utc_set(42,true) && system_utc(&sample)==UTC_UNSET);
    output[0]=0;
    assert(system_utc_set(1796000000,true) && strstr(output,"source=rtc"));
    tick=100;
    output[0]=0;
    assert(!system_utc_set(1796000000,false) && strstr(output,"UTC would move backward"));
    assert(system_utc(&sample)==UTC_OK && sample.seconds==1796000001);
    output[0]=0;
    assert(trust_time_command("utc clear") && system_utc(&sample)==UTC_UNSET);
    output[0]=0;
    assert(trust_time_command("utc set 2026-11-24T12:00:00Z") && strstr(output,"source=operator"));
    puts("PASS: explicit RTC clock source, backward-time refusal, clear and operator source.");
}

int main(void)
{
    static const struct { const char *text;uint64_t seconds; } dates[]={
        {"1970-01-01T00:00:00Z",0},{"2000-02-29T00:00:00Z",951782400},
        {"2024-02-29T23:59:59Z",1709251199},{"2038-01-19T03:14:08Z",2147483648ULL},
        {"2100-03-01T00:00:00Z",4107542400ULL},{"9999-12-31T23:59:59Z",253402300799ULL}
    };
    for (unsigned i=0;i<sizeof(dates)/sizeof(dates[0]);++i) {
        uint64_t seconds=123;char text[UTC_TEXT_BYTES];
        assert(utc_parse(dates[i].text,&seconds) && seconds==dates[i].seconds);
        assert(utc_format(seconds,text) && !strcmp(text,dates[i].text));
    }
    static const char *const invalid[]={"","2026-11-22T23:59:59","2026-11-22T23:59:59ZZ","2026-11-22T23:59:60Z",
        "2026-11-22T24:00:00Z","2026-00-22T00:00:00Z","2026-04-31T00:00:00Z","2026-01-00T00:00:00Z",
        "2100-02-29T00:00:00Z","1969-12-31T00:00:00Z","2026-11-22T00:00:00+05:30"};
    uint64_t seconds=42;
    for (unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);++i) { assert(!utc_parse(invalid[i],&seconds) && seconds==42); }
    for (unsigned i=0;i<20;++i) {
        char text[UTC_TEXT_BYTES];memcpy(text,dates[1].text,sizeof(text));text[i]=0;
        assert(!utc_parse(text,&seconds) && seconds==42);
    }
    struct utc_clock clock={0};struct utc_sample now={123,4,8};uint64_t origin=1024,hz=100;
    assert(utc_clock_read(&clock,origin,hz,&now)==UTC_UNSET && now.seconds==123);
    assert(!utc_clock_set(&clock,0,origin,hz) && !utc_clock_set(&clock,1709251199,origin,0));
    assert(utc_clock_set(&clock,1709251199,origin,hz));
    assert(utc_clock_read(&clock,origin+99,hz,&now)==UTC_OK && !now.age && now.remaining==86400);
    assert(utc_clock_read(&clock,origin+100,hz,&now)==UTC_OK && now.seconds==1709251200 && now.age==1);
    assert(utc_clock_read(&clock,origin+8639999,hz,&now)==UTC_OK && now.remaining==1);
    assert(utc_clock_read(&clock,origin+8640000,hz,&now)==UTC_STALE && now.remaining==1);
    assert(utc_clock_read(&clock,origin+8630000,hz,&now)==UTC_COUNTER);
    assert(utc_clock_read(&clock,origin+8650000,hz,&now)==UTC_UNSET);
    assert(utc_clock_set(&clock,1709251199,origin,hz) && utc_clock_read(&clock,origin,2*hz,&now)==UTC_COUNTER);
    assert(utc_clock_set(&clock,253402300799ULL,origin,hz) && utc_clock_read(&clock,origin+hz,hz,&now)==UTC_STALE);

    unsigned char digest[32]={0},other[32]={1};struct trust_store store={0};
    const char *pin="0123456789aBcDeF0123456789ABCDEF0123456789abcdef0123456789ABCDEF";
    assert(trust_parse_digest(pin,digest) && digest[0]==1 && digest[2]==0x45 && digest[31]==0xef);
    assert(!trust_parse_digest("abc",digest) && digest[31]==0xef && !trust_parse_digest("",digest));
    char bad_pin[66];memcpy(bad_pin,pin,65);bad_pin[63]='g';assert(!trust_parse_digest(bad_pin,digest));
    bad_pin[63]='f';bad_pin[64]=' ';bad_pin[65]=0;assert(!trust_parse_digest(bad_pin,digest));
    assert(!trust_store_has(&store,digest) && trust_store_add(&store,digest));
    assert(trust_store_add(&store,digest) && store.count==1 && !trust_store_has(&store,other));
    now=(struct utc_sample){1000,0,1};
    assert(trust_check(&store,digest,NULL,999,1000)==TRUST_NO_TIME);
    assert(trust_check(&store,other,&now,999,1001)==TRUST_UNKNOWN);
    assert(trust_check(&store,digest,&now,1000,1000)==TRUST_OK);
    assert(trust_check(&store,digest,&now,1001,1002)==TRUST_CERT_TIME);
    assert(trust_check(&store,digest,&now,998,999)==TRUST_CERT_TIME);
    assert(trust_check(&store,digest,&now,1001,999)==TRUST_CERT_TIME);
    assert(trust_check(&store,digest,&now,0,UINT64_MAX)==TRUST_CERT_TIME);
    now.remaining=0;assert(trust_check(&store,digest,&now,999,1000)==TRUST_NO_TIME);
    for (unsigned i=1;i<TRUST_ANCHORS;++i) { other[0]=(unsigned char)i;assert(trust_store_add(&store,other)); }
    other[0]=4;assert(!trust_store_add(&store,other) && store.count==TRUST_ANCHORS);
    puts("PASS: UTC calendar and rollover, stale/counter invalidation, exact bounded anchors and certificate validity boundaries.");
    system_clock_test();
    return 0;
}
