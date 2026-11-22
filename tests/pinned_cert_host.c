#include "pinned_cert.h"
#include "trust_time.h"
#include "host_assert.h"
#include "cert_demo.h"
#include <stdio.h>
#include <string.h>

int main(void)
{
    struct pinned_cert cert={0};unsigned char digest[32];
    assert(trust_parse_digest("7a2c46997c1c27ba1fb3fae02cc88759655c345eb3391e3220b1966a36cca467",digest));
    assert(pinned_cert_verify(cert_demo,sizeof(cert_demo),"demo.ouros.test",&cert)==CERT_OK);
    assert(!memcmp(cert.digest,digest,32) && cert.not_before==1577836800 && cert.not_after==2493072000ULL);
    assert(pinned_cert_verify(cert_demo,sizeof(cert_demo),"DEMO.OuROS.TeST",&cert)==CERT_OK);
    assert(pinned_cert_verify(cert_demo,sizeof(cert_demo),"wrong.ouros.test",&cert)==CERT_NAME);
    assert(pinned_cert_verify(cert_demo,sizeof(cert_demo),"*.ouros.test",&cert)==CERT_NAME);
    char long_name[255];memset(long_name,'x',254);long_name[254]=0;
    assert(pinned_cert_verify(cert_demo,sizeof(cert_demo),long_name,&cert)==CERT_NAME);
    assert(!memcmp(cert.digest,digest,32));
    for (size_t n=0;n<sizeof(cert_demo);++n) {
        assert(pinned_cert_verify(cert_demo,n,"demo.ouros.test",&cert)==CERT_FORMAT);
    }
    unsigned char der[sizeof(cert_demo)+1];memcpy(der,cert_demo,sizeof(cert_demo));der[sizeof(cert_demo)]=0;
    assert(pinned_cert_verify(der,sizeof(der),"demo.ouros.test",&cert)==CERT_FORMAT);
    der[1]=0x80;assert(pinned_cert_verify(der,sizeof(cert_demo),"demo.ouros.test",&cert)==CERT_FORMAT);
    memcpy(der,cert_demo,sizeof(cert_demo));
    static const unsigned char serial[]={0xa0,3,2,1,2,2,1,1};
    bool changed_serial=false;
    for (unsigned i=0;i<20;++i) {
        if (memcmp(der+i,serial,sizeof(serial))) { continue; }
        der[i+7]=2;changed_serial=true;break;
    }
    assert(changed_serial && pinned_cert_verify(der,sizeof(cert_demo),"demo.ouros.test",&cert)==CERT_SIGNATURE);
    for (unsigned i=0;i<8;++i) {
        memcpy(der,cert_demo,sizeof(cert_demo));der[sizeof(cert_demo)-1-i]^=1;
        assert(pinned_cert_verify(der,sizeof(cert_demo),"demo.ouros.test",&cert)==CERT_SIGNATURE);
    }
    memcpy(der,cert_demo,sizeof(cert_demo));
    static const unsigned char server_auth[]={0x2b,6,1,5,5,7,3,1};
    bool changed=false;
    for (unsigned i=0;i<sizeof(cert_demo)-sizeof(server_auth);++i) {
        if (memcmp(der+i,server_auth,sizeof(server_auth))) { continue; }
        der[i+7]=2;changed=true;break;
    }
    assert(changed && pinned_cert_verify(der,sizeof(cert_demo),"demo.ouros.test",&cert)==CERT_FORMAT);
    struct trust_store store={0};struct utc_sample now={1795338473,0,1};
    assert(trust_check(&store,cert.digest,&now,cert.not_before,cert.not_after)==TRUST_UNKNOWN);
    assert(trust_store_add(&store,digest));
    assert(trust_check(&store,cert.digest,NULL,cert.not_before,cert.not_after)==TRUST_NO_TIME);
    assert(trust_check(&store,cert.digest,&now,cert.not_before,cert.not_after)==TRUST_OK);
    now.seconds=cert.not_before-1;assert(trust_check(&store,cert.digest,&now,cert.not_before,cert.not_after)==TRUST_CERT_TIME);
    now.seconds=cert.not_after+1;assert(trust_check(&store,cert.digest,&now,cert.not_before,cert.not_after)==TRUST_CERT_TIME);
    puts("PASS: OpenSSL-signed P-256 certificate, DER truncation, signature mutation, name, purpose, explicit pin and UTC gates.");return 0;
}
