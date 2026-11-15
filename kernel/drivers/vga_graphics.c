#include "vga_graphics.h"

enum { WIDTH = 320, HEIGHT = 200, GLYPH_SIZE = 8 };
static volatile unsigned char *const video = (volatile unsigned char *)0xa0000;
/* boot.S copies the BIOS 8x8 font here before entering long mode. */
static const unsigned char *const font = (const unsigned char *)0x6000;

static void rect(unsigned int x, unsigned int y, unsigned int width,
                 unsigned int height, unsigned char color)
{
    if (x >= WIDTH || y >= HEIGHT)
        return;
    if (width > WIDTH - x)
        width = WIDTH - x;
    if (height > HEIGHT - y)
        height = HEIGHT - y;

    for (unsigned int row = 0; row < height; ++row)
        for (unsigned int col = 0; col < width; ++col)
            video[(y + row) * WIDTH + x + col] = color;
}

static void draw_text(unsigned int x, unsigned int y, const char *text,
                      unsigned int scale, unsigned char color)
{
    while (*text) {
        const unsigned char *glyph = font + ((unsigned char)*text++ & 0x7f) * GLYPH_SIZE;
        for (unsigned int row = 0; row < GLYPH_SIZE; ++row)
            for (unsigned int col = 0; col < GLYPH_SIZE; ++col)
                if (glyph[row] & (0x80 >> col))
                    rect(x + col * scale, y + row * scale, scale, scale, color);
        x += GLYPH_SIZE * scale;
    }
}

void vga_graphics_splash(void)
{
    rect(0, 0, WIDTH, HEIGHT, 1);
    rect(18, 35, 284, 130, 9);
    rect(20, 37, 280, 126, 0);
    rect(20, 37, 280, 4, 11);
    draw_text(96, 68, "GPT-R OS", 2, 15);
    draw_text(84, 112, "VGA 320x200  256 cores", 1, 11);
    for (unsigned int i = 0; i < 16; ++i)
        rect(64 + i * 12, 135, 12, 9, (unsigned char)i);
}
