#include "trust_time.h"
#include "console.h"
#include "cpu.h"

static struct utc_clock clock;
static struct trust_store anchors;

enum utc_result system_utc(struct utc_sample *sample)
{ return utc_clock_read(&clock,timer_ticks(),timer_frequency(),sample); }
enum trust_result system_trust_check(const unsigned char digest[32],uint64_t lower,uint64_t upper)
{
    struct utc_sample now;
    return trust_check(&anchors,digest,system_utc(&now)==UTC_OK ? &now : NULL,lower,upper);
}
static bool match(const char *a,const char *b)
{ while (*a && *a==*b) { ++a;++b; }return *a==*b; }
static bool prefix(const char *text,const char *start)
{ while (*start) { if (*text++!=*start++) { return false; } }return true; }
static bool token(const char **line,char *out,unsigned length)
{
    const char *p=*line;
    for (unsigned i=0;i<length;++i) {
        if (!*p || *p==' ') { return false; }out[i]=*p++;
    }
    if (*p && *p!=' ') { return false; }
    out[length]=0;*line=p;return true;
}
static bool valid_range(const char *params,uint64_t *lower,uint64_t *upper)
{
    char date[UTC_TEXT_BYTES];
    if (*params++!=' ' || !token(&params,date,20) || !utc_parse(date,lower)) { return false; }
    if (*params++!=' ' || !token(&params,date,20) || *params || !utc_parse(date,upper)) { return false; }
    return *lower<=*upper;
}
static void show_utc(void)
{
    struct utc_sample now;enum utc_result r=system_utc(&now);
    if (r!=UTC_OK) {
        console_write(r==UTC_STALE ? "UTC expired. Set the current UTC again.\n" :
            r==UTC_COUNTER ? "UTC invalidated after a counter change. Set UTC again.\n" : "UTC unset. Use utc set <YYYY-MM-DDTHH:MM:SSZ>.\n");return;
    }
    char date[UTC_TEXT_BYTES];(void)utc_format(now.seconds,date);
    console_write("UTC ");console_write(date);console_write(" source=operator age_seconds=");
    console_u64(now.age);console_write(" remaining_seconds=");console_u64(now.remaining);console_write("\n");
}
static void list_anchors(void)
{
    static const char digits[]="0123456789abcdef";
    console_write("Trusted SHA-256 anchors=");console_u64(anchors.count);console_write("\n");
    for (unsigned i=0;i<anchors.count;++i) {
        const unsigned char *hash=anchors.anchors[i];
        for (unsigned j=0;j<32;++j) { console_putc(digits[hash[j]>>4]);console_putc(digits[hash[j]&15]); }
        console_write("\n");
    }
}
bool trust_time_command(const char *line)
{
    if (match(line,"utc")) { show_utc();return true; }
    if (match(line,"utc clear")) { clock=(struct utc_clock){0};console_write("UTC cleared.\n");return true; }
    if (prefix(line,"utc ")) {
        uint64_t seconds;struct utc_sample now;
        if (prefix(line,"utc set ") && utc_parse(line+8,&seconds)) {
            if (system_utc(&now)==UTC_OK && seconds<now.seconds) { console_write("UTC would move backward. Use utc clear to correct it.\n");return true; }
            if (utc_clock_set(&clock,seconds,timer_ticks(),timer_frequency())) { show_utc();return true; }
        }
        console_write("Usage: utc set <YYYY-MM-DDTHH:MM:SSZ> (year 2000 or later) | utc clear\n");return true;
    }
    if (match(line,"trustlist")) { list_anchors();return true; }
    if (match(line,"trustclear")) { anchors=(struct trust_store){0};console_write("Trust anchors cleared.\n");return true; }
    if (match(line,"trustadd") || prefix(line,"trustadd ")) {
        unsigned char digest[32];
        if (!line[8] || !trust_parse_digest(line+9,digest)) { console_write("Usage: trustadd <64 hex digits>\n");return true; }
        console_write(trust_store_add(&anchors,digest) ? "Anchor enrolled for this boot.\n" : "Trust store full.\n");return true;
    }
    if (match(line,"trustcheck") || prefix(line,"trustcheck ")) {
        unsigned char digest[32];char text[65];uint64_t lower,upper;
        const char *args=line+10;
        if (*args++!=' ' || !token(&args,text,64) || !trust_parse_digest(text,digest) || !valid_range(args,&lower,&upper)) {
            console_write("Usage: trustcheck <SHA-256> <not-before UTC> <not-after UTC>\n");return true;
        }
        static const char *const results[]={"Anchor and UTC validity passed.\n","Anchor is not enrolled.\n",
            "Trust check refused. Set UTC first.\n","UTC falls outside the certificate validity window.\n"};
        console_write(results[system_trust_check(digest,lower,upper)]);return true;
    }
    return false;
}
