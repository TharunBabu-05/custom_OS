#include "wpa_keys.h"
#include "crypto.h"
#include "aes.h"
#include "wpa_kde.h"
#include "wpa_probe.h"
static unsigned nibble(char c) { return c<='9' ? (unsigned)(c-'0') : (unsigned)(c-'a'+10); }
static void unhex(unsigned char *out,const char *s,size_t n) { for (size_t i=0;i<n;++i) { out[i]=(unsigned char)(16*nibble(s[2*i])+nibble(s[2*i+1])); } }
bool wpa_keys_selftest(void)
{
    static const unsigned char rsn[]={48,26,1,0,0,15,172,4,1,0,0,15,172,4,1,0,0,15,172,8,192,0,0,0,0,15,172,6};
    struct { unsigned char pmk[32],anonce[32],expected[48],frame[141];struct wpa_sae_ptk ptk;struct wpa_m2_input in;struct wpa_m4_input final; } w={.in={.local={2,0,0,0,0,1},.ap={2,0,0,0,0,2},.version=2,.rsn=rsn,.rsn_bytes=sizeof(rsn)}};
    unhex(w.pmk,"45c81d7094f6bfc4b41f166934baa20874d6d41ac67cc5e8c5309de1eb009aae",32);
    unhex(w.in.snonce,"a7e7be99571a0fd96e2c89855481e9e47b80df1b53daa1e0358f0a56d79c496c",32);
    unhex(w.anonce,"f6579b97a24afc54942ed84ea3266c2384156a12c8d88128fe28438a41727c0e",32);
    unhex(w.expected,"23d8687d0165ef1ebf8c92b01b1fa22e2606e6424e530cb7cacaf31b7fde94c72a713ddb2dd4645bf76ea658df54babd",48);
    bool ok=aes128_selftest() && wpa_sae_ptk(w.pmk,w.in.local,w.in.ap,w.in.snonce,w.anonce,&w.ptk) && crypto_equal(&w.ptk,w.expected,48);
    for (unsigned i=0;i<16;++i) { w.in.kck[i]=w.ptk.kck[i]; }w.in.replay[7]=1;size_t n=0;
    unhex(w.expected,"36a4d7b1e06102c3731cb0f4ded984a9",16);
    ok=ok && wpa_sae_m2(&w.in,w.frame,sizeof(w.frame),&n) && n==141 && crypto_equal(w.frame+95,w.expected,16);
    w.final.version=2;w.final.replay[7]=2;for (unsigned i=0;i<6;++i) { w.final.local[i]=w.in.local[i];w.final.ap[i]=w.in.ap[i]; }
    for (unsigned i=0;i<16;++i) { w.final.kck[i]=w.ptk.kck[i]; }unhex(w.expected,"d3d186b96f84ae174bd1319d46ded7d5",16);
    ok=ok && wpa_sae_m4(&w.final,w.frame,sizeof(w.frame),&n) && n==113 && crypto_equal(w.frame+95,w.expected,16);
    ok=ok && wpa_kde_selftest() && wpa_retry_selftest();crypto_wipe(&w,sizeof(w));return ok;
}
