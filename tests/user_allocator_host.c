#include <stdio.h>
#include <string.h>
#include "host_assert.h"
#include "allocator.h"
static _Alignas(4096) unsigned char pool[16][4096];
static unsigned char runs[16];static unsigned allocations,releases,fail_allocate,fail_release_count;
static void *allocate(void *context,unsigned pages)
{
    (void)context;if (++allocations==fail_allocate) { return NULL; }unsigned first=0,span=0;
    for (unsigned i=0;i<16;++i) { if (runs[i]) { span=0; }else { if (!span) { first=i; }if (++span==pages) { break; } } }
    if (span!=pages) { return NULL; }runs[first]=(unsigned char)pages;for (unsigned i=1;i<pages;++i) { runs[first+i]=255; }
    memset(pool[first],0xcc,pages*4096);return pool[first];
}
static bool release(void *context,void *base)
{
    (void)context;++releases;if (fail_release_count) { --fail_release_count;return false; }
    uintptr_t at=(uintptr_t)base-(uintptr_t)pool;assert(at<sizeof(pool) && !(at&4095));unsigned first=(unsigned)(at/4096),pages=runs[first];
    assert(pages && pages!=255 && first+pages<=16);memset(base,0,pages*4096);for (unsigned i=0;i<pages;++i) { runs[first+i]=0; }return true;
}
static const struct uos_allocator_ops ops={allocate,release,NULL};
static unsigned active_pages(void) { unsigned n=0;for (unsigned i=0;i<16;++i) { n+=runs[i]!=0; }return n; }
static void reset(void) { memset(runs,0,sizeof(runs));memset(pool,0xdd,sizeof(pool));allocations=releases=fail_allocate=fail_release_count=0; }
static bool zeroed(const void *p,size_t n) { const unsigned char *b=p;for (size_t i=0;i<n;++i) { if (b[i]) { return false; } }return true; }
static void content(const void *p,size_t n,unsigned char byte) { const unsigned char *b=p;for (size_t i=0;i<n;++i) { assert(b[i]==byte); } }
static void basics(void)
{
    reset();struct uos_allocator a={0},other={0};assert(uos_allocator_setup(&a,&ops) && uos_allocator_setup(&other,&ops));
    unsigned char *p=uos_malloc(&a,17),*q=uos_calloc(&a,3,11);assert(p && q && !((uintptr_t)p&15) && !((uintptr_t)q&15) && zeroed(p,17) && zeroed(q,33));
    memset(p,0x5a,17);memset(q,0x39,33);assert(!uos_free(&a,p+1) && !uos_free(&other,p) && !uos_free(&a,&a) && uos_free(&a,NULL));content(p,17,0x5a);content(q,33,0x39);
    struct uos_allocator_stats before=uos_allocator_stats(&a);assert(!uos_malloc(&a,0) && !uos_malloc(&a,65537) && !uos_calloc(&a,SIZE_MAX,2));
    assert(uos_allocator_stats(&a).requested_bytes==before.requested_bytes && !uos_allocator_setup(&a,&ops));
    unsigned char *same=uos_realloc(&a,p,31);assert(same==p);content(p,17,0x5a);assert(zeroed(p+17,14));
    memset(p,0x27,31);assert(uos_realloc(&a,p,2)==p && uos_realloc(&a,p,30)==p);content(p,2,0x27);assert(zeroed(p+2,28));
    unsigned char *larger=uos_realloc(&a,p,5000);assert(larger && larger!=p && zeroed(larger+30,4970));content(larger,2,0x27);
    assert(!uos_free(&a,p) && uos_free(&a,larger) && uos_free(&a,q) && !active_pages());
    void *small[64];for (unsigned i=0;i<64;++i) { small[i]=uos_malloc(&a,1);assert(small[i] && zeroed(small[i],1));*(unsigned char *)small[i]=(unsigned char)i; }
    assert(active_pages()==1 && !uos_malloc(&a,1));
    assert(!uos_realloc(&a,small[0],33) && *(unsigned char *)small[0]==0);
    for (unsigned i=0;i<64;++i) { assert(*(unsigned char *)small[i]==(unsigned char)i && uos_free(&a,small[i])); }
    assert(!active_pages() && !uos_free(&a,small[0]));
    void *all=uos_malloc(&a,65536);assert(all && zeroed(all,65536) && active_pages()==16 && !uos_malloc(&a,1));
    assert(uos_realloc(&a,all,0)==NULL && !active_pages());
    assert(uos_allocator_destroy(&a) && uos_allocator_destroy(&a) && uos_allocator_destroy(&other) && !active_pages());
    puts("PASS: zeroed/aligned malloc/calloc, packed small objects, exact-pointer/cross-context/double free refusal, overflow/limits, in-place/moving resize, zeroed growth and page reclamation.");
}
static void failures(void)
{
    reset();struct uos_allocator a={0};assert(uos_allocator_setup(&a,&ops));
    unsigned char *p=uos_malloc(&a,9000);assert(p);memset(p,0x68,9000);unsigned held=active_pages();
    fail_allocate=allocations+1;assert(!uos_realloc(&a,p,20000) && active_pages()==held);content(p,9000,0x68);fail_allocate=0;
    fail_release_count=1;assert(!uos_free(&a,p) && active_pages()==held);content(p,9000,0x68);
    fail_release_count=1;assert(!uos_realloc(&a,p,18000) && active_pages()==held && uos_allocator_stats(&a).live_blocks==1 && !a.poisoned);content(p,9000,0x68);
    fail_release_count=2;assert(!uos_realloc(&a,p,18000) && a.poisoned && uos_allocator_stats(&a).live_blocks==2 && !uos_malloc(&a,16));content(p,9000,0x68);
    fail_release_count=1;assert(!uos_allocator_destroy(&a) && a.poisoned && active_pages()==3);
    assert(uos_allocator_destroy(&a) && !active_pages());
    assert(uos_allocator_setup(&a,&ops));p=uos_malloc(&a,40000);assert(p);memset(p,0x51,40000);
    assert(!uos_realloc(&a,p,60000));content(p,40000,0x51);assert(uos_allocator_destroy(&a) && !active_pages());
    puts("PASS: allocation/quota failure and failed release preserve the old object; realloc rollback frees candidates, failed rollback stays tracked/poisoned, and destroy retry releases retained runs.");
}
static uint32_t random_state=0xf81ca357;
static unsigned random_next(void) { random_state=random_state*1664525+1013904223;return random_state>>8; }
static void model(void)
{
    reset();struct uos_allocator a={0};assert(uos_allocator_setup(&a,&ops));
    struct { unsigned char *p;size_t bytes;unsigned char byte; } live[48]={{0}};
    for (unsigned step=0;step<6000;++step) {
        unsigned id=random_next()%48,action=random_next()%4;
        if (!live[id].p) {
            size_t bytes=1+random_next()%5000;unsigned char *p=uos_malloc(&a,bytes);
            if (p) { assert(!((uintptr_t)p&15) && zeroed(p,bytes));live[id].p=p;live[id].bytes=bytes;live[id].byte=(unsigned char)(1+step%254);memset(p,live[id].byte,bytes); }
        }else if (action==0) { assert(uos_free(&a,live[id].p));live[id].p=NULL; }
        else if (action==1) {
            size_t bytes=1+random_next()%7000;unsigned char *p=uos_realloc(&a,live[id].p,bytes);
            if (p) {
                size_t preserved=bytes<live[id].bytes ? bytes : live[id].bytes;content(p,preserved,live[id].byte);
                if (bytes>preserved) { assert(zeroed(p+preserved,bytes-preserved)); }live[id].p=p;live[id].bytes=bytes;memset(p,live[id].byte,bytes);
            }
        }
        size_t objects=0,bytes=0;for (unsigned i=0;i<48;++i) { if (live[i].p) {
            ++objects;bytes+=live[i].bytes;content(live[i].p,live[i].bytes,live[i].byte);
            for (unsigned j=0;j<i;++j) { if (live[j].p) { assert((uintptr_t)live[i].p+live[i].bytes<=(uintptr_t)live[j].p || (uintptr_t)live[j].p+live[j].bytes<=(uintptr_t)live[i].p); } }
        } }
        struct uos_allocator_stats s=uos_allocator_stats(&a);assert(!s.poisoned && s.live_blocks==objects && s.requested_bytes==bytes && s.pages==active_pages() && s.pages<=16);
    }
    for (unsigned i=0;i<48;++i) { if (live[i].p) { assert(uos_free(&a,live[i].p)); } }
    assert(!active_pages() && uos_allocator_destroy(&a));puts("PASS: 6000 independent malloc/free/realloc model operations; every live byte, non-overlap, requested-byte count, zeroed growth and final page baseline checked.");
}
int main(void) { basics();failures();model();return 0; }
