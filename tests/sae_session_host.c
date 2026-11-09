#include <stdio.h>
#include <string.h>
#include "sae_session.h"
#include "p256.h"
#include "crypto.h"
#include "host_assert.h"
struct fixture_random { unsigned calls,mode;unsigned char r[32],mask[32]; };
static bool random_bytes(void *ctx,unsigned char out[32])
{
    struct fixture_random *f=ctx;unsigned call=f->calls++;memset(out,0,32);
    if (f->mode==1 || (f->mode==2 && call==1)) { memset(out,0xa5,16);return false; }
    if (f->mode==3) { return true; } /* zeros exhaust the bounded rejection loop */
    if (f->mode==4) { memcpy(out,f->r,32);return true; } /* repeated scalar */
    if (f->mode==5 && call<2) { out[31]=1;return true; }
    memcpy(out,call&1 ? f->mask : f->r,32);return true;
}
static unsigned nibble(char c) { assert((c>='0' && c<='9') || (c>='a' && c<='f'));return c<='9' ? (unsigned)(c-'0') : (unsigned)(c-'a'+10); }
static size_t unhex(unsigned char *out,const char *s)
{ size_t n=strlen(s);assert(!(n&1));for (size_t i=0;i<n/2;++i) { out[i]=(unsigned char)(16*nibble(s[2*i])+nibble(s[2*i+1])); }return n/2; }
static void zero(const void *p,size_t n) { const unsigned char *b=p;for (size_t i=0;i<n;++i) { assert(!b[i]); } }
static void failed(const struct sae_session *s)
{
    assert(s->phase==SAE_FAILED);zero(s->pwe,64);zero(s->random,32);zero(s->own,98);zero(s->peer,98);zero(s->kck,32);zero(s->confirm,34);zero(&s->keys,sizeof(s->keys));
}
int main(int argc,char **argv)
{
    assert(argc==2 && sae_session_selftest());FILE *f=fopen(argv[1],"r");assert(f);
    char pwe_hex[129],ra[65],ma[65],rb[65],mb[65],ca_hex[197],cb_hex[197],fa_hex[69],fb_hex[69],pmk_hex[65],id_hex[33];
    unsigned char pwe[64],ca[100],cb[98],fa[36],fb[34],expected[100],bad[100],scalar[32],other[32],sum[32];
    struct sae_session a={0},b={0},prepared={0},sent={0},trial;
    struct fixture_random ar={0},br={0};struct sae_keys ka,kb;unsigned count=0;
    while (fscanf(f,"%128s %64s %64s %64s %64s %196s %196s %68s %68s %64s %32s",pwe_hex,ra,ma,rb,mb,ca_hex,cb_hex,fa_hex,fb_hex,pmk_hex,id_hex)==11) {
        sae_session_clear(&a);sae_session_clear(&b);ar=(struct fixture_random){0};br=(struct fixture_random){0};
        assert(unhex(pwe,pwe_hex)==64);unhex(ar.r,ra);unhex(ar.mask,ma);unhex(br.r,rb);unhex(br.mask,mb);
        assert(sae_session_start(&a,pwe,random_bytes,&ar)==SAE_OK && ar.calls==2);
        assert(sae_session_start(&b,pwe,random_bytes,&br)==SAE_OK && br.calls==2);
        if (!count) { prepared=a; }
        memset(ca,0xa5,sizeof(ca));assert(sae_session_commit(&a,ca)==SAE_OK);unhex(expected,ca_hex);assert(!memcmp(ca,expected,98) && ca[98]==0xa5);
        assert(sae_session_commit(&b,cb)==SAE_OK);unhex(expected,cb_hex);assert(!memcmp(cb,expected,98));
        assert(sae_session_peer(&a,cb,98)==SAE_OK && sae_session_peer(&b,ca,98)==SAE_OK);zero(a.random,32);zero(a.pwe,64);
        memset(&ka,0xa5,sizeof(ka));assert(sae_session_take_keys(&a,&ka)==SAE_STATE);zero(&ka,sizeof(ka));
        assert(sae_session_confirm(&a,fa)==SAE_OK && sae_session_confirm(&b,fb)==SAE_OK);
        if (!count) { sent=a; }
        unhex(expected,fa_hex);assert(!memcmp(fa,expected,34));unhex(expected,fb_hex);assert(!memcmp(fb,expected,34));
        assert(sae_session_confirm(&a,bad)==SAE_OK && !memcmp(fa,bad,34));
        assert(sae_session_verify(&a,fb,34)==SAE_OK && sae_session_verify(&b,fa,34)==SAE_OK);zero(a.kck,32);
        assert(sae_session_verify(&a,fb,34)==SAE_STATE); /* replay cannot advance accepted state */
        assert(sae_session_take_keys(&a,&ka)==SAE_OK && sae_session_take_keys(&b,&kb)==SAE_OK && !memcmp(&ka,&kb,sizeof(ka)));
        unhex(expected,pmk_hex);assert(!memcmp(ka.pmk,expected,32));unhex(expected,id_hex);assert(!memcmp(ka.pmkid,expected,16));
        assert(a.phase==SAE_EXPORTED);zero(&a.keys,sizeof(a.keys));zero(a.kck,32);zero(a.random,32);
        assert(sae_session_take_keys(&a,&ka)==SAE_STATE);zero(&ka,sizeof(ka));++count;
    }assert(feof(f) && count==12);fclose(f);
    puts("PASS: 12 independent SAE commit/confirm/PMK/PMKID exchanges, affine/OpenSSL shared-X checks, matching peer keys, cached confirms, export gating and one-time key destruction");
    /* First fixture's public states make hostile-body tests cheap and isolated. */
    assert(sae_session_commit(&prepared,ca)==SAE_OK);
    assert(sae_session_commit(&sent,cb)==SAE_OK); /* own public body */
    for (size_t n=0;n<98;++n) { trial=prepared;assert(sae_session_peer(&trial,ca,n)==SAE_FORMAT);failed(&trial); }
    trial=prepared;assert(sae_session_peer(&trial,NULL,98)==SAE_FORMAT);failed(&trial);
    trial=prepared;assert(sae_session_peer(&trial,ca,99)==SAE_FORMAT);failed(&trial);
    trial=prepared;assert(sae_session_peer(&trial,ca,98)==SAE_REFLECTION);failed(&trial);
    memcpy(bad,ca,98);bad[0]=20;trial=prepared;assert(sae_session_peer(&trial,bad,98)==SAE_FORMAT);failed(&trial);
    memcpy(bad,ca,98);memset(bad+2,0,32);bad[33]=1;trial=prepared;assert(sae_session_peer(&trial,bad,98)==SAE_FORMAT);failed(&trial);
    memcpy(bad,ca,98);memset(bad+2,255,32);trial=prepared;assert(sae_session_peer(&trial,bad,98)==SAE_FORMAT);failed(&trial);
    memcpy(bad,ca,98);memset(bad+34,0,64);trial=prepared;assert(sae_session_peer(&trial,bad,98)==SAE_FORMAT);failed(&trial);
    /* Valid scalar/point whose peer sum cancels to infinity must not yield keys. */
    memcpy(bad,ca,98);assert(p256_multiply(bad+2,prepared.pwe,bad+34) && p256_negate(bad+34,bad+34));
    trial=prepared;assert(sae_session_peer(&trial,bad,98)==SAE_MATH);failed(&trial);
    for (size_t n=0;n<34;++n) { trial=sent;assert(sae_session_verify(&trial,fa,n)==SAE_FORMAT);failed(&trial); }
    trial=sent;assert(sae_session_verify(&trial,NULL,34)==SAE_FORMAT);failed(&trial);
    trial=sent;assert(sae_session_verify(&trial,fa,35)==SAE_FORMAT);failed(&trial);
    assert(sae_session_confirm(&sent,fa)==SAE_OK);
    trial=sent;assert(sae_session_verify(&trial,fa,34)==SAE_CONFIRM);failed(&trial); /* reflected confirm */
    for (unsigned i=0;i<34;++i) { memcpy(bad,fa,34);bad[i]^=1;trial=sent;assert(sae_session_verify(&trial,bad,34)==SAE_CONFIRM);failed(&trial); }
    memset(bad,0,34);trial=sent;assert(sae_session_verify(&trial,bad,34)==SAE_CONFIRM);failed(&trial);
    bad[0]=bad[1]=255;trial=sent;assert(sae_session_verify(&trial,bad,34)==SAE_CONFIRM);failed(&trial);
    puts("PASS: truncated/extended/group/scalar/point bodies, reflection, identity cancellation, confirm tampering/counters/replay and full failed-state erasure");
    /* RNG error/partial output, repeats, zero/one rejection and bounded retry. */
    unhex(pwe,"f9681ec9f5e39390b50ffaa69bdf04621f662f42fe85e2f101f7dcdcaa6a5bab948a94f8e068a5e976060fc5cb751f876e4346d7b7fabb5c6b636ed17aaf59f5");
    for (unsigned mode=1;mode<=5;++mode) {
        ar=(struct fixture_random){.mode=mode};ar.r[31]=2;ar.mask[31]=3;sae_session_clear(&trial);
        enum sae_result r=sae_session_start(&trial,pwe,random_bytes,&ar);
        if (mode==5) { assert(r==SAE_OK && ar.calls==4 && trial.random_pairs==2); }
        else { assert(r==SAE_RANDOM && ar.calls==(mode==1 ? 1u : mode==2 ? 2u : 32u));failed(&trial); }
        unsigned calls=ar.calls;assert(sae_session_start(&trial,pwe,random_bytes,&ar)==SAE_STATE && ar.calls==calls);
    }
    ar=(struct fixture_random){0};unhex(ar.r,"ffffffff00000000ffffffffffffffffbce6faada7179e84f3b9cac2fc632550");ar.mask[31]=2;
    sae_session_clear(&trial);assert(sae_session_start(&trial,pwe,random_bytes,&ar)==SAE_RANDOM && ar.calls==32);failed(&trial); /* sum=1 */
    /* Mod-n addition covers carry, zero result, endpoints and aliasing. */
    unhex(scalar,"ffffffff00000000ffffffffffffffffbce6faada7179e84f3b9cac2fc632550");memset(other,0,32);other[31]=1;
    assert(p256_scalar_sum(scalar,other,sum));zero(sum,32);
    assert(p256_scalar_sum(scalar,scalar,sum));--scalar[31];assert(!memcmp(sum,scalar,32));
    assert(p256_scalar_sum(other,other,other) && other[31]==2);
    memset(scalar,0,32);assert(!p256_scalar_sum(scalar,other,sum));zero(sum,32);
    assert(!p256_scalar_sum(NULL,other,sum));zero(sum,32);assert(!p256_scalar_sum(other,other,NULL));
    sae_session_clear(&trial);zero(&trial,sizeof(trial));assert(sae_session_take_keys(&trial,&ka)==SAE_STATE);zero(&ka,sizeof(ka));
    assert(sae_session_verify(&trial,fa,34)==SAE_STATE && sae_session_peer(&trial,ca,98)==SAE_STATE);
    assert(sae_session_commit(&trial,ca)==SAE_STATE);zero(ca,98);assert(sae_session_confirm(&trial,fa)==SAE_STATE);zero(fa,34);
    assert(sae_session_start(NULL,pwe,random_bytes,&ar)==SAE_ARGUMENT);assert(sae_session_start(&trial,pwe,NULL,NULL)==SAE_ARGUMENT);
    assert(sae_session_start(&trial,trial.pwe,random_bytes,&ar)==SAE_ARGUMENT);
    assert(sae_session_commit(&prepared,prepared.own)==SAE_ARGUMENT);
    assert(sae_session_peer(&prepared,prepared.own,98)==SAE_ARGUMENT);
    assert(sae_session_confirm(&sent,sent.confirm)==SAE_ARGUMENT);
    assert(sae_session_verify(&sent,sent.confirm,34)==SAE_ARGUMENT);
    assert(sae_session_commit(NULL,ca)==SAE_ARGUMENT && sae_session_confirm(NULL,fa)==SAE_ARGUMENT);
    assert(sae_session_peer(NULL,ca,98)==SAE_ARGUMENT && sae_session_verify(NULL,fa,34)==SAE_ARGUMENT);
    assert(sae_session_take_keys(NULL,&ka)==SAE_ARGUMENT && sae_session_take_keys(&trial,NULL)==SAE_ARGUMENT);
    /* Different password elements must never produce an accepted peer proof. */
    memset(scalar,0,32);scalar[31]=2;assert(p256_multiply(scalar,pwe,expected));
    sae_session_clear(&a);sae_session_clear(&b);ar=(struct fixture_random){0};br=(struct fixture_random){0};ar.r[31]=2;ar.mask[31]=3;br.r[31]=5;br.mask[31]=7;
    assert(sae_session_start(&a,pwe,random_bytes,&ar)==SAE_OK && sae_session_start(&b,expected,random_bytes,&br)==SAE_OK);
    assert(sae_session_commit(&a,ca)==SAE_OK && sae_session_commit(&b,cb)==SAE_OK);
    assert(sae_session_peer(&a,cb,98)==SAE_OK && sae_session_peer(&b,ca,98)==SAE_OK);
    assert(sae_session_confirm(&a,fa)==SAE_OK && sae_session_confirm(&b,fb)==SAE_OK);
    assert(sae_session_verify(&a,fb,34)==SAE_CONFIRM && sae_session_verify(&b,fa,34)==SAE_CONFIRM);failed(&a);failed(&b);
    assert(sae_session_take_keys(&a,&ka)==SAE_STATE);zero(&ka,sizeof(ka));
    puts("PASS: RNG failure without fallback, bounded rejection sampling, repeated draws, invalid scalar sums, mod-n arithmetic, wrong-state/alias guards and explicit reset");return 0;
}
