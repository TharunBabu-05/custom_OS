#include "framebuffer.h"
#include "mmu.h"
#include "host_assert.h"
#include <stdio.h>
#include <string.h>

static void discovery(const char *path,enum fb_result expected)
{
    static unsigned char dtb[2*1024*1024];
    FILE *file=fopen(path,"rb");
    assert(file);
    size_t bytes=fread(dtb,1,sizeof(dtb),file);
    assert(bytes>40 && fgetc(file)==EOF && !ferror(file));
    assert(!fclose(file));
    struct memory_map map;
    struct fb_mode mode={0};
    assert(fdt_read_memory(dtb,bytes,&map));
    assert(fb_discover(dtb,bytes,&mode)==expected);
    if (expected!=FB_READY) { assert(!mode.size && !map.framebuffer.size);return; }
    assert(mode.base==0x46000000 && mode.width==320 && mode.height==192 && mode.stride==1288);
    assert(fb_claim(&mode,&map,false) && map.framebuffer.base==mode.base && map.framebuffer.size==249856);
    assert(!fb_claim(&mode,&map,false)); /* Repeated claim retains the initial reservation. */
    struct fb_mode unchanged={.width=7};
    assert(fb_discover(dtb,39,&unchanged)==FB_HANDOFF && unchanged.width==7);
}

static void pi_firmware_gap(void)
{
    const struct fb_mode mode={0x3f800000,3840*1080,1920,1080,3840,FB_RGB565};
    struct memory_map map={.ram={{0,0x3f800000},{0x40000000,0xc0000000}},.ram_count=2};
    assert(memory_reserve(&map,0,0x40000000,"dynamic reservation window (conservative)"));
    assert(!fb_claim(&mode,&map,false));
    struct memory_map conflict=map;
    assert(memory_reserve(&conflict,mode.base+PAGE_SIZE,PAGE_SIZE,"DTB"));
    assert(!fb_claim(&mode,&conflict,true));
    assert(fb_claim(&mode,&map,true));
    assert(map.reserved_count==2);
    struct mmu_layout layout={
        .text_start=0x80000,.text_end=0x82000,.rodata_end=0x84000,.kernel_end=0x111000,
        .guard=0x100000,.stack_bottom=0x101000,.stack_top=0x111000,.tables=0x85000,
        .dtb=0x200000,.dtb_bytes=0x10000
    };
    struct mmu_plan plan;
    static struct mmu_tables tables;
    struct mmu_translation pixel;
    assert(mmu_plan_make(&map,MMU_PI5,&layout,&plan)==MMU_OK);
    assert(mmu_tables_build(&tables,layout.tables,&plan)==MMU_OK);
    assert(mmu_lookup(&tables,mode.base,&pixel) && pixel.physical==mode.base && pixel.kind==MMU_RW);
    assert(!mmu_lookup(&tables,mode.base+map.framebuffer.size,&pixel));
    struct fb_mode bad=mode;
    bad.base-=PAGE_SIZE;assert(!fb_claim(&bad,&conflict,true));
    bad.base=0x41000000;assert(!fb_claim(&bad,&conflict,true));
}

static uint32_t pixel_at(const unsigned char *pixels,unsigned stride,unsigned x,unsigned y,unsigned bytes)
{
    uint32_t pixel=0;
    memcpy(&pixel,pixels+y*stride+x*bytes,bytes);
    return pixel;
}

static void render(enum fb_format format,uint32_t red,uint32_t green,uint32_t blue,uint32_t white)
{
    _Alignas(4) unsigned char data[1792];
    memset(data,0xa5,sizeof(data));
    unsigned char *pixels=data+8;
    unsigned bytes=format==FB_RGB565 ? 2 : 4;
    const unsigned stride=24*bytes+8;
    struct fb_mode mode={0x46000000,stride*16,24,16,stride,format};
    struct fb_terminal terminal;
    assert(!fb_terminal_attach(&terminal,&mode,pixels+1));
    assert(fb_terminal_attach(&terminal,&mode,pixels));
    fb_terminal_pattern(&terminal);
    assert(pixel_at(pixels,stride,0,0,bytes)==red);
    assert(pixel_at(pixels,stride,7,1,bytes)==green);
    assert(pixel_at(pixels,stride,13,2,bytes)==blue);
    assert(pixel_at(pixels,stride,23,15,bytes)==white);
    fb_terminal_clear(&terminal);
    fb_terminal_putc(&terminal,'A');
    assert(pixel_at(pixels,stride,2,0,bytes)==white);
    fb_terminal_putc(&terminal,'\n');
    fb_terminal_putc(&terminal,'B');
    unsigned char last[32];
    memcpy(last,pixels+stride*8,8*bytes);
    fb_terminal_putc(&terminal,'\n');
    assert(!memcmp(last,pixels,8*bytes) && terminal.x==0 && terminal.y==1);
    fb_terminal_putc(&terminal,'A');fb_terminal_putc(&terminal,'B');fb_terminal_putc(&terminal,'C');
    assert(terminal.wrap && terminal.x==2);
    fb_terminal_putc(&terminal,'\b');assert(!terminal.wrap && terminal.x==2);
    fb_terminal_putc(&terminal,'\r');assert(terminal.x==0);
    fb_terminal_putc(&terminal,'\t');assert(terminal.x==1 && terminal.y==1);
    for (unsigned y=0;y<16;++y) {
        for (unsigned x=24*bytes;x<stride;++x) { assert(pixels[y*stride+x]==0xa5); }
    }
    for (unsigned i=0;i<8;++i) { assert(data[i]==0xa5 && pixels[mode.size+i]==0xa5); }
    mode.stride=24*bytes-4;assert(!fb_mode_valid(&mode));
    mode.stride=stride;--mode.size;assert(!fb_mode_valid(&mode));
}

int main(int argc,char **argv)
{
    assert(argc==4);
    discovery(argv[1],FB_ABSENT);
    discovery(argv[2],FB_READY);
    discovery(argv[3],FB_GEOMETRY);
    pi_firmware_gap();
    render(FB_RGB565,0xf800,0x07e0,0x001f,0xffff);
    render(FB_ARGB8888,0xffff0000,0xff00ff00,0xff0000ff,0xffffffff);
    render(FB_XBGR8888,0xff0000ff,0xff00ff00,0xffff0000,0xffffffff);
    puts("PASS: framebuffer discovery, Pi scanout gap mapping, format/stride bounds, text editing, wrap and scroll.");
    return 0;
}
