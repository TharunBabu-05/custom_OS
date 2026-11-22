#include "framebuffer.h"
#include "font8x8.h"

enum { GLYPH=8, COLUMNS=80, ROWS=25 };

static unsigned pixel_bytes(const struct fb_terminal *terminal)
{ return terminal->mode.format==FB_RGB565 ? 2 : 4; }

static uint32_t pixel_color(const struct fb_terminal *terminal,uint32_t rgb)
{
    if (terminal->mode.format==FB_RGB565) {
        return ((rgb>>8)&0xf800)|((rgb>>5)&0x07e0)|((rgb>>3)&0x001f);
    }
    if (terminal->mode.format==FB_XBGR8888 || terminal->mode.format==FB_ABGR8888) {
        rgb=((rgb&0xff)<<16)|(rgb&0xff00)|((rgb>>16)&0xff);
    }
    return 0xff000000|rgb;
}

static void fill(struct fb_terminal *terminal,unsigned first_x,unsigned first_y,unsigned width,unsigned height,uint32_t rgb)
{
    unsigned bytes=pixel_bytes(terminal);
    uint32_t color=pixel_color(terminal,rgb);
    for (unsigned y=0;y<height;++y) {
        volatile unsigned char *row=terminal->pixels+(size_t)(first_y+y)*terminal->mode.stride+first_x*bytes;
        if (bytes==2) {
            volatile uint16_t *p=(volatile uint16_t *)row;
            for (unsigned x=0;x<width;++x) { p[x]=(uint16_t)color; }
        } else {
            volatile uint32_t *p=(volatile uint32_t *)row;
            for (unsigned x=0;x<width;++x) { p[x]=color; }
        }
    }
}

bool fb_terminal_attach(struct fb_terminal *terminal,const struct fb_mode *mode,volatile void *pixels)
{
    if (!terminal || !fb_mode_valid(mode) || !pixels || ((uintptr_t)pixels&3) ||
        mode->size-1>UINTPTR_MAX-(uintptr_t)pixels) { return false; }
    unsigned scale=mode->width>=1280 && mode->height>=400 ? 2 : 1;
    unsigned columns=mode->width/(GLYPH*scale),rows=mode->height/(GLYPH*scale);
    *terminal=(struct fb_terminal){.mode=*mode,.pixels=pixels,.scale=scale,
        .columns=columns<COLUMNS ? columns : COLUMNS,.rows=rows<ROWS ? rows : ROWS};
    return true;
}

void fb_terminal_pattern(struct fb_terminal *terminal)
{
    static const uint32_t bars[]={0xff0000,0x00ff00,0x0000ff,0xffffff};
    for (unsigned i=0;i<sizeof(bars)/sizeof(bars[0]);++i) {
        unsigned left=i*terminal->mode.width/4,right=(i+1)*terminal->mode.width/4;
        fill(terminal,left,0,right-left,terminal->mode.height,bars[i]);
    }
}

void fb_terminal_clear(struct fb_terminal *terminal)
{
    fill(terminal,0,0,terminal->mode.width,terminal->mode.height,0);
    terminal->x=terminal->y=0;
    terminal->wrap=false;
}

static void newline(struct fb_terminal *terminal)
{
    terminal->x=0;
    terminal->wrap=false;
    if (terminal->y+1<terminal->rows) { ++terminal->y;return; }
    unsigned rows=GLYPH*terminal->scale,width=terminal->columns*rows,bytes=pixel_bytes(terminal);
    unsigned lines=(terminal->rows-1)*rows;
    for (unsigned y=0;y<lines;++y) {
        volatile unsigned char *dst=terminal->pixels+(size_t)y*terminal->mode.stride;
        volatile unsigned char *src=dst+(size_t)rows*terminal->mode.stride;
        if (bytes==2) {
            volatile uint16_t *to=(volatile uint16_t *)dst,*from=(volatile uint16_t *)src;
            for (unsigned x=0;x<width;++x) { to[x]=from[x]; }
        } else {
            volatile uint32_t *to=(volatile uint32_t *)dst,*from=(volatile uint32_t *)src;
            for (unsigned x=0;x<width;++x) { to[x]=from[x]; }
        }
    }
    fill(terminal,0,lines,width,rows,0);
}

static void glyph(struct fb_terminal *terminal,unsigned char ch)
{
    if (terminal->wrap) { newline(terminal); }
    unsigned scale=terminal->scale,tile=GLYPH*scale;
    const unsigned char *bits=font[ch-32];
    unsigned x=terminal->x*tile,y=terminal->y*tile;
    for (unsigned row=0;row<GLYPH;++row) {
        for (unsigned column=0;column<GLYPH;++column) {
            fill(terminal,x+column*scale,y+row*scale,scale,scale,(bits[row]>>column)&1 ? 0xffffff : 0);
        }
    }
    terminal->wrap=terminal->x+1==terminal->columns;
    if (!terminal->wrap) { ++terminal->x; }
}

void fb_terminal_putc(struct fb_terminal *terminal,char ch)
{
    if (ch=='\n') { newline(terminal); }
    else if (ch=='\r') { terminal->x=0;terminal->wrap=false; }
    else if (ch=='\b') {
        if (terminal->wrap) { terminal->wrap=false; }
        else if (terminal->x) { --terminal->x; }
    } else if (ch=='\t') {
        if (terminal->wrap) { newline(terminal); }
        unsigned spaces=4-(terminal->x&3);
        while (spaces--) { glyph(terminal,' '); }
    } else if (ch>=32 && ch<=126) { glyph(terminal,(unsigned char)ch); }
}
