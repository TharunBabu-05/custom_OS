#include "framebuffer.h"
#include "console.h"
#include "mmu.h"

static struct fb_mode mode;
static struct fb_terminal terminal;
static enum fb_result status;
static bool mirrored;

void framebuffer_boot_init(const void *dtb,size_t bytes,struct memory_map *map)
{
#ifdef OUROS_PI5
    const bool pi5=true;
#else
    const bool pi5=false;
#endif
    status=fb_discover(dtb,bytes,&mode);
    if (status==FB_READY && !fb_claim(&mode,map,pi5)) { status=FB_MEMORY; }
}

void framebuffer_console_putc(char c)
{ if (mirrored) { fb_terminal_putc(&terminal,c); } }

void framebuffer_console_clear(void)
{ if (mirrored) { fb_terminal_clear(&terminal); } }

bool framebuffer_console_start(void)
{
    if (mirrored) { return true; }
    if (status!=FB_READY || !mmu_dma_buffer(mode.base,(size_t)mode.size) || !mmu_enabled() ||
        !fb_terminal_attach(&terminal,&mode,(void *)(uintptr_t)mode.base)) { return false; }
    fb_terminal_clear(&terminal);
    mirrored=true;
    return true;
}

static bool same(const char *a,const char *b)
{ while (*a && *a==*b) { ++a;++b; }return *a==*b; }

static void information(void)
{
    switch (status) {
    case FB_ABSENT:console_write("Framebuffer: no enabled firmware simple-framebuffer.\n");return;
    case FB_HANDOFF:console_write("Framebuffer: unsupported device-tree handoff.\n");return;
    case FB_GEOMETRY:console_write("Framebuffer: unsupported format or unsafe geometry.\n");return;
    case FB_MEMORY:console_write("Framebuffer: unsafe or conflicting memory range.\n");return;
    case FB_READY:break;
    }
    console_write("Framebuffer: firmware ");console_u64(mode.width);console_putc('x');console_u64(mode.height);
    console_putc(' ');console_write(fb_format_name(mode.format));
    console_write(" stride=");console_u64(mode.stride);
    console_write(" base=");console_hex(mode.base);console_write(" bytes=");console_u64(mode.size);
    console_write(mirrored ? " mirror=on\n" : " mirror=off\n");
}

bool framebuffer_command(const char *line)
{
    if (same(line,"fbinfo")) { information();return true; }
    if (same(line,"fboff")) {
        mirrored=false;console_write("HDMI console mirror off.\n");return true;
    }
    bool pattern=same(line,"fbtest");
    if (!pattern && !same(line,"fbon")) { return false; }
    if (status!=FB_READY) { information();return true; }
    if (!mmu_enabled() || !mmu_dma_buffer(mode.base,(size_t)mode.size)) {
        console_write("Framebuffer requires the verified MMU mapping. Use mmuinit.\n");return true;
    }
    if (pattern) {
        if (!fb_terminal_attach(&terminal,&mode,(void *)(uintptr_t)mode.base)) { return true; }
        mirrored=false;
        fb_terminal_pattern(&terminal);
        __asm__ volatile("dsb oshst" ::: "memory");
        console_write("Framebuffer test drawn. Look for red, green, blue and white bars.\n");
    } else if (framebuffer_console_start()) {
        console_write("OurOS HDMI console mirror on.\n");
    }
    return true;
}
