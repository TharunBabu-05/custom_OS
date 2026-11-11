#include <stdio.h>
#include <string.h>
#include "host_assert.h"
void p256_field_product_test(const unsigned char *,const unsigned char *,unsigned char *);
static unsigned nibble(char c) { assert((c>='0' && c<='9') || (c>='a' && c<='f'));return c<='9' ? (unsigned)(c-'0') : (unsigned)(c-'a'+10); }
static void unhex(unsigned char *p,const char *s) { assert(strlen(s)==64);for (unsigned i=0;i<32;++i) { p[i]=(unsigned char)(16*nibble(s[2*i])+nibble(s[2*i+1])); } }
int main(int argc,char **argv)
{
    assert(argc==2);FILE *f=fopen(argv[1],"r");assert(f);char a[65],b[65],expected[65];unsigned char x[32],y[32],want[32],out[34];unsigned count=0;
    while (fscanf(f,"%64s %64s %64s",a,b,expected)==3) {
        unhex(x,a);unhex(y,b);unhex(want,expected);memset(out,0xa5,sizeof(out));
        p256_field_product_test(x,y,out);assert(!memcmp(out,want,32) && out[32]==0xa5 && out[33]==0xa5);
        p256_field_product_test(x,y,x);assert(!memcmp(x,want,32));++count;
    }
    assert(feof(f) && count>13000);fclose(f);printf("PASS: %u BigInt P-256 field products, full-width/max/prime/bit boundaries, aliasing and output bounds\n",count);return 0;
}
