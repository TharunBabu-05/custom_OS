/* Original bounded DNS A stub resolver. Wire definitions: RFC 1035;
 * transaction/question/endpoint matching: RFC 5452. No DNSSEC or TCP. */
#include "dns.h"
static unsigned be16(const unsigned char *p) { return (unsigned)p[0]<<8|p[1]; }
static uint32_t be32(const unsigned char *p) { return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3]; }
static void put16(unsigned char *p,unsigned v) { p[0]=(unsigned char)(v>>8);p[1]=(unsigned char)v; }
static bool equal(const char *a,const char *b) { while (*a && *a==*b) { ++a;++b; }return *a==*b; }
static bool letter(unsigned c) { return (c>='a' && c<='z') || (c>='A' && c<='Z'); }
static bool digit(unsigned c) { return c>='0' && c<='9'; }
static char lower(unsigned c) { return (char)(c>='A' && c<='Z' ? c+32 : c); }
bool dns_name_normalize(const char *p,char out[DNS_NAME_MAX+1])
{
    if (!p || !out) { return false; }char name[DNS_NAME_MAX+1];unsigned n=0,label=0;
    for (;p[n];++n) {
        if (n>=DNS_NAME_MAX) { /* A final root dot is not part of the stored name. */
            if (n==DNS_NAME_MAX && p[n]=='.' && !p[n+1] && label && name[n-1]!='-') { break; }return false;
        }
        unsigned c=(unsigned char)p[n];
        if (c=='.') {
            if (!label || name[n-1]=='-') { return false; }if (!p[n+1]) { break; }label=0;name[n]='.';
        }else {
            if ((!letter(c) && !digit(c) && c!='-') || (!label && c=='-') || ++label>63) { return false; }name[n]=lower(c);
        }
    }
    if (!n || !label || name[n-1]=='-') { return false; }name[n]=0;for (unsigned i=0;i<=n;++i) { out[i]=name[i]; }return true;
}
enum block_result dns_begin(struct dns_query *s,const char *name,const unsigned char e[32])
{
    if (!s || !e) { return BLOCK_ARGUMENT; }char normalized[DNS_NAME_MAX+1];unsigned any=0;for (unsigned i=0;i<32;++i) { any|=e[i]; }
    unsigned id=be16(e),port=0; /* Rejection sampling avoids modulo bias over 1024..65535. */
    for (unsigned i=2;i<32;i+=2) { unsigned v=be16(e+i);if (v<64512) { port=1024+v;break; } }
    if (!any || !port || !dns_name_normalize(name,normalized) || (s->attempted && id==s->id && port==s->port)) { return BLOCK_ARGUMENT; }
    if (s->status==DNS_PENDING) { return BLOCK_NOT_READY; }
    *s=(struct dns_query){.attempted=true,.status=DNS_PENDING,.id=id,.port=port};for (unsigned i=0;i<=DNS_NAME_MAX;++i) { s->name[i]=normalized[i];if (!normalized[i]) { break; } }return BLOCK_OK;
}
enum block_result dns_encode(const struct dns_query *s,void *output,size_t cap,size_t *actual)
{
    if (actual) { *actual=0; }if (!s || !output || !actual) { return BLOCK_ARGUMENT; }
    if (s->status!=DNS_PENDING || s->port<1024 || s->port>65535 || s->id>65535) { return BLOCK_NOT_READY; }
    unsigned char p[DNS_QUERY_MAX]={0};put16(p,s->id);put16(p+2,0x100);put16(p+4,1);unsigned at=12,i=0;
    do {
        unsigned label=0,start=i;while (s->name[i] && s->name[i]!='.' && i<DNS_NAME_MAX) { ++i;++label; }
        if (!label || label>63 || at+label+1>DNS_QUERY_MAX-5) { return BLOCK_ARGUMENT; }p[at++]=(unsigned char)label;
        for (unsigned j=0;j<label;++j) { p[at++]=(unsigned char)s->name[start+j]; }
        if (!s->name[i]) { break; }++i;
    }while (i<DNS_NAME_MAX);
    p[at++]=0;put16(p+at,1);put16(p+at+2,1);at+=4;if (cap<at) { return BLOCK_RANGE; }
    for (unsigned j=0;j<at;++j) { ((unsigned char *)output)[j]=p[j]; }*actual=at;return BLOCK_OK;
}
/* Backward compression pointers only, bounded hops/expanded length. `used`
 * counts bytes in the original field, not bytes visited through pointers. */
static bool name_at(const unsigned char *p,size_t n,size_t at,char out[DNS_NAME_MAX+1],size_t *used)
{
    size_t start=at;unsigned length=0,hops=0;bool jumped=false;*used=0;
    for (unsigned steps=0;steps<128;++steps) {
        if (at>=n) { return false; }unsigned k=p[at++];
        if (!k) { if (!jumped) { *used=at-start; }out[length]=0;return true; }
        if ((k&0xc0)==0xc0) {
            if (at>=n || ++hops>16) { return false; }size_t target=((size_t)(k&63)<<8)|p[at++];
            if (target<12 || target>=at-2) { return false; }if (!jumped) { *used=at-start;jumped=true; }at=target;continue;
        }
        if (k>63 || k>n-at || length+k+(length ? 1u : 0u)>DNS_NAME_MAX) { return false; }
        if (length) { out[length++]='.'; }
        for (unsigned j=0;j<k;++j) { unsigned c=p[at++];if (!letter(c) && !digit(c) && c!='-' && c!='_') { return false; }out[length++]=lower(c); }
    }return false;
}
struct record { size_t owner,data;unsigned type,cls,bytes;uint32_t ttl; };
void dns_receive(struct dns_query *s,const void *buffer,size_t n)
{
    if (!s || s->status!=DNS_PENDING || !s->issued || s->seen || !buffer) { return; }++s->received;const unsigned char *p=buffer;
    if (n<12 || n>DNS_MESSAGE_MAX) { goto malformed; }
    unsigned flags=be16(p+2);if (be16(p)!=s->id || !(flags&0x8000) || (flags&0x7800) || be16(p+4)!=1) { goto ignored; }
    char owner[DNS_NAME_MAX+1],current[DNS_NAME_MAX+1],next[DNS_NAME_MAX+1];size_t used=0,at=12;
    if (!name_at(p,n,at,owner,&used) || used>n-at || n-at-used<4) { goto malformed; }at+=used;
    if (!equal(owner,s->name) || be16(p+at)!=1 || be16(p+at+2)!=1) { goto ignored; }at+=4;
    if (flags&0x40) { goto malformed; }
    /* A matched truncated response must not turn a partial RR into an address. */
    if (flags&0x200) { s->observed_status=DNS_TRUNCATED;s->rcode=flags&15;s->seen=true;return; }
    unsigned answers=be16(p+6),authority=be16(p+8),additional=be16(p+10);
    if (answers>32 || authority>32 || additional>32 || answers+authority+additional>32) { goto malformed; }
    struct record rr[32];
    for (unsigned i=0;i<answers+authority+additional;++i) {
        size_t pos=at;if (!name_at(p,n,at,owner,&used) || used>n-at || n-at-used<10) { goto malformed; }at+=used;
        unsigned type=be16(p+at),cls=be16(p+at+2),bytes=be16(p+at+8);uint32_t ttl=be32(p+at+4);at+=10;
        if (bytes>n-at) { goto malformed; }
        if (cls==1 && type==1 && bytes!=4) { goto malformed; }
        if (cls==1 && type==5 && (!name_at(p,n,at,next,&used) || used!=bytes || !next[0])) { goto malformed; }
        if (i<answers) { rr[i]=(struct record){pos,at,type,cls,bytes,ttl}; }at+=bytes;
    }
    if (at!=n) { goto malformed; }
    enum dns_status result=(flags&15)==3 ? DNS_NXDOMAIN : (flags&15) ? DNS_SERVER_ERROR : DNS_NODATA;
    unsigned cnames=0;uint32_t ttl=0xffffffffu,visited=0;unsigned char address[4]={0};
    if (!(flags&15)) {
        for (unsigned i=0;i<=DNS_NAME_MAX;++i) { current[i]=s->name[i];if (!current[i]) { break; } }
        for (unsigned hop=0;hop<=8;++hop) {
            bool have_a=false,have_cname=false;unsigned cname_index=0;uint32_t a_ttl=0,c_ttl=0;
            for (unsigned i=0;i<answers;++i) {
                if (rr[i].cls!=1 || !name_at(p,n,rr[i].owner,owner,&used) || !equal(owner,current)) { continue; }
                if (rr[i].type==1) {
                    const unsigned char *v=p+rr[i].data;
                    if (v[0] && v[0]!=127 && v[0]<224 && !have_a) { for (unsigned j=0;j<4;++j) { address[j]=v[j]; }a_ttl=rr[i].ttl;have_a=true; }
                }else if (rr[i].type==5) {
                    char alias[DNS_NAME_MAX+1];if (!name_at(p,n,rr[i].data,alias,&used)) { goto malformed; }
                    if (have_cname && !equal(alias,next)) { goto malformed; }
                    for (unsigned j=0;j<=DNS_NAME_MAX;++j) { next[j]=alias[j];if (!alias[j]) { break; } }have_cname=true;cname_index=i;c_ttl=rr[i].ttl;
                }
            }
            if (have_a && have_cname) { goto malformed; }
            if (have_a) { if (a_ttl<ttl) { ttl=a_ttl; }result=DNS_SUCCESS;break; }
            if (!have_cname) { break; }
            if (visited&(1u<<cname_index)) { goto malformed; }visited|=1u<<cname_index;
            if (hop==8) { result=DNS_UNSUPPORTED;break; }++cnames;if (c_ttl<ttl) { ttl=c_ttl; }
            for (unsigned j=0;j<=DNS_NAME_MAX;++j) { current[j]=next[j];if (!next[j]) { break; } }
        }
    }
    s->observed_status=result;s->rcode=flags&15;s->cnames=cnames;s->ttl=result==DNS_SUCCESS ? ttl : 0;
    for (unsigned i=0;i<4;++i) { s->address[i]=result==DNS_SUCCESS ? address[i] : 0; }s->seen=true;return;
ignored:++s->ignored;return;
malformed:++s->malformed;
}
const char *dns_status_text(enum dns_status s)
{
    switch (s) {
    case DNS_IDLE:return "not-attempted";case DNS_PENDING:return "pending";case DNS_SUCCESS:return "resolved";case DNS_NXDOMAIN:return "NXDOMAIN (name does not exist)";
    case DNS_NODATA:return "no usable A answer/complete CNAME chain";case DNS_SERVER_ERROR:return "DNS server error";case DNS_TRUNCATED:return "truncated response; TCP fallback unavailable";
    case DNS_UNSUPPORTED:return "CNAME hop limit exceeded";case DNS_TIMEOUT:return "reply deadline expired";case DNS_CANCELLED:return "cancelled";
    }return "invalid status";
}
