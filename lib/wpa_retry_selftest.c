/* Public OpenSSL-generated fixture; never uses credentials, RNG or radio. */
#include "wpa_probe.h"
#include "crypto.h"
static unsigned digit(char c) { return c<='9' ? (unsigned)(c-'0') : (unsigned)(c-'a'+10); }
static void unhex(unsigned char *out,const char *hex,size_t n) { for (size_t i=0;i<n;++i) { out[i]=(unsigned char)(16*digit(hex[2*i])+digit(hex[2*i+1])); } }
bool wpa_retry_selftest(void)
{
    static const unsigned char station[]={48,26,1,0,0,15,172,4,1,0,0,15,172,4,1,0,0,15,172,8,192,0,0,0,0,15,172,6};
    static const unsigned char adv[]={48,24,1,0,0,15,172,4,1,0,0,15,172,4,2,0,0,15,172,2,0,15,172,8,128,0};
    const unsigned char local[6]={2,0,0,0,0,1},ap[6]={2,0,0,0,0,2};
    struct { struct wpa_probe s;struct wpa_group_keys group;unsigned char pmk[32],sn[32],m1[99],m3[187],frame[370],id[16],mic[16]; } w={0};
    unhex(w.pmk,"45c81d7094f6bfc4b41f166934baa20874d6d41ac67cc5e8c5309de1eb009aae",32);
    unhex(w.sn,"a7e7be99571a0fd96e2c89855481e9e47b80df1b53daa1e0358f0a56d79c496c",32);
    w.m1[0]=2;w.m1[1]=3;w.m1[3]=95;w.m1[4]=2;w.m1[6]=0x88;w.m1[8]=16;w.m1[16]=1;
    unhex(w.m1+17,"f6579b97a24afc54942ed84ea3266c2384156a12c8d88128fe28438a41727c0e",32);
    bool ok=wpa_probe_seed(&w.s,w.pmk,w.sn)==WPA_PROBE_OK && wpa_probe_bind_ap(&w.s,adv,sizeof(adv))==WPA_PROBE_OK;
    w.s.retain_group=true;size_t n=0;
    ok=ok && wpa_probe_receive(&w.s,w.m1,sizeof(w.m1))==WPA_PROBE_OK && wpa_probe_m2(&w.s,local,ap,station,sizeof(station),w.id,w.frame,sizeof(w.frame),&n)==WPA_PROBE_OK;
    w.s.m2_written=true;
    unhex(w.m3,"020300b70213c800100000000000000002f6579b97a24afc54942ed84ea3266c2384156a12c8d88128fe28438a41727c0e000000000000000000000000000000000102030405060000000000000000000079be62d8a0b68cdc7a010ed38dc883fd00581fdd1410f4deb87f89f9bb889c18c2cd4b9e55f002dcc9f32cecb771893af4f0b58fdc9ed820646cc5a49bb460914d0371e9050d2e6f1d207667a484e49fe2fb9184aea8606dcb2510286ab063c50f7dcb56bb3da7e8e54a",187);
    ok=ok && wpa_probe_receive(&w.s,w.m3,sizeof(w.m3))==WPA_PROBE_OK && wpa_probe_verify(&w.s)==WPA_PROBE_OK && w.s.group_retained;
    w.group=w.s.group;ok=ok && wpa_probe_m4(&w.s,local,ap,w.frame,sizeof(w.frame),&n)==WPA_PROBE_OK;
    w.m3[16]=5;unhex(w.m3+81,"0ea9a6206784432ba1281879d1178bb0",16);
    ok=ok && wpa_probe_receive(&w.s,w.m3,sizeof(w.m3))==WPA_PROBE_OK && w.s.retry_pending && !w.s.m4_encoded;
    ok=ok && wpa_probe_verify(&w.s)==WPA_PROBE_OK && w.s.retries_verified==1 && crypto_equal(&w.group,&w.s.group,sizeof(w.group));
    unhex(w.mic,"29e5e3d71d58ef2ee7e21f50e8056c9c",16);
    ok=ok && wpa_probe_m4(&w.s,local,ap,w.frame,sizeof(w.frame),&n)==WPA_PROBE_OK && n==113 && w.frame[30]==5 && crypto_equal(w.frame+95,w.mic,16);
    wpa_probe_clear(&w.s);const unsigned char *p=(const void *)&w.s;for (size_t i=0;i<sizeof(w.s);++i) { ok=ok && !p[i]; }
    crypto_wipe(&w,sizeof(w));return ok;
}
