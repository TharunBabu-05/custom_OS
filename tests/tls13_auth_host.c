#include "tls13_auth.h"
#include "trust_time.h"
#include "host_assert.h"
#include "tls13_demo.h"
#include <stdio.h>
#include <string.h>

static bool exact_pin(const struct pinned_cert *cert,void *pin)
{ return memcmp(cert->digest,pin,32)==0; }

static size_t next_message(const unsigned char *p,size_t at)
{ return at+4+((size_t)p[at+1]<<16)+((size_t)p[at+2]<<8)+p[at+3]; }

static enum tls13_auth_result verify(const unsigned char *data,size_t bytes,void *pin)
{ return tls13_authenticate(data,bytes,tls13_demo_secret,"demo.ouros.test",exact_pin,pin); }

int main(void)
{
    unsigned char pin[32];
    const size_t bytes=sizeof(tls13_demo_transcript);
    assert(trust_parse_digest(TLS13_DEMO_PIN,pin));
    assert(verify(tls13_demo_transcript,bytes,pin)==TLS13_AUTH_OK);
    assert(tls13_authenticate(tls13_demo_transcript,bytes,tls13_demo_secret,"wrong.ouros.test",
        exact_pin,pin)==TLS13_AUTH_NAME);
    pin[0]^=1;
    assert(verify(tls13_demo_transcript,bytes,pin)==TLS13_AUTH_TRUST);
    pin[0]^=1;
    unsigned char copy[sizeof(tls13_demo_transcript)+1];
    memcpy(copy,tls13_demo_transcript,bytes);
    for (size_t i=0;i<bytes;++i) { assert(verify(copy,i,pin)==TLS13_AUTH_FORMAT); }
    copy[bytes]=0;
    assert(verify(copy,bytes+1,pin)==TLS13_AUTH_FORMAT);
    assert(verify(copy,TLS13_TRANSCRIPT_MAX+1,pin)==TLS13_AUTH_FORMAT);

    size_t server=next_message(copy,0),encrypted=next_message(copy,server);
    size_t certificate=next_message(copy,encrypted),signature=next_message(copy,certificate);
    assert(copy[server]==2 && copy[encrypted]==8 && copy[certificate]==11 && copy[signature]==15);
    size_t suite=server+4+35+copy[server+4+34];
    copy[suite+1]=2;
    assert(verify(copy,bytes,pin)==TLS13_AUTH_FORMAT);
    copy[suite+1]=1;
    copy[encrypted]=13;
    assert(verify(copy,bytes,pin)==TLS13_AUTH_FORMAT);
    copy[encrypted]=8;
    copy[certificate+4]=1;
    assert(verify(copy,bytes,pin)==TLS13_AUTH_FORMAT);
    copy[certificate+4]=0;
    copy[certificate+11]^=1; /* Leaf DER tag. */
    assert(verify(copy,bytes,pin)==TLS13_AUTH_CERTIFICATE);
    copy[certificate+11]^=1;
    copy[signature+8]^=1; /* CertificateVerify DER tag. */
    assert(verify(copy,bytes,pin)==TLS13_AUTH_SIGNATURE);
    copy[signature+8]^=1;
    copy[6]^=1;
    assert(verify(copy,bytes,pin)==TLS13_AUTH_SIGNATURE);
    copy[6]^=1;
    copy[bytes-1]^=1;
    assert(verify(copy,bytes,pin)==TLS13_AUTH_FINISHED);
    unsigned char wrong_secret[32]={0};
    assert(tls13_authenticate(tls13_demo_transcript,bytes,wrong_secret,"demo.ouros.test",
        exact_pin,pin)==TLS13_AUTH_FINISHED);
    puts("PASS: OpenSSL TLS 1.3 transcript, pinned leaf, CertificateVerify, Finished, and negative cases.");
    return 0;
}
