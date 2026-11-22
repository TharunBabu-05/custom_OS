#include "aes_gcm.h"
#include "aes.h"
#include "crypto.h"
#include <stdint.h>

static void multiply(unsigned char block[16],const unsigned char h[16])
{
    unsigned char z[16]={0},v[16];
    for (unsigned i=0;i<16;++i) { v[i]=h[i]; }
    for (unsigned bit=0;bit<128;++bit) {
        unsigned char mask=(unsigned char)(0u-((block[bit/8]>>(7-bit%8))&1u));
        unsigned char reduce=(unsigned char)(0u-(v[15]&1u));
        for (unsigned i=0;i<16;++i) { z[i]^=v[i]&mask; }
        for (unsigned i=15;i>0;--i) { v[i]=(unsigned char)((v[i]>>1)|(v[i-1]<<7)); }
        v[0]=(unsigned char)((v[0]>>1)^(0xe1&reduce));
    }
    for (unsigned i=0;i<16;++i) { block[i]=z[i]; }
    crypto_wipe(v,sizeof(v));
    crypto_wipe(z,sizeof(z));
}

static void ghash(unsigned char sum[16],const unsigned char h[16],const unsigned char *data,size_t bytes)
{
    while (bytes) {
        size_t n=bytes<16 ? bytes : 16;
        for (size_t i=0;i<n;++i) { sum[i]^=data[i]; }
        multiply(sum,h);
        data+=n;
        bytes-=n;
    }
}

static void counter(unsigned char block[16],const unsigned char nonce[12],uint32_t index)
{
    for (unsigned i=0;i<12;++i) { block[i]=nonce[i]; }
    for (unsigned i=0;i<4;++i) { block[12+i]=(unsigned char)(index>>(24-8*i)); }
}

static void ctr(const unsigned char key[16],const unsigned char nonce[12],unsigned char *data,size_t bytes)
{
    unsigned char block[16];
    for (uint32_t index=2;bytes;++index) {
        counter(block,nonce,index);
        aes128_encrypt(key,block,block);
        size_t n=bytes<16 ? bytes : 16;
        for (size_t i=0;i<n;++i) { data[i]^=block[i]; }
        data+=n;
        bytes-=n;
    }
    crypto_wipe(block,sizeof(block));
}

static void auth(const unsigned char key[16],const unsigned char nonce[12],const void *aad,size_t aad_bytes,
    const void *data,size_t bytes,unsigned char tag[16])
{
    unsigned char h[16]={0},sum[16]={0},lengths[16],first[16];
    aes128_encrypt(key,h,h);
    ghash(sum,h,aad,aad_bytes);
    ghash(sum,h,data,bytes);
    for (unsigned i=0;i<8;++i) {
        lengths[i]=(unsigned char)(((uint64_t)aad_bytes*8)>>(56-8*i));
        lengths[i+8]=(unsigned char)(((uint64_t)bytes*8)>>(56-8*i));
    }
    ghash(sum,h,lengths,sizeof(lengths));
    counter(first,nonce,1);
    aes128_encrypt(key,first,first);
    for (unsigned i=0;i<16;++i) { tag[i]=first[i]^sum[i]; }
    crypto_wipe(h,sizeof(h));
    crypto_wipe(sum,sizeof(sum));
    crypto_wipe(first,sizeof(first));
}

static bool valid(const void *key,const void *nonce,const void *aad,size_t aad_bytes,
    const void *data,size_t bytes,const void *tag)
{ return key && nonce && (aad || !aad_bytes) && (data || !bytes) && tag && bytes<=16640 && aad_bytes<=16640; }

bool aes128_gcm_seal(const unsigned char key[16],const unsigned char nonce[12],
    const void *aad,size_t aad_bytes,void *data,size_t bytes,unsigned char tag[16])
{
    if (!valid(key,nonce,aad,aad_bytes,data,bytes,tag)) { return false; }
    ctr(key,nonce,data,bytes);
    auth(key,nonce,aad,aad_bytes,data,bytes,tag);
    return true;
}

bool aes128_gcm_open(const unsigned char key[16],const unsigned char nonce[12],
    const void *aad,size_t aad_bytes,void *data,size_t bytes,const unsigned char tag[16])
{
    if (!valid(key,nonce,aad,aad_bytes,data,bytes,tag)) { return false; }
    unsigned char expected[16];
    auth(key,nonce,aad,aad_bytes,data,bytes,expected);
    bool ok=crypto_equal(tag,expected,sizeof(expected));
    crypto_wipe(expected,sizeof(expected));
    if (ok) { ctr(key,nonce,data,bytes); }
    return ok;
}
