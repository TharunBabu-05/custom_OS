/* PUBLIC synthetic exchange only. Expected bytes cross-checked with the
 * independent affine/OpenSSL host model in tests/sae_session_vectors.mjs. */
#include "sae_session.h"
#include "crypto.h"
struct public_random { unsigned calls;unsigned char first,second; };
static bool fixture_random(void *context,unsigned char output[32])
{
    struct public_random *r=context;crypto_wipe(output,32);if (r->calls>=2) { return false; }
    output[31]=r->calls++ ? r->second : r->first;return true;
}
static unsigned nibble(char c) { return c<='9' ? (unsigned)(c-'0') : (unsigned)(c-'a'+10); }
static void unhex(unsigned char *out,const char *s,unsigned n)
{ for (unsigned i=0;i<n;++i) { out[i]=(unsigned char)(16*nibble(s[2*i])+nibble(s[2*i+1])); } }
bool sae_session_selftest(void)
{
    struct { struct sae_session a,b,bad;struct sae_keys ka,kb;unsigned char pwe[64],ca[98],cb[98],fa[34],fb[34],again[34],expected[98]; } w={0};
    struct public_random a={0,2,3},b={0,5,7};
    unhex(w.pwe,"f9681ec9f5e39390b50ffaa69bdf04621f662f42fe85e2f101f7dcdcaa6a5bab948a94f8e068a5e976060fc5cb751f876e4346d7b7fabb5c6b636ed17aaf59f5",64);
    bool ok=sae_session_start(&w.a,w.pwe,fixture_random,&a)==SAE_OK && sae_session_start(&w.b,w.pwe,fixture_random,&b)==SAE_OK;
    ok=ok && sae_session_commit(&w.a,w.ca)==SAE_OK && sae_session_commit(&w.b,w.cb)==SAE_OK;
    unhex(w.expected,"1300000000000000000000000000000000000000000000000000000000000000000544ed94a3ef3915493b48475af3ade696d085cc667946357a5fccbc5c218833c61dfc5384c43a74069a02e722691772e248bc1c50c847f16cad234f2febad711e",98);
    ok=ok && crypto_equal(w.ca,w.expected,98) && a.calls==2 && b.calls==2;
    ok=ok && sae_session_peer(&w.a,w.cb,98)==SAE_OK && sae_session_peer(&w.b,w.ca,98)==SAE_OK;
    ok=ok && sae_session_take_keys(&w.a,&w.ka)==SAE_STATE;
    ok=ok && sae_session_confirm(&w.a,w.fa)==SAE_OK && sae_session_confirm(&w.b,w.fb)==SAE_OK;
    ok=ok && sae_session_confirm(&w.a,w.again)==SAE_OK && crypto_equal(w.fa,w.again,34);
    unhex(w.expected,"01004c167d299b5214b8a3db39fa6e8432694e483095a7aa0d9efb6c3ba7bd1c2fdc",34);
    ok=ok && crypto_equal(w.fa,w.expected,34);
    w.bad=w.b;w.again[2]^=1;ok=ok && sae_session_verify(&w.bad,w.again,34)==SAE_CONFIRM && w.bad.phase==SAE_FAILED;
    ok=ok && sae_session_take_keys(&w.bad,&w.ka)==SAE_STATE;
    ok=ok && sae_session_verify(&w.a,w.fb,34)==SAE_OK && sae_session_verify(&w.b,w.fa,34)==SAE_OK;
    ok=ok && sae_session_take_keys(&w.a,&w.ka)==SAE_OK && sae_session_take_keys(&w.b,&w.kb)==SAE_OK && crypto_equal(&w.ka,&w.kb,sizeof(w.ka));
    unhex(w.expected,"f9d594cd2e2d8dc1b33b346e28e9ccefdf58c792142afac8eec46f033bf2827a",32);
    ok=ok && crypto_equal(w.ka.pmk,w.expected,32) && w.a.phase==SAE_EXPORTED && w.b.phase==SAE_EXPORTED;
    crypto_wipe(&w,sizeof(w));crypto_wipe(&a,sizeof(a));crypto_wipe(&b,sizeof(b));return ok;
}
