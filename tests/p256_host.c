#include <stdio.h>
#include <string.h>
#include "p256.h"
#include "host_assert.h"
static unsigned nibble(char c)
{ assert((c>='0' && c<='9') || (c>='a' && c<='f'));return c<='9' ? (unsigned)(c-'0') : (unsigned)(c-'a'+10); }
static void unhex(unsigned char *p,const char *s,unsigned n)
{ assert(strlen(s)==n*2);for (unsigned i=0;i<n;++i) { p[i]=(unsigned char)(16*nibble(s[2*i])+nibble(s[2*i+1])); } }
static void erased(const unsigned char *p) { for (unsigned i=0;i<64;++i) { assert(p[i]==0); } }
int main(int argc,char **argv)
{
    unsigned char scalar[32]={0},g[64],out[66],p[64],q[64],nminus[32];p256_generator(g);
    if (argc==2) {
        FILE *f=fopen(argv[1],"r");assert(f);char s[65],point[129],expected[129],sum[129],neg[129];unsigned count=0;
        while (fscanf(f,"%64s %128s %128s %128s %128s",s,point,expected,sum,neg)==5) {
            unhex(scalar,s,32);unhex(p,point,64);unhex(q,expected,64);
            assert(p256_multiply(scalar,p,out) && !memcmp(out,q,64));
            unhex(q,sum,64);assert(p256_add(g,p,out) && !memcmp(out,q,64));
            unhex(q,neg,64);assert(p256_negate(p,out) && !memcmp(out,q,64));++count;
        }
        assert(feof(f) && count>=32);fclose(f);printf("PASS: %u independent OpenSSL scalar/point/addition/negation vector sets\n",count);return 0;
    }
    assert(argc==1 && p256_selftest());assert(!p256_point_valid(NULL) && !p256_scalar_valid(NULL));
    assert(!p256_scalar_valid(scalar));scalar[31]=1;assert(p256_scalar_valid(scalar));
    unhex(nminus,"ffffffff00000000ffffffffffffffffbce6faada7179e84f3b9cac2fc632550",32);
    assert(p256_scalar_valid(nminus) && p256_multiply(nminus,g,p) && p256_negate(g,q) && !memcmp(p,q,64));
    ++nminus[31];assert(!p256_scalar_valid(nminus));memset(out,0xa5,sizeof(out));assert(!p256_multiply(nminus,g,out));erased(out);assert(out[64]==0xa5 && out[65]==0xa5);
    memset(nminus,255,32);assert(!p256_scalar_valid(nminus));
    memset(p,0,64);assert(!p256_point_valid(p));assert(!p256_multiply(scalar,p,out));erased(out);
    memcpy(p,g,64);p[63]^=1;assert(!p256_point_valid(p));assert(!p256_add(g,p,out));erased(out);
    memcpy(p,g,64);unhex(p,"ffffffff00000001000000000000000000000000ffffffffffffffffffffffff",32);
    assert(!p256_point_valid(p));assert(!p256_negate(p,out));erased(out);
    memset(p,255,64);assert(!p256_point_valid(p));
    assert(!p256_multiply(NULL,g,out));erased(out);assert(!p256_multiply(scalar,NULL,out));erased(out);
    assert(!p256_multiply(scalar,g,NULL));assert(!p256_add(NULL,g,out));erased(out);assert(!p256_add(g,NULL,out));erased(out);assert(!p256_add(g,g,NULL));
    assert(!p256_negate(NULL,out));erased(out);assert(!p256_negate(g,NULL));
    /* Inputs may alias output. Failures must erase it even when aliased. */
    memcpy(p,g,64);assert(p256_multiply(scalar,p,p) && !memcmp(p,g,64));
    memset(p,0,64);p[31]=1;assert(p256_multiply(p,g,p) && !memcmp(p,g,64));
    memcpy(p,g,64);assert(p256_add(p,p,p) && p256_add(g,g,q) && !memcmp(p,q,64));
    assert(p256_negate(p,p) && p256_negate(p,p) && !memcmp(p,q,64));
    memset(p,0,64);assert(!p256_multiply(p,g,p));erased(p);
    puts("PASS: RFC5903 public/shared point vectors, scalar 1/2/n-1, doubling/inverse/infinity, canonical/off-curve checks, null arguments, aliasing, output bounds and failure erasure");return 0;
}
