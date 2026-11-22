#include "tls_net.h"
#include "http_download.h"
#include "trust_time.h"
#include "crypto.h"
#include "elf_app.h"
#include "tls13_keys.h"
#include <string.h>

static bool destination(const char *name,char path[38])
{
    size_t bytes=uos_length(name);
    if (bytes<5 || bytes>31 || (!crypto_equal(name+bytes-4,".ELF",4) && !crypto_equal(name+bytes-4,".elf",4))) { return false; }
    for (size_t i=0;i<bytes-4;++i) {
        unsigned char c=(unsigned char)name[i];
        if (!((c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') ||
            (i && (c=='-' || c=='_')))) { return false; }
    }
    memcpy(path,"/data/",6);
    memcpy(path+6,name,bytes+1);
    return true;
}

enum store_result { STORE_OK,STORE_EXISTS,STORE_IO,STORE_CLEANUP };

static enum store_result publish(const char *target,const unsigned char *data,size_t size,
    const unsigned char expected[32],const unsigned char nonce[8])
{
    static const char hex[]="0123456789abcdef";
    char directory[30]="/data/.fetch-";
    for (unsigned i=0;i<8;++i) {
        directory[13+2*i]=hex[nonce[i]>>4];
        directory[14+2*i]=hex[nonce[i]&15];
    }
    if (uos_mkdir(directory)) { return STORE_IO; }
    char stage[39];
    memcpy(stage,directory,29);
    memcpy(stage+29,"/file",6);
    int64_t fd=uos_open(stage,UOS_WRITE|UOS_CREATE);
    bool written=fd>=0;
    for (size_t at=0;written && at<size;) {
        size_t n=size-at<512 ? size-at : 512;
        written=uos_write_file((uint64_t)fd,data+at,n)==(int64_t)n;
        at+=n;
    }
    if (fd>=0 && uos_close((uint64_t)fd)) { written=false; }

    enum store_result result=STORE_IO;
    unsigned char stored[32];
    if (written && !uos_filehash(stage,stored) && crypto_equal(expected,stored,sizeof(stored))) {
        int64_t move=uos_rename(stage,target);
        if (!move) { result=STORE_OK; }
        else if (move==UOS_EEXIST) { result=STORE_EXISTS; }
    }
    if (result!=STORE_OK) { (void)uos_remove(stage,0); }
    if (uos_remove(directory,1) && result==STORE_OK) { result=STORE_CLEANUP; }
    return result;
}

static int verify_and_publish(const char *path,const unsigned char *body,size_t bytes,
    const unsigned char expected[32],const unsigned char nonce[8])
{
    unsigned char digest[32];
    tls13_hash(body,bytes,digest);
    if (!crypto_equal(digest,expected,sizeof(digest))) {
        uos_puts("Package SHA-256 mismatch.\n");
        return 1;
    }
    struct app_image_plan plan;
    if (app_elf_plan(body,bytes,&plan)!=APP_ELF_OK) {
        uos_puts("Package ELF rejected by the OurOS loader rules.\n");
        return 1;
    }
    enum store_result result=publish(path,body,bytes,expected,nonce);
    if (result==STORE_EXISTS) { uos_puts("Package destination already exists.\n");return 1; }
    if (result==STORE_IO) { uos_puts("Package staging or publication failed. Check /data.\n");return 1; }
    if (result==STORE_CLEANUP) { uos_puts("Package saved. The empty staging directory could not be removed.\n"); }
    uos_puts("PASS: HTTPS package SHA-256 and ELF verified. Saved ");
    uos_puts(path);
    uos_puts("\n");
    return 0;
}

static int check_storage(void)
{
    static const char path[]="/data/PKGTEST.ELF";
    struct uos_entry file;
    struct uos_fs_info before,after;
    if (!uos_stat(path,&file) || uos_statfs("/data",&before) || before.type!=UOS_FS_SNAPSHOT ||
        before.version<3) { return 3; }
    unsigned char fixture[1024]={0},digest[32],wrong[32],stored[32],nonce[8];
    uint64_t pid=(uint64_t)uos_getpid();
    for (unsigned i=0;i<sizeof(nonce);++i) { nonce[i]=(unsigned char)(pid>>(8*i)); }
    int64_t fd=uos_open("/boot/HELLO.ELF",UOS_READ);
    if (fd<0) { return 3; }
    int64_t bytes=uos_read((uint64_t)fd,fixture,512);
    int64_t extra=bytes>0 ? uos_read((uint64_t)fd,fixture+bytes,512) : 0;
    if (uos_close((uint64_t)fd) || bytes<=0 || extra) { return 3; }
    /* Unmapped trailing bytes exercise the second staging write. */
    size_t size=sizeof(fixture);
    tls13_hash(fixture,size,digest);
    memcpy(wrong,digest,sizeof(wrong));
    wrong[0]^=1;
    if (!verify_and_publish(path,fixture,size,wrong,nonce) || !uos_stat(path,&file)) { return 3; }
    fixture[0]^=1;
    tls13_hash(fixture,size,wrong);
    if (!verify_and_publish(path,fixture,size,wrong,nonce) || !uos_stat(path,&file)) { return 3; }
    fixture[0]^=1;
    if (verify_and_publish(path,fixture,size,digest,nonce)) { return 3; }
    bool saved=publish(path,fixture,size,digest,nonce)==STORE_EXISTS &&
        !uos_filehash(path,stored) && crypto_equal(stored,digest,sizeof(stored));
    if (uos_remove(path,0) || !saved || uos_statfs("/data",&after) || after.used_nodes!=before.used_nodes) { return 3; }
    uos_puts("PASS: package staging preserved the destination, verified readback, and released its files.\n");
    return 0;
}

int app_main(void)
{
    char address[24],port[8],hash[65],name[32],path[38];
    if (uos_argc()==2 && uos_arg(1,address,sizeof(address))==5 && crypto_equal(address,"check",5)) {
        return check_storage();
    }
    struct uos_tcp_peer peer={0};
    unsigned char expected[32];
    if (uos_argc()!=5 || uos_arg(1,address,sizeof(address))<=0 || uos_arg(2,port,sizeof(port))<=0 ||
        uos_arg(3,hash,sizeof(hash))!=64 || uos_arg(4,name,sizeof(name))<=0 ||
        !tls_net_peer(address,port,&peer) || !destination(name,path) || !trust_parse_digest(hash,expected)) {
        uos_puts("Usage: PKGGET <LAN IPv4> <TCP port> <SHA-256> <new-name.ELF>\n");
        return 2;
    }
    struct uos_fs_info fs;
    if (uos_statfs("/data",&fs) || fs.type!=UOS_FS_SNAPSHOT || fs.version<3 || (fs.flags&UOS_FS_READONLY) || !fs.max_write_bytes) {
        uos_puts("Package download requires a writable /data v3 or later volume.\n");
        return 1;
    }
    struct uos_entry existing;
    if (!uos_stat(path,&existing)) { uos_puts("Package destination already exists.\n");return 1; }
    struct uos_utc_time utc;
    if (uos_utc_time(&utc)) { uos_puts("Package certificate trust requires UTC.\n");return 1; }
    size_t capacity=fs.max_write_bytes<PACKAGE_MAX_BYTES ? (size_t)fs.max_write_bytes : PACKAGE_MAX_BYTES;
    int64_t pages=uos_alloc_pages((capacity+4095)/4096);
    if (pages<0) { uos_puts("Package download buffer allocation failed.\n");return 1; }

    struct tls_net net={0};
    struct http_download download={.body=(void *)(uintptr_t)pages,.capacity=capacity};
    enum tls13_auth_result auth=TLS13_AUTH_FORMAT;
    enum tls13_client_result result=TLS13_CLIENT_IO;
    int64_t policy=0;
    if (tls_net_open(&net,&peer)) {
        static const char request[]=PACKAGE_HTTP_REQUEST;
        struct tls13_io io=tls_net_io(&net);
        result=tls13_request(&io,PACKAGE_TLS_NAME,tls_net_check_pin,&policy,request,sizeof(request)-1,
            http_download_receive,&download,&auth);
    }
    tls_net_close(&net);
    int status;
    if (result!=TLS13_CLIENT_OK) { status=tls_net_failure(result,auth,policy,net.error); }
    else {
        unsigned char nonce[8];
        if (uos_random(nonce,sizeof(nonce))) {
            uos_puts("Package staging needs hardware randomness.\n");
            status=1;
        } else { status=verify_and_publish(path,download.body,download.received,expected,nonce); }
    }
    (void)uos_free_pages(download.body);
    return status;
}
