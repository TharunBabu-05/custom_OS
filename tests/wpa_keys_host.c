#include <stdio.h>
#include <string.h>
#include "aes.h"
#include "wpa_keys.h"
#include "wpa_rsn.h"
#include "crypto.h"
#include "host_assert.h"
static size_t unhex(unsigned char *p,const char *s)
{ if (!strcmp(s,"-")) { return 0; }size_t n=strlen(s);assert(!(n&1));for (size_t i=0;i<n/2;++i) { unsigned v;assert(sscanf(s+2*i,"%2x",&v)==1);p[i]=(unsigned char)v; }return n/2; }
static void erased(const void *p,size_t n) { const unsigned char *v=p;for (size_t i=0;i<n;++i) { assert(!v[i]); } }
static const unsigned char rsn[]={0x30,26,1,0,0,15,172,4,1,0,0,15,172,4,1,0,0,15,172,8,0xc0,0,0,0,0,15,172,6};
int main(int argc,char **argv)
{
    assert(argc==2 && wpa_keys_selftest());FILE *f=fopen(argv[1],"r");assert(f);char kind,ah[8193],bh[8193],ch[8193],dh[8193],eh[8193],fh[8193];
    unsigned char key[32],a[520],b[4096],out[4097],expected[4096],other[4096],sn[32],an[32];unsigned counts[256]={0};
    while (fscanf(f," %c %8192s %8192s %8192s",&kind,ah,bh,ch)==4) {
        ++counts[(unsigned char)kind];size_t kn=unhex(key,ah),n=kind=='C' ? 0 : unhex(a,bh),want=unhex(expected,ch);memset(out,0xa5,sizeof(out));
        if (kind=='A') {
            assert(kn==16 && n==16 && want==16 && aes128_encrypt(key,a,out) && !memcmp(out,expected,16) && out[16]==0xa5);
            assert(aes128_decrypt(key,out,out) && !memcmp(out,a,16));
        }else if (kind=='C') {
            /* Long CMAC input is decoded directly into the larger buffer. */
            n=unhex(b,bh);assert(kn==16 && want==16 && aes128_cmac(key,b,n,out) && !memcmp(out,expected,16));
            memcpy(other,b,n);assert(aes128_cmac(key,other,n,other) && !memcmp(other,expected,16));
        }else if (kind=='U') {
            size_t actual=0;assert(kn==16 && aes128_unwrap(key,a,n,out,512,&actual) && actual==want && !memcmp(out,expected,want) && out[512]==0xa5);
            for (size_t i=0;i<n;++i) { a[i]^=1;memset(out,0xa5,512);assert(!aes128_unwrap(key,a,n,out,512,&actual) && !actual);erased(out,512);a[i]^=1; }
        }else {
            assert((kind=='P' || kind=='M') && kn==32 && n==6 && want==6);memcpy(b,expected,6);
            assert(fscanf(f,"%8192s %8192s %8192s",dh,eh,fh)==3 && unhex(sn,dh)==32 && unhex(an,eh)==32);want=unhex(expected,fh);
            struct wpa_sae_ptk ptk,reversed;assert(wpa_sae_ptk(key,a,b,sn,an,&ptk) && wpa_sae_ptk(key,b,a,an,sn,&reversed) && !memcmp(&ptk,&reversed,sizeof(ptk)));
            if (kind=='P') { assert(want==48 && !memcmp(&ptk,expected,48)); }
            else {
                struct wpa_m2_input in={.version=2,.rsn=rsn,.rsn_bytes=sizeof(rsn)};
                memcpy(in.local,a,6);memcpy(in.ap,b,6);memcpy(in.kck,ptk.kck,16);memcpy(in.snonce,sn,32);in.replay[7]=(unsigned char)counts['M'];size_t actual=0;
                assert(wpa_sae_m2(&in,out,sizeof(out),&actual) && actual==want && !memcmp(out,expected,want));
                unsigned char altered[28];memcpy(altered,rsn,28);altered[19]=2;in.rsn=altered;memset(out,0xa5,sizeof(out));
                assert(!wpa_sae_m2(&in,out,sizeof(out),&actual) && !actual && out[0]==0xa5);
            }
        }
    }assert(feof(f) && counts['A']==24 && counts['C']==12 && counts['U']==6 && counts['P']==16 && counts['M']==16);fclose(f);
    assert(!aes128_encrypt(NULL,a,out));erased(out,16);assert(!aes128_decrypt(key,NULL,out));erased(out,16);
    assert(!aes128_cmac(key,NULL,1,out));erased(out,16);assert(!aes128_cmac(key,b,4097,out));erased(out,16);
    size_t actual=99;assert(!aes128_unwrap(key,a,23,out,512,&actual) && !actual);erased(out,512);
    struct wpa_sae_ptk ptk;memset(&ptk,0xa5,sizeof(ptk));assert(!wpa_sae_ptk(key,a,a,sn,an,&ptk));erased(&ptk,sizeof(ptk));
    struct wpa_rsn_info info;unsigned char ie[64],pmkid[16]={0};memcpy(ie,rsn,28);
    assert(wpa_sae_rsn(ie,28,pmkid,&info)==RSN_OK && info.management_explicit);
    ie[1]=20;assert(wpa_sae_rsn(ie,22,pmkid,&info)==RSN_OK && !info.management_explicit);
    ie[1]=22;assert(wpa_sae_rsn(ie,24,pmkid,&info)==RSN_OK && !info.management_explicit);
    ie[1]=38;ie[22]=1;memset(ie+24,0,16);assert(wpa_sae_rsn(ie,40,pmkid,&info)==RSN_OK && info.pmkid_count==1);
    ie[1]=42;memcpy(ie+40,rsn+24,4);assert(wpa_sae_rsn(ie,44,pmkid,&info)==RSN_OK);
    ie[24]=1;assert(wpa_sae_rsn(ie,44,pmkid,&info)==RSN_PMKID);
    for (unsigned i=0;i<28;++i) { assert(wpa_sae_rsn(rsn,i,pmkid,&info)!=RSN_OK); }
    memcpy(ie,rsn,28);ie[19]=2;assert(wpa_sae_rsn(ie,28,pmkid,&info)==RSN_AKM);
    memcpy(ie,rsn,28);ie[20]=0x80;assert(wpa_sae_rsn(ie,28,pmkid,&info)==RSN_PMF);
    memcpy(ie,rsn,28);ie[21]=0x20;assert(wpa_sae_rsn(ie,28,pmkid,&info)==RSN_CAPABILITIES);
    memcpy(ie,rsn,28);ie[27]=5;assert(wpa_sae_rsn(ie,28,pmkid,&info)==RSN_MANAGEMENT);
    struct wpa_m2_input in={.local={2,0,0,0,0,1},.ap={2,0,0,0,0,2},.version=2,.rsn=rsn,.rsn_bytes=28};in.snonce[0]=1;
    assert(!wpa_sae_m2(&in,out,140,&actual) && !actual);in.version=0;assert(!wpa_sae_m2(&in,out,sizeof(out),&actual));
    in.version=4;assert(!wpa_sae_m2(&in,out,sizeof(out),&actual));in.version=1;assert(wpa_sae_m2(&in,out,sizeof(out),&actual) && actual==141 && out[14]==1);
    in.version=3;assert(wpa_sae_m2(&in,out,sizeof(out),&actual) && out[14]==3);in.rsn=out;in.rsn_bytes=28;assert(!wpa_sae_m2(&in,out,sizeof(out),&actual));
    memset(a,0,24);assert(!aes128_unwrap(key,a,24,out,15,&actual) && !actual);erased(out,15);
    assert(!aes128_unwrap(key,a,24,a,24,&actual) && !actual);erased(a,24);
    puts("PASS: FIPS197 AES encrypt/decrypt, RFC4493 CMAC and RFC3394 unwrap KATs; 24 ECB, 12 CMAC, six unwrap, 16 PTK and 16 exact M2 OpenSSL/Node fixtures, tamper rejection and output erasure");return 0;
}
