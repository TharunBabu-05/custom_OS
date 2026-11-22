#ifndef OS_FRAMEBUFFER_H
#define OS_FRAMEBUFFER_H
#include "memory.h"

enum fb_format { FB_RGB565,FB_XRGB8888,FB_ARGB8888,FB_XBGR8888,FB_ABGR8888 };
enum fb_result { FB_ABSENT,FB_HANDOFF,FB_GEOMETRY,FB_MEMORY,FB_READY };
struct fb_mode {
    uint64_t base,size;
    uint32_t width,height,stride;
    enum fb_format format;
};
struct fb_terminal {
    struct fb_mode mode;
    volatile unsigned char *pixels;
    unsigned columns,rows,x,y,scale;
    bool wrap;
};

bool fb_mode_valid(const struct fb_mode *);
const char *fb_format_name(enum fb_format);
enum fb_result fb_discover(const void *dtb,size_t bytes,struct fb_mode *);
bool fb_claim(const struct fb_mode *,struct memory_map *,bool pi5);
bool fb_terminal_attach(struct fb_terminal *,const struct fb_mode *,volatile void *pixels);
void fb_terminal_pattern(struct fb_terminal *);
void fb_terminal_clear(struct fb_terminal *);
void fb_terminal_putc(struct fb_terminal *,char);
void framebuffer_boot_init(const void *dtb,size_t bytes,struct memory_map *);
bool framebuffer_command(const char *);
void framebuffer_console_putc(char);
void framebuffer_console_clear(void);
#endif
