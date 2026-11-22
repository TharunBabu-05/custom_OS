#include "pinned_cert.h"
#include "trust_time.h"
#include "ecdsa.h"
#include "crypto.h"

struct der { const unsigned char *p;size_t n; };
static bool take(struct der *in,unsigned tag,struct der *out)
{
    if (in->n<2 || in->p[0]!=tag) { return false; }
    const unsigned char *p=in->p;size_t at=2,n=p[1];
    if (n&128) {
        unsigned count=(unsigned)n&127;
        if (!count || count>2 || in->n<2+count || !p[2]) { return false; }
        n=0;for (unsigned i=0;i<count;++i) { n=(n<<8)|p[at++]; }
        if (n<128) { return false; }
    }
    if (n>in->n-at) { return false; }
    *out=(struct der){p+at,n};in->p+=at+n;in->n-=at+n;return true;
}
static bool exact(struct der in,const unsigned char *p,size_t n)
{ return in.n==n && crypto_equal(in.p,p,n); }
static bool single(struct der in,unsigned tag,struct der *out)
{ return take(&in,tag,out) && !in.n; }
static bool positive(struct der in,size_t max)
{
    if (!in.n || in.p[0]&128 || (in.p[0]==0 && (in.n==1 || !(in.p[1]&128)))) { return false; }
    return in.n-(in.p[0]==0)<=max;
}
static bool scalar(struct der *in,unsigned char out[32])
{
    struct der value;
    if (!take(in,2,&value) || !positive(value,32)) { return false; }
    if (!value.p[0]) { ++value.p;--value.n; }
    for (unsigned i=0;i<32;++i) { out[i]=i<32-value.n ? 0 : value.p[i-(32-value.n)]; }
    return true;
}
bool ecdsa_p256_decode_der(const void *bytes,size_t length,unsigned char r[32],unsigned char s[32])
{
    struct der sequence;
    return bytes && single((struct der){bytes,length},0x30,&sequence) &&
        scalar(&sequence,r) && scalar(&sequence,s) && !sequence.n;
}
static bool time_value(struct der *in,uint64_t *out)
{
    struct der value;char text[UTC_TEXT_BYTES];unsigned at=0,off=0;
    if (take(in,0x17,&value)) {
        if (value.n!=13) { return false; }
        bool twentieth=value.p[0]>='5';text[at++]=twentieth?'1':'2';text[at++]=twentieth?'9':'0';
    } else if (!take(in,0x18,&value) || value.n!=15) { return false; }
    else if (value.p[0]<'2' || (value.p[0]=='2' && value.p[1]=='0' && value.p[2]<'5')) { return false; }
    while (at<4) { text[at++]=(char)value.p[off++]; }
    const char separators[]={'-','-','T',':',':'};
    for (unsigned i=0;i<sizeof(separators);++i) {
        text[at++]=separators[i];text[at++]=(char)value.p[off++];text[at++]=(char)value.p[off++];
    }
    text[at++]=(char)value.p[off];text[at]=0;
    return utc_parse(text,out);
}
static unsigned char lower(unsigned char c) { return c>='A' && c<='Z' ? (unsigned char)(c+32) : c; }
static bool dns(struct der name)
{
    if (!name.n || name.n>253) { return false; }
    unsigned label=0;
    for (size_t i=0;i<name.n;++i) {
        unsigned c=lower(name.p[i]);
        if (c=='.') {
            if (!label || name.p[i-1]=='-') { return false; }label=0;continue;
        }
        if (!((c>='a' && c<='z') || (c>='0' && c<='9') || (c=='-' && label)) || ++label>63) { return false; }
    }
    return label && name.p[name.n-1]!='-';
}
static bool names_match(struct der a,struct der b)
{
    if (a.n!=b.n) { return false; }
    for (size_t i=0;i<a.n;++i) { if (lower(a.p[i])!=lower(b.p[i])) { return false; } }
    return true;
}
static bool extensions(struct der in,struct der expected,bool *matches)
{
    static const unsigned char server_auth[]={6,8,0x2b,6,1,5,5,7,3,1};
    static const unsigned char digital_signature[]={3,2,7,0x80},end_entity[]={0x30,0};
    if (!single(in,0x30,&in)) { return false; }
    unsigned seen=0;
    while (in.n) {
        struct der item,oid,critical,body,sequence;
        if (!take(&in,0x30,&item) || !take(&item,6,&oid) || oid.n!=3 || oid.p[0]!=0x55 || oid.p[1]!=0x1d) { return false; }
        if (take(&item,1,&critical) && (critical.n!=1 || critical.p[0]!=255)) { return false; }
        if (!single(item,4,&body)) { return false; }
        unsigned bit;
        switch (oid.p[2]) {
        case 17:
            if (!single(body,0x30,&sequence) || !sequence.n) { return false; }
            while (sequence.n) {
                struct der name;
                if (!take(&sequence,0x82,&name) || !dns(name)) { return false; }
                *matches|=names_match(name,expected);
            }
            bit=1;break;
        case 15:if (!exact(body,digital_signature,sizeof(digital_signature))) { return false; }bit=2;break;
        case 19:if (!exact(body,end_entity,sizeof(end_entity))) { return false; }bit=4;break;
        case 37:if (!single(body,0x30,&sequence) || !exact(sequence,server_auth,sizeof(server_auth))) { return false; }bit=8;break;
        default:return false;
        }
        if (seen&bit) { return false; }seen|=bit;
    }
    return seen==15;
}
static void digest(const void *bytes,size_t n,unsigned char out[32])
{
    struct sha256 hash;sha256_init(&hash);sha256_update(&hash,bytes,n);sha256_final(&hash,out);
}
enum cert_result pinned_cert_verify(const void *data,size_t n,const char *name,struct pinned_cert *out)
{
    static const unsigned char version3[]={2,1,2};
    static const unsigned char sig_alg[]={6,8,0x2a,0x86,0x48,0xce,0x3d,4,3,2};
    static const unsigned char ec_alg[]={6,7,0x2a,0x86,0x48,0xce,0x3d,2,1,6,8,0x2a,0x86,0x48,0xce,0x3d,3,1,7};
    if (!data || !n || n>PINNED_CERT_MAX || !name || !out) { return CERT_FORMAT; }
    size_t name_bytes=0;while (name_bytes<=253 && name[name_bytes]) { ++name_bytes; }
    struct der expected={(const unsigned char *)name,name_bytes};
    if (!dns(expected)) { return CERT_NAME; }
    struct der cert={data,n},tbs,alg,sig,field,issuer,subject,validity,spki,key;
    if (!single(cert,0x30,&cert)) { return CERT_FORMAT; }
    const unsigned char *signed_bytes=cert.p;
    if (!take(&cert,0x30,&tbs)) { return CERT_FORMAT; }
    size_t signed_size=(size_t)(cert.p-signed_bytes);
    if (!take(&cert,0x30,&alg) || !exact(alg,sig_alg,sizeof(sig_alg)) || !single(cert,3,&sig) || !sig.n || *sig.p) {
        return CERT_FORMAT;
    }
    unsigned char r[32],s[32];
    if (!ecdsa_p256_decode_der(sig.p+1,sig.n-1,r,s)) { return CERT_FORMAT; }
    struct pinned_cert result={0};bool matches=false;
    if (!take(&tbs,0xa0,&field) || !exact(field,version3,sizeof(version3)) || !take(&tbs,2,&field) || !positive(field,20) ||
        !take(&tbs,0x30,&alg) || !exact(alg,sig_alg,sizeof(sig_alg)) || !take(&tbs,0x30,&issuer) || !issuer.n ||
        !take(&tbs,0x30,&validity) || !time_value(&validity,&result.not_before) || !time_value(&validity,&result.not_after) ||
        validity.n || result.not_before>result.not_after || !take(&tbs,0x30,&subject) || !exact(subject,issuer.p,issuer.n) ||
        !take(&tbs,0x30,&spki) || !take(&spki,0x30,&alg) || !exact(alg,ec_alg,sizeof(ec_alg)) ||
        !single(spki,3,&key) || key.n!=66 || key.p[0]!=0 || key.p[1]!=4 ||
        !single(tbs,0xa3,&field) || !extensions(field,expected,&matches)) { return CERT_FORMAT; }
    if (!matches) { return CERT_NAME; }
    unsigned char hash[32];digest(signed_bytes,signed_size,hash);
    if (!ecdsa_p256_verify(key.p+2,hash,r,s)) { return CERT_SIGNATURE; }
    digest(data,n,result.digest);for (unsigned i=0;i<64;++i) { result.key[i]=key.p[2+i]; }
    *out=result;return CERT_OK;
}
