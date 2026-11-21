#include <stdio.h>
#include <string.h>
#include "host_assert.h"
#include "user_heap.h"
enum { POOL=32,PAGE=4096 };
static struct user_heap heap;
static struct user_mapping maps[USER_MAPPING_LIMIT];static unsigned count;
static struct user_tables tables,stage;
static uint64_t root[512];static bool used[POOL],fail_publish;
static unsigned char storage[POOL][PAGE];
static unsigned allocations,releases,publishes,fail_alloc,fail_release;
static unsigned slot(uint64_t physical) { assert(physical>=0x50000000 && !(physical&4095));unsigned i=(unsigned)((physical-0x50000000)/PAGE);assert(i<POOL);return i; }
static bool allocate(void *ctx,uint64_t *physical)
{
    (void)ctx;if (++allocations==fail_alloc) { return false; }
    for (unsigned i=0;i<POOL;++i) { if (!used[i]) { used[i]=true;memset(storage[i],0,PAGE);*physical=0x50000000+(uint64_t)i*PAGE;return true; } }return false;
}
static bool release(void *ctx,uint64_t physical)
{
    (void)ctx;unsigned i=slot(physical);assert(used[i]);if (++releases==fail_release) { return false; }
    memset(storage[i],0,PAGE);used[i]=false;return true;
}
static bool publish(void *ctx,const struct user_mapping *next,unsigned n)
{
    (void)ctx;++publishes;if (fail_publish) { return false; }
    assert(user_mmu_build(&stage,0x100000,root,next,n)==USER_MMU_OK);tables=stage;return true;
}
static const struct user_heap_ops ops={allocate,release,publish,NULL};
static void seed(void)
{
    memset(&heap,0,sizeof(heap));memset(maps,0,sizeof(maps));memset(root,0,sizeof(root));memset(used,0,sizeof(used));memset(storage,0xcc,sizeof(storage));
    allocations=releases=publishes=fail_alloc=fail_release=0;fail_publish=false;count=2;
    maps[0]=(struct user_mapping){USER_VA_BASE,0x40000000,USER_PAGE_RX};maps[1]=(struct user_mapping){USER_STACK_BASE,0x41000000,USER_PAGE_RW};
    assert(user_mmu_build(&tables,0x100000,root,maps,count)==USER_MMU_OK);
}
static unsigned active(void) { unsigned n=0;for (unsigned i=0;i<POOL;++i) { n+=used[i]; }return n; }
static unsigned char *at(uint64_t va)
{ struct user_translation t;assert(user_mmu_lookup(&tables,va,&t) && t.kind==USER_PAGE_RW);return storage[slot(t.physical&~4095ULL)]+(t.physical&4095); }
static void filled(uint64_t va,unsigned pages,unsigned char value)
{ for (unsigned i=0;i<pages;++i) { const unsigned char *p=at(va+(uint64_t)i*PAGE);for (unsigned j=0;j<PAGE;++j) { assert(p[j]==value); } } }
static void fill(uint64_t va,unsigned pages,unsigned char value)
{ for (unsigned i=0;i<pages;++i) { memset(at(va+(uint64_t)i*PAGE),value,PAGE); } }
static void basics(void)
{
    seed();uint64_t a=999,b,c;
    assert(user_heap_allocate(&heap,maps,&count,0,&ops,&a)==USER_HEAP_ARGUMENT && a==999 && !allocations);
    assert(user_heap_allocate(&heap,maps,&count,17,&ops,&a)==USER_HEAP_ARGUMENT && !allocations);
    assert(user_heap_allocate(&heap,maps,&count,3,&ops,&a)==USER_HEAP_OK && a==USER_HEAP_BASE);filled(a,3,0);fill(a,3,0x5a);
    assert(user_heap_allocate(&heap,maps,&count,13,&ops,&b)==USER_HEAP_OK && b==a+3*PAGE && heap.live==16 && heap.peak==16);filled(b,13,0);
    unsigned before=allocations;assert(user_heap_allocate(&heap,maps,&count,1,&ops,&c)==USER_HEAP_QUOTA && allocations==before);filled(a,3,0x5a);
    assert(user_heap_largest_free_run(&heap)==0);
    struct user_heap saved=heap;struct user_tables original=tables;unsigned n=count;
    const uint64_t bad[]={0,USER_VA_BASE,USER_STACK_BASE,USER_HEAP_BASE-4096,USER_HEAP_BASE+16*PAGE,USER_HEAP_BASE+1,USER_HEAP_BASE+PAGE,UINT64_MAX};
    for (unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);++i) { assert(user_heap_free(&heap,maps,&count,bad[i],&ops)==USER_HEAP_ARGUMENT && count==n && !memcmp(&heap,&saved,sizeof(heap)) && !memcmp(&tables,&original,sizeof(tables))); }
    assert(user_heap_free(&heap,maps,&count,a,&ops)==USER_HEAP_OK);struct user_translation t;assert(!user_mmu_lookup(&tables,a,&t));
    assert(user_heap_free(&heap,maps,&count,a,&ops)==USER_HEAP_ARGUMENT);
    assert(user_heap_allocate(&heap,maps,&count,3,&ops,&c)==USER_HEAP_OK && c==a);filled(c,3,0);
    assert(user_heap_free(&heap,maps,&count,b,&ops)==USER_HEAP_OK && user_heap_free(&heap,maps,&count,c,&ops)==USER_HEAP_OK && !active() && count==2 && heap.live==0);
    uint64_t all[16];for (unsigned i=0;i<16;++i) { assert(user_heap_allocate(&heap,maps,&count,1,&ops,&all[i])==USER_HEAP_OK); }
    for (unsigned i=0;i<16;i+=2) { assert(user_heap_free(&heap,maps,&count,all[i],&ops)==USER_HEAP_OK); }
    assert(user_heap_allocate(&heap,maps,&count,2,&ops,&a)==USER_HEAP_FRAGMENTED && heap.live==8);
    assert(user_heap_largest_free_run(&heap)==1);
    for (unsigned i=1;i<16;i+=2) { assert(user_heap_free(&heap,maps,&count,all[i],&ops)==USER_HEAP_OK); }
    assert(!active() && !heap.live && count==2 && user_heap_largest_free_run(&heap)==USER_HEAP_PAGES);
    puts("PASS: page quota, zeroing, retained data, exact-head/interior/double/foreign-range free refusal, reuse, fragmentation and complete coalescing.");
}
static void failures(void)
{
    for (unsigned pages=1;pages<=16;++pages) { for (unsigned point=1;point<=pages;++point) {
        seed();struct user_tables old=tables;struct user_heap saved=heap;uint64_t out=123;fail_alloc=point;
        assert(user_heap_allocate(&heap,maps,&count,pages,&ops,&out)==USER_HEAP_NO_MEMORY && out==123 && count==2 && !active() && !memcmp(&heap,&saved,sizeof(heap)) && !memcmp(&tables,&old,sizeof(tables)));
    } }
    for (unsigned pages=1;pages<=16;++pages) {
        seed();uint64_t address=123;struct user_tables old=tables;struct user_heap saved=heap;fail_publish=true;
        assert(user_heap_allocate(&heap,maps,&count,pages,&ops,&address)==USER_HEAP_MAP_FAILED && address==123 && !active() && count==2 && !memcmp(&heap,&saved,sizeof(heap)) && !memcmp(&tables,&old,sizeof(tables)));
        fail_publish=false;assert(user_heap_allocate(&heap,maps,&count,pages,&ops,&address)==USER_HEAP_OK);fill(address,pages,0xa5);old=tables;saved=heap;fail_publish=true;
        assert(user_heap_free(&heap,maps,&count,address,&ops)==USER_HEAP_MAP_FAILED && !memcmp(&heap,&saved,sizeof(heap)) && !memcmp(&tables,&old,sizeof(tables)) && active()==pages);filled(address,pages,0xa5);
    }
    seed();uint64_t address;assert(user_heap_allocate(&heap,maps,&count,4,&ops,&address)==USER_HEAP_OK);fail_release=2;
    assert(user_heap_free(&heap,maps,&count,address,&ops)==USER_HEAP_CORRUPT && heap.failed && active()==1 && count==3 && heap.live==0);
    assert(user_heap_allocate(&heap,maps,&count,1,&ops,&address)==USER_HEAP_CORRUPT);
    assert(release(NULL,maps[2].physical) && !active());
    seed();fail_alloc=4;fail_release=2;
    assert(user_heap_allocate(&heap,maps,&count,4,&ops,&address)==USER_HEAP_CORRUPT && heap.failed && active()==1 && count==3);
    assert(release(NULL,maps[2].physical) && !active());
    puts("PASS: every one of 136 partial allocation failures and 32 mapping publication failures preserves old state/resources; internal release failures retain unreleased pages for cleanup and refuse reuse.");
}
static void retained_failures(void)
{
    for (unsigned point=1;point<=12;++point) {
        seed();uint64_t address,extra=999;assert(user_heap_allocate(&heap,maps,&count,4,&ops,&address)==USER_HEAP_OK);fill(address,4,0x37);
        struct user_heap saved=heap;struct user_tables old=tables;unsigned n=count;fail_alloc=allocations+point;
        assert(user_heap_allocate(&heap,maps,&count,12,&ops,&extra)==USER_HEAP_NO_MEMORY && extra==999 && count==n && active()==4 &&
            !memcmp(&heap,&saved,sizeof(heap)) && !memcmp(&tables,&old,sizeof(tables)));filled(address,4,0x37);
    }
    puts("PASS: every partial 12-page expansion failure retains an existing four-page allocation, contents, tables and counters.");
}
static uint32_t random_state=0x4ca93152;
static unsigned random_next(void) { random_state=random_state*1664525+1013904223;return random_state>>8; }
static void model(void)
{
    struct { uint64_t base;unsigned pages;unsigned char byte; } owners[16]={{0}};seed();
    for (unsigned step=0;step<3000;++step) {
        unsigned chosen=random_next()%16;
        if (owners[chosen].pages && (random_next()&1)) {
            assert(user_heap_free(&heap,maps,&count,owners[chosen].base,&ops)==USER_HEAP_OK);owners[chosen].pages=0;
        } else if (!owners[chosen].pages) {
            bool occupied[16]={0};for (unsigned i=0;i<16;++i) { if (owners[i].pages) {
                unsigned start=(unsigned)((owners[i].base-USER_HEAP_BASE)/PAGE);for (unsigned j=0;j<owners[i].pages;++j) { assert(!occupied[start+j]);occupied[start+j]=true; }
            } }
            unsigned pages=1+random_next()%4,run=0;bool fits=false;
            for (unsigned i=0;i<16;++i) { run=occupied[i] ? 0 : run+1;if (run>=pages) { fits=true; } }
            uint64_t address=999;enum user_heap_result r=user_heap_allocate(&heap,maps,&count,pages,&ops,&address);
            if (!fits) {
                enum user_heap_result expected=pages>USER_HEAP_PAGES-heap.live ? USER_HEAP_QUOTA : USER_HEAP_FRAGMENTED;
                assert(r==expected && address==999);
            }
            else {
                assert(r==USER_HEAP_OK && address>=USER_HEAP_BASE && address+pages*PAGE<=USER_HEAP_BASE+16*PAGE);
                unsigned start=(unsigned)((address-USER_HEAP_BASE)/PAGE);for (unsigned j=0;j<pages;++j) { assert(!occupied[start+j]); }
                filled(address,pages,0);owners[chosen].base=address;owners[chosen].pages=pages;owners[chosen].byte=(unsigned char)(1+step%254);fill(address,pages,owners[chosen].byte);
            }
        }
        unsigned total=0;for (unsigned i=0;i<16;++i) { if (owners[i].pages) { total+=owners[i].pages;filled(owners[i].base,owners[i].pages,owners[i].byte); } }
        assert(heap.live==total && active()==total && count==2+total);
    }
    for (unsigned i=0;i<16;++i) { if (owners[i].pages) { assert(user_heap_free(&heap,maps,&count,owners[i].base,&ops)==USER_HEAP_OK); } }
    assert(!active() && !heap.live && count==2);
    puts("PASS: 3000 allocation/free operations against an independent occupancy model; every live byte, mapping count, zeroed new page and final release checked.");
}
static void resize_run(void)
{
    seed();
    uint64_t base,neighbor;
    assert(user_heap_allocate(&heap,maps,&count,10,&ops,&base)==USER_HEAP_OK);
    fill(base,10,0x7c);
    assert(user_heap_allocate(&heap,maps,&count,1,&ops,&neighbor)==USER_HEAP_OK);
    unsigned before=publishes;
    assert(user_heap_resize(&heap,maps,&count,base,10,&ops)==USER_HEAP_OK && publishes==before);
    assert(user_heap_resize(&heap,maps,&count,base,16,&ops)==USER_HEAP_QUOTA && publishes==before);
    assert(user_heap_resize(&heap,maps,&count,base,15,&ops)==USER_HEAP_FRAGMENTED && publishes==before);
    assert(user_heap_resize(&heap,maps,&count,base+PAGE,2,&ops)==USER_HEAP_ARGUMENT);
    filled(base,10,0x7c);
    assert(user_heap_free(&heap,maps,&count,neighbor,&ops)==USER_HEAP_OK);

    struct user_heap saved=heap;
    struct user_tables old=tables;
    for (unsigned point=1;point<=5;++point) {
        fail_alloc=allocations+point;
        assert(user_heap_resize(&heap,maps,&count,base,15,&ops)==USER_HEAP_NO_MEMORY);
        assert(count==12 && active()==10 && !memcmp(&heap,&saved,sizeof(heap)) && !memcmp(&tables,&old,sizeof(tables)));
        filled(base,10,0x7c);
    }
    fail_alloc=0;
    fail_publish=true;
    assert(user_heap_resize(&heap,maps,&count,base,15,&ops)==USER_HEAP_MAP_FAILED && active()==10);
    assert(user_heap_resize(&heap,maps,&count,base,2,&ops)==USER_HEAP_MAP_FAILED && active()==10);
    assert(!memcmp(&heap,&saved,sizeof(heap)) && !memcmp(&tables,&old,sizeof(tables)));
    fail_publish=false;

    assert(user_heap_resize(&heap,maps,&count,base,15,&ops)==USER_HEAP_OK);
    filled(base,10,0x7c);
    filled(base+10*PAGE,5,0);
    assert(user_heap_resize(&heap,maps,&count,base,2,&ops)==USER_HEAP_OK);
    struct user_translation unused;
    assert(!user_mmu_lookup(&tables,base+2*PAGE,&unused) && active()==2);
    assert(heap.live==2 && heap.peak==15 && heap.allocations==2 && heap.free_calls==1);
    filled(base,2,0x7c);
    assert(user_heap_allocate(&heap,maps,&count,14,&ops,&neighbor)==USER_HEAP_OK && neighbor==base+2*PAGE);
    filled(neighbor,14,0);
    assert(user_heap_free(&heap,maps,&count,neighbor,&ops)==USER_HEAP_OK);
    assert(user_heap_free(&heap,maps,&count,base,&ops)==USER_HEAP_OK && !active());
    puts("PASS: in-place growth/shrink, adjacent-run refusal, zeroed page reuse and rollback for allocation or table publication failures.");
}
int main(void) { basics();failures();retained_failures();resize_run();model();return 0; }
