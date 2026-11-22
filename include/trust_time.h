#ifndef OS_TRUST_TIME_H
#define OS_TRUST_TIME_H
#include <stdbool.h>
#include <stdint.h>

enum { UTC_TEXT_BYTES=21,UTC_MAX_AGE=86400,TRUST_ANCHORS=4 };
enum utc_result { UTC_OK,UTC_UNSET,UTC_STALE,UTC_COUNTER };
struct utc_clock { uint64_t seconds,start_ticks,last_ticks,hz; };
struct utc_sample { uint64_t seconds;uint32_t age,remaining; };

bool utc_parse(const char *,uint64_t *);
bool utc_format(uint64_t,char [UTC_TEXT_BYTES]);
bool utc_clock_set(struct utc_clock *,uint64_t seconds,uint64_t ticks,uint64_t hz);
enum utc_result utc_clock_read(struct utc_clock *,uint64_t ticks,uint64_t hz,struct utc_sample *);
bool utc_validity_contains(uint64_t not_before,uint64_t not_after,uint64_t now);

struct trust_store { unsigned count;unsigned char anchors[TRUST_ANCHORS][32]; };
enum trust_result { TRUST_OK,TRUST_UNKNOWN,TRUST_NO_TIME,TRUST_CERT_TIME };
bool trust_parse_digest(const char *,unsigned char [32]);
bool trust_store_add(struct trust_store *,const unsigned char [32]);
bool trust_store_has(const struct trust_store *,const unsigned char [32]);
/* Policy preflight. Inputs must come from the caller's certificate parser.
 * A successful result is only an anchor/validity check. */
enum trust_result trust_check(const struct trust_store *,const unsigned char [32],const struct utc_sample *,uint64_t not_before,uint64_t not_after);

enum utc_result system_utc(struct utc_sample *);
enum trust_result system_trust_check(const unsigned char [32],uint64_t not_before,uint64_t not_after);
bool trust_time_command(const char *);
#endif
