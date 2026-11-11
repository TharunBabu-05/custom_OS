#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "host_assert.h"
#include "wpa_probe.h"
#include "aes.h"
#include "crypto.h"
static unsigned char pmk[32],local[6],ap[6],sn[32],m1[700],m2[700],m3[700],m4[700],rsn[257],plain[512],station[28];
static size_t n1,n2,n3,n4,rn,pn;
static size_t unhex(const char *s,unsigned char *out,size_t cap)
{ size_t n=strlen(s);assert(!(n%2) && n/2<=cap);for (size_t i=0;i<n/2;++i) { unsigned x;assert(sscanf(s+2*i,"%2x",&x)==1);out[i]=(unsigned char)x; }return n/2; }
static void erased(const void *p,size_t n) { const unsigned char *b=p;for (size_t i=0;i<n;++i) { assert(!b[i]); } }
static void prepare(struct wpa_probe *s)
{
    unsigned char out[370],id[16]={0};size_t n=0;memset(s,0,sizeof(*s));
    assert(wpa_probe_seed(s,pmk,sn)==WPA_PROBE_OK && wpa_probe_bind_ap(s,rsn,rn)==WPA_PROBE_OK);
    assert(wpa_probe_receive(s,m1,n1)==WPA_PROBE_OK);
    assert(wpa_probe_m2(s,local,ap,station,28,id,out,sizeof(out),&n)==WPA_PROBE_OK && n==n2 && !memcmp(out,m2,n));s->m2_written=true;
}
static void put16(unsigned char *p,unsigned n) { p[0]=(unsigned char)(n>>8);p[1]=(unsigned char)n; }
static void rewrap(const unsigned char kek[16],const unsigned char *p,size_t n,unsigned char *out)
{
    assert(n>=16 && n<=512 && !(n%8));memset(out,0xa6,8);memcpy(out+8,p,n);size_t blocks=n/8;
    for (unsigned j=0;j<6;++j) { for (size_t i=1;i<=blocks;++i) {
        unsigned char b[16];memcpy(b,out,8);memcpy(b+8,out+8*i,8);assert(aes128_encrypt(kek,b,b));memcpy(out,b,8);
        unsigned t=(unsigned)(blocks*j+i);out[6]^=(unsigned char)(t>>8);out[7]^=(unsigned char)t;memcpy(out+8*i,b+8,8);
    } }
}
static size_t pack(unsigned char out[512],const unsigned char *gtk,const unsigned char *igtk,unsigned mode)
{
    size_t n=0;memset(out,0,512);
    if (mode!=1) { memcpy(out+n,rsn,rn);n+=rn; }if (mode!=2) { memcpy(out+n,gtk,24);n+=24; }if (mode!=3) { memcpy(out+n,igtk,30);n+=30; }
    if (mode==4) { memcpy(out+n,rsn,rn);n+=rn; }if (mode==5) { memcpy(out+n,gtk,24);n+=24; }if (mode==6) { memcpy(out+n,igtk,30);n+=30; }
    if (mode==7) { out[4]^=1; }if (mode==8) { out[rn+6]|=128; }if (mode==9) { out[rn+7]=1; }
    if (mode==10) { out[rn+24+6]=6; }if (mode==11) { out[rn+5]=42; }if (mode==12) { out[rn+2]=1; }
    if (mode==13) { out[rn+24+1]=27; }if (mode==14) { out[rn+1]=21; }
    if (mode==15) { out[n++]=0;out[n++]=0; } /* Unknown empty IE, never padding. */
    size_t pad=(8-n%8)%8;if (pad) { out[n]=221;n+=pad; }return n;
}
int main(int argc,char **argv)
{
    assert(argc==2 && wpa_kde_selftest());FILE *f=fopen(argv[1],"r");assert(f);char line[20000];unsigned count=0;
    unhex("301a0100000fac040100000fac040100000fac08c0000000000fac06",station,sizeof(station));
    while (fgets(line,sizeof(line),f)) {
        char *v[13];unsigned fields=0;for (char *p=strtok(line," \r\n");p;p=strtok(NULL," \r\n")) { assert(fields<13);v[fields++]=p; }assert(fields==13 && v[0][0]=='D');
        unhex(v[1],pmk,32);unhex(v[2],local,6);unhex(v[3],ap,6);unhex(v[4],sn,32);n1=unhex(v[5],m1,700);n2=unhex(v[6],m2,700);rn=unhex(v[7],rsn,257);pn=unhex(v[8],plain,512);n3=unhex(v[9],m3,700);n4=unhex(v[10],m4,700);
        assert(wpa_ap_rsn_profile(rsn,rn));const unsigned char *gtk=NULL,*igtk=NULL;
        for (size_t at=0;at+2<=pn && plain[at+1];at+=2+plain[at+1]) {
            if (plain[at]==221 && plain[at+5]==1) { gtk=plain+at; }if (plain[at]==221 && plain[at+5]==9) { igtk=plain+at; }
        }assert(gtk && igtk);
        struct wpa_group_keys keys;struct wpa_kde_info info;
        assert(wpa_sae_key_data(rsn,rn,plain,pn,m3+65,&info,&keys)==KDE_OK && info.rsn_match && info.gtk_seen && info.igtk_seen);
        assert(!memcmp(keys.gtk,gtk+8,16) && !memcmp(keys.igtk,igtk+14,16) && !memcmp(keys.ipn,igtk+8,6) && !memcmp(keys.rsc,m3+65,6));
        assert(keys.gtk_id==(gtk[6]&3) && keys.igtk_id==igtk[6]);
        for (size_t cut=0;cut<pn;++cut) { memset(&keys,0xa5,sizeof(keys));assert(wpa_sae_key_data(rsn,rn,plain,cut,m3+65,&info,&keys)!=KDE_OK);erased(&keys,sizeof(keys)); }
        struct wpa_probe s;prepare(&s);unsigned char out[114];size_t n=9;
        assert(wpa_probe_m4(&s,local,ap,out,sizeof(out),&n)==WPA_PROBE_STATE && !n);
        assert(wpa_probe_receive(&s,m3,n3)==WPA_PROBE_OK && wpa_probe_verify(&s)==WPA_PROBE_OK && s.key_data_verified);
        memset(out,0xa5,sizeof(out));assert(wpa_probe_m4(&s,local,ap,out,sizeof(out),&n)==WPA_PROBE_OK && n==n4 && n==113 && !memcmp(out,m4,n) && out[113]==0xa5);
        assert(wpa_probe_m4(&s,local,ap,out,sizeof(out),&n)==WPA_PROBE_STATE && !n);wpa_probe_clear(&s);erased(&s,sizeof(s));
        /* Higher replay/MIC changes are authenticated, with no republish of
         * retained group keys or reset of their counters. Oracle is OpenSSL. */
        unsigned char retry[700],answer[113];size_t retry_n=unhex(v[11],retry,sizeof(retry));assert(unhex(v[12],answer,sizeof(answer))==113);
        prepare(&s);s.retain_group=true;assert(wpa_probe_receive(&s,m3,n3)==WPA_PROBE_OK && wpa_probe_verify(&s)==WPA_PROBE_OK && s.group_retained);
        struct wpa_group_keys original=s.group;assert(wpa_probe_m4(&s,local,ap,out,sizeof(out),&n)==WPA_PROBE_OK);
        assert(wpa_probe_receive(&s,retry,retry_n)==WPA_PROBE_OK && s.retry_pending && !s.m4_encoded && !s.retries_verified);
        assert(wpa_probe_m4(&s,local,ap,out,sizeof(out),&n)==WPA_PROBE_STATE);
        assert(wpa_probe_verify(&s)==WPA_PROBE_OK && !s.retry_pending && s.retries_verified==1 && !memcmp(&s.group,&original,sizeof(original)));
        assert(wpa_probe_m4(&s,local,ap,out,sizeof(out),&n)==WPA_PROBE_OK && n==113 && !memcmp(out,answer,113));
        assert(wpa_probe_receive(&s,retry,retry_n)==WPA_PROBE_OK && s.m4_due && !s.m4_encoded);
        assert(wpa_probe_m4(&s,local,ap,out,sizeof(out),&n)==WPA_PROBE_OK && !memcmp(out,answer,113) && s.retries_verified==1);
        assert(wpa_probe_receive(&s,m3,n3)==WPA_PROBE_REPLAY);wpa_probe_clear(&s);
        for (unsigned mode=0;mode<7;++mode) {
            unsigned char candidate[700];memcpy(candidate,retry,retry_n);prepare(&s);s.retain_group=true;
            assert(wpa_probe_receive(&s,m3,n3)==WPA_PROBE_OK && wpa_probe_verify(&s)==WPA_PROBE_OK);original=s.group;
            if (mode==0) { candidate[81]^=1; }
            if (mode==1) { candidate[65]^=1; }
            if (mode==2 || mode==3) {
                unsigned char changed[512];memcpy(changed,plain,pn);size_t at=mode==2 ? (size_t)(gtk-plain)+8 : (size_t)(igtk-plain)+8;changed[at]^=1;rewrap(s.ptk.kek,changed,pn,candidate+99);
            }
            if (mode==4) { memcpy(candidate+9,m3+9,8);candidate[81]^=1; }
            if (mode==5) { candidate[17]^=1; }
            if (mode==6) { candidate[99]^=1; }
            if (mode==1 || mode==2 || mode==3 || mode==6) { memset(candidate+81,0,16);assert(aes128_cmac(s.ptk.kck,candidate,retry_n,candidate+81)); }
            enum wpa_probe_result r=wpa_probe_receive(&s,candidate,retry_n);if (r==WPA_PROBE_OK) { r=wpa_probe_verify(&s); }
            assert(r!=WPA_PROBE_OK && s.failed && !s.retries_verified && !memcmp(&s.group,&original,sizeof(original)));
            if (mode==0) { assert(r==WPA_PROBE_MIC); }if (mode>=1 && mode<=3) { assert(r==WPA_PROBE_CHANGED); }
            wpa_probe_clear(&s);erased(&s,sizeof(s));
        }
        /* Queue a retry even before first M3 verification, but validate both. */
        prepare(&s);assert(wpa_probe_receive(&s,m3,n3)==WPA_PROBE_OK && wpa_probe_receive(&s,retry,retry_n)==WPA_PROBE_OK);
        assert(wpa_probe_verify(&s)==WPA_PROBE_OK && s.retries_verified==1);wpa_probe_clear(&s);
        prepare(&s);assert(wpa_probe_receive(&s,m3,n3)==WPA_PROBE_OK && wpa_probe_verify(&s)==WPA_PROBE_OK && wpa_probe_receive(&s,retry,retry_n)==WPA_PROBE_OK);
        unsigned char conflict[700];memcpy(conflict,retry,retry_n);conflict[81]^=1;
        assert(wpa_probe_receive(&s,conflict,retry_n)==WPA_PROBE_REPLAY && s.retry_rejection==4);wpa_probe_clear(&s);
        /* Bounded higher-replay candidates and exact duplicate flood. */
        prepare(&s);assert(wpa_probe_receive(&s,m3,n3)==WPA_PROBE_OK && wpa_probe_verify(&s)==WPA_PROBE_OK);
        for (unsigned j=0;j<4;++j) {
            unsigned char candidate[700];memcpy(candidate,retry,retry_n);candidate[16]=(unsigned char)(retry[16]+j);memset(candidate+81,0,16);assert(aes128_cmac(s.ptk.kck,candidate,retry_n,candidate+81));
            enum wpa_probe_result r=wpa_probe_receive(&s,candidate,retry_n);if (j<3) { assert(r==WPA_PROBE_OK && wpa_probe_verify(&s)==WPA_PROBE_OK); }else { assert(r==WPA_PROBE_LIMIT); }
        }wpa_probe_clear(&s);prepare(&s);assert(wpa_probe_receive(&s,m3,n3)==WPA_PROBE_OK && wpa_probe_verify(&s)==WPA_PROBE_OK);
        for (unsigned j=0;j<9;++j) { assert(wpa_probe_receive(&s,m3,n3)==(j<8 ? WPA_PROBE_OK : WPA_PROBE_LIMIT)); }wpa_probe_clear(&s);
        for (unsigned mode=1;mode<=17;++mode) {
            unsigned char changed[512],third[700];size_t bytes=pack(changed,gtk,igtk,mode);prepare(&s);memcpy(third,m3,99);
            if (mode==17) { third[71]=1; } /* RSC upper two bytes unsupported. */
            rewrap(s.ptk.kek,changed,bytes,third+99);put16(third+2,(unsigned)(95+bytes+8));put16(third+97,(unsigned)(bytes+8));memset(third+81,0,16);assert(aes128_cmac(s.ptk.kck,third,107+bytes,third+81));
            if (mode==16) { third[81]^=1; }
            assert(wpa_probe_receive(&s,third,107+bytes)==WPA_PROBE_OK);
            assert(wpa_probe_verify(&s)==(mode==16 ? WPA_PROBE_MIC : WPA_PROBE_KEY_DATA) && !s.key_data_verified && !s.m4_encoded);
            if (mode==16) { assert(!s.unwrap_verified && !s.key_data.rsn_match); }else { assert(s.mic_verified && s.unwrap_verified); }
            assert(wpa_probe_m4(&s,local,ap,out,sizeof(out),&n)==WPA_PROBE_STATE);wpa_probe_clear(&s);
        }
        prepare(&s);assert(wpa_probe_receive(&s,m3,n3)==WPA_PROBE_OK && wpa_probe_verify(&s)==WPA_PROBE_OK);
        unsigned char wrong[6];memcpy(wrong,local,6);wrong[5]^=2;assert(wpa_probe_m4(&s,wrong,ap,out,sizeof(out),&n)==WPA_PROBE_FORMAT);wpa_probe_clear(&s);
        ++count;
    }
    fclose(f);assert(count==16);uint32_t random=0x1964;struct wpa_kde_info info;
    for (unsigned i=0;i<10000;++i) {
        struct { unsigned char before[8];struct wpa_group_keys keys;unsigned char after[8]; } guard;memset(&guard,0xa5,sizeof(guard));
        unsigned char mutated[512];memset(mutated,0,sizeof(mutated));memcpy(mutated,plain,pn);random=random*1664525u+1013904223u;mutated[random%512]^=(unsigned char)(random>>24);
        enum wpa_kde_result r=wpa_sae_key_data(rsn,rn,mutated,i%3 ? pn : random%513,m3+65,&info,&guard.keys);
        if (r!=KDE_OK) { erased(&guard.keys,sizeof(guard.keys)); }for (unsigned j=0;j<8;++j) { assert(guard.before[j]==0xa5 && guard.after[j]==0xa5); }
    }
    unsigned char changed[257];memcpy(changed,rsn,rn);changed[4]^=1;assert(!wpa_ap_rsn_profile(changed,rn));
    struct wpa_m4_input in={.version=2};memcpy(in.local,local,6);memcpy(in.ap,ap,6);unsigned char small[113];size_t n=9;memset(small,0xa5,sizeof(small));
    assert(!wpa_sae_m4(&in,small,112,&n) && !n && small[0]==0xa5);in.version=4;assert(!wpa_sae_m4(&in,small,113,&n));
    struct wpa_group_keys keys;memset(&keys,0xa5,sizeof(keys));assert(wpa_sae_key_data(rsn,rn,plain,pn,m3+65,NULL,&keys)==KDE_ARGUMENT);erased(&keys,sizeof(keys));
    assert(wpa_sae_key_data(rsn,rn,plain,pn,m3+65,&info,(void *)plain)==KDE_ARGUMENT);erased(plain,sizeof(keys));
    for (size_t cut=0;cut<rn;++cut) { assert(!wpa_ap_rsn_profile(rsn,cut)); }
    puts("PASS: 16 independent wrapped/signed M3 key-data, higher-replay retry and exact M4 fixtures; scan-bound RSN, GTK/IGTK/counters, bad MIC/changed keys/counters/stale replay refusal, bounded duplicates/retries, no key republish, pending-M4 gate, truncations, 10000 mutations and private-state erasure");return 0;
}
