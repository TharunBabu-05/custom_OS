#include "user_heap.h"
#define PAGE 4096ULL
#define INTERIOR 255
static bool heap_page(uint64_t va)
{ return va>=USER_HEAP_BASE && va-USER_HEAP_BASE<USER_HEAP_PAGES*PAGE && !(va&(PAGE-1)); }
static bool valid(const struct user_heap *h,const struct user_mapping *maps,unsigned count)
{
    if (h->failed || count>USER_MAPPING_LIMIT || h->live>USER_HEAP_PAGES || h->peak<h->live || h->peak>USER_HEAP_PAGES) { return false; }
    unsigned occupied=0;bool seen[USER_HEAP_PAGES]={0};
    for (unsigned i=0;i<USER_HEAP_PAGES;) {
        unsigned n=h->runs[i];if (!n) { ++i;continue; }
        if (n>USER_HEAP_PAGES-i) { return false; }
        for (unsigned j=1;j<n;++j) { if (h->runs[i+j]!=INTERIOR) { return false; } }
        occupied+=n;i+=n;
    }
    if (occupied!=h->live) { return false; }unsigned mapped=0;
    for (unsigned i=0;i<count;++i) {
        if (!maps[i].physical || (maps[i].physical&(PAGE-1))) { return false; }
        for (unsigned j=0;j<i;++j) {
            if (maps[i].physical==maps[j].physical || maps[i].virtual_address==maps[j].virtual_address) { return false; }
        }
        uint64_t va=maps[i].virtual_address;if (!heap_page(va)) { continue; }
        unsigned slot=(unsigned)((va-USER_HEAP_BASE)/PAGE);
        if (!h->runs[slot] || seen[slot] || maps[i].kind!=USER_PAGE_RW) { return false; }seen[slot]=true;++mapped;
    }
    return mapped==h->live;
}
static bool ops_valid(const struct user_heap_ops *ops)
{ return ops && ops->allocate && ops->release && ops->publish; }
static void append_maps(struct user_mapping *maps,const struct user_mapping *next,unsigned count)
{ for (unsigned i=0;i<count;++i) { maps[i]=next[i]; } }
static enum user_heap_result rollback(struct user_heap *h,struct user_mapping *maps,unsigned *count,
    const struct user_mapping *added,unsigned n,const struct user_heap_ops *ops,enum user_heap_result result)
{
    for (unsigned i=0;i<n;++i) {
        if (!ops->release(ops->context,added[i].physical)) { maps[(*count)++]=added[i];h->failed=true; }
    }
    return h->failed ? USER_HEAP_CORRUPT : result;
}
enum user_heap_result user_heap_allocate(struct user_heap *h,struct user_mapping *maps,unsigned *count,
    unsigned pages,const struct user_heap_ops *ops,uint64_t *address)
{
    if (!h || !maps || !count || !address || !ops_valid(ops) || !pages || pages>USER_HEAP_PAGES) { return USER_HEAP_ARGUMENT; }
    if (!valid(h,maps,*count)) { h->failed=true;return USER_HEAP_CORRUPT; }
    if (h->allocations==UINT64_MAX) { return USER_HEAP_ARGUMENT; }
    unsigned first=0,span=0;
    for (unsigned i=0;i<USER_HEAP_PAGES;++i) {
        if (h->runs[i]) { span=0; }else { if (!span) { first=i; }if (++span==pages) { break; } }
    }
    if (span!=pages || *count>USER_MAPPING_LIMIT-pages) { return USER_HEAP_NO_SPACE; }
    struct user_mapping next[USER_MAPPING_LIMIT];append_maps(next,maps,*count);unsigned made=0;
    for (;made<pages;++made) {
        uint64_t physical=0;
        if (!ops->allocate(ops->context,&physical)) { return rollback(h,maps,count,next+*count,made,ops,USER_HEAP_NO_MEMORY); }
        next[*count+made]=(struct user_mapping){USER_HEAP_BASE+(first+made)*PAGE,physical,USER_PAGE_RW};
    }
    if (!ops->publish(ops->context,next,*count+pages)) { return rollback(h,maps,count,next+*count,pages,ops,USER_HEAP_MAP_FAILED); }
    append_maps(maps,next,*count+pages);*count+=pages;h->runs[first]=(unsigned char)pages;
    for (unsigned i=1;i<pages;++i) { h->runs[first+i]=INTERIOR; }
    h->live+=pages;if (h->live>h->peak) { h->peak=h->live; }++h->allocations;
    *address=USER_HEAP_BASE+first*PAGE;return USER_HEAP_OK;
}
enum user_heap_result user_heap_free(struct user_heap *h,struct user_mapping *maps,unsigned *count,
    uint64_t address,const struct user_heap_ops *ops)
{
    if (!h || !maps || !count || !ops_valid(ops) || !heap_page(address)) { return USER_HEAP_ARGUMENT; }
    if (!valid(h,maps,*count)) { h->failed=true;return USER_HEAP_CORRUPT; }
    unsigned first=(unsigned)((address-USER_HEAP_BASE)/PAGE),pages=h->runs[first];
    if (!pages || pages==INTERIOR || h->free_calls==UINT64_MAX) { return USER_HEAP_ARGUMENT; }
    struct user_mapping next[USER_MAPPING_LIMIT],removed[USER_HEAP_PAGES];unsigned kept=0,n=0;
    for (unsigned i=0;i<*count;++i) {
        uint64_t va=maps[i].virtual_address;
        if (va>=address && va-address<pages*PAGE) { removed[n++]=maps[i]; }else { next[kept++]=maps[i]; }
    }
    if (n!=pages) { h->failed=true;return USER_HEAP_CORRUPT; }
    if (!ops->publish(ops->context,next,kept)) { return USER_HEAP_MAP_FAILED; }
    append_maps(maps,next,kept);*count=kept;h->live-=pages;++h->free_calls;
    for (unsigned i=0;i<pages;++i) { h->runs[first+i]=0; }
    for (unsigned i=0;i<n;++i) {
        if (!ops->release(ops->context,removed[i].physical)) { maps[(*count)++]=removed[i];h->failed=true; }
    }
    return h->failed ? USER_HEAP_CORRUPT : USER_HEAP_OK;
}
