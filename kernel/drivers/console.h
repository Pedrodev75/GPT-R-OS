#ifndef GPT_R_CONSOLE_H
#define GPT_R_CONSOLE_H

#ifdef GPT_R_GRAPHICS
#include "vga_graphics.h"
enum {
    CONSOLE_WIDTH = VGA_GRAPHICS_WIDTH / VGA_CELL_WIDTH,
    CONSOLE_HEIGHT = VGA_GRAPHICS_HEIGHT / VGA_LINE_HEIGHT
};
#define console_put vga_graphics_put
#define console_get vga_graphics_get
#else
enum { CONSOLE_WIDTH = 80, CONSOLE_HEIGHT = 25 };

static inline void console_put(unsigned int cell, unsigned short value)
{
    ((volatile unsigned short*)0xb8000)[cell] = value;
}

static inline unsigned short console_get(unsigned int cell)
{
    return ((volatile unsigned short*)0xb8000)[cell];
}
#endif

#endif
