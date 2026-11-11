#include "aes.h"
#include "crypto.h"
static unsigned nibble(char c) { return c<='9' ? (unsigned)(c-'0') : (unsigned)(c-'a'+10); }
static void unhex(unsigned char *out,const char *s,size_t n) { for (size_t i=0;i<n;++i) { out[i]=(unsigned char)(16*nibble(s[2*i])+nibble(s[2*i+1])); } }
bool aes128_selftest(void)
{
    struct { unsigned char key[16],plain[16],want[16],cipher[24],out[16]; } w={0};
    unhex(w.key,"000102030405060708090a0b0c0d0e0f",16);unhex(w.plain,"00112233445566778899aabbccddeeff",16);
    unhex(w.want,"69c4e0d86a7b0430d8cdb78070b4c55a",16);
    bool ok=aes128_encrypt(w.key,w.plain,w.out) && crypto_equal(w.out,w.want,16) && aes128_decrypt(w.key,w.out,w.out) && crypto_equal(w.out,w.plain,16);
    unhex(w.cipher,"1fa68b0a8112b447aef34bd8fb5a7b829d3e862371d2cfe5",24);size_t n=0;
    ok=ok && aes128_unwrap(w.key,w.cipher,24,w.out,16,&n) && n==16 && crypto_equal(w.out,w.plain,16);
    w.cipher[0]^=1;ok=ok && !aes128_unwrap(w.key,w.cipher,24,w.out,16,&n) && !n;
    unhex(w.key,"2b7e151628aed2a6abf7158809cf4f3c",16);unhex(w.want,"bb1d6929e95937287fa37d129b756746",16);
    ok=ok && aes128_cmac(w.key,NULL,0,w.out) && crypto_equal(w.out,w.want,16);
    unhex(w.plain,"6bc1bee22e409f96e93d7e117393172a",16);unhex(w.want,"070a16b46b4d4144f79bdd9dd04a287c",16);
    ok=ok && aes128_cmac(w.key,w.plain,16,w.out) && crypto_equal(w.out,w.want,16);crypto_wipe(&w,sizeof(w));return ok;
}
