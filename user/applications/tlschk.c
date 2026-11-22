#include "user.h"
#include "tls13_auth.h"
#include "../../tests/tls13_demo.h"

static bool same(const char *a,const char *b)
{
    while (*a && *a==*b) { ++a;++b; }
    return *a==*b;
}

static bool check_pin(const struct pinned_cert *cert,void *context)
{
    int64_t *policy=context;
    *policy=uos_trust_check(cert->digest,cert->not_before,cert->not_after);
    return *policy==0;
}

int app_main(void)
{
    char mode[16],name[254]="demo.ouros.test";
    uint64_t argc=uos_argc();
    if (argc<2 || argc>3 || uos_arg(1,mode,sizeof(mode))<=0 ||
        (argc==3 && uos_arg(2,name,sizeof(name))<=0)) {
        uos_puts("Usage: TLSCHK demo|transcript|finished [DNS name]\n");
        return 2;
    }
    bool corrupt_hello=same(mode,"transcript"),corrupt_finished=same(mode,"finished");
    if (!same(mode,"demo") && !corrupt_hello && !corrupt_finished) {
        uos_puts("Usage: TLSCHK demo|transcript|finished [DNS name]\n");
        return 2;
    }
    unsigned char transcript[sizeof(tls13_demo_transcript)];
    for (size_t i=0;i<sizeof(transcript);++i) { transcript[i]=tls13_demo_transcript[i]; }
    if (corrupt_hello) { transcript[6]^=1; } /* First byte of ClientHello.random. */
    if (corrupt_finished) { transcript[sizeof(transcript)-1]^=1; }
    int64_t policy=0;
    switch (tls13_authenticate(transcript,sizeof(transcript),tls13_demo_secret,name,check_pin,&policy)) {
    case TLS13_AUTH_OK:
        uos_puts("PASS: TLS 1.3 pinned leaf, CertificateVerify and server Finished.\n");
        return 0;
    case TLS13_AUTH_NAME:
        uos_puts("TLS certificate DNS name mismatch or unsupported name.\n");
        break;
    case TLS13_AUTH_TRUST:
        uos_puts(policy==UOS_ENOTIME ? "TLS certificate trust requires UTC.\n" :
            policy==UOS_ENOENT ? "TLS certificate fingerprint is not enrolled.\n" :
            policy==UOS_ECERTTIME ? "TLS certificate is outside its UTC validity window.\n" :
            "TLS certificate trust query failed.\n");
        break;
    case TLS13_AUTH_SIGNATURE:
        uos_puts("TLS CertificateVerify rejected.\n");
        break;
    case TLS13_AUTH_FINISHED:
        uos_puts("TLS server Finished rejected.\n");
        break;
    case TLS13_AUTH_CERTIFICATE:
        uos_puts("TLS certificate rejected.\n");
        break;
    case TLS13_AUTH_FORMAT:
        uos_puts("TLS handshake format or profile rejected.\n");
        break;
    }
    return 1;
}
