#include "user.h"
static int digest_is(const unsigned char *d,const char *hex)
{ const char *digits="0123456789abcdef";for (unsigned i=0;i<32;++i) { if (digits[d[i]>>4]!=hex[2*i] || digits[d[i]&15]!=hex[2*i+1]) { return 0; } }return 1; }
int app_main(void)
{
    const char *a="/tmp/.copy-source",*b="/tmp/.copy-dest";struct uos_entry e;unsigned char chunk[512],digest[32],other[32];
    if (uos_stat(a,&e)!=UOS_ERROR || uos_stat(b,&e)!=UOS_ERROR) { return 1; }
    if (uos_replace(a,"abc",3)!=3 || uos_filehash(a,digest) || !digest_is(digest,"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")) { return 2; }
    if (uos_copy(a,b)!=3 || uos_copy(a,b)!=UOS_ERROR || uos_copy(a,a)!=UOS_ERROR || uos_filehash(b,other)) { return 3; }
    for (unsigned i=0;i<32;++i) { if (digest[i]!=other[i]) { return 4; } }
    if (uos_remove(b,0) || uos_replace(a,"",0) || uos_copy(a,b)!=0 || uos_filehash(b,digest) ||
        !digest_is(digest,"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855")) { return 5; }
    if (uos_remove(b,0)) { return 6; }
    int64_t h=uos_open(a,UOS_READ|UOS_WRITE);if (h<0) { return 7; }
    for (unsigned i=0;i<512;++i) { chunk[i]=(unsigned char)i; }
    for (unsigned i=0;i<8;++i) { if (uos_write_file((uint64_t)h,chunk,512)!=512) { return 8; } }
    if (uos_copy(a,b)!=UOS_ERROR || uos_close((uint64_t)h) || uos_copy(a,b)!=4096) { return 9; }
    h=uos_open(b,UOS_READ);if (h<0) { return 10; }
    for (unsigned i=0;i<8;++i) { if (uos_read((uint64_t)h,chunk,512)!=512) { return 11; }for (unsigned j=0;j<512;++j) { if (chunk[j]!=(unsigned char)j) { return 12; } } }
    if (uos_close((uint64_t)h) || uos_filehash(a,digest) || uos_filehash(b,other)) { return 13; }
    for (unsigned i=0;i<32;++i) { if (digest[i]!=other[i]) { return 14; } }
    if (uos_filehash(a,(unsigned char *)0x2000000000ULL)!=UOS_EFAULT || uos_call(UOS_SYS_COPY,0,3,(uintptr_t)b,uos_length(b),0,0)!=UOS_EFAULT) { return 15; }
    if (uos_remove(b,0) || uos_copy("/boot/FW43455.BIN",b)!=UOS_ERROR || uos_stat(b,&e)!=UOS_ERROR || uos_copy(a,"/boot/new.bin")!=UOS_ERROR) { return 16; }
    if (uos_remove(a,0)) { return 17; }
    uos_puts("PASS: binary copy, zero/4096-byte bounds, no overwrite, SHA256 vectors, leases and pointer checks.\n");return 0;
}
