#ifndef GPT_R_VGA_GRAPHICS_H
#define GPT_R_VGA_GRAPHICS_H

enum {
    VGA_GRAPHICS_WIDTH = 640,
    VGA_GRAPHICS_HEIGHT = 480,
    VGA_GLYPH_SIZE = 8,
    VGA_CELL_WIDTH = 13,
    VGA_LINE_HEIGHT = 19
};

void vga_graphics_init(void);
void vga_graphics_put(unsigned int cell, unsigned short value);
unsigned short vga_graphics_get(unsigned int cell);
void vga_graphics_cursor(unsigned int cell);
void vga_graphics_blink(void);
struct keyboard_state;
void vga_graphics_demo(struct keyboard_state* keyboard);
void vga_graphics_restore_console(void);

#endif
