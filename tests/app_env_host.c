#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "host_assert.h"
#include "app_env.h"
static void get(const struct app_environment *env,const char *key,const char *expected)
{
    char out[128];size_t n=999;memset(out,0xa5,sizeof(out));
    assert(app_env_get(env,key,out,sizeof(out),&n)==APP_ENV_OK && n==strlen(expected) && !strcmp(out,expected));
    for (size_t i=n+1;i<sizeof(out);++i) { assert((unsigned char)out[i]==0xa5); }
}
static void bounds(void)
{
    struct app_environment env,before;app_env_init(&env);assert(env.count==3);
    get(&env,"PATH","/data/bin:/boot");get(&env,"HOME","/home/user");get(&env,"TERM","serial");
    assert(app_env_set(&env,"empty","")==APP_ENV_OK && app_env_set(&env,"Case_2","spaces = : and 'quotes'")==APP_ENV_OK);
    get(&env,"empty","");get(&env,"Case_2","spaces = : and 'quotes'");before=env;
    const char *invalid[]={"","2KEY","has space","bad-key","KEY=VALUE","\n","bad\x7f"};
    for (unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);++i) {
        assert(app_env_set(&env,invalid[i],"value")==APP_ENV_INVALID && app_env_unset(&env,invalid[i])==APP_ENV_INVALID && !memcmp(&env,&before,sizeof(env)));
    }
    char name[33],value[129];memset(name,'N',32);name[32]=0;memset(value,'V',128);value[128]=0;
    assert(app_env_set(&env,name,"x")==APP_ENV_INVALID && app_env_set(&env,"PATH",value)==APP_ENV_INVALID);
    const char *controls[]={"new\nline","\t","\033[2J","\x7f","\x80"};
    for (unsigned i=0;i<sizeof(controls)/sizeof(controls[0]);++i) { assert(app_env_set(&env,"PATH",controls[i])==APP_ENV_INVALID && !memcmp(&env,&before,sizeof(env))); }
    name[31]=0;value[127]=0;assert(app_env_set(&env,name,value)==APP_ENV_OK);get(&env,name,value);
    char out[128],copy[128];memset(out,0x35,sizeof(out));memcpy(copy,out,sizeof(out));size_t bytes=321;
    assert(app_env_get(&env,name,out,127,&bytes)==APP_ENV_SMALL && bytes==321 && !memcmp(out,copy,sizeof(out)));
    assert(app_env_get(&env,"MISSING",out,sizeof(out),&bytes)==APP_ENV_MISSING && bytes==321 && !memcmp(out,copy,sizeof(out)));
    assert(app_env_get(&env,"case_2",out,sizeof(out),&bytes)==APP_ENV_MISSING);
    assert(app_env_set(&env,"ONE","1")==APP_ENV_OK && app_env_set(&env,"TWO","2")==APP_ENV_OK && env.count==8);before=env;
    assert(app_env_set(&env,"THREE","3")==APP_ENV_FULL && !memcmp(&env,&before,sizeof(env)));
    assert(app_env_set(&env,"PATH","/boot")==APP_ENV_OK);get(&env,"PATH","/boot");
    assert(app_env_unset(&env,"empty")==APP_ENV_OK && app_env_unset(&env,"empty")==APP_ENV_OK && env.count==7);
    const unsigned char *tail=(const unsigned char *)&env.entries[7];for (size_t i=0;i<sizeof(env.entries[7]);++i) { assert(!tail[i]); }
    assert(app_env_set(&env,"THREE","3")==APP_ENV_OK && env.count==8);get(&env,"THREE","3");
    struct app_environment child=env;assert(app_env_set(&child,"PATH","child")==APP_ENV_OK && app_env_unset(&child,"HOME")==APP_ENV_OK);
    get(&env,"PATH","/boot");get(&env,"HOME","/home/user");app_env_init(&child);assert(child.count==3);
    get(&child,"PATH","/data/bin:/boot");
    puts("PASS: environment defaults, printable/empty values, name/case/length boundaries, exact buffer bounds, full-table overwrite/reuse, zeroed removal, failed-operation preservation and private copies.");
}
static uint32_t state=0x6d293a71;
static unsigned random_next(void) { state=state*1664525+1013904223;return state; }
static void model(void)
{
    struct app_environment env={0};char expected[12][128]={{0}};int present[12]={0};unsigned count=0;
    for (unsigned step=0;step<20000;++step) {
        unsigned id=random_next()%12,operation=random_next()%3;char key[32],text[128];snprintf(key,sizeof(key),"VAR_%u",id);
        snprintf(text,sizeof(text),"value %u=%u",step,random_next());struct app_environment old=env;
        if (operation==0) {
            enum app_env_result r=app_env_set(&env,key,text);
            if (!present[id] && count==8) { assert(r==APP_ENV_FULL && !memcmp(&env,&old,sizeof(env))); }
            else { assert(r==APP_ENV_OK);if (!present[id]) { present[id]=1;++count; }strcpy(expected[id],text); }
        } else if (operation==1) {
            assert(app_env_unset(&env,key)==APP_ENV_OK);if (present[id]) { present[id]=0;--count; }
        }
        assert(env.count==count);
        for (unsigned i=0;i<12;++i) {
            snprintf(key,sizeof(key),"VAR_%u",i);char output[128];size_t n;
            enum app_env_result r=app_env_get(&env,key,output,sizeof(output),&n);
            assert(present[i] ? r==APP_ENV_OK && n==strlen(expected[i]) && !strcmp(output,expected[i]) : r==APP_ENV_MISSING);
        }
        for (unsigned i=0;i<env.count;++i) {
            unsigned index=99;assert(sscanf(env.entries[i].name,"VAR_%u",&index)==1 && index<12 && present[index] && !strcmp(env.entries[i].value,expected[index]));
            for (unsigned j=0;j<i;++j) { assert(strcmp(env.entries[i].name,env.entries[j].name)); }
        }
    }
    puts("PASS: 20000 deterministic set/unset/get operations agree with an independent 12-key model; capacity, every value and unique enumeration checked after each operation.");
}
int main(void) { bounds();model();return 0; }
