#include "tls13_auth.h"
#include "crypto.h"
#include "ecdsa.h"
#include "tls13_keys.h"

struct bytes { const unsigned char *p;size_t n; };

static bool take(struct bytes *in,size_t count,struct bytes *out)
{
    if (count>in->n) { return false; }
    *out=(struct bytes){in->p,count};
    in->p+=count;
    in->n-=count;
    return true;
}

static bool vector(struct bytes *in,size_t width,struct bytes *out)
{
    struct bytes prefix;
    if (!take(in,width,&prefix)) { return false; }
    size_t length=0;
    for (size_t i=0;i<width;++i) { length=(length<<8)|prefix.p[i]; }
    return take(in,length,out);
}

static unsigned u16(const unsigned char *p)
{ return ((unsigned)p[0]<<8)|p[1]; }

static bool extensions(struct bytes in)
{
    struct bytes list,tag,extension;
    if (!vector(&in,2,&list) || in.n) { return false; }
    while (list.n) {
        if (!take(&list,2,&tag) || !vector(&list,2,&extension)) { return false; }
    }
    return true;
}

static bool client_hello(struct bytes in,struct bytes *session)
{
    struct bytes prefix,suites,compression;
    if (!take(&in,34,&prefix) || u16(prefix.p)!=0x0303 ||
        !vector(&in,1,session) || session->n>32 ||
        !vector(&in,2,&suites) || (suites.n&1) ||
        !vector(&in,1,&compression) || compression.n!=1 || compression.p[0] ||
        !extensions(in)) { return false; }
    for (size_t i=0;i<suites.n;i+=2) {
        if (u16(suites.p+i)==0x1301) { return true; }
    }
    return false;
}

static bool server_hello(struct bytes in,struct bytes session,const unsigned char **key)
{
    static const unsigned char retry_random[32]={
        0xcf,0x21,0xad,0x74,0xe5,0x9a,0x61,0x11,0xbe,0x1d,0x8c,0x02,0x1e,0x65,0xb8,0x91,
        0xc2,0xa2,0x11,0x16,0x7a,0xbb,0x8c,0x5e,0x07,0x9e,0x09,0xe2,0xc8,0xa8,0x33,0x9c
    };
    struct bytes prefix,echo,suite,list,tag,extension;
    if (!take(&in,34,&prefix) || u16(prefix.p)!=0x0303 || crypto_equal(prefix.p+2,retry_random,32) ||
        !vector(&in,1,&echo) || echo.n!=session.n || !crypto_equal(echo.p,session.p,echo.n) ||
        !take(&in,3,&suite) || u16(suite.p)!=0x1301 || suite.p[2] ||
        !vector(&in,2,&list) || in.n) { return false; }
    bool version=false,share=false;
    while (list.n) {
        if (!take(&list,2,&tag) || !vector(&list,2,&extension)) { return false; }
        const unsigned char *data=extension.p;
        switch (u16(tag.p)) {
        case 43: /* supported_versions */
            if (version || extension.n!=2 || u16(data)!=0x0304) { return false; }
            version=true;
            break;
        case 51: /* key_share */
            if (share || extension.n!=69 || u16(data)!=23 || u16(data+2)!=65 || data[4]!=4) { return false; }
            *key=data+5;
            share=true;
            break;
        default:
            return false;
        }
    }
    return version && share;
}

const unsigned char *tls13_server_share(const void *hellos,size_t length)
{
    if (!hellos || length>TLS13_TRANSCRIPT_MAX) { return NULL; }
    struct bytes in={hellos,length},type,client,server,session;
    const unsigned char *share=NULL;
    if (!take(&in,1,&type) || *type.p!=1 || !vector(&in,3,&client) ||
        !take(&in,1,&type) || *type.p!=2 || !vector(&in,3,&server) || in.n ||
        !client_hello(client,&session) || !server_hello(server,session,&share)) { return NULL; }
    return share;
}

static bool certificate(struct bytes in,struct bytes *leaf)
{
    struct bytes request,list,extension;
    return vector(&in,1,&request) && !request.n && vector(&in,3,&list) && !in.n &&
        vector(&list,3,leaf) && leaf->n && leaf->n<=PINNED_CERT_MAX &&
        vector(&list,2,&extension) && !extension.n && !list.n;
}

static bool verify_signature(const struct pinned_cert *cert,const unsigned char digest[32],
    const unsigned char r[32],const unsigned char s[32])
{
    static const char context[]="TLS 1.3, server CertificateVerify";
    unsigned char spaces[64],signed_hash[32];
    for (size_t i=0;i<sizeof(spaces);++i) { spaces[i]=0x20; }
    struct sha256 hash;
    sha256_init(&hash);
    sha256_update(&hash,spaces,sizeof(spaces));
    sha256_update(&hash,context,sizeof(context)); /* Includes the zero separator. */
    sha256_update(&hash,digest,32);
    sha256_final(&hash,signed_hash);
    return ecdsa_p256_verify(cert->key,signed_hash,r,s);
}

static bool verify_finished(const unsigned char secret[32],const unsigned char digest[32],struct bytes finished)
{
    unsigned char expected[32];
    bool valid=tls13_finished(secret,digest,expected) &&
        crypto_equal(expected,finished.p,sizeof(expected));
    crypto_wipe(expected,sizeof(expected));
    return valid;
}

enum tls13_auth_result tls13_authenticate(const void *transcript,size_t length,const unsigned char secret[32],
    const char *name,tls13_trust_fn trust,void *context)
{
    static const unsigned char types[]={1,2,8,11,15,20};
    if (!transcript || length>TLS13_TRANSCRIPT_MAX || !secret || !name || !trust) { return TLS13_AUTH_FORMAT; }
    struct bytes in={transcript,length},body[sizeof(types)],type;
    size_t offsets[sizeof(types)];
    for (size_t i=0;i<sizeof(types);++i) {
        offsets[i]=length-in.n;
        if (!take(&in,1,&type) || type.p[0]!=types[i] || !vector(&in,3,&body[i])) { return TLS13_AUTH_FORMAT; }
    }
    struct bytes session,leaf,verify=body[4],algorithm,signature;
    const unsigned char *share;
    unsigned char r[32],s[32];
    if (in.n || !client_hello(body[0],&session) || !server_hello(body[1],session,&share) || !extensions(body[2]) ||
        !certificate(body[3],&leaf) || !take(&verify,2,&algorithm) || u16(algorithm.p)!=0x0403 ||
        !vector(&verify,2,&signature) || verify.n || body[5].n!=32) { return TLS13_AUTH_FORMAT; }
    if (!ecdsa_p256_decode_der(signature.p,signature.n,r,s)) { return TLS13_AUTH_SIGNATURE; }

    struct pinned_cert cert;
    enum cert_result result=pinned_cert_verify(leaf.p,leaf.n,name,&cert);
    if (result!=CERT_OK) { return result==CERT_NAME ? TLS13_AUTH_NAME : TLS13_AUTH_CERTIFICATE; }
    if (!trust(&cert,context)) { return TLS13_AUTH_TRUST; }
    unsigned char digest[32];
    tls13_hash(transcript,offsets[4],digest);
    if (!verify_signature(&cert,digest,r,s)) { return TLS13_AUTH_SIGNATURE; }
    tls13_hash(transcript,offsets[5],digest);
    return verify_finished(secret,digest,body[5]) ? TLS13_AUTH_OK : TLS13_AUTH_FINISHED;
}
