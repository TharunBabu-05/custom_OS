#ifndef OS_DNS_H
#define OS_DNS_H
#include "block.h"
#define DNS_NAME_MAX 253
#define DNS_MESSAGE_MAX 512
#define DNS_QUERY_MAX 271
enum dns_status { DNS_IDLE,DNS_PENDING,DNS_SUCCESS,DNS_NXDOMAIN,DNS_NODATA,DNS_SERVER_ERROR,DNS_TRUNCATED,DNS_UNSUPPORTED,DNS_TIMEOUT,DNS_CANCELLED };
/* One outstanding A/IN query, no cache, recursion, allocation or I/O here.
 * A response is only committed by the owner after a successful packet write. */
struct dns_query {
    enum dns_status status,observed_status;
    bool attempted,issued,written,seen;
    char name[DNS_NAME_MAX+1];unsigned id,port;
    unsigned char address[4];uint32_t ttl;unsigned cnames,rcode;
    unsigned sent,received,ignored,malformed;
};
bool dns_name_normalize(const char *,char out[DNS_NAME_MAX+1]);
enum block_result dns_begin(struct dns_query *,const char *,const unsigned char entropy[32]);
enum block_result dns_encode(const struct dns_query *,void *,size_t,size_t *);
void dns_receive(struct dns_query *,const void *,size_t);
const char *dns_status_text(enum dns_status);
bool dns_selftest(void);
#endif
