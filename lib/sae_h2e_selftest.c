/* Public synthetic fixtures, plus an RFC9380 J.1.1 SSWU intermediate. */
#include "sae_h2e.h"
#include "p256.h"
#include "crypto.h"
static unsigned nibble(char c) { return c<='9' ? (unsigned)(c-'0') : (unsigned)(c-'a'+10); }
static void unhex(unsigned char *out,const char *s,unsigned n)
{ for (unsigned i=0;i<n;++i) { out[i]=(unsigned char)(16*nibble(s[2*i])+nibble(s[2*i+1])); } }
bool sae_h2e_selftest(void)
{
    static const char ssid[]="OurOS-H2E-public-test",password[]="not-a-network-secret";
    static const unsigned char mac_a[6]={2,0,0,0,0,1},mac_b[6]={2,0,0,0,0,2};
    struct { unsigned char value[48],point[64],expected[64],pt[64],pwe[64]; } w={0};
    unhex(w.value+16,"ad5342c66a6dd0ff080df1da0ea1c04b96e0330dd89406465eeba11582515009",32);
    unhex(w.expected,"ab640a12220d3ff283510ff3f4b1953d09fad35795140b1c5d64f313967934d5dccb558863804a881d4fff3455716c836cef230e5209594ddd33d85c565b19b1",64);
    bool ok=p256_sswu(w.value,w.point) && crypto_equal(w.point,w.expected,64);
    unhex(w.expected,"6bbf69fca4c909b31fb87a4f92f821bc43469db8b4476dbf635056379ef751defa64563b7a12b2ce48fb8e70dbcf11fb79be8c59b4d90ecf628435592d39ca03",64);
    ok=ok && sae_h2e_pt(ssid,sizeof(ssid)-1,password,sizeof(password)-1,NULL,0,w.pt) && crypto_equal(w.pt,w.expected,64);
    unhex(w.expected,"f9681ec9f5e39390b50ffaa69bdf04621f662f42fe85e2f101f7dcdcaa6a5bab948a94f8e068a5e976060fc5cb751f876e4346d7b7fabb5c6b636ed17aaf59f5",64);
    ok=ok && sae_h2e_pwe(w.pt,mac_a,mac_b,w.pwe) && crypto_equal(w.pwe,w.expected,64);
    crypto_wipe(&w,sizeof(w));return ok;
}
