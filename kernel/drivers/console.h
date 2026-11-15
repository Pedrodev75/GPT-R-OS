#ifndef GPT_R_CONSOLE_H
#define GPT_R_CONSOLE_H

enum { CONSOLE_HEIGHT = 25 };

#ifdef GPT_R_GRAPHICS
#include "vga_graphics.h"
enum { CONSOLE_WIDTH = 40 };
#define console_put vga_graphics_put
#define console_get vga_graphics_get
#else
enum { CONSOLE_WIDTH = 80 };

static inline void console_put(unsigned int cell, unsigned short value)
{
    ((volatile unsigned short *)0xb8000)[cell] = value;
}

static inline unsigned short console_get(unsigned int cell)
{
    return ((volatile unsigned short *)0xb8000)[cell];
}
#endif

#endif
