/* Public known-answer vectors from RFC5903 section 8.1; not live keys. */
#include "p256.h"
#include "crypto.h"
static unsigned nibble(char c) { return c<='9' ? (unsigned)(c-'0') : (unsigned)(c-'a'+10); }
static void unhex(unsigned char *out,const char *hex,unsigned n)
{ for (unsigned i=0;i<n;++i) { out[i]=(unsigned char)(16*nibble(hex[2*i])+nibble(hex[2*i+1])); } }
bool p256_selftest(void)
{
    unsigned char scalar[32],g[64],expected[64],other[64],out[64],neg[64];bool ok;
    p256_generator(g);ok=p256_point_valid(g);
    unhex(scalar,"c88f01f510d9ac3f70a292daa2316de544e9aab8afe84049c62a9c57862d1433",32);
    unhex(expected,"dad0b65394221cf9b051e1feca5787d098dfe637fc90b9ef945d0c37725811805271a0461cdb8252d61f1c456fa3e59ab1f45b33accf5f58389e0577b8990bb3",64);
    ok=ok && p256_multiply(scalar,g,out) && crypto_equal(out,expected,64);
    unhex(other,"d12dfb5289c8d4f81208b70270398c342296970a0bccb74c736fc7554494bf6356fbf3ca366cc23e8157854c13c58d6aac23f046ada30f8353e74f33039872ab",64);
    unhex(expected,"d6840f6b42f6edafd13116e0e12565202fef8e9ece7dce03812464d04b9442de522bde0af0d8585b8def9c183b5ae38f50235206a8674ecb5d98edb20eb153a2",64);
    ok=ok && p256_multiply(scalar,other,out) && crypto_equal(out,expected,64);
    crypto_wipe(scalar,sizeof(scalar));scalar[31]=1;
    ok=ok && p256_multiply(scalar,g,out) && crypto_equal(out,g,64);
    scalar[31]=2;ok=ok && p256_multiply(scalar,g,out) && p256_add(g,g,other) && crypto_equal(out,other,64);
    ok=ok && p256_negate(g,neg) && p256_negate(neg,other) && crypto_equal(g,other,64) && !p256_add(g,neg,out);
    for (unsigned i=0;i<64;++i) { ok=ok && !out[i]; }
    crypto_wipe(scalar,sizeof(scalar));ok=ok && !p256_multiply(scalar,g,out);
    crypto_wipe(g,sizeof(g));crypto_wipe(expected,sizeof(expected));crypto_wipe(other,sizeof(other));crypto_wipe(out,sizeof(out));crypto_wipe(neg,sizeof(neg));return ok;
}
