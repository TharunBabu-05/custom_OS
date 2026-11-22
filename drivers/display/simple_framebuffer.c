#include "framebuffer.h"
#include "dt_property.h"

static const char *const formats[]={"r5g6b5","x8r8g8b8","a8r8g8b8","x8b8g8r8","a8b8g8r8"};

static uint32_t be32(const unsigned char *p)
{ return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3]; }

static uint64_t cells(const unsigned char *p,unsigned count)
{ return count==1 ? be32(p) : (uint64_t)be32(p)<<32|be32(p+4); }

static bool text(const unsigned char *p,uint32_t bytes,const char *expected)
{
    uint32_t i=0;
    while (i<bytes && expected[i] && p[i]==(unsigned char)expected[i]) { ++i; }
    return i<bytes && i+1==bytes && !p[i] && !expected[i];
}

static enum dt_lookup active(const void *dtb,size_t bytes,const char *path)
{
    const unsigned char *p;uint32_t n;
    enum dt_lookup state=dt_optional_property(dtb,bytes,path,"status",&p,&n);
    if (state==DT_MISSING) { return DT_FOUND; }
    if (state!=DT_FOUND || !n || p[n-1]) { return DT_INVALID; }
    return text(p,n,"okay") || text(p,n,"ok") ? DT_FOUND : DT_MISSING;
}

static bool compatible(const unsigned char *p,uint32_t n)
{
    bool simple=false;
    for (uint32_t at=0;at<n;) {
        uint32_t start=at;
        while (at<n && p[at]) { ++at; }
        if (at==n || at==start) { return false; }
        simple|=text(p+start,at-start+1,"simple-framebuffer");
        ++at;
    }
    return simple;
}

static bool cell_count(const void *dtb,size_t bytes,const char *key,unsigned *count)
{
    const unsigned char *p;uint32_t n;
    enum dt_lookup r=dt_optional_property(dtb,bytes,"/chosen",key,&p,&n);
    if (r==DT_MISSING) { r=dt_optional_property(dtb,bytes,"/",key,&p,&n); }
    if (r!=DT_FOUND || n!=4) { return false; }
    *count=be32(p);
    return *count==1 || *count==2;
}

static bool word(const void *dtb,size_t bytes,const char *path,const char *key,uint32_t *value)
{
    const unsigned char *p;uint32_t n;
    if (!dt_property(dtb,bytes,path,key,&p,&n) || n!=4) { return false; }
    *value=be32(p);
    return true;
}

const char *fb_format_name(enum fb_format format)
{ return (unsigned)format<sizeof(formats)/sizeof(formats[0]) ? formats[format] : "unsupported"; }

bool fb_mode_valid(const struct fb_mode *mode)
{
    if (!mode || (unsigned)mode->format>=sizeof(formats)/sizeof(formats[0])) { return false; }
    unsigned pixel=mode->format==FB_RGB565 ? 2 : 4;
    return mode->base>0 && !(mode->base&(PAGE_SIZE-1)) && mode->base<PMM_ADDRESS_LIMIT &&
        mode->width>=8 && mode->width<=4096 && mode->height>=8 && mode->height<=2160 &&
        mode->stride>=mode->width*pixel && !(mode->stride&(pixel-1)) &&
        (uint64_t)mode->stride*mode->height<=mode->size && mode->size<=32*1024*1024 &&
        mode->size<=PMM_ADDRESS_LIMIT-mode->base;
}

static enum fb_result read_mode(const void *dtb,size_t bytes,const char *path,struct fb_mode *out)
{
    const unsigned char *p;uint32_t n;
    /* This narrow profile accepts an identity child bus and a single reg. */
    enum dt_lookup range=dt_optional_property(dtb,bytes,"/chosen","ranges",&p,&n);
    if (range==DT_INVALID || (range==DT_FOUND && n) ||
        dt_optional_property(dtb,bytes,path,"memory-region",&p,&n)!=DT_MISSING) { return FB_HANDOFF; }
    unsigned ac,sc;
    if (!cell_count(dtb,bytes,"#address-cells",&ac) || !cell_count(dtb,bytes,"#size-cells",&sc) ||
        !dt_property(dtb,bytes,path,"reg",&p,&n) || n!=(ac+sc)*4) { return FB_HANDOFF; }
    struct fb_mode mode={.base=cells(p,ac),.size=cells(p+4*ac,sc)};
    if (!word(dtb,bytes,path,"width",&mode.width) || !word(dtb,bytes,path,"height",&mode.height) ||
        !word(dtb,bytes,path,"stride",&mode.stride) || !dt_property(dtb,bytes,path,"format",&p,&n)) { return FB_HANDOFF; }
    for (unsigned i=0;i<sizeof(formats)/sizeof(formats[0]);++i) {
        if (!text(p,n,formats[i])) { continue; }
        mode.format=(enum fb_format)i;
        if (!fb_mode_valid(&mode)) { return FB_GEOMETRY; }
        *out=mode;
        return FB_READY;
    }
    return FB_GEOMETRY;
}

enum fb_result fb_discover(const void *dtb,size_t bytes,struct fb_mode *out)
{
    if (!out) { return FB_HANDOFF; }
    enum dt_lookup enabled=active(dtb,bytes,"/chosen");
    if (enabled!=DT_FOUND) { return enabled==DT_MISSING ? FB_ABSENT : FB_HANDOFF; }
    for (unsigned index=0;index<32;++index) {
        const char *name;
        enum dt_lookup child=dt_child(dtb,bytes,"/chosen",index,&name);
        if (child!=DT_FOUND) { return child==DT_MISSING ? FB_ABSENT : FB_HANDOFF; }
        char path[80]="/chosen/";
        unsigned at=8;
        for (;*name && at+1<sizeof(path);++name) { path[at++]=*name; }
        if (*name) { continue; }
        path[at]=0;
        const unsigned char *p;uint32_t n;
        enum dt_lookup r=dt_optional_property(dtb,bytes,path,"compatible",&p,&n);
        if (r==DT_INVALID) { return FB_HANDOFF; }
        if (r!=DT_FOUND || !compatible(p,n)) { continue; }
        r=active(dtb,bytes,path);
        if (r==DT_MISSING) { continue; }
        return r==DT_FOUND ? read_mode(dtb,bytes,path,out) : FB_HANDOFF;
    }
    return FB_HANDOFF;
}

static bool same(const char *a,const char *b)
{ while (*a && *a==*b) { ++a;++b; }return *a==*b; }

bool fb_claim(const struct fb_mode *mode,struct memory_map *map,bool pi5)
{
    if (!fb_mode_valid(mode) || !map || map->framebuffer.size || !map->ram_count ||
        map->ram_count>MEMORY_MAX_RAM || map->reserved_count>MEMORY_MAX_RESERVED) { return false; }
    uint64_t base=mode->base,span=(mode->size+PAGE_SIZE-1)&~(PAGE_SIZE-1),end=base+span;
    if (pi5 && (base<0x3c000000 || end>0x40000000)) { return false; }
    bool in_ram=false;
    for (unsigned i=0;i<map->ram_count;++i) {
        struct memory_range r=map->ram[i];
        if (r.base>=end || r.base+r.size<=base) { continue; }
        if (r.base>base || r.base+r.size<end) { return false; }
        in_ram=true;
    }
    /* Pi 5 firmware can trim its first bank at the scanout allocation.
     * Only accept the bounded top-of-low-RAM gap, beyond the reported bank. */
    if (!in_ram && (!pi5 || map->ram[0].base || map->ram[0].size<0x3c000000 ||
        base<map->ram[0].size || end>0x40000000)) { return false; }
    for (unsigned i=0;i<map->reserved_count;++i) {
        const struct memory_reservation *entry=&map->reserved[i];
        struct memory_range r=entry->range;
        if (r.base>=end || r.base+r.size<=base) { continue; }
        if (!entry->reason) { return false; }
        /* OurOS excludes the CMA window but does not allocate a CMA pool. */
        if (same(entry->reason,"dynamic reservation window (conservative)")) { continue; }
        if (r.base!=base || r.size<mode->size ||
            (!same(entry->reason,"memreserve") && !same(entry->reason,"reserved-memory"))) { return false; }
    }
    if (!memory_reserve(map,base,span,"firmware framebuffer")) { return false; }
    map->framebuffer=(struct memory_range){base,span};
    return true;
}
