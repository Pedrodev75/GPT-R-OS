#ifndef GPT_R_VGA_GRAPHICS_H
#define GPT_R_VGA_GRAPHICS_H

enum {
    VGA_GRAPHICS_WIDTH = 1024,
    VGA_GRAPHICS_HEIGHT = 768,
    VGA_GLYPH_SIZE = 8,
    VGA_ACCENT_ROWS = 2,
    VGA_GLYPH_SCALE = 2,
    VGA_OVERLAY_CELL_WIDTH = VGA_GLYPH_SIZE * VGA_GLYPH_SCALE,
    VGA_OVERLAY_GLYPH_HEIGHT = (VGA_GLYPH_SIZE + VGA_ACCENT_ROWS) * VGA_GLYPH_SCALE,
    VGA_CELL_WIDTH = VGA_OVERLAY_CELL_WIDTH,
    VGA_LINE_HEIGHT = VGA_OVERLAY_GLYPH_HEIGHT + 2
};

void vga_graphics_init(void);
void vga_graphics_put(unsigned int cell, unsigned short value);
unsigned short vga_graphics_get(unsigned int cell);
void vga_graphics_cursor(unsigned int cell);
void vga_graphics_blink(void);
struct keyboard_state;
void vga_graphics_demo(struct keyboard_state* keyboard);
void vga_graphics_begin_overlay(void);
void vga_graphics_place_console(unsigned int x, unsigned int y);
void vga_graphics_snapshot_console(void);
void vga_graphics_fill(unsigned int x, unsigned int y, unsigned int width,
    unsigned int height, unsigned int rgb);
void vga_graphics_overlay_glyph(unsigned int x, unsigned int y,
    unsigned char glyph, unsigned int rgb);
void vga_graphics_overlay_cell(unsigned int x, unsigned int y,
    unsigned char glyph, unsigned int foreground, unsigned int background);
void vga_graphics_restore_console(void);

#endif
