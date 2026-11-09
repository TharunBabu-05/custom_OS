#include <stdio.h>
#include <string.h>
#include "crypto.h"
#include "host_assert.h"
static unsigned char nibble(char c) { return (unsigned char)(c<='9' ? c-'0' : c-'a'+10); }
static void check(const unsigned char *p,const char *hex)
{ for (size_t i=0;hex[i*2];++i) { assert(p[i]==(unsigned char)(nibble(hex[i*2])<<4|nibble(hex[i*2+1]))); } }
int main(void)
{
    assert(crypto_selftest());unsigned char key[131],data[152],out[8162];struct crypto_span parts[3];
    parts[0]=(struct crypto_span){"what do ya want for nothing?",28};assert(hmac_sha256("Jefe",4,parts,1,out));check(out,"5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
    memset(key,0xaa,20);memset(data,0xdd,50);parts[0]=(struct crypto_span){data,50};
    assert(hmac_sha256(key,20,parts,1,out));check(out,"773ea91e36800e46854db8ebd09181a72959098b3ef8c122d9635514ced565fe");
    for (unsigned i=0;i<25;++i) { key[i]=(unsigned char)(i+1); }memset(data,0xcd,50);
    assert(hmac_sha256(key,25,parts,1,out));check(out,"82558a389a443c0ea4cc819899f2083a85f0faa3e578f8077a2e3ff46729665b");
    memset(key,0xaa,131);const char *longkey="Test Using Larger Than Block-Size Key - Hash Key First";
    parts[0]=(struct crypto_span){longkey,strlen(longkey)};assert(hmac_sha256(key,131,parts,1,out));check(out,"60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54");
    parts[0]=(struct crypto_span){longkey,7};parts[1]=(struct crypto_span){longkey+7,strlen(longkey)-7};parts[2]=(struct crypto_span){NULL,0};
    assert(hmac_sha256(key,131,parts,3,out));check(out,"60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54");
    memset(key,11,22);assert(hkdf_sha256(NULL,0,key,22,NULL,0,out,42));check(out,"8da4e775a563c18f715f802a063c5a31b8a11f5c5ee1879ec3454e5f3c738d2d9d201395faa4b61a96c8");
    unsigned char ikm[80],salt[80],info[80];for (unsigned i=0;i<80;++i) { ikm[i]=(unsigned char)i;salt[i]=(unsigned char)(0x60+i);info[i]=(unsigned char)(0xb0+i); }
    assert(hkdf_sha256(salt,80,ikm,80,info,80,out,82));check(out,"b11e398dc80327a1c8e7f78c596a49344f012eda2d4efad8a050cc4c19afa97c59045a99cac7827271cb41c65e590e09da3275600c2f09b8367793a9aca3db71cc30c58179ec3e87c14c01d5c1f3434f1d87");
    memset(out,0xa5,sizeof(out));assert(hkdf_sha256(salt,80,ikm,80,info,80,out,8160) && out[8160]==0xa5 && out[8161]==0xa5);
    assert(!hkdf_sha256(salt,80,ikm,80,info,80,out,8161));assert(hkdf_sha256(NULL,0,NULL,0,NULL,0,NULL,0));
    assert(!hmac_sha256(NULL,1,NULL,0,out));assert(!hmac_sha256(key,20,NULL,1,out));parts[0]=(struct crypto_span){NULL,1};assert(!hmac_sha256(key,20,parts,1,out));
    assert(!hkdf_sha256(NULL,1,key,20,NULL,0,out,32));assert(!hkdf_sha256(NULL,0,NULL,1,NULL,0,out,32));assert(!hkdf_sha256(NULL,0,key,20,NULL,1,out,32));
    assert(!crypto_equal(NULL,out,32));assert(crypto_equal(NULL,NULL,0));crypto_wipe(out,sizeof(out));
    for (unsigned i=0;i<sizeof(out);++i) { assert(!out[i]); }
    puts("PASS: RFC4231 HMAC long/split keys and RFC5869 HKDF vectors, empty salt/info, 255-block boundary, invalid inputs, bounded output and explicit wiping");return 0;
}
