/* Native tests use Win32 guard pages only in the host test executable. */
#include "host_assert.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "memory.h"
static uint64_t test_bitmap[PMM_BITMAP_BYTES/8];
#define INIT(map) pages_init((map),test_bitmap,sizeof(test_bitmap))

struct builder {
    unsigned char bytes[65536], structure[32768], strings[8192];
    size_t pos, names;
    struct memory_range reservations[8]; unsigned reserved;
};
static struct builder b;
static void put32(unsigned char *p, uint32_t n)
{ p[0]=(unsigned char)(n>>24); p[1]=(unsigned char)(n>>16); p[2]=(unsigned char)(n>>8); p[3]=(unsigned char)n; }
static void word(uint32_t n) { assert(b.pos+4<sizeof(b.structure)); put32(b.structure+b.pos,n); b.pos+=4; }
static void begin(const char *name)
{
    word(1); size_t n=strlen(name)+1; memcpy(b.structure+b.pos,name,n); b.pos+=n;
    while (b.pos&3) { b.structure[b.pos++]=0; }
}
static void end(void) { word(2); }
static void property(const char *name, const void *value, size_t n)
{
    word(3); word((uint32_t)n); word((uint32_t)b.names);
    size_t z=strlen(name)+1; assert(b.names+z<sizeof(b.strings));
    memcpy(b.strings+b.names,name,z); b.names+=z;
    if (n) { assert(b.pos+n<sizeof(b.structure)); memcpy(b.structure+b.pos,value,n); b.pos+=n; }
    while (b.pos&3) { b.structure[b.pos++]=0; }
}
static void u32(const char *name,uint32_t value) { unsigned char p[4]; put32(p,value); property(name,p,4); }
static void u64(const char *name,uint64_t value)
{ unsigned char p[8]; put32(p,(uint32_t)(value>>32)); put32(p+4,(uint32_t)value); property(name,p,8); }
static void string(const char *name,const char *value) { property(name,value,strlen(value)+1); }
static void reg(const char *name,unsigned ac,unsigned sc,uint64_t base,uint64_t size)
{
    unsigned char p[16]; unsigned at=0;
    if (ac==2) { put32(p+at,(uint32_t)(base>>32)); at+=4; }
    put32(p+at,(uint32_t)base); at+=4;
    if (sc==2) { put32(p+at,(uint32_t)(size>>32)); at+=4; }
    put32(p+at,(uint32_t)size); at+=4; property(name,p,at);
}
static void start(unsigned ac,unsigned sc)
{ memset(&b,0,sizeof(b)); begin(""); u32("#address-cells",ac); u32("#size-cells",sc); }
static void memory(unsigned ac,unsigned sc,uint64_t base,uint64_t size)
{ begin("memory@0"); string("device_type","memory"); reg("reg",ac,sc,base,size); end(); }
static void reserved_start(unsigned ac,unsigned sc)
{ begin("reserved-memory"); u32("#address-cells",ac); u32("#size-cells",sc); property("ranges",NULL,0); }
static size_t finish(void)
{
    end(); word(9);
    uint32_t ro=40, so=ro+(b.reserved+1)*16, no=so+(uint32_t)b.pos, total=no+(uint32_t)b.names;
    assert(total<sizeof(b.bytes)); memset(b.bytes,0,total);
    put32(b.bytes,0xd00dfeed); put32(b.bytes+4,total); put32(b.bytes+8,so); put32(b.bytes+12,no);
    put32(b.bytes+16,ro); put32(b.bytes+20,17); put32(b.bytes+24,16);
    put32(b.bytes+32,(uint32_t)b.names); put32(b.bytes+36,(uint32_t)b.pos);
    for (unsigned i=0;i<b.reserved;++i) {
        unsigned char *p=b.bytes+ro+i*16;
        put32(p,(uint32_t)(b.reservations[i].base>>32)); put32(p+4,(uint32_t)b.reservations[i].base);
        put32(p+8,(uint32_t)(b.reservations[i].size>>32)); put32(p+12,(uint32_t)b.reservations[i].size);
    }
    memcpy(b.bytes+so,b.structure,b.pos); memcpy(b.bytes+no,b.strings,b.names); return total;
}
static void parser_cases(void)
{
    struct memory_map m; size_t n;
    start(2,2); memory(2,2,0,0x1000000); memory(2,2,0x100000000ULL,0x2000000);
    b.reservations[b.reserved++]=(struct memory_range){0x3001,1};
    reserved_start(2,2); begin("firmware@10000"); reg("reg",2,2,0x10000,0x1000); property("no-map",NULL,0); end();
    begin("disabled@20000"); string("status","disabled"); reg("reg",2,2,0x20000,0x1000); end(); end();
    begin("chosen"); u64("linux,initrd-start",0x800000); u64("linux,initrd-end",0x810000); end();
    n=finish(); assert(fdt_read_memory(b.bytes,n,&m));
    assert(m.ram_count==2 && m.ram_bytes==0x3000000 && m.reserved_count==3 && m.dtb_size==n);
    assert(m.ram[1].base==0x100000000ULL);

    start(1,1); memory(1,1,0x100000,0x100000); n=finish();
    assert(fdt_read_memory(b.bytes,n,&m) && m.ram_bytes==0x100000);
    start(2,1); memory(2,1,0,0x100000); memory(2,1,0x80000,0x100000); n=finish();
    assert(fdt_read_memory(b.bytes,n,&m) && m.ram_count==1 && m.ram_bytes==0x180000);
    start(2,2); begin("memory@0"); string("device_type","memory");
    unsigned char multiple[32]={0}; put32(multiple+12,0x10000); put32(multiple+16,1); put32(multiple+28,0x20000);
    property("reg",multiple,sizeof(multiple)); end(); n=finish();
    assert(fdt_read_memory(b.bytes,n,&m) && m.ram_count==2 && m.ram_bytes==0x30000);
    start(1,1); memory(1,1,0,0x100000); begin("memory@200000"); string("status","disabled");
    string("device_type","memory"); reg("reg",1,1,0x200000,0x100000); end(); n=finish();
    assert(fdt_read_memory(b.bytes,n,&m) && m.ram_bytes==0x100000);

    start(2,2); memory(2,2,0,0x1000000); reserved_start(2,2); begin("cma"); u32("size",0x1000);
    reg("alloc-ranges",2,2,0x400000,0x200000); end(); end(); n=finish();
    assert(fdt_read_memory(b.bytes,n,&m) && m.reserved_count==1 && m.reserved[0].range.size==0x200000);
    start(2,2); memory(2,2,0,0x1000000); reserved_start(2,2); begin("cma"); u32("size",0x1000); end(); end();
    n=finish(); assert(!fdt_read_memory(b.bytes,n,&m));
    start(2,2); memory(2,2,0,0x1000000); reserved_start(2,2); begin("cma"); u32("size",0x1000);
    reg("alloc-ranges",2,2,0,0); end(); end(); n=finish(); assert(!fdt_read_memory(b.bytes,n,&m));
    start(2,2); memory(2,2,UINT64_MAX-4095,8192); n=finish(); assert(!fdt_read_memory(b.bytes,n,&m));
    start(1,1); memory(1,1,0,0x100000); begin("chosen"); u32("linux,initrd-start",0x80000); end();
    n=finish(); assert(!fdt_read_memory(b.bytes,n,&m));
    start(1,1); memory(1,1,0,0x100000); begin("chosen"); u32("linux,usable-memory-range",1); end();
    n=finish(); assert(!fdt_read_memory(b.bytes,n,&m));
    start(2,2); begin("memory@0"); string("device_type","memory"); reg("reg",2,2,0,0x100000);
    reg("linux,usable-memory",2,2,0,0x80000); end(); n=finish(); assert(!fdt_read_memory(b.bytes,n,&m));
    start(2,2); memory(2,2,0,0x100000); begin("reserved-memory"); u32("#address-cells",2); u32("#size-cells",2);
    u32("ranges",1); end(); n=finish(); assert(!fdt_read_memory(b.bytes,n,&m));
    start(2,2); memory(2,2,0,0x100000); reserved_start(2,2); begin("parent"); begin("nested");
    reg("reg",2,2,0x4000,0x1000); end(); end(); end(); n=finish(); assert(!fdt_read_memory(b.bytes,n,&m));
    start(2,2); for (unsigned i=0; i<MEMORY_MAX_RAM+1; ++i) { memory(2,2,i*0x10000,0x1000); }
    n=finish(); assert(!fdt_read_memory(b.bytes,n,&m));
    start(2,2); memory(2,2,0,16ULL*1024*1024*1024); reserved_start(2,2);
    begin("linux,cma"); u32("size",0x4000000); reg("alloc-ranges",2,2,0,0x40000000); end(); end();
    n=finish(); assert(fdt_read_memory(b.bytes,n,&m));
    assert(memory_reserve(&m,0,0x200000,"boot guard"));
    assert(memory_reserve(&m,0x80000,0x120000,"kernel"));
    assert(memory_reserve(&m,0x200000,0x14000,"DTB"));
    assert(INIT(&m)); assert(pages_stats().managed_pages==15ULL*1024*1024*1024/PAGE_SIZE);
    uint64_t first; assert(page_alloc(&first) && first==0x40000000); assert(page_free(first));
    puts("PASS: DT cells, multi-bank/high RAM, overlap normalization, fixed/dynamic reservations, initrd and fail-closed cases");
}
static void allocator_cases(void)
{
    struct memory_map m={0}; uint64_t p[8]={0};
    m.ram_count=2; m.ram[0]=(struct memory_range){0x1000,8*PAGE_SIZE};
    m.ram[1]=(struct memory_range){0x100000000ULL,2*PAGE_SIZE};
    assert(memory_reserve(&m,0x2001,1,"partial"));
    assert(memory_reserve(&m,0x3000,0x2000,"first"));
    assert(memory_reserve(&m,0x4000,0x2000,"overlap"));
    assert(memory_normalize(&m)); assert(INIT(&m));
    assert(pages_stats().managed_pages==6 && pages_stats().free_pages==6);
    assert(!page_alloc(NULL));
    for (unsigned i=0;i<6;++i) {
        assert(page_alloc(&p[i])); assert(!(p[i]&(PAGE_SIZE-1)));
        assert(p[i]<0x2000 || p[i]>=0x6000);
        for (unsigned j=0;j<i;++j) { assert(p[i]!=p[j]); }
    }
    assert(p[4]==0x100000000ULL); assert(!page_alloc(&p[6]));
    assert(!page_free(0x2000)); assert(!page_free(0x9000)); assert(!page_free(p[0]+1));
    assert(!page_free(PMM_ADDRESS_LIMIT));
    assert(page_free(p[2])); assert(!page_free(p[2]));
    uint64_t replacement; assert(page_alloc(&replacement) && replacement==p[2]);
    for (unsigned i=0;i<6;++i) { assert(page_free(p[i])); }
    assert(pages_stats().free_pages==6 && pages_stats().allocated_pages==0);

    memset(&m,0,sizeof(m)); m.ram_count=1;
    m.ram[0]=(struct memory_range){PMM_ADDRESS_LIMIT-PAGE_SIZE,2*PAGE_SIZE};
    assert(memory_normalize(&m) && INIT(&m));
    assert(pages_stats().managed_pages==1); assert(page_alloc(&replacement) && replacement==PMM_ADDRESS_LIMIT-PAGE_SIZE);
    assert(!page_alloc(&replacement));
    m.ram[0]=(struct memory_range){PMM_ADDRESS_LIMIT,2*PAGE_SIZE}; assert(!INIT(&m));
    assert(!page_alloc(&replacement) && !page_free(p[0]));
    m.ram[0]=(struct memory_range){1,PAGE_SIZE-1}; assert(!INIT(&m));
    m.ram[0]=(struct memory_range){0x1000,PAGE_SIZE};
    assert(memory_reserve(&m,0x1000,PAGE_SIZE,"all")); assert(!INIT(&m));
    assert(!memory_reserve(&m,UINT64_MAX,1,"overflow"));
    puts("PASS: page uniqueness, holes, overlapping/partial reservations, high addresses, exhaustion, reuse, double-free and cap");
}
static void bitmap_cases(void)
{
    struct memory_map m={0};m.ram_count=1;m.ram[0]=(struct memory_range){0x40000000,4*PMM_BITMAP_BYTES};assert(memory_normalize(&m));
    assert(memory_reserve(&m,0x40000000,0x20001,"unaligned guard"));assert(memory_reserve(&m,0x40010000,0x30000,"overlap"));
    uint64_t address=0xdeadbeef;assert(memory_claim_bitmap(&m,&address) && address==0x40040000 && m.reserved_count==3);
    assert(!strcmp(m.reserved[2].reason,"physical page bitmap") && m.reserved[2].range.size==PMM_BITMAP_BYTES);
    assert(INIT(&m) && !page_free(address));uint64_t p;unsigned count=0;
    while (page_alloc(&p)) { assert(p<address || p>=address+PMM_BITMAP_BYTES);++count; }
    assert(count==(4*PMM_BITMAP_BYTES-0x40000-PMM_BITMAP_BYTES)/PAGE_SIZE);
    struct memory_map bad=m;bad.ram[0].size=PMM_BITMAP_BYTES-1;address=0xdeadbeef;unsigned reservations=bad.reserved_count;
    assert(!memory_claim_bitmap(&bad,&address) && address==0xdeadbeef && bad.reserved_count==reservations);
    bad=m;bad.reserved_count=MEMORY_MAX_RESERVED;assert(!memory_claim_bitmap(&bad,&address));
    bad=m;bad.ram[0]=(struct memory_range){PMM_ADDRESS_LIMIT,2*PMM_BITMAP_BYTES};assert(!memory_claim_bitmap(&bad,&address));
    bad=m;bad.reserved[0].range=(struct memory_range){UINT64_MAX,2};assert(!memory_claim_bitmap(&bad,&address));
    bad=m;bad.ram_count=2;bad.ram[1]=bad.ram[0];assert(!memory_claim_bitmap(&bad,&address));
    memset(test_bitmap,0xa5,sizeof(test_bitmap));assert(!pages_init(&m,test_bitmap,sizeof(test_bitmap)-1) && test_bitmap[0]==0xa5a5a5a5a5a5a5a5ULL);
    assert(!pages_init(&m,(unsigned char *)test_bitmap+1,sizeof(test_bitmap)) && !page_alloc(&p));
    assert(!pages_init(&m,NULL,PMM_BITMAP_BYTES));
    puts("PASS: external 1 MiB bitmap claim, overlap/alignment/cap/exhaustion checks, self-reservation excluded from every allocation/free, bounded storage and no static kernel bitmap");
}
static void guarded_cases(void)
{
    start(2,2); memory(2,2,0,0x1000000); size_t n=finish();
    SYSTEM_INFO sys; GetSystemInfo(&sys); size_t rounded=(n+sys.dwPageSize-1)/sys.dwPageSize*sys.dwPageSize;
    unsigned char *allocation=VirtualAlloc(NULL,rounded+sys.dwPageSize,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    assert(allocation); DWORD old; assert(VirtualProtect(allocation+rounded,sys.dwPageSize,PAGE_NOACCESS,&old));
    unsigned char *guarded=allocation+rounded-n; memcpy(guarded,b.bytes,n);
    struct memory_map result, sentinel; memset(&sentinel,0xa5,sizeof(sentinel));
    for (size_t length=0; length<n; ++length) {
        memcpy(guarded+n-length,b.bytes,length); result=sentinel;
        assert(!fdt_read_memory(guarded+n-length,length,&result)); assert(!memcmp(&result,&sentinel,sizeof(result)));
    }
    uint32_t random=0x91d04171;
    for (unsigned i=0;i<5000;++i) {
        memcpy(guarded,b.bytes,n); random=random*1664525u+1013904223u;
        guarded[random%n]^=(unsigned char)(1u<<(i%8)); result=sentinel;
        if (!fdt_read_memory(guarded,n,&result)) { assert(!memcmp(&result,&sentinel,sizeof(result))); }
        else { assert(result.ram_count>0 && result.ram_count<=MEMORY_MAX_RAM); }
    }
    assert(!fdt_read_memory(NULL,n,&result)); assert(!fdt_read_memory(b.bytes,n,NULL));
    VirtualFree(allocation,0,MEM_RELEASE);
    puts("PASS: every input truncation and 5000 guard-page mutations; failures preserve output");
}
static void real_dtb(const char *path)
{
    FILE *f=fopen(path,"rb"); assert(f); assert(!fseek(f,0,SEEK_END)); long n=ftell(f); assert(n>=40 && n<=0x200000);
    rewind(f); unsigned char *data=malloc((size_t)n); assert(data); assert(fread(data,1,(size_t)n,f)==(size_t)n); fclose(f);
    struct memory_map map; assert(fdt_read_memory(data,(size_t)n,&map));
    printf("PASS: reference DTB %s: RAM banks=%u reservations=%u bytes=%llu (template, not hardware capacity)\n",
        path,map.ram_count,map.reserved_count,(unsigned long long)map.ram_bytes); free(data);
}
int main(int argc,char **argv)
{
    parser_cases(); allocator_cases();bitmap_cases(); guarded_cases();
    for (int i=1;i<argc;++i) { real_dtb(argv[i]); }
    return 0;
}
