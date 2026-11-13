#include "host_assert.h"
#include <string.h>
#include <stdbool.h>
#include "elf_app.h"
static unsigned char bytes[APP_ELF_MAX_BYTES],seed[APP_ELF_MAX_BYTES];
static void p16(unsigned char *p,uint16_t n) { p[0]=(unsigned char)n;p[1]=(unsigned char)(n>>8); }
static void p32(unsigned char *p,uint32_t n) { for (unsigned i=0;i<4;++i) { p[i]=(unsigned char)(n>>(8*i)); } }
static void p64(unsigned char *p,uint64_t n) { p32(p,(uint32_t)n);p32(p+4,(uint32_t)(n>>32)); }
static void ph(unsigned slot,uint32_t type,uint32_t flags,uint64_t offset,uint64_t va,uint64_t file,uint64_t mem,uint64_t align)
{
    unsigned char *p=bytes+64+slot*56;memset(p,0,56);p32(p,type);p32(p+4,flags);p64(p+8,offset);p64(p+16,va);p64(p+32,file);p64(p+40,mem);p64(p+48,align);
}
static void genesis(void)
{
    memset(bytes,0,sizeof(bytes));memcpy(bytes,"\177ELF",4);bytes[4]=2;bytes[5]=1;bytes[6]=1;
    p16(bytes+16,2);p16(bytes+18,183);p32(bytes+20,1);p64(bytes+24,APP_VA_BASE+0x180);p64(bytes+32,64);
    p16(bytes+52,64);p16(bytes+54,56);p16(bytes+56,2);
    ph(0,1,APP_PF_R|APP_PF_X,0,APP_VA_BASE,512,512,4096);
    ph(1,1,APP_PF_R|APP_PF_W,512,APP_VA_BASE+4096,64,4096,16);
    memcpy(seed,bytes,sizeof(bytes));
}
static void reject(size_t n)
{
    struct app_image_plan plan,old;memset(&plan,0xa5,sizeof(plan));memcpy(&old,&plan,sizeof(plan));
    assert(app_elf_plan(bytes,n,&plan)!=APP_ELF_OK);assert(!memcmp(&plan,&old,sizeof(plan)));
}
static void cases(void)
{
    genesis();struct app_image_plan p;
    assert(app_elf_plan(bytes,1024,&p)==APP_ELF_OK && p.count==2 && p.entry==APP_VA_BASE+0x180 && p.end==APP_VA_BASE+8192);
    assert(p.segments[0].vaddr==APP_VA_BASE && p.segments[0].flags==5 && p.segments[1].filesz==64 && p.segments[1].memsz==4096);
    assert(app_elf_plan(NULL,0,&p)==APP_ELF_ARGUMENT && app_elf_plan(bytes,1024,NULL)==APP_ELF_ARGUMENT);
    for (size_t n=0;n<176;++n) { reject(n); }reject(APP_ELF_MAX_BYTES+1);
    for (unsigned i=0;i<9;++i) { memcpy(bytes,seed,sizeof(bytes));bytes[i]^=0xff;reject(1024); }
    for (unsigned i=9;i<16;++i) { memcpy(bytes,seed,sizeof(bytes));bytes[i]=1;reject(1024); }
    const unsigned fields[]={16,18,20,48,52,54};
    for (unsigned i=0;i<sizeof(fields)/sizeof(fields[0]);++i) { memcpy(bytes,seed,sizeof(bytes));bytes[fields[i]]^=1;reject(1024); }
    memcpy(bytes,seed,sizeof(bytes));p16(bytes+56,0);reject(1024);p16(bytes+56,33);reject(1024);
    memcpy(bytes,seed,sizeof(bytes));p64(bytes+32,UINT64_MAX);reject(1024);p64(bytes+32,0);reject(1024);
    memcpy(bytes,seed,sizeof(bytes));p64(bytes+24,APP_VA_BASE+1);reject(1024);
    p64(bytes+24,APP_VA_BASE+512);reject(1024);p64(bytes+24,APP_VA_BASE+4096);reject(1024);
    for (unsigned type=2;type<=8;++type) {
        if (type==6) { continue; }memcpy(bytes,seed,sizeof(bytes));p32(bytes+64,type);reject(1024);
    }
    memcpy(bytes,seed,sizeof(bytes));p32(bytes+64,0x6474e551);p32(bytes+68,7);reject(1024);
    for (unsigned flags=0;flags<16;++flags) {
        if (flags==4 || flags==5 || flags==6) { continue; }
        memcpy(bytes,seed,sizeof(bytes));p32(bytes+64+56+4,flags);reject(1024);
    }
    memcpy(bytes,seed,sizeof(bytes));p64(bytes+64+40,0);reject(1024);
    p64(bytes+64+40,511);reject(1024);
    memcpy(bytes,seed,sizeof(bytes));p64(bytes+64+16,APP_VA_BASE-4096);reject(1024);
    p64(bytes+64+16,UINT64_MAX-4095);reject(1024);p64(bytes+64+16,APP_VA_BASE+1);reject(1024);
    p64(bytes+64+16,APP_VA_BASE+APP_IMAGE_PAGES*4096ULL);reject(1024);
    memcpy(bytes,seed,sizeof(bytes));p64(bytes+64+40,UINT64_MAX);reject(1024);
    p64(bytes+64+40,APP_IMAGE_PAGES*4096ULL+1);reject(1024);
    memcpy(bytes,seed,sizeof(bytes));p64(bytes+64+8,UINT64_MAX);reject(1024);
    p64(bytes+64+8,513);reject(1024);p64(bytes+64+8,0);p64(bytes+64+32,UINT64_MAX);reject(1024);
    memcpy(bytes,seed,sizeof(bytes));p64(bytes+64+48,3);reject(1024);p64(bytes+64+48,8192);reject(1024);
    memcpy(bytes,seed,sizeof(bytes));p64(bytes+64+56+16,APP_VA_BASE);reject(1024);
    memcpy(bytes,seed,sizeof(bytes));p64(bytes+64+40,4097);reject(1024);
    /* A zero-file-size BSS segment at EOF is permitted, but cannot be entry. */
    memcpy(bytes,seed,sizeof(bytes));ph(1,1,6,1024,APP_VA_BASE+4096,0,4096,1);
    assert(app_elf_plan(bytes,1024,&p)==APP_ELF_OK);
    p64(bytes+24,APP_VA_BASE+4096);reject(1024);
    memcpy(bytes,seed,sizeof(bytes));p16(bytes+56,3);ph(2,0x6474e551,6,0,0,0,0,16);
    assert(app_elf_plan(bytes,1024,&p)==APP_ELF_OK && p.count==2);
    /* Section metadata may be absent or irrelevant: only program headers load. */
    p64(bytes+40,UINT64_MAX);p16(bytes+58,65535);p16(bytes+60,65535);
    assert(app_elf_plan(bytes,1024,&p)==APP_ELF_OK);
    memcpy(bytes,seed,sizeof(bytes));p16(bytes+56,1);ph(0,1,5,0,APP_VA_BASE,512,APP_IMAGE_PAGES*4096,4096);
    assert(app_elf_plan(bytes,1024,&p)==APP_ELF_OK && p.end==APP_VA_BASE+APP_IMAGE_PAGES*4096ULL);
    /* Exactly eight disjoint read/execute load pages fit the image arena. */
    genesis();p16(bytes+56,8);p64(bytes+24,APP_VA_BASE+0x380);
    for (unsigned i=0;i<8;++i) { ph(i,1,5,0,APP_VA_BASE+(uint64_t)i*4096,1024,4096,1); }
    assert(app_elf_plan(bytes,1024,&p)==APP_ELF_OK && p.count==8);
    puts("PASS: ELF64LE AArch64 static header/program bounds, modes/W^X, alignments, page ownership, arena/entry/BSS rules, ignored section metadata and unchanged output on every rejection");
}
static void oracle(const unsigned char *source,size_t n,const struct app_image_plan *p)
{
    (void)source;assert(p->count && p->count<=8 && p->entry%4==0);
    bool executable=false;uint64_t end=APP_VA_BASE;
    for (unsigned i=0;i<p->count;++i) {
        const struct app_segment *s=&p->segments[i];assert(s->flags==4 || s->flags==5 || s->flags==6);
        assert(s->vaddr%4096==0 && s->vaddr>=APP_VA_BASE && s->vaddr<APP_VA_BASE+32768 && s->memsz && s->filesz<=s->memsz);
        assert(s->offset<=n && s->filesz<=n-s->offset && s->memsz<=APP_VA_BASE+32768-s->vaddr);
        uint64_t stop=s->vaddr+((s->memsz+4095)/4096)*4096;assert(stop<=APP_VA_BASE+32768);
        if (i) { const struct app_segment *prior=&p->segments[i-1];assert(prior->vaddr+((prior->memsz+4095)/4096)*4096<=s->vaddr); }
        if (stop>end) { end=stop; }if ((s->flags&1) && p->entry>=s->vaddr && p->entry-s->vaddr<s->filesz) { executable=true; }
    }assert(executable && end==p->end);
}
static void mutation(void)
{
    genesis();uint32_t random=0x93284717;
    for (unsigned i=0;i<25000;++i) {
        memcpy(bytes,seed,1024);random=random*1664525u+1013904223u;unsigned at=random%256;
        random=random*1664525u+1013904223u;bytes[at]^=(unsigned char)(1u<<(random%8));
        size_t n=(i%3) ? 1024 : random%1050;struct app_image_plan p,old;memset(&p,0xa5,sizeof(p));memcpy(&old,&p,sizeof(p));
        enum app_elf_result r=app_elf_plan(bytes,n,&p);
        if (r==APP_ELF_OK) { oracle(bytes,n,&p); }else { assert(!memcmp(&p,&old,sizeof(p))); }
    }
    puts("PASS: 25000 deterministic header/program mutations and truncated lengths; independent accepted-plan invariants and failure-output preservation");
}
int main(int argc,char **argv)
{
    cases();mutation();
    for (int i=1;i<argc;++i) {
        FILE *f=fopen(argv[i],"rb");assert(f);assert(!fseek(f,0,SEEK_END));long size=ftell(f);assert(size>=64 && size<=APP_ELF_MAX_BYTES);rewind(f);
        assert(fread(bytes,1,(size_t)size,f)==(size_t)size);fclose(f);struct app_image_plan plan;
        assert(app_elf_plan(bytes,(size_t)size,&plan)==APP_ELF_OK);oracle(bytes,(size_t)size,&plan);
        printf("PASS: actual user ELF %s, bytes=%ld segments=%u\n",argv[i],size,plan.count);
    }return 0;
}
