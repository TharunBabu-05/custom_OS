#include "console.h"
#include "cpu.h"
#include "memory.h"
#include "mmu.h"
#include "smp.h"
#include "smp_memory.h"

enum { RUN_LIMIT=4, PAGE_LIMIT=16 };
enum operation { ALLOCATE, RELEASE, BARRIER };
enum answer { NO_REPLY, GRANTED, DENIED };
struct request {
    enum operation op;
    uint64_t address;
    unsigned pages,alignment;
};
struct mailbox {
    uint64_t sequence,reply;
    struct request request;
    uint64_t address;
    enum answer answer;
};
struct run { uint64_t base;unsigned pages; };
static struct {
    struct mailbox mailbox;
    struct run runs[RUN_LIMIT];
    uint64_t allocations,releases,rejected,quota_rejections,reclaimed;
    unsigned pages,peak;
} owners[SMP_WORKER_LIMIT];
static unsigned active,retained;
static bool aborted;

static uint64_t load(const uint64_t *value)
{ return __atomic_load_n(value,__ATOMIC_ACQUIRE); }

static void publish(uint64_t *value,uint64_t next)
{ __atomic_store_n(value,next,__ATOMIC_RELEASE); }

static void clear_pages(struct run run)
{
    uint64_t *words=(uint64_t *)(uintptr_t)run.base;
    size_t count=(size_t)run.pages*PAGE_SIZE/sizeof(*words);
    for (size_t i=0;i<count;++i) { words[i]=0; }
    __asm__ volatile("dsb sy" ::: "memory");
}

static bool grant(unsigned slot,unsigned pages,unsigned alignment,uint64_t *address)
{
    if (!pages || pages>PAGE_LIMIT || !alignment || alignment>PAGE_LIMIT || (alignment&(alignment-1))) { return false; }
    if (pages>PAGE_LIMIT-owners[slot].pages) {
        ++owners[slot].quota_rejections;
        return false;
    }
    unsigned index=0;
    while (index<RUN_LIMIT && owners[slot].runs[index].pages) { ++index; }
    if (index==RUN_LIMIT) { ++owners[slot].quota_rejections;return false; }

    uint64_t base;
    if (!page_alloc_aligned(pages,alignment,&base)) { return false; }
    if (!mmu_dma_buffer(base,(size_t)pages*PAGE_SIZE)) {
        (void)page_free_run(base,pages);
        return false;
    }
    struct run run={base,pages};
    owners[slot].runs[index]=run;
    owners[slot].pages+=pages;
    if (owners[slot].pages>owners[slot].peak) { owners[slot].peak=owners[slot].pages; }
    ++owners[slot].allocations;
    clear_pages(run);
    *address=base;
    return true;
}

static bool release_run(unsigned slot,unsigned index,bool reclaim)
{
    struct run run=owners[slot].runs[index];
    clear_pages(run);
    if (!page_free_run(run.base,run.pages)) { return false; }
    owners[slot].runs[index]=(struct run){0};
    owners[slot].pages-=run.pages;
    ++owners[slot].releases;
    if (reclaim) { owners[slot].reclaimed+=run.pages; }
    return true;
}

static bool release(unsigned slot,uint64_t base,unsigned pages)
{
    if (!pages) { return false; }
    for (unsigned i=0;i<RUN_LIMIT;++i) {
        struct run run=owners[slot].runs[i];
        if (run.base==base && run.pages==pages) { return release_run(slot,i,false); }
    }
    return false;
}

bool smp_memory_reclaim(unsigned slot)
{
    bool clean=true;
    for (unsigned i=0;i<RUN_LIMIT;++i) {
        if (owners[slot].runs[i].pages && !release_run(slot,i,true)) { clean=false; }
    }
    if (clean) { retained&=~(1u<<slot); }
    else { retained|=1u<<slot; }
    return clean;
}

bool smp_memory_begin(unsigned workers)
{
    if (active || retained || !workers) { return false; }
    aborted=false;
    for (unsigned slot=0;slot<SMP_WORKER_LIMIT;++slot) {
        if (workers&(1u<<slot)) { owners[slot].mailbox=(struct mailbox){0}; }
    }
    active=workers;
    return true;
}

bool smp_memory_finish(unsigned acknowledged)
{
    bool returned=true;
    retained|=active&~acknowledged;
    for (unsigned slot=0;slot<SMP_WORKER_LIMIT;++slot) {
        if (acknowledged&(1u<<slot)) {
            returned=returned && !owners[slot].pages;
            (void)smp_memory_reclaim(slot);
        }
    }
    active=0;
    return returned && !retained;
}

void smp_memory_abort(void)
{ aborted=true; }

static void respond(unsigned slot,uint64_t sequence,bool granted,uint64_t address)
{
    struct mailbox *mail=&owners[slot].mailbox;
    mail->address=address;
    mail->answer=granted ? GRANTED : DENIED;
    publish(&mail->reply,sequence);
}

static bool disjoint_runs(void)
{
    struct run live[SMP_WORKER_LIMIT*RUN_LIMIT];
    unsigned count=0;
    for (unsigned slot=0;slot<SMP_WORKER_LIMIT;++slot) {
        if (!(active&(1u<<slot))) { continue; }
        for (unsigned i=0;i<RUN_LIMIT;++i) {
            struct run run=owners[slot].runs[i];
            if (!run.pages) { return false; }
            for (unsigned j=0;j<count;++j) {
                struct run other=live[j];
                if (run.base<other.base+other.pages*PAGE_SIZE &&
                    other.base<run.base+run.pages*PAGE_SIZE) { return false; }
            }
            live[count++]=run;
        }
    }
    return true;
}

void smp_memory_service(void)
{
    unsigned waiting=0;
    uint64_t sequences[SMP_WORKER_LIMIT];
    for (unsigned slot=0;slot<SMP_WORKER_LIMIT;++slot) {
        if (!(active&(1u<<slot))) { continue; }
        struct mailbox *mail=&owners[slot].mailbox;
        uint64_t sequence=load(&mail->sequence);
        if (sequence==load(&mail->reply)) { continue; }
        struct request req=mail->request;
        if (req.op==BARRIER && !aborted) {
            waiting|=1u<<slot;
            sequences[slot]=sequence;
            continue;
        }
        uint64_t address=0;
        bool ok=!aborted && (req.op==ALLOCATE ? grant(slot,req.pages,req.alignment,&address) :
                            req.op==RELEASE && release(slot,req.address,req.pages));
        if (!ok) { ++owners[slot].rejected; }
        respond(slot,sequence,ok,address);
    }
    if (!waiting || waiting!=active) { return; }

    bool valid=disjoint_runs();
    for (unsigned slot=0;slot<SMP_WORKER_LIMIT;++slot) {
        if (!(waiting&(1u<<slot))) { continue; }
        uint64_t peer=0;
        for (unsigned other=0;other<SMP_WORKER_LIMIT;++other) {
            if (other!=slot && (waiting&(1u<<other))) { peer=owners[other].runs[0].base;break; }
        }
        respond(slot,sequences[slot],valid,peer);
    }
}

void smp_memory_print(unsigned slot)
{
    console_write("memory_pages=");console_u64(owners[slot].pages);
    console_write(" peak=");console_u64(owners[slot].peak);
    console_write(" allocations=");console_u64(owners[slot].allocations);
    console_write(" releases=");console_u64(owners[slot].releases);
    console_write(" rejected=");console_u64(owners[slot].rejected);
    console_write(" quota_rejections=");console_u64(owners[slot].quota_rejections);
    console_write(" reclaimed_pages=");console_u64(owners[slot].reclaimed);
    console_write(retained&(1u<<slot) ? " retained=yes\n" : " retained=no\n");
}

static enum answer request(unsigned slot,struct request req,uint64_t *address)
{
    struct mailbox *mail=&owners[slot].mailbox;
    uint64_t sequence=load(&mail->sequence)+1;
    mail->request=req;
    publish(&mail->sequence,sequence);
    uint64_t start=timer_ticks(),budget=timer_frequency()*3;
    while (load(&mail->reply)!=sequence) {
        if (timer_ticks()-start>=budget) { return NO_REPLY; }
        __asm__ volatile("yield");
    }
    if (address && mail->answer==GRANTED) { *address=mail->address; }
    return mail->answer;
}

static enum answer allocate(unsigned slot,unsigned pages,unsigned alignment,uint64_t *address)
{ return request(slot,(struct request){.op=ALLOCATE,.pages=pages,.alignment=alignment},address); }

static enum answer return_pages(unsigned slot,uint64_t address,unsigned pages)
{ return request(slot,(struct request){.op=RELEASE,.address=address,.pages=pages},NULL); }

static bool pattern(struct run run,uint64_t seed,bool fill)
{
    volatile uint64_t *words=(volatile uint64_t *)(uintptr_t)run.base;
    size_t count=(size_t)run.pages*PAGE_SIZE/sizeof(*words);
    for (size_t i=0;i<count;++i) {
        uint64_t expected=seed^i;
        if (words[i]!=(fill ? 0 : expected)) { return false; }
        if (fill) { words[i]=expected; }
    }
    return true;
}

static bool check_runs(unsigned slot,unsigned round)
{
    static const unsigned pages[RUN_LIMIT]={3,5,1,1},alignment[RUN_LIMIT]={4,8,1,2};
    struct run runs[RUN_LIMIT];
    uint64_t seed=(uint64_t)(slot+1)<<56|(uint64_t)(round+1)<<32;
    if (allocate(slot,1,3,NULL)!=DENIED) { return false; }
    for (unsigned i=0;i<RUN_LIMIT;++i) {
        runs[i].pages=pages[i];
        if (allocate(slot,pages[i],alignment[i],&runs[i].base)!=GRANTED ||
            runs[i].base%(alignment[i]*PAGE_SIZE) || !pattern(runs[i],seed|((uint64_t)i<<24),true)) { return false; }
    }
    if (allocate(slot,7,1,NULL)!=DENIED || allocate(slot,1,1,NULL)!=DENIED ||
        return_pages(slot,runs[0].base,1)!=DENIED) { return false; }

    uint64_t peer;
    if (request(slot,(struct request){.op=BARRIER},&peer)!=GRANTED ||
        (peer && return_pages(slot,peer,pages[0])!=DENIED) ||
        request(slot,(struct request){.op=BARRIER},NULL)!=GRANTED) { return false; }
    for (unsigned i=0;i<RUN_LIMIT;++i) {
        if (!pattern(runs[i],seed|((uint64_t)i<<24),false) ||
            return_pages(slot,runs[i].base,runs[i].pages)!=GRANTED) { return false; }
    }
    return return_pages(slot,runs[0].base,pages[0])==DENIED;
}

bool smp_memory_worker(unsigned slot,unsigned rounds)
{
    for (unsigned round=0;round<rounds;++round) {
        if (!check_runs(slot,round)) { return false; }
    }
    return true;
}
