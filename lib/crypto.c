/* Original HMAC-SHA256 / HKDF-SHA256. RFC 4231 / RFC 5869 test vectors
 * exercise the primitives; this is not an SAE or EAPOL implementation. */
#include "crypto.h"
void crypto_wipe(void *p,size_t n)
{ volatile unsigned char *v=p;while (n--) { *v++=0; } }
bool crypto_equal(const void *a,const void *b,size_t n)
{
    if ((!a || !b) && n) { return false; }
    const unsigned char *x=a,*y=b;unsigned char diff=0;
    for (size_t i=0;i<n;++i) { diff|=x[i]^y[i]; }return diff==0;
}
bool hmac_sha256(const void *key,size_t key_bytes,const struct crypto_span *parts,size_t count,unsigned char output[32])
{
    if (!output || (!key && key_bytes) || (!parts && count) || count>16 || key_bytes>1048576) { return false; }
    for (size_t i=0;i<count;++i) { if ((!parts[i].data && parts[i].length) || parts[i].length>1048576) { return false; } }
    unsigned char pad[64]={0},inner[32];struct sha256 hash;
    if (key_bytes>64) { sha256_init(&hash);sha256_update(&hash,key,key_bytes);sha256_final(&hash,pad); }
    else { for (size_t i=0;i<key_bytes;++i) { pad[i]=((const unsigned char *)key)[i]; } }
    for (unsigned i=0;i<64;++i) { pad[i]^=0x36; }
    sha256_init(&hash);sha256_update(&hash,pad,64);
    for (size_t i=0;i<count;++i) { sha256_update(&hash,parts[i].data,parts[i].length); }sha256_final(&hash,inner);
    for (unsigned i=0;i<64;++i) { pad[i]^=0x36^0x5c; }
    sha256_init(&hash);sha256_update(&hash,pad,64);sha256_update(&hash,inner,32);sha256_final(&hash,output);
    crypto_wipe(&hash,sizeof(hash));crypto_wipe(pad,sizeof(pad));crypto_wipe(inner,sizeof(inner));return true;
}
bool hkdf_sha256(const void *salt,size_t salt_bytes,const void *ikm,size_t ikm_bytes,const void *info,size_t info_bytes,void *output,size_t n)
{
    if ((!salt && salt_bytes) || (!ikm && ikm_bytes) || (!info && info_bytes) || (!output && n) || n>8160 || salt_bytes>1048576 || ikm_bytes>1048576 || info_bytes>1048576) { return false; }
    unsigned char zero[32]={0},prk[32],block[32]={0};
    struct crypto_span input={ikm,ikm_bytes};
    if (!hmac_sha256(salt_bytes ? salt : zero,salt_bytes ? salt_bytes : 32,&input,1,prk)) { return false; }
    size_t at=0,previous=0;unsigned char counter=1;bool ok=true;
    while (at<n) {
        struct crypto_span parts[]={{block,previous},{info,info_bytes},{&counter,1}};
        if (!hmac_sha256(prk,32,parts,3,block)) { ok=false;break; }
        size_t amount=n-at;if (amount>32) { amount=32; }
        for (size_t i=0;i<amount;++i) { ((unsigned char *)output)[at+i]=block[i]; }at+=amount;previous=32;++counter;
    }
    crypto_wipe(prk,sizeof(prk));crypto_wipe(block,sizeof(block));crypto_wipe(zero,sizeof(zero));if (!ok && n) { crypto_wipe(output,n); }return ok;
}
bool crypto_selftest(void)
{
    static const unsigned char expected_hmac[32]={0xb0,0x34,0x4c,0x61,0xd8,0xdb,0x38,0x53,0x5c,0xa8,0xaf,0xce,0xaf,0x0b,0xf1,0x2b,0x88,0x1d,0xc2,0x00,0xc9,0x83,0x3d,0xa7,0x26,0xe9,0x37,0x6c,0x2e,0x32,0xcf,0xf7};
    static const unsigned char expected_hkdf[42]={0x3c,0xb2,0x5f,0x25,0xfa,0xac,0xd5,0x7a,0x90,0x43,0x4f,0x64,0xd0,0x36,0x2f,0x2a,0x2d,0x2d,0x0a,0x90,0xcf,0x1a,0x5a,0x4c,0x5d,0xb0,0x2d,0x56,0xec,0xc4,0xc5,0xbf,0x34,0x00,0x72,0x08,0xd5,0xb8,0x87,0x18,0x58,0x65};
    unsigned char key[22],salt[13],info[10],out[42];for (unsigned i=0;i<22;++i) { key[i]=11; }
    struct crypto_span text={"Hi There",8};bool ok=hmac_sha256(key,20,&text,1,out) && crypto_equal(out,expected_hmac,32);
    for (unsigned i=0;i<13;++i) { salt[i]=(unsigned char)i; }for (unsigned i=0;i<10;++i) { info[i]=(unsigned char)(0xf0+i); }
    ok=ok && hkdf_sha256(salt,13,key,22,info,10,out,42) && crypto_equal(out,expected_hkdf,42);
    crypto_wipe(key,sizeof(key));crypto_wipe(out,sizeof(out));crypto_wipe(salt,sizeof(salt));crypto_wipe(info,sizeof(info));return ok;
}
