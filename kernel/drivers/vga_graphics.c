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

enum { COLUMNS = WIDTH / GLYPH_SIZE, CELL_COUNT = COLUMNS * (HEIGHT / GLYPH_SIZE) };
static unsigned short cells[CELL_COUNT];
/* Zero means hidden, including before the shell starts. */
static unsigned int cursor_slot;
static unsigned int cursor_visible;
static unsigned char glyph_row(unsigned char ch, unsigned int row)
{
    static const unsigned char accents[][2] = {
        {0x0c, 0x18}, {0x30, 0x18}, {0x00, 0x32}, {0x10, 0x28}
    };
    static const char vowels[] = "aeiouAEIOU";

    if (ch == 0x80 || ch == 0x81)
        return row == 7 ? 0x18 : font[(ch == 0x80 ? 'c' : 'C') * 8 + row];
    if (ch >= 0xf0 && ch <= 0xf3)
        return row < 2 ? accents[ch - 0xf0][row] : 0;
    if (ch >= 0x82 && ch < 0xaa) {
        const unsigned int index = ch - 0x82;
        if (row < 2)
            return accents[index / 10][row];
        return font[(unsigned int)vowels[index % 10] * 8 + row - 1];
    }
    return font[(ch < 128 ? ch : '?') * 8 + row];
}

static void render_cell(unsigned int cell, unsigned short value)
{
    const unsigned char foreground = (value >> 8) & 15;
    const unsigned char background = (value >> 12) & 7;
    const unsigned int offset = (cell / COLUMNS) * 8 * WIDTH + (cell % COLUMNS) * 8;
    for (unsigned int y = 0; y < 8; ++y) {
        const unsigned char bits = glyph_row(value, y);
        for (unsigned int x = 0; x < 8; ++x)
            video[offset + y * WIDTH + x] = (bits & (0x80 >> x)) ? foreground : background;
    }
}

static void redraw_cursor(void)
{
    const unsigned int cell = cursor_slot - 1;
    render_cell(cell, cells[cell]);
    if (cursor_visible)
        rect(cell % COLUMNS * 8, cell / COLUMNS * 8 + 7, 8, 1, 15);
}

void vga_graphics_put(unsigned int cell, unsigned short value)
{
    if (cell < CELL_COUNT) {
        cells[cell] = value;
        render_cell(cell, value);
    }
}

unsigned short vga_graphics_get(unsigned int cell)
{
    return cells[cell];
}

void vga_graphics_cursor(unsigned int cell)
{
    unsigned long long flags;
    __asm__ volatile ("pushfq; pop %0; cli" : "=r"(flags) : : "memory");
    if (cursor_slot)
        render_cell(cursor_slot - 1, cells[cursor_slot - 1]);
    cursor_slot = cell < CELL_COUNT ? cell + 1 : 0;
    cursor_visible = 1;
    if (cursor_slot)
        redraw_cursor();
    if (flags & 0x200)
        __asm__ volatile ("sti" : : : "memory");
}

void vga_graphics_blink(void)
{
    if (cursor_slot) {
        cursor_visible ^= 1;
        redraw_cursor();
    }
}

void vga_graphics_init(void)
{
    cursor_slot = 0;
    cursor_visible = 0;
}
