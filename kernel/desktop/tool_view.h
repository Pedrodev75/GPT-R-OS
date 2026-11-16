#ifndef GPT_R_TOOL_VIEW_H
#define GPT_R_TOOL_VIEW_H

#ifdef GPT_R_GRAPHICS
#include "../drivers/vga_graphics.h"

enum {
    TOOL_BG = 0x0c1728, TOOL_PANEL = 0x15263c, TOOL_ROW = 0x122136,
    TOOL_BORDER = 0x324962, TOOL_HILITE = 0x234665,
    TOOL_TEXT = 0xe5edf8, TOOL_MUTED = 0x9db2cb,
    TOOL_BLUE = 0x65daf1, TOOL_GOLD = 0xf4c477, TOOL_GREEN = 0x81e2b1,
    TOOL_RED = 0xffaaa8, TOOL_CELL = VGA_OVERLAY_CELL_WIDTH
};

static inline void tool_label(unsigned int x, unsigned int y, const char *text, unsigned int color)
{
    for (; *text; ++text, x += TOOL_CELL)
        vga_graphics_overlay_glyph(x, y, (unsigned char)*text, color);
}

static inline void tool_number(unsigned int x, unsigned int y, unsigned int number, unsigned int color)
{
    char digits[10];
    unsigned int count = 0;
    do {
        digits[count++] = '0' + number % 10;
        number /= 10;
    } while (number);
    while (count) {
        vga_graphics_overlay_glyph(x, y, digits[--count], color);
        x += TOOL_CELL;
    }
}

static inline void tool_button(unsigned int x, unsigned int y, unsigned int width,
    const char *label, unsigned int color, int enabled)
{
    vga_graphics_fill(x, y, width, 33, enabled ? TOOL_HILITE : TOOL_PANEL);
    vga_graphics_fill(x, y + 32, width, 1, TOOL_BORDER);
    tool_label(x + 10, y + 5, label, enabled ? color : TOOL_MUTED);
}

static inline int tool_hit(unsigned int x, unsigned int y, unsigned int left,
    unsigned int top, unsigned int width, unsigned int height)
{
    return x >= left && x - left < width && y >= top && y - top < height;
}
#endif
#endif
