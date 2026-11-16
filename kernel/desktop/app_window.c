#include "app_window.h"

#ifdef GPT_R_GRAPHICS
#include "../drivers/keyboard.h"
#include "../shell/shell_internal.h"

struct app_console {
    unsigned char cells[APP_WINDOW_ROWS][APP_WINDOW_COLUMNS];
    unsigned char line[APP_WINDOW_COLUMNS * 3];
    unsigned int top, row, col, bytes, capacity, dirty;
    unsigned int codepoint, minimum, continuation;
};

static struct app_console consoles[APP_PROCESS_LIMIT];

static unsigned char *row_cells(struct app_console *console, unsigned int row)
{
    return console->cells[(console->top + row) % APP_WINDOW_ROWS];
}

static void mark_row(struct app_console *console)
{
    console->dirty |= 1u << console->row;
}

static void next_row(struct app_console *console)
{
    mark_row(console);
    console->col = 0;
    if (console->row + 1 < APP_WINDOW_ROWS) {
        ++console->row;
    } else {
        console->top = (console->top + 1) % APP_WINDOW_ROWS;
        unsigned char *last = row_cells(console, console->row);
        for (unsigned int i = 0; i < APP_WINDOW_COLUMNS; ++i)
            last[i] = 0;
        console->dirty = (1u << APP_WINDOW_ROWS) - 1;
    }
    mark_row(console);
}

static void put(struct app_console *console, unsigned char glyph)
{
    if (console->col == APP_WINDOW_COLUMNS)
        next_row(console);
    row_cells(console, console->row)[console->col++] = glyph;
    mark_row(console);
}

void app_window_reset(unsigned int slot)
{
    consoles[slot] = (struct app_console){0};
}

void *app_window_context(unsigned int slot)
{
    return &consoles[slot];
}

void app_window_write(void *context, const unsigned char *data, unsigned int count)
{
    struct app_console *console = context;
    for (unsigned int i = 0; i < count; ++i) {
        const unsigned char ch = data[i];
        if (console->continuation) {
            if ((ch & 0xc0) == 0x80) {
                console->codepoint = (console->codepoint << 6) | (ch & 63);
                if (!--console->continuation) {
                    const unsigned int cp = console->codepoint;
                    put(console, cp >= console->minimum ? shell_codepoint_glyph(cp) : '?');
                }
                continue;
            }
            console->continuation = 0;
            put(console, '?');
        }
        if (ch >= 0xc2 && ch <= 0xdf) {
            console->continuation = 1;
            console->minimum = 0x80;
            console->codepoint = ch & 31;
        } else if (ch >= 0xe0 && ch <= 0xef) {
            console->continuation = 2;
            console->minimum = 0x800;
            console->codepoint = ch & 15;
        } else if (ch >= 0xf0 && ch <= 0xf4) {
            console->continuation = 3;
            console->minimum = 0x10000;
            console->codepoint = ch & 7;
        } else if (ch == '\n') {
            next_row(console);
        } else if (ch == '\t') {
            const unsigned int spaces = 4 - console->col % 4;
            for (unsigned int j = 0; j < spaces; ++j)
                put(console, ' ');
        } else if (ch != '\r') {
            put(console, ch >= ' ' && ch <= '~' ? ch : '?');
        }
    }
}

static void print(struct app_console *console, const char *label)
{
    for (; *label; ++label)
        put(console, (unsigned char)*label);
}

static void decimal(struct app_console *console, unsigned long long value)
{
    unsigned char digits[20];
    unsigned int length = 0;
    do {
        digits[length++] = '0' + value % 10;
        value /= 10;
    } while (value);
    while (length)
        put(console, digits[--length]);
}

static void address(struct app_console *console, const char *label, unsigned long long value)
{
    char result[17];
    shell_hex64(result, value);
    result[16] = 0;
    print(console, label);
    print(console, result);
    next_row(console);
}

static void end_utf8(struct app_console *console)
{
    if (console->continuation) {
        put(console, '?');
        console->continuation = 0;
    }
}

void app_window_update(unsigned int slot)
{
    struct app_console *console = &consoles[slot];
    const unsigned int capacity = app_read_capacity(slot);
    if (capacity && !console->capacity) {
        end_utf8(console);
        if (console->col)
            next_row(console);
        mark_row(console);
    }
    console->capacity = capacity;

    struct app_result result;
    if (!app_take_result(slot, &result))
        return;
    end_utf8(console);
    if (console->col)
        next_row(console);
    switch (result.status) {
    case APP_EXITED:
        print(console, "Codigo de saida ");
        decimal(console, result.exit_code);
        next_row(console);
        break;
    case APP_INTERRUPTED:
        print(console, "Interrompido (Ctrl+C)");
        next_row(console);
        break;
    case APP_FAULTED:
        print(console, "Falha do programa. Vetor ");
        decimal(console, result.vector);
        next_row(console);
        address(console, "RIP 0x", result.rip);
        if (result.vector == 14)
            address(console, "CR2 0x", result.fault_address);
        break;
    default:
        break;
    }
}

void app_window_key(unsigned int slot, unsigned int key)
{
    struct app_console *console = &consoles[slot];
    if (key == KEY_INTERRUPT) {
        app_cancel(slot);
        app_window_update(slot);
        return;
    }
    if (!console->capacity)
        return;
    if (key == '\n') {
        app_complete_read(slot, console->line, console->bytes);
        console->capacity = console->bytes = 0;
        next_row(console);
    } else if (key == '\b' && console->bytes) {
        do {
            --console->bytes;
        } while (console->bytes && (console->line[console->bytes] & 0xc0) == 0x80);
        row_cells(console, console->row)[--console->col] = 0;
        mark_row(console);
    } else if (key >= ' ' && key <= 255 && console->col + 1 < APP_WINDOW_COLUMNS) {
        unsigned char encoded[3];
        const unsigned int size = shell_encode_glyph(encoded, key);
        if (size < console->capacity - console->bytes) {
            for (unsigned int i = 0; i < size; ++i)
                console->line[console->bytes++] = encoded[i];
            put(console, key);
        }
    }
}

unsigned int app_window_dirty(unsigned int slot)
{
    struct app_console *console = &consoles[slot];
    const unsigned int mask = console->dirty;
    console->dirty = 0;
    return mask;
}

void app_window_focus(unsigned int slot)
{
    if (consoles[slot].capacity)
        mark_row(&consoles[slot]);
}

void app_window_draw(unsigned int slot, unsigned int x, unsigned int y, int focused,
    unsigned int clip_y, unsigned int clip_height)
{
    struct app_console *console = &consoles[slot];
    vga_graphics_fill(x, y, APP_WINDOW_WIDTH, APP_WINDOW_HEIGHT, 0x0a1422);
    const unsigned int first = clip_y > y ? (clip_y - y) / VGA_LINE_HEIGHT : 0;
    const unsigned int end = (clip_y + clip_height - y + VGA_LINE_HEIGHT - 1) / VGA_LINE_HEIGHT;
    for (unsigned int r = first; r < APP_WINDOW_ROWS && r < end; ++r) {
        const unsigned char *cells = row_cells(console, r);
        for (unsigned int c = 0; c < APP_WINDOW_COLUMNS; ++c)
            if (cells[c] > ' ')
                vga_graphics_overlay_glyph(x + c * VGA_OVERLAY_CELL_WIDTH, y + r * VGA_LINE_HEIGHT,
                    cells[c], 0xe7eef8);
    }
    if (focused && console->capacity)
        vga_graphics_fill(x + console->col * VGA_OVERLAY_CELL_WIDTH,
            y + (console->row + 1) * VGA_LINE_HEIGHT - 3, VGA_OVERLAY_CELL_WIDTH - 2, 2, 0x61dcf5);
}

#endif
