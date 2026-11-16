#include "console.h"
#include "keyboard.h"
#include "timer.h"

enum {
    WIDTH = VGA_GRAPHICS_WIDTH,
    HEIGHT = VGA_GRAPHICS_HEIGHT,
    GLYPH_SIZE = VGA_GLYPH_SIZE
};
static volatile unsigned char* video;
static unsigned int pitch;
static unsigned int palette[256];

static void init_palette(void)
{
    static const unsigned int base[] = {
        0x000000, 0x0000aa, 0x00aa00, 0x00aaaa,
        0xaa0000, 0xaa00aa, 0xaa5500, 0xaaaaaa,
        0x555555, 0x5555ff, 0x55ff55, 0x55ffff,
        0xff5555, 0xff55ff, 0xffff55, 0xffffff
    };
    for (unsigned int i = 0; i < 16; ++i)
        palette[i] = base[i];
    for (unsigned int i = 0; i < 216; ++i) {
        const unsigned int r = (i / 36) * 51;
        const unsigned int g = (i / 6 % 6) * 51;
        const unsigned int b = (i % 6) * 51;
        palette[16 + i] = (r << 16) | (g << 8) | b;
    }
    for (unsigned int i = 0; i < 24; ++i)
        palette[232 + i] = (i * 255 / 23) * 0x010101;
}

static void put_pixel(volatile unsigned char* pixel, unsigned int rgb)
{
    pixel[0] = rgb;
    pixel[1] = rgb >> 8;
    pixel[2] = rgb >> 16;
}
/* boot.S copies the BIOS 8x8 font here before entering long mode. */
static const unsigned char* const font = (const unsigned char*)0x6000;

void vga_graphics_fill(unsigned int x, unsigned int y, unsigned int width,
    unsigned int height, unsigned int rgb)
{
    if (x >= WIDTH || y >= HEIGHT)
        return;
    if (width > WIDTH - x)
        width = WIDTH - x;
    if (height > HEIGHT - y)
        height = HEIGHT - y;

    for (unsigned int row = 0; row < height; ++row) {
        volatile unsigned char* pixel = video + (y + row) * pitch + x * 3;
        for (unsigned int col = 0; col < width; ++col, pixel += 3)
            put_pixel(pixel, rgb);
    }
}

static void rect(unsigned int x, unsigned int y, unsigned int width,
    unsigned int height, unsigned char color)
{
    vga_graphics_fill(x, y, width, height, palette[color]);
}

static void draw_text(unsigned int x, unsigned int y, const char* text,
    unsigned int scale, unsigned char color)
{
    while (*text) {
        const unsigned char* glyph = font + ((unsigned char)*text++ & 0x7f) * GLYPH_SIZE;
        for (unsigned int row = 0; row < GLYPH_SIZE; ++row)
            for (unsigned int col = 0; col < GLYPH_SIZE; ++col)
                if (glyph[row] & (0x80 >> col))
                    rect(x + col * scale, y + row * scale, scale, scale, color);
        x += GLYPH_SIZE * scale;
    }
}

enum { COLUMNS = CONSOLE_WIDTH, CELL_COUNT = COLUMNS * CONSOLE_HEIGHT };
static unsigned short cells[CELL_COUNT];
/* Zero means hidden, including before the shell starts. */
static unsigned int cursor_slot;
static unsigned int cursor_visible;
static volatile int overlay_active;
/* Seven rows per glyph, five pixels packed into each row. */
static const unsigned char terminal_font['z' - '0' + 1][7] = {
    ['A' - '0'] = { 0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11 },
    ['B' - '0'] = { 0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e },
    ['C' - '0'] = { 0x0e, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0e },
    ['D' - '0'] = { 0x1e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1e },
    ['E' - '0'] = { 0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f },
    ['F' - '0'] = { 0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10 },
    ['G' - '0'] = { 0x0e, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0f },
    ['H' - '0'] = { 0x11, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11 },
    ['I' - '0'] = { 0x0e, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0e },
    ['J' - '0'] = { 0x07, 0x02, 0x02, 0x02, 0x12, 0x12, 0x0c },
    ['K' - '0'] = { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 },
    ['L' - '0'] = { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f },
    ['M' - '0'] = { 0x11, 0x1b, 0x15, 0x15, 0x11, 0x11, 0x11 },
    ['N' - '0'] = { 0x11, 0x19, 0x19, 0x15, 0x13, 0x13, 0x11 },
    ['O' - '0'] = { 0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e },
    ['P' - '0'] = { 0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10 },
    ['Q' - '0'] = { 0x0e, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0d },
    ['R' - '0'] = { 0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11 },
    ['S' - '0'] = { 0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e },
    ['T' - '0'] = { 0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 },
    ['U' - '0'] = { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e },
    ['V' - '0'] = { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0a, 0x04 },
    ['W' - '0'] = { 0x11, 0x11, 0x11, 0x15, 0x15, 0x1b, 0x11 },
    ['X' - '0'] = { 0x11, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x11 },
    ['Y' - '0'] = { 0x11, 0x11, 0x0a, 0x04, 0x04, 0x04, 0x04 },
    ['Z' - '0'] = { 0x1f, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1f },
    ['a' - '0'] = { 0x00, 0x00, 0x0e, 0x01, 0x0f, 0x11, 0x0f },
    ['b' - '0'] = { 0x10, 0x10, 0x1e, 0x11, 0x11, 0x11, 0x1e },
    ['c' - '0'] = { 0x00, 0x00, 0x0f, 0x10, 0x10, 0x10, 0x0f },
    ['d' - '0'] = { 0x01, 0x01, 0x0f, 0x11, 0x11, 0x11, 0x0f },
    ['e' - '0'] = { 0x00, 0x00, 0x0e, 0x11, 0x1f, 0x10, 0x0f },
    ['f' - '0'] = { 0x06, 0x09, 0x09, 0x1c, 0x08, 0x08, 0x08 },
    ['g' - '0'] = { 0x00, 0x00, 0x0f, 0x11, 0x11, 0x0f, 0x01 },
    ['h' - '0'] = { 0x10, 0x10, 0x1e, 0x11, 0x11, 0x11, 0x11 },
    ['i' - '0'] = { 0x04, 0x00, 0x0c, 0x04, 0x04, 0x04, 0x0e },
    ['j' - '0'] = { 0x02, 0x00, 0x06, 0x02, 0x02, 0x02, 0x12 },
    ['k' - '0'] = { 0x10, 0x10, 0x11, 0x12, 0x1c, 0x12, 0x11 },
    ['l' - '0'] = { 0x0c, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0e },
    ['m' - '0'] = { 0x00, 0x00, 0x1a, 0x15, 0x15, 0x15, 0x15 },
    ['n' - '0'] = { 0x00, 0x00, 0x1e, 0x11, 0x11, 0x11, 0x11 },
    ['o' - '0'] = { 0x00, 0x00, 0x0e, 0x11, 0x11, 0x11, 0x0e },
    ['p' - '0'] = { 0x00, 0x00, 0x1e, 0x11, 0x11, 0x11, 0x1e },
    ['q' - '0'] = { 0x00, 0x00, 0x0f, 0x11, 0x11, 0x0f, 0x01 },
    ['r' - '0'] = { 0x00, 0x00, 0x16, 0x19, 0x10, 0x10, 0x10 },
    ['s' - '0'] = { 0x00, 0x00, 0x0f, 0x10, 0x0e, 0x01, 0x1e },
    ['t' - '0'] = { 0x08, 0x08, 0x1e, 0x08, 0x08, 0x09, 0x06 },
    ['u' - '0'] = { 0x00, 0x00, 0x11, 0x11, 0x11, 0x11, 0x0f },
    ['v' - '0'] = { 0x00, 0x00, 0x11, 0x11, 0x11, 0x0a, 0x04 },
    ['w' - '0'] = { 0x00, 0x00, 0x11, 0x11, 0x15, 0x15, 0x0a },
    ['x' - '0'] = { 0x00, 0x00, 0x11, 0x0a, 0x04, 0x0a, 0x11 },
    ['y' - '0'] = { 0x00, 0x00, 0x11, 0x11, 0x11, 0x0f, 0x01 },
    ['z' - '0'] = { 0x00, 0x00, 0x1f, 0x02, 0x04, 0x08, 0x1f },
    ['0' - '0'] = { 0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e },
    ['1' - '0'] = { 0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e },
    ['2' - '0'] = { 0x0e, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1f },
    ['3' - '0'] = { 0x1e, 0x01, 0x01, 0x06, 0x01, 0x01, 0x1e },
    ['4' - '0'] = { 0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02 },
    ['5' - '0'] = { 0x1f, 0x10, 0x10, 0x1e, 0x01, 0x01, 0x1e },
    ['6' - '0'] = { 0x06, 0x08, 0x10, 0x1e, 0x11, 0x11, 0x0e },
    ['7' - '0'] = { 0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 },
    ['8' - '0'] = { 0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e },
    ['9' - '0'] = { 0x0e, 0x11, 0x11, 0x0f, 0x01, 0x02, 0x0c },
};

static unsigned char letter_row(unsigned char ch, unsigned int row)
{
    if (row == 7) {
        if (ch == 'g' || ch == 'y')
            return 0x38;
        if (ch == 'p')
            return 0x40;
        if (ch == 'j')
            return 0x18;
        return 0;
    }
    return terminal_font[ch - '0'][row] << 2;
}

enum { ACCENT_ROWS = 2, GLYPH_HEIGHT = ACCENT_ROWS + GLYPH_SIZE };

static unsigned char glyph_row(unsigned char ch, unsigned int row)
{
    static const unsigned char accents[][2] = {
        { 0x08, 0x10 },{ 0x20, 0x10 },{ 0x14, 0x28 },{ 0x10, 0x28 }
    };
    static const char vowels[] = "aeiouAEIOU";

    if (row < ACCENT_ROWS) {
        if (ch >= 0x82 && ch < 0xaa)
            return accents[(ch - 0x82) / 10][row];
        if (ch >= 0xf0 && ch <= 0xf3)
            return accents[ch - 0xf0][row];
        return 0;
    }

    row -= ACCENT_ROWS;
    if (ch >= 0xf0 && ch <= 0xf3)
        return 0;
    if (ch == 0x80 || ch == 0x81) {
        const unsigned char base = ch == 0x80 ? 'c' : 'C';
        if (row == 7)
            return 0x18;
        if (row == 6)
            return letter_row(base, row) | 0x08;
        return letter_row(base, row);
    }
    if (ch >= 0x82 && ch < 0xaa) {
        ch = vowels[(ch - 0x82) % 10];
        /* The accent takes the place of the dot on lowercase i. */
        if (ch == 'i' && row == 0)
            return 0;
    }
    if ((ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z') ||
        (ch >= 'a' && ch <= 'z'))
        return letter_row(ch, row);
    return font[(ch < 128 ? ch : '?') * 8 + row];
}

void vga_graphics_overlay_glyph(unsigned int x, unsigned int y,
    unsigned char glyph, unsigned int rgb)
{
    if (x > WIDTH - VGA_OVERLAY_CELL_WIDTH || y > HEIGHT - VGA_OVERLAY_GLYPH_HEIGHT)
        return;
    for (unsigned int row = 0; row < VGA_OVERLAY_GLYPH_HEIGHT; ++row) {
        const unsigned int source_y = (2 * row + 1) * GLYPH_HEIGHT /
            (2 * VGA_OVERLAY_GLYPH_HEIGHT);
        const unsigned char bits = glyph_row(glyph, source_y);
        volatile unsigned char *pixels = video + (y + row) * pitch + x * 3;
        for (unsigned int col = 0; col < VGA_OVERLAY_CELL_WIDTH; ++col) {
            const unsigned int source_x = (2 * col + 1) * GLYPH_SIZE /
                (2 * VGA_OVERLAY_CELL_WIDTH);
            if (bits & (0x80 >> source_x))
                put_pixel(pixels + col * 3, rgb);
        }
    }
}

enum { DISPLAY_GLYPH_HEIGHT = VGA_LINE_HEIGHT - 1 };

static void render_cell(unsigned int cell, unsigned short value)
{
    const unsigned int foreground = palette[(value >> 8) & 15];
    const unsigned int background = palette[(value >> 12) & 7];
    const unsigned int x0 = (cell % COLUMNS) * VGA_CELL_WIDTH;
    const unsigned int y0 = (cell / COLUMNS) * VGA_LINE_HEIGHT;
    volatile unsigned char* line = video + y0 * pitch + x0 * 3;

    /* Sample the original bitmap at the center of each destination pixel. */
    for (unsigned int y = 0; y < DISPLAY_GLYPH_HEIGHT; ++y, line += pitch) {
        const unsigned int source_y = (2 * y + 1) * GLYPH_HEIGHT / (2 * DISPLAY_GLYPH_HEIGHT);
        const unsigned char bits = glyph_row(value, source_y);
        for (unsigned int x = 0; x < VGA_CELL_WIDTH; ++x) {
            const unsigned int source_x = (2 * x + 1) * GLYPH_SIZE / (2 * VGA_CELL_WIDTH);
            const unsigned int rgb = (bits & (0x80 >> source_x)) ? foreground : background;
            put_pixel(line + x * 3, rgb);
        }
    }
    for (unsigned int x = 0; x < VGA_CELL_WIDTH; ++x)
        put_pixel(line + x * 3, background);
}

static void redraw_cursor(void)
{
    const unsigned int cell = cursor_slot - 1;
    render_cell(cell, cells[cell]);
    if (cursor_visible)
        rect(cell % COLUMNS * VGA_CELL_WIDTH,
            cell / COLUMNS * VGA_LINE_HEIGHT + DISPLAY_GLYPH_HEIGHT - 1,
            VGA_CELL_WIDTH, 1, 15);
}

void vga_graphics_put(unsigned int cell, unsigned short value)
{
    if (cell < CELL_COUNT) {
        cells[cell] = value;
        if (!overlay_active)
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

static void clear_console_margins(void)
{
    const unsigned int right = COLUMNS * VGA_CELL_WIDTH;
    const unsigned int bottom = CONSOLE_HEIGHT * VGA_LINE_HEIGHT;
    rect(right, 0, WIDTH - right, bottom, 0);
    rect(0, bottom, WIDTH, HEIGHT - bottom, 0);
}

void vga_graphics_init(void)
{
    /* Firmware mode info and the virtual window established by boot.S. */
    const unsigned int physical = *(const volatile unsigned int*)0x6428;
    pitch = *(const volatile unsigned short*)0x6410;
    video = (volatile unsigned char*)(0x100000000ull + (physical & 0x1fffff));
    init_palette();
    cursor_slot = 0;
    cursor_visible = 0;
    overlay_active = 0;
    clear_console_margins();
}

void vga_graphics_begin_overlay(void)
{
    vga_graphics_cursor(CELL_COUNT);
    overlay_active = 1;
}

void vga_graphics_restore_console(void)
{
    if (!overlay_active)
        return;

    /* Timer IRQ updates the shadow cells while the overlay owns the screen. */
    for (unsigned int cell = 0; cell < CELL_COUNT; ++cell)
        if (cell / COLUMNS != 1)
            render_cell(cell, cells[cell]);

    unsigned long long flags;
    __asm__ volatile ("pushfq; pop %0; cli" : "=r"(flags) : : "memory");
    for (unsigned int cell = COLUMNS; cell < 2 * COLUMNS; ++cell)
        render_cell(cell, cells[cell]);
    overlay_active = 0;
    if (flags & 0x200)
        __asm__ volatile ("sti" : : : "memory");
    clear_console_margins();
}

void vga_graphics_demo(struct keyboard_state* keyboard)
{
    enum { SQUARE = 28, STEP = 4, TOP = 112, BOTTOM = HEIGHT - 36, ANIMATION_TICKS = 12 };
    static const unsigned char colors[] = { 9, 10, 11, 12, 13, 14, 15 };
    unsigned int x = (WIDTH - SQUARE) / 2;
    unsigned int y = (TOP + BOTTOM - SQUARE) / 2;
    unsigned int color = 0;
    unsigned int last = timer_ticks();

    vga_graphics_begin_overlay();
    rect(0, 0, WIDTH, HEIGHT, 0);
    draw_text(24, 20, "GPT-R OS / DEMO", 2, 15);
    for (unsigned int i = 0; i < 256; ++i)
        rect((WIDTH - 512) / 2 + i * 2, 64, 2, 28, i);
    draw_text(24, HEIGHT - 24, "Setas: mover   Esc: voltar", 1, 15);
    rect(x, y, SQUARE, SQUARE, colors[color]);

    for (;;) {
        const unsigned int key = keyboard_poll(keyboard);
        if (key == KEY_ESCAPE)
            break;
        unsigned int next_x = x;
        unsigned int next_y = y;
        if (key == KEY_LEFT && x >= STEP)
            next_x -= STEP;
        if (key == KEY_RIGHT && x + SQUARE + STEP <= WIDTH)
            next_x += STEP;
        if (key == KEY_UP && y >= TOP + STEP)
            next_y -= STEP;
        if (key == KEY_DOWN && y + SQUARE + STEP <= BOTTOM)
            next_y += STEP;

        const unsigned int now = timer_ticks();
        const int animate = now - last >= ANIMATION_TICKS;
        if (animate) {
            color = (color + 1) % sizeof(colors);
            last = now;
        }
        if (x != next_x || y != next_y) {
            rect(x, y, SQUARE, SQUARE, 0);
            x = next_x;
            y = next_y;
            rect(x, y, SQUARE, SQUARE, colors[color]);
        }
        else if (animate) {
            rect(x, y, SQUARE, SQUARE, colors[color]);
        }
        if (!key)
            __asm__ volatile ("hlt" : : : "memory");
    }
    vga_graphics_restore_console();
}
