#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "host_assert.h"
#include "wpa_probe.h"
#include "aes.h"
static unsigned char pmk[32],local[6],ap[6],sn[32],m1[700],m2[700],m3[700],rsn[28];
static size_t unhex(const char *s,unsigned char *out)
{ size_t n=strlen(s)/2;assert(n<700);for (size_t i=0;i<n;++i) { unsigned x;assert(sscanf(s+2*i,"%2x",&x)==1);out[i]=(unsigned char)x; }return n; }
static void prepare(struct wpa_probe *s,size_t n1,size_t n2)
{
    unsigned char out[370],id[16]={0};size_t n=0;memset(s,0,sizeof(*s));
    assert(wpa_probe_seed(s,pmk,sn)==WPA_PROBE_OK);
    assert(wpa_probe_receive(s,m1,n1)==WPA_PROBE_OK && s->m1 && !s->m2_issued);
    assert(wpa_probe_receive(s,m1,n1)==WPA_PROBE_OK);
    assert(wpa_probe_m2(s,local,ap,rsn,28,id,out,sizeof(out),&n)==WPA_PROBE_OK && n==n2 && !memcmp(out,m2,n));
    for (unsigned i=0;i<32;++i) { assert(!s->pmk[i]); }
}
int main(int argc,char **argv)
{
    assert(argc==2);FILE *f=fopen(argv[1],"r");assert(f);char line[20000];unsigned count=0;
    const char *rs="301a0100000fac040100000fac040100000fac08c0000000000fac06";unhex(rs,rsn);
    while (fgets(line,sizeof(line),f)) {
        if (line[0]!='H') { continue; }char *v[9];unsigned fields=0;
        for (char *p=strtok(line," \r\n");p;p=strtok(NULL," \r\n")) { assert(fields<9);v[fields++]=p; }assert(fields==9);
        unhex(v[1],pmk);unhex(v[2],local);unhex(v[3],ap);unhex(v[4],sn);size_t n1=unhex(v[5],m1),n2=unhex(v[6],m2),n3=unhex(v[7],m3);size_t plain=(size_t)strtoul(v[8],NULL,10);
        struct wpa_probe s;prepare(&s,n1,n2);
        /* RX can arrive before write completion, but cannot be verified yet. */
        assert(wpa_probe_receive(&s,m3,n3)==WPA_PROBE_OK && !s.mic_verified);
        assert(wpa_probe_verify(&s)==WPA_PROBE_STATE && !s.unwrap_verified);
        s.m2_written=true;assert(wpa_probe_verify(&s)==WPA_PROBE_OK && s.mic_verified && s.unwrap_verified && s.plain_bytes==plain);
        assert(wpa_probe_receive(&s,m3,n3)==WPA_PROBE_OK);wpa_probe_clear(&s);
        const unsigned char *wipe=(void *)&s;for (size_t j=0;j<sizeof(s);++j) { assert(!wipe[j]); }
        for (unsigned which=0;which<7;++which) {
            unsigned char altered[700];memcpy(altered,m3,n3);prepare(&s,n1,n2);s.m2_written=true;
            if (which==0) { altered[81]^=1; }
            if (which==1) { memcpy(altered+9,m1+9,8); }
            if (which==2) { altered[17]^=1; }
            if (which==3) { altered[0]=1; }
            if (which==4) { altered[6]^=2; }
            if (which==5) { altered[99]^=1;memset(altered+81,0,16);assert(aes128_cmac(s.ptk.kck,altered,n3,altered+81)); }
            if (which==6) { altered[97]=2; }
            enum wpa_probe_result r=wpa_probe_receive(&s,altered,n3);
            if (r==WPA_PROBE_OK) { r=wpa_probe_verify(&s); }
            assert(r!=WPA_PROBE_OK && !s.unwrap_verified && !s.plain_bytes);
            assert(which==5 ? s.mic_verified && r==WPA_PROBE_UNWRAP : !s.mic_verified);wpa_probe_clear(&s);
        }
        for (size_t cut=0;cut<n3;++cut) { prepare(&s,n1,n2);s.m2_written=true;assert(wpa_probe_receive(&s,m3,cut)!=WPA_PROBE_OK);wpa_probe_clear(&s); }
        memset(&s,0,sizeof(s));assert(wpa_probe_seed(&s,pmk,sn)==WPA_PROBE_OK);assert(wpa_probe_receive(&s,m3,n3)!=WPA_PROBE_OK);wpa_probe_clear(&s);
        ++count;
    }
    fclose(f);assert(count==16);puts("PASS: 16 independent OpenSSL M1/M2/M3 exchanges; MIC-before-unwrap, replay/nonce/profile guards, early RX/write completion, truncations, tampered ciphertext and complete private-state wipe; no M4/key install");return 0;
}
