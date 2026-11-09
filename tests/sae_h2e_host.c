#include <stdio.h>
#include <string.h>
#include "sae_h2e.h"
#include "p256.h"
#include "crypto.h"
#include "host_assert.h"
static unsigned nibble(char c) { assert((c>='0' && c<='9') || (c>='a' && c<='f'));return c<='9' ? (unsigned)(c-'0') : (unsigned)(c-'a'+10); }
static size_t unhex(unsigned char *out,const char *s)
{ size_t n=strlen(s);assert(!(n&1));for (size_t i=0;i<n/2;++i) { out[i]=(unsigned char)(16*nibble(s[2*i])+nibble(s[2*i+1])); }return n/2; }
static void erased(const unsigned char *p) { for (unsigned i=0;i<64;++i) { assert(p[i]==0); } }
static FILE *open_vectors(const char *directory,const char *name)
{ char path[512];assert(snprintf(path,sizeof(path),"%s/%s",directory,name)>0);FILE *f=fopen(path,"r");assert(f);return f; }
int main(int argc,char **argv)
{
    assert(argc==2 && sae_h2e_selftest());
    unsigned char value[48],out[8193],expected[64],pt[64],pwe[64],a[6],b[6],ssid[32],password[128],id[64];
    char s[65],pw[257],ident[129],ma[13],mb[13],pt_hex[129],pwe_hex[129],v[97],want[129];unsigned count=0;
    FILE *f=open_vectors(argv[1],"sswu-public-vectors.txt");
    while (fscanf(f,"%96s %128s",v,want)==2) {
        assert(unhex(value,v)==48 && unhex(expected,want)==64);memset(out,0xa5,sizeof(out));
        assert(p256_sswu(value,out) && !memcmp(out,expected,64) && p256_point_valid(out) && out[64]==0xa5);
        if (!count) { memcpy(out,value,48);assert(p256_sswu(out,out) && !memcmp(out,expected,64)); }++count;
    }assert(feof(f) && count==32);fclose(f);
    puts("PASS: 32 SSWU/reduction vectors including RFC9380, zero and both nonzero exceptional inputs, p boundaries and maximum 384-bit input");
    f=open_vectors(argv[1],"h2e-scalar-vectors.txt");count=0;
    while (fscanf(f,"%64s %64s",s,want)==2) {
        assert(unhex(value,s)==32 && unhex(expected,want)==32);assert(p256_scalar_from_hash(value,out) && !memcmp(out,expected,32) && p256_scalar_valid(out));
        memcpy(out,value,32);assert(p256_scalar_from_hash(out,out) && !memcmp(out,expected,32));++count;
    }assert(feof(f) && count==6);fclose(f);
    f=open_vectors(argv[1],"h2e-public-vectors.txt");count=0;
    while (fscanf(f,"%64s %256s %128s %12s %12s %128s %128s",s,pw,ident,ma,mb,pt_hex,pwe_hex)==7) {
        size_t sn=unhex(ssid,s),pn=unhex(password,pw),in=!strcmp(ident,"-") ? 0 : unhex(id,ident);
        assert(unhex(a,ma)==6 && unhex(b,mb)==6 && unhex(pt,pt_hex)==64 && unhex(pwe,pwe_hex)==64);
        assert(sae_h2e_pt(ssid,sn,password,pn,in ? id : NULL,in,out) && !memcmp(out,pt,64));
        assert(sae_h2e_pwe(pt,a,b,out) && !memcmp(out,pwe,64));
        assert(sae_h2e_pwe(pt,b,a,out) && !memcmp(out,pwe,64));++count;
    }assert(feof(f) && count==16);fclose(f);
    puts("PASS: 16 synthetic SAE PT/PWE vector sets, optional identifiers, binary/max-length inputs, symmetric MAC binding, independent affine oracle and OpenSSL PWE-X comparisons");
    /* Expand-only RFC5869 KAT catches accidental extraction of the PRK again. */
    unhex(value,"077709362c2e32df0ddc3f0dc47bba6390b6c73bb50f9c3122ec844ad7c2b3e5");
    unsigned char info[10];unhex(info,"f0f1f2f3f4f5f6f7f8f9");
    unhex(expected,"3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf34007208d5b887185865");
    assert(hkdf_sha256_expand(value,info,10,out,42) && !memcmp(out,expected,42));
    memset(out,0xa5,sizeof(out));assert(hkdf_sha256_expand(value,NULL,0,out,8160) && out[8160]==0xa5);
    assert(!hkdf_sha256_expand(value,NULL,0,out,8161));assert(!hkdf_sha256_expand(NULL,NULL,0,out,1));
    assert(!hkdf_sha256_expand(value,NULL,1,out,1));assert(!hkdf_sha256_expand(value,NULL,0,NULL,1));
    assert(hkdf_sha256_expand(value,NULL,0,NULL,0));assert(!hkdf_sha256_expand(value,NULL,0,value,1));
    assert(!hkdf_sha256_expand(value,out,8,out,16));assert(!hkdf_sha256(NULL,0,out,8,NULL,0,out,16));
    memset(out,0xa5,sizeof(out));assert(!sae_h2e_pt(NULL,1,password,1,NULL,0,out));erased(out);assert(out[64]==0xa5);
    assert(!sae_h2e_pt(ssid,0,password,1,NULL,0,out));erased(out);
    assert(!sae_h2e_pt(ssid,33,password,1,NULL,0,out));erased(out);
    assert(!sae_h2e_pt(ssid,1,NULL,1,NULL,0,out));erased(out);
    assert(!sae_h2e_pt(ssid,1,password,0,NULL,0,out));erased(out);
    assert(!sae_h2e_pt(ssid,1,password,129,NULL,0,out));erased(out);
    assert(!sae_h2e_pt(ssid,1,password,1,NULL,1,out));erased(out);
    assert(!sae_h2e_pt(ssid,1,password,1,id,65,out));erased(out);
    assert(!sae_h2e_pt(ssid,1,password,1,NULL,0,NULL));
    assert(!sae_h2e_pt(out,1,password,1,NULL,0,out));erased(out);
    assert(!sae_h2e_pwe(pt,a,a,out));erased(out);
    unsigned char invalid[6]={0};assert(!sae_h2e_pwe(pt,invalid,b,out));erased(out);
    invalid[0]=1;assert(!sae_h2e_pwe(pt,invalid,b,out));erased(out);
    assert(!sae_h2e_pwe(pt,NULL,b,out));erased(out);assert(!sae_h2e_pwe(NULL,a,b,out));erased(out);
    assert(!sae_h2e_pwe(pt,a,b,NULL));assert(!sae_h2e_pwe(pt,a,b,pt));erased(pt);
    assert(!sae_h2e_pwe(pt,a,b,out));erased(out);
    assert(!p256_sswu(NULL,out));erased(out);assert(!p256_sswu(value,NULL));
    assert(!p256_scalar_from_hash(NULL,out));assert(!p256_scalar_from_hash(value,NULL));
    puts("PASS: expand-only HKDF KAT/bounds/overlap refusal, binding scalar endpoints, invalid credentials/lengths/MACs/points, no-alias contracts and output erasure");return 0;
}
