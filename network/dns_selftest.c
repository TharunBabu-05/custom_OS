#include "dns.h"
bool dns_selftest(void)
{
    /* Public synthetic example.com -> 192.0.2.80, ID 0x1234. */
    static const unsigned char response[]={0x12,0x34,0x81,0x80,0,1,0,1,0,0,0,0,7,'e','x','a','m','p','l','e',3,'c','o','m',0,0,1,0,1,0xc0,12,0,1,0,1,0,0,0,60,0,4,192,0,2,80};
    unsigned char entropy[32]={0x12,0x34,0xab,0xcd},wire[DNS_QUERY_MAX];size_t bytes=0;struct dns_query s={0};
    if (dns_begin(&s,"Example.COM.",entropy)!=BLOCK_OK || dns_encode(&s,wire,sizeof(wire),&bytes)!=BLOCK_OK || bytes!=29 || wire[2]!=1 || wire[3] || wire[4] || wire[5]!=1) { return false; }
    for (unsigned i=12;i<29;++i) { if (wire[i]!=response[i]) { return false; } }
    s.issued=true;dns_receive(&s,response,sizeof(response));return s.status==DNS_PENDING && s.seen && s.observed_status==DNS_SUCCESS && s.address[0]==192 && s.address[3]==80 && s.ttl==60 && !s.cnames;
}
