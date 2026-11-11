#include <stdio.h>
#include <string.h>
#include "sae_hunt.h"
#include "p256.h"
#include "sae_session.h"
#include "host_assert.h"
static size_t unhex(unsigned char *out,const char *s)
{ size_t n=strlen(s);assert(!(n&1));for (size_t i=0;i<n/2;++i) { unsigned v;assert(sscanf(s+2*i,"%2x",&v)==1);out[i]=(unsigned char)v; }return n/2; }
static void erased(const unsigned char *p) { for (unsigned i=0;i<64;++i) { assert(!p[i]); } }
static FILE *vectors(const char *directory,const char *name)
{ char path[512];assert(snprintf(path,sizeof(path),"%s/%s",directory,name)>0);FILE *f=fopen(path,"r");assert(f);return f; }
static bool random_public(void *ctx,unsigned char out[32]) { unsigned *n=ctx;memset(out,0,32);out[31]=(unsigned char)(*n)++;return true; }
int main(int argc,char **argv)
{
    assert(argc==2);unsigned char password[129],a[6],b[6],out[65],want[64],point[64],x[32];
    char ph[257],ah[13],bh[13],qh[129],xh[65];unsigned parity,count=0,first;
    FILE *f=vectors(argv[1],"hunt-root-vectors.txt");
    while (fscanf(f,"%64s %u %128s",xh,&parity,qh)==3) {
        assert(unhex(x,xh)==32);memset(out,0xa5,sizeof(out));bool ok=p256_from_x(x,parity,out);
        if (!strcmp(qh,"-")) { assert(!ok);erased(out); }else { assert(unhex(want,qh)==64 && ok && !memcmp(out,want,64) && p256_point_valid(out)); }
        assert(out[64]==0xa5);++count;
    }assert(feof(f) && count==62);fclose(f);
    assert(!p256_from_x(x,2,out));erased(out);assert(!p256_from_x(NULL,0,out));erased(out);
    f=vectors(argv[1],"hunt-public-vectors.txt");count=0;
    while (fscanf(f,"%256s %12s %12s %128s %u",ph,ah,bh,qh,&first)==5) {
        size_t n=unhex(password,ph);assert(unhex(a,ah)==6 && unhex(b,bh)==6 && unhex(want,qh)==64 && first>=1 && first<=40);
        memset(out,0xa5,sizeof(out));assert(sae_hunt_pwe(password,n,a,b,out) && !memcmp(out,want,64) && out[64]==0xa5);
        assert(sae_hunt_pwe(password,n,b,a,point) && !memcmp(out,point,64));
        struct sae_session sta={0},ap={0};unsigned ar=3,br=11;unsigned char sc[98],ac[98],sf[34],af[34];struct sae_keys sk,ak;
        assert(sae_session_start(&sta,point,random_public,&ar)==SAE_OK && sae_session_start(&ap,point,random_public,&br)==SAE_OK);
        assert(sae_session_commit(&sta,sc)==SAE_OK && sae_session_commit(&ap,ac)==SAE_OK);
        assert(sae_session_peer(&sta,ac,98)==SAE_OK && sae_session_peer(&ap,sc,98)==SAE_OK);
        assert(sae_session_confirm(&sta,sf)==SAE_OK && sae_session_confirm(&ap,af)==SAE_OK);
        assert(sae_session_verify(&sta,af,34)==SAE_OK && sae_session_verify(&ap,sf,34)==SAE_OK);
        assert(sae_session_take_keys(&sta,&sk)==SAE_OK && sae_session_take_keys(&ap,&ak)==SAE_OK && !memcmp(&sk,&ak,sizeof(sk)));++count;
    }assert(feof(f) && count==24);fclose(f);
    assert(!sae_hunt_pwe(NULL,1,a,b,out));erased(out);assert(!sae_hunt_pwe(password,0,a,b,out));erased(out);
    assert(!sae_hunt_pwe(password,129,a,b,out));erased(out);assert(!sae_hunt_pwe(password,1,a,a,out));erased(out);
    a[0]|=1;assert(!sae_hunt_pwe(password,1,a,b,out));erased(out);a[0]&=0xfe;
    memset(b,0,6);assert(!sae_hunt_pwe(password,1,a,b,out));erased(out);b[0]=2;b[5]=2;
    memcpy(out,"public",6);assert(!sae_hunt_pwe(out,6,a,b,out));erased(out);
    memcpy(out,a,6);assert(!sae_hunt_pwe(password,1,out,b,out));erased(out);
    assert(!sae_hunt_pwe(password,1,a,b,NULL));
    puts("PASS: 62 root vectors, 24 independent/OpenSSL H&P PWEs and full matching SAE exchanges, MAC-order symmetry, binary/max-length password, invalid/overlap guards and output erasure");return 0;
}
