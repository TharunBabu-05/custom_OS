#include "user.h"
#include "pinned_cert.h"
#include "../../tests/cert_demo.h"

static int same(const char *a,const char *b)
{ while (*a && *a==*b) { ++a;++b; }return *a==*b; }

static void fingerprint(const unsigned char digest[32])
{
    static const char digits[]="0123456789abcdef";
    char text[65];
    for (unsigned i=0;i<32;++i) { text[2*i]=digits[digest[i]>>4];text[2*i+1]=digits[digest[i]&15]; }
    text[64]='\n';uos_puts("Certificate SHA-256 ");uos_write(text,sizeof(text));
}
static int verify(const void *der,size_t bytes,const char *name)
{
    struct pinned_cert cert;
    enum cert_result result=pinned_cert_verify(der,bytes,name,&cert);
    if (result!=CERT_OK) {
        uos_puts(result==CERT_NAME ? "Certificate DNS name mismatch or unsupported name.\n" :
            result==CERT_SIGNATURE ? "Certificate signature rejected.\n" : "Certificate DER or profile rejected.\n");
        return 1;
    }
    fingerprint(cert.digest);
    int64_t policy=uos_trust_check(cert.digest,cert.not_before,cert.not_after);
    if (!policy) { uos_puts("PASS: pinned certificate name, self-signature, trust and UTC.\n");return 0; }
    uos_puts(policy==UOS_ENOTIME ? "Certificate trust check requires UTC.\n" :
        policy==UOS_ENOENT ? "Certificate fingerprint is not enrolled.\n" :
        policy==UOS_ECERTTIME ? "Certificate is outside its UTC validity window.\n" : "Certificate trust query failed.\n");
    return 1;
}
static int verify_file(const char *path,const char *name)
{
    struct uos_entry entry;
    if (uos_stat(path,&entry) || entry.type!=UOS_FILE || !entry.bytes || entry.bytes>PINNED_CERT_MAX) { return 2; }
    int64_t page=uos_alloc_pages(1);
    if (page<0) { return 2; }
    unsigned char *bytes=(void *)(uintptr_t)page;
    int result=2;int64_t file=uos_open(path,UOS_READ);
    if (file>=0) {
        size_t total=0;
        while (total<entry.bytes) {
            size_t remain=(size_t)entry.bytes-total;
            int64_t n=uos_read((uint64_t)file,bytes+total,remain>512 ? 512 : remain);
            if (n<=0) { break; }total+=(size_t)n;
        }
        if (total==entry.bytes && uos_read((uint64_t)file,bytes+total,1)==0) { result=verify(bytes,total,name); }
        (void)uos_close((uint64_t)file);
    }
    (void)uos_free_pages(bytes);
    if (result==2) { uos_puts("Certificate file could not be read completely.\n"); }
    return result;
}
int app_main(void)
{
    char path[256],name[254];
    if (uos_call4(UOS_SYS_TRUST_CHECK,1,0,0,0)!=UOS_EFAULT) { return 3; }
    if (uos_argc()!=3 || uos_arg(1,path,sizeof(path))<=0 || uos_arg(2,name,sizeof(name))<=0) {
        uos_puts("Usage: CERTCHK demo|<DER path> <DNS name>\n");return 2;
    }
    if (same(path,"demo")) { return verify(cert_demo,sizeof(cert_demo),name); }
    return verify_file(path,name);
}
