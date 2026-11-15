#ifndef GPT_R_VGA_GRAPHICS_H
#define GPT_R_VGA_GRAPHICS_H

enum {
    VGA_GRAPHICS_WIDTH = 320,
    VGA_GRAPHICS_HEIGHT = 200,
    VGA_GLYPH_SIZE = 8,
    VGA_LINE_HEIGHT = 11
};

void vga_graphics_splash(void);
void vga_graphics_init(void);
void vga_graphics_put(unsigned int cell, unsigned short value);
unsigned short vga_graphics_get(unsigned int cell);
void vga_graphics_cursor(unsigned int cell);
void vga_graphics_blink(void);

#endif
