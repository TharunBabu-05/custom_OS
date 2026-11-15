#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "host_assert.h"
#include "app_profile.h"
static unsigned char data[APP_PROFILE_BYTES],encoded[APP_PROFILE_BYTES];
static void behavior(void)
{
    struct app_environment env,decoded,old;app_env_init(&env);
    assert(app_env_set(&env,"EMPTY","")==APP_ENV_OK && app_env_set(&env,"LITERAL","$PATH = 'hello world'")==APP_ENV_OK);
    size_t n=999;assert(app_profile_encode(&env,data,sizeof(data),&n) && n>10 && !memcmp(data,"OUROSENV1\n",10));
    memset(&decoded,0xa5,sizeof(decoded));assert(app_profile_decode(data,n,&decoded) && !memcmp(&decoded,&env,sizeof(env)));
    size_t crlf=0;for (size_t i=0;i<n;++i) { if (data[i]=='\n') { encoded[crlf++]='\r'; }encoded[crlf++]=data[i]; }
    assert(app_profile_decode(encoded,crlf,&decoded) && !memcmp(&decoded,&env,sizeof(env)));
    struct app_environment maximum={0};char key[32],value[128];memset(value,'v',127);value[127]=0;
    for (unsigned i=0;i<8;++i) { memset(key,'N',31);key[0]=(char)('A'+i);key[31]=0;assert(app_env_set(&maximum,key,value)==APP_ENV_OK); }
    assert(app_profile_encode(&maximum,data,sizeof(data),&n) && n==1290 && app_profile_decode(data,n,&decoded) && !memcmp(&decoded,&maximum,sizeof(decoded)));
    memset(encoded,0xa5,sizeof(encoded));unsigned char before[APP_PROFILE_BYTES];memcpy(before,encoded,sizeof(encoded));size_t size=123;
    assert(!app_profile_encode(&maximum,encoded,n-1,&size) && size==123 && !memcmp(encoded,before,sizeof(encoded)));
    maximum.entries[7]=maximum.entries[0];assert(!app_profile_encode(&maximum,encoded,sizeof(encoded),&size) && size==123 && !memcmp(encoded,before,sizeof(encoded)));
    const char *bad[]={"","OUROSENV1","OUROSENV2\n","OUROSENV1\n\n","OUROSENV1\nA=ok\nA=duplicate\n",
        "OUROSENV1\nA=ok\nBAD-NAME=v\n","OUROSENV1\nPATH=/boot\nNOEQUALS\n","OUROSENV1\nPATH=/boot",
        "OUROSENV1\nA=bad\033value\n","OUROSENV1\n=empty-name\n","OUROSENV1\nA=1\nB=2\nC=3\nD=4\nE=5\nF=6\nG=7\nH=8\nI=9\n"};
    old=env;
    for (unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);++i) { decoded=old;assert(!app_profile_decode(bad[i],strlen(bad[i]),&decoded) && !memcmp(&decoded,&old,sizeof(old))); }
    assert(app_profile_decode("OUROSENV1\n",10,&decoded) && decoded.count==0);
    assert(app_profile_encode(&decoded,data,sizeof(data),&n) && n==10);
    puts("PASS: profile LF/CRLF roundtrip, literal/empty values, empty/full environments, exact 1290-byte maximum, duplicate/control/truncated/version/capacity refusal and unchanged failed output.");
}
static void mutations(void)
{
    struct app_environment original,before,out;app_env_init(&original);before=original;
    size_t bytes=0;assert(app_profile_encode(&original,data,sizeof(data),&bytes));uint32_t seed=0x439101ab;unsigned accepted=0;
    for (unsigned i=0;i<15000;++i) {
        unsigned char copy[APP_PROFILE_BYTES];memcpy(copy,data,bytes);seed=seed*1664525+1013904223;
        size_t at=seed%bytes;copy[at]^=(unsigned char)(1+(seed>>24)%255);out=before;
        size_t used=i%3 ? bytes : seed%(bytes+1);
        if (app_profile_decode(copy,used,&out)) {
            ++accepted;size_t n;assert(out.count<=8 && app_profile_encode(&out,encoded,sizeof(encoded),&n));
            struct app_environment again;assert(app_profile_decode(encoded,n,&again) && !memcmp(&out,&again,sizeof(out)));
        }else { assert(!memcmp(&out,&before,sizeof(out))); }
    }
    printf("PASS: 15000 mutated/truncated profiles; %u valid alternatives roundtrip canonically, every refusal preserves the old environment.\n",accepted);
}
int main(void) { behavior();mutations();return 0; }
