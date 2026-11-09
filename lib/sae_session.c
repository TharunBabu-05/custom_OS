/* Original bounded SAE crypto exchange. Protocol definitions: hostap sae.c
 * and sha256-prf.c. This backend is not yet connected to management frames.
 * Only group 19/H2E/AKM8 without optional IEs or rejected-group salt is accepted. */
#include "sae_session.h"
#include "p256.h"
#include "crypto.h"
static bool overlap(const void *a,size_t an,const void *b,size_t bn)
{ uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return an && bn && (x<=y ? y-x<an : x-y<bn); }
static void copy(unsigned char *out,const unsigned char *in,size_t n)
{ for (size_t i=0;i<n;++i) { out[i]=in[i]; } }
void sae_session_clear(struct sae_session *s) { if (s) { crypto_wipe(s,sizeof(*s)); } }
static enum sae_result fail(struct sae_session *s,enum sae_result error)
{ unsigned pairs=s->random_pairs;sae_session_clear(s);s->phase=SAE_FAILED;s->error=error;s->random_pairs=pairs;return error; }
static bool scalar(const unsigned char *value)
{
    unsigned high=0;for (unsigned i=0;i<31;++i) { high|=value[i]; }
    return p256_scalar_valid(value) && (high || value[31]>1);
}
enum sae_result sae_session_start(struct sae_session *s,const unsigned char pwe[64],sae_random_fn random,void *context)
{
    if (!s || !pwe || !random || overlap(pwe,64,s,sizeof(*s))) { return SAE_ARGUMENT; }
    if (s->phase!=SAE_EMPTY) { return SAE_STATE; }
    if (!p256_point_valid(pwe)) { return fail(s,SAE_FORMAT); }
    struct { unsigned char random[32],mask[32],sum[32],element[64]; } w={0};enum sae_result result=SAE_RANDOM;
    copy(s->pwe,pwe,64);
    for (unsigned attempt=0;attempt<16;++attempt) {
        ++s->random_pairs;
        if (!random(context,w.random) || !random(context,w.mask)) { break; }
        if (!scalar(w.random) || !scalar(w.mask) || crypto_equal(w.random,w.mask,32)) { continue; }
        if (!p256_scalar_sum(w.random,w.mask,w.sum) || !scalar(w.sum)) { continue; }
        if (!p256_multiply(w.mask,s->pwe,w.element) || !p256_negate(w.element,w.element)) { result=SAE_MATH;break; }
        s->own[0]=19;s->own[1]=0;copy(s->own+2,w.sum,32);copy(s->own+34,w.element,64);copy(s->random,w.random,32);
        s->phase=SAE_COMMITTED;s->error=SAE_OK;result=SAE_OK;break;
    }
    crypto_wipe(&w,sizeof(w));return result==SAE_OK ? result : fail(s,result);
}
enum sae_result sae_session_commit(const struct sae_session *s,unsigned char output[SAE_COMMIT_BYTES])
{
    if (!s || !output || overlap(output,SAE_COMMIT_BYTES,s,sizeof(*s))) { return SAE_ARGUMENT; }
    crypto_wipe(output,SAE_COMMIT_BYTES);
    if (s->phase<SAE_COMMITTED || s->phase>SAE_VERIFIED) { return SAE_STATE; }
    copy(output,s->own,SAE_COMMIT_BYTES);return SAE_OK;
}
static bool derive_keys(struct sae_session *s,const unsigned char shared[64])
{
    static const char label[]="SAE KCK and PMK";
    struct { unsigned char zero[32],seed[32],context[32],keys[64],counter[2],bits[2]; } w={0};
    w.bits[1]=2; /* 512 output bits, little endian. This is not HKDF-Expand. */
    struct crypto_span input={shared,32};
    bool ok=hmac_sha256(w.zero,32,&input,1,w.seed) && p256_scalar_sum(s->own+2,s->peer+2,w.context);
    for (unsigned block=0;ok && block<2;++block) {
        w.counter[0]=(unsigned char)(block+1);
        struct crypto_span parts[]={{w.counter,2},{label,sizeof(label)-1},{w.context,32},{w.bits,2}};
        ok=hmac_sha256(w.seed,32,parts,4,w.keys+32*block);
    }
    if (ok) { copy(s->kck,w.keys,32);copy(s->keys.pmk,w.keys+32,32);copy(s->keys.pmkid,w.context,16); }
    crypto_wipe(&w,sizeof(w));return ok;
}
enum sae_result sae_session_peer(struct sae_session *s,const void *body,size_t bytes)
{
    if (!s) { return SAE_ARGUMENT; }if (s->phase!=SAE_COMMITTED) { return SAE_STATE; }
    if (!body || bytes!=SAE_COMMIT_BYTES) { return fail(s,SAE_FORMAT); }
    if (overlap(body,bytes,s,sizeof(*s))) { return SAE_ARGUMENT; }
    const unsigned char *p=body;
    if (p[0]!=19 || p[1] || !scalar(p+2) || !p256_point_valid(p+34)) { return fail(s,SAE_FORMAT); }
    if (crypto_equal(p+2,s->own+2,96)) { return fail(s,SAE_REFLECTION); }
    copy(s->peer,p,bytes);struct { unsigned char t[64],sum[64],shared[64]; } w={0};
    bool ok=p256_multiply(p+2,s->pwe,w.t) && p256_add(w.t,p+34,w.sum) && p256_multiply(s->random,w.sum,w.shared) && derive_keys(s,w.shared);
    crypto_wipe(&w,sizeof(w));crypto_wipe(s->pwe,64);crypto_wipe(s->random,32);
    if (!ok) { return fail(s,SAE_MATH); }s->phase=SAE_KEYS;return SAE_OK;
}
static bool confirm(const struct sae_session *s,const unsigned char counter[2],bool peer,unsigned char output[32])
{
    struct crypto_span parts[]={{counter,2},{peer ? s->peer+2 : s->own+2,96},{peer ? s->own+2 : s->peer+2,96}};
    return hmac_sha256(s->kck,32,parts,3,output);
}
enum sae_result sae_session_confirm(struct sae_session *s,unsigned char output[SAE_CONFIRM_BYTES])
{
    if (!s || !output || overlap(output,SAE_CONFIRM_BYTES,s,sizeof(*s))) { return SAE_ARGUMENT; }
    crypto_wipe(output,SAE_CONFIRM_BYTES);
    if (s->phase==SAE_KEYS) {
        s->confirm[0]=1;s->confirm[1]=0;
        if (!confirm(s,s->confirm,false,s->confirm+2)) { return fail(s,SAE_MATH); }s->phase=SAE_SENT;
    }
    if (s->phase!=SAE_SENT && s->phase!=SAE_VERIFIED) { return SAE_STATE; }
    copy(output,s->confirm,SAE_CONFIRM_BYTES);return SAE_OK;
}
enum sae_result sae_session_verify(struct sae_session *s,const void *body,size_t bytes)
{
    if (!s) { return SAE_ARGUMENT; }if (s->phase!=SAE_SENT) { return SAE_STATE; }
    if (!body || bytes!=SAE_CONFIRM_BYTES) { return fail(s,SAE_FORMAT); }
    if (overlap(body,bytes,s,sizeof(*s))) { return SAE_ARGUMENT; }
    const unsigned char *p=body;unsigned counter=p[0]|(unsigned)p[1]<<8;
    if (!counter || counter==65535) { return fail(s,SAE_CONFIRM); }
    unsigned char expected[32];bool ok=confirm(s,p,true,expected) && crypto_equal(expected,p+2,32);crypto_wipe(expected,sizeof(expected));
    if (!ok) { return fail(s,SAE_CONFIRM); }
    crypto_wipe(s->kck,32);s->phase=SAE_VERIFIED;return SAE_OK;
}
enum sae_result sae_session_take_keys(struct sae_session *s,struct sae_keys *output)
{
    if (!s || !output || overlap(output,sizeof(*output),s,sizeof(*s))) { return SAE_ARGUMENT; }
    crypto_wipe(output,sizeof(*output));if (s->phase!=SAE_VERIFIED) { return SAE_STATE; }
    *output=s->keys;sae_session_clear(s);s->phase=SAE_EXPORTED;return SAE_OK;
}
