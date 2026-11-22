/* Explicit UTC and exact SHA-256 anchor identity for later TLS use. */
#include "trust_time.h"
#include "crypto.h"

#define UTC_LAST 253402300799ULL
#define UTC_FIRST_SET 946684800ULL
static bool leap(unsigned y) { return !(y%4) && (y%100 || !(y%400)); }
static unsigned month_days(unsigned y,unsigned m)
{
    static const unsigned char normal[]={31,28,31,30,31,30,31,31,30,31,30,31};
    return normal[m-1]+(m==2 && leap(y));
}
static unsigned leap_count(unsigned y) { return y/4-y/100+y/400; }
static unsigned year_days_before(unsigned y) { return (y-1970)*365+leap_count(y-1)-leap_count(1969); }
static bool fixed(const char *p,unsigned n,unsigned *out)
{
    unsigned value=0;
    for (unsigned i=0;i<n;++i) {
        if (p[i]<'0' || p[i]>'9') { return false; }
        value=value*10+(unsigned)(p[i]-'0');
    }
    *out=value;return true;
}
bool utc_parse(const char *text,uint64_t *seconds)
{
    if (!text || !seconds) { return false; }
    for (unsigned i=0;i<20;++i) { if (!text[i]) { return false; } }
    unsigned y,m,d,h,min,s;
    if (text[20] || text[4]!='-' || text[7]!='-' || text[10]!='T' || text[13]!=':' || text[16]!=':' || text[19]!='Z' ||
        !fixed(text,4,&y) || !fixed(text+5,2,&m) || !fixed(text+8,2,&d) || !fixed(text+11,2,&h) ||
        !fixed(text+14,2,&min) || !fixed(text+17,2,&s) || y<1970 || !m || m>12 || !d || d>month_days(y,m) ||
        h>23 || min>59 || s>59) { return false; }
    unsigned days=year_days_before(y)+d-1;
    for (unsigned i=1;i<m;++i) { days+=month_days(y,i); }
    *seconds=(uint64_t)days*86400+h*3600+min*60+s;return true;
}
static void put(char *p,unsigned n,unsigned value)
{
    while (n) { p[--n]=(char)('0'+value%10);value/=10; }
}
bool utc_format(uint64_t seconds,char out[UTC_TEXT_BYTES])
{
    if (!out || seconds>UTC_LAST) { return false; }
    unsigned days=(unsigned)(seconds/86400),tod=(unsigned)(seconds%86400);
    unsigned low=1970,high=10000;
    while (high-low>1) {
        unsigned mid=(low+high)/2;
        if (year_days_before(mid)>days) { high=mid; }else { low=mid; }
    }
    days-=year_days_before(low);unsigned month=1;
    while (days>=month_days(low,month)) { days-=month_days(low,month);++month; }
    put(out,4,low);out[4]='-';put(out+5,2,month);out[7]='-';put(out+8,2,days+1);out[10]='T';
    put(out+11,2,tod/3600);out[13]=':';put(out+14,2,tod/60%60);out[16]=':';put(out+17,2,tod%60);
    out[19]='Z';out[20]=0;return true;
}
bool utc_clock_set(struct utc_clock *clock,uint64_t seconds,uint64_t ticks,uint64_t hz)
{
    if (!clock || !hz || seconds<UTC_FIRST_SET || seconds>UTC_LAST) { return false; }
    *clock=(struct utc_clock){seconds,ticks,ticks,hz};return true;
}
enum utc_result utc_clock_read(struct utc_clock *clock,uint64_t ticks,uint64_t hz,struct utc_sample *out)
{
    if (!clock || !out || !clock->hz) { return UTC_UNSET; }
    if (hz!=clock->hz || ticks<clock->last_ticks) { clock->hz=0;return UTC_COUNTER; }
    clock->last_ticks=ticks;
    uint64_t age=(ticks-clock->start_ticks)/hz;
    if (age>=UTC_MAX_AGE || age>UTC_LAST-clock->seconds) { return UTC_STALE; }
    *out=(struct utc_sample){clock->seconds+age,(uint32_t)age,UTC_MAX_AGE-(uint32_t)age};return UTC_OK;
}
bool utc_validity_contains(uint64_t lower,uint64_t upper,uint64_t now)
{ return lower<=now && now<=upper && upper<=UTC_LAST; }

static unsigned hex(char c)
{
    if (c>='0' && c<='9') { return (unsigned)(c-'0'); }
    c=(char)(c|32);
    return c>='a' && c<='f' ? (unsigned)(c-'a'+10) : 16;
}
bool trust_parse_digest(const char *text,unsigned char out[32])
{
    if (!text || !out) { return false; }
    unsigned char digest[32];
    for (unsigned i=0;i<32;++i) {
        unsigned high=hex(*text++);if (high>15) { return false; }
        unsigned low=hex(*text++);if (low>15) { return false; }
        digest[i]=(unsigned char)((high<<4)|low);
    }
    if (*text) { return false; }
    for (unsigned i=0;i<32;++i) { out[i]=digest[i]; }
    return true;
}
bool trust_store_has(const struct trust_store *store,const unsigned char digest[32])
{
    if (!store || !digest || store->count>TRUST_ANCHORS) { return false; }
    for (unsigned i=0;i<store->count;++i) { if (crypto_equal(store->anchors[i],digest,32)) { return true; } }
    return false;
}
bool trust_store_add(struct trust_store *store,const unsigned char digest[32])
{
    if (trust_store_has(store,digest)) { return true; }
    if (!store || !digest || store->count>=TRUST_ANCHORS) { return false; }
    for (unsigned i=0;i<32;++i) { store->anchors[store->count][i]=digest[i]; }
    ++store->count;return true;
}
enum trust_result trust_check(const struct trust_store *store,const unsigned char digest[32],const struct utc_sample *sample,uint64_t lower,uint64_t upper)
{
    if (!sample || !sample->remaining) { return TRUST_NO_TIME; }
    if (!trust_store_has(store,digest)) { return TRUST_UNKNOWN; }
    return utc_validity_contains(lower,upper,sample->seconds) ? TRUST_OK : TRUST_CERT_TIME;
}
