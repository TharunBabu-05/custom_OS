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
static bool fits_quota(const struct user_heap *h,unsigned mapped,unsigned added)
{ return added<=USER_HEAP_PAGES-h->live && mapped<=USER_MAPPING_LIMIT-added; }

unsigned user_heap_largest_free_run(const struct user_heap *h)
{
    unsigned largest=0,span=0;
    for (unsigned i=0;i<USER_HEAP_PAGES;++i) {
        span=h->runs[i] ? 0 : span+1;
        if (span>largest) { largest=span; }
    }
    return largest;
}
static enum user_heap_result append_pages(struct user_heap *h,struct user_mapping *maps,unsigned *count,
    unsigned first,unsigned pages,const struct user_heap_ops *ops)
{
    struct user_mapping next[USER_MAPPING_LIMIT];
    append_maps(next,maps,*count);
    unsigned made=0;
    for (;made<pages;++made) {
        uint64_t physical=0;
        if (!ops->allocate(ops->context,&physical)) {
            return rollback(h,maps,count,next+*count,made,ops,USER_HEAP_NO_MEMORY);
        }
        next[*count+made]=(struct user_mapping){USER_HEAP_BASE+(first+made)*PAGE,physical,USER_PAGE_RW};
    }
    if (!ops->publish(ops->context,next,*count+pages)) {
        return rollback(h,maps,count,next+*count,pages,ops,USER_HEAP_MAP_FAILED);
    }
    append_maps(maps,next,*count+pages);
    *count+=pages;
    h->live+=pages;
    if (h->live>h->peak) { h->peak=h->live; }
    return USER_HEAP_OK;
}
enum user_heap_result user_heap_allocate(struct user_heap *h,struct user_mapping *maps,unsigned *count,
    unsigned pages,const struct user_heap_ops *ops,uint64_t *address)
{
    if (!h || !maps || !count || !address || !ops_valid(ops) || !pages || pages>USER_HEAP_PAGES) { return USER_HEAP_ARGUMENT; }
    if (!valid(h,maps,*count)) { h->failed=true;return USER_HEAP_CORRUPT; }
    if (h->allocations==UINT64_MAX) { return USER_HEAP_ARGUMENT; }
    if (!fits_quota(h,*count,pages)) { return USER_HEAP_QUOTA; }
    unsigned first=0,span=0;
    for (unsigned i=0;i<USER_HEAP_PAGES;++i) {
        if (h->runs[i]) { span=0; }else { if (!span) { first=i; }if (++span==pages) { break; } }
    }
    if (span!=pages) { return USER_HEAP_FRAGMENTED; }
    enum user_heap_result result=append_pages(h,maps,count,first,pages,ops);
    if (result!=USER_HEAP_OK) { return result; }
    h->runs[first]=(unsigned char)pages;
    for (unsigned i=1;i<pages;++i) { h->runs[first+i]=INTERIOR; }
    ++h->allocations;
    *address=USER_HEAP_BASE+first*PAGE;return USER_HEAP_OK;
}
static enum user_heap_result trim_run(struct user_heap *h,struct user_mapping *maps,unsigned *count,
    unsigned first,unsigned old_pages,unsigned pages,const struct user_heap_ops *ops)
{
    uint64_t start=USER_HEAP_BASE+(first+pages)*PAGE;
    unsigned released=old_pages-pages;
    struct user_mapping next[USER_MAPPING_LIMIT],removed[USER_HEAP_PAGES];unsigned kept=0,n=0;
    for (unsigned i=0;i<*count;++i) {
        uint64_t va=maps[i].virtual_address;
        if (va>=start && va-start<released*PAGE) { removed[n++]=maps[i]; }else { next[kept++]=maps[i]; }
    }
    if (n!=released) { h->failed=true;return USER_HEAP_CORRUPT; }
    if (!ops->publish(ops->context,next,kept)) { return USER_HEAP_MAP_FAILED; }
    append_maps(maps,next,kept);*count=kept;h->live-=released;
    if (!pages) { ++h->free_calls; }
    for (unsigned i=pages;i<old_pages;++i) { h->runs[first+i]=0; }
    h->runs[first]=(unsigned char)pages;
    for (unsigned i=0;i<n;++i) {
        if (!ops->release(ops->context,removed[i].physical)) { maps[(*count)++]=removed[i];h->failed=true; }
    }
    return h->failed ? USER_HEAP_CORRUPT : USER_HEAP_OK;
}
enum user_heap_result user_heap_free(struct user_heap *h,struct user_mapping *maps,unsigned *count,
    uint64_t address,const struct user_heap_ops *ops)
{
    if (!h || !maps || !count || !ops_valid(ops) || !heap_page(address)) { return USER_HEAP_ARGUMENT; }
    if (!valid(h,maps,*count)) { h->failed=true;return USER_HEAP_CORRUPT; }
    unsigned first=(unsigned)((address-USER_HEAP_BASE)/PAGE),pages=h->runs[first];
    if (!pages || pages==INTERIOR || h->free_calls==UINT64_MAX) { return USER_HEAP_ARGUMENT; }
    return trim_run(h,maps,count,first,pages,0,ops);
}
enum user_heap_result user_heap_resize(struct user_heap *h,struct user_mapping *maps,unsigned *count,
    uint64_t address,unsigned pages,const struct user_heap_ops *ops)
{
    if (!h || !maps || !count || !ops_valid(ops) || !heap_page(address) || !pages || pages>USER_HEAP_PAGES) {
        return USER_HEAP_ARGUMENT;
    }
    if (!valid(h,maps,*count)) { h->failed=true;return USER_HEAP_CORRUPT; }
    unsigned first=(unsigned)((address-USER_HEAP_BASE)/PAGE),old_pages=h->runs[first];
    if (!old_pages || old_pages==INTERIOR) { return USER_HEAP_ARGUMENT; }
    if (pages==old_pages) { return USER_HEAP_OK; }
    if (pages<old_pages) { return trim_run(h,maps,count,first,old_pages,pages,ops); }
    if (!fits_quota(h,*count,pages-old_pages)) { return USER_HEAP_QUOTA; }
    if (pages>USER_HEAP_PAGES-first) { return USER_HEAP_FRAGMENTED; }
    for (unsigned i=old_pages;i<pages;++i) {
        if (h->runs[first+i]) { return USER_HEAP_FRAGMENTED; }
    }
    enum user_heap_result result=append_pages(h,maps,count,first+old_pages,pages-old_pages,ops);
    if (result!=USER_HEAP_OK) { return result; }
    h->runs[first]=(unsigned char)pages;
    for (unsigned i=old_pages;i<pages;++i) { h->runs[first+i]=INTERIOR; }
    return USER_HEAP_OK;
}
