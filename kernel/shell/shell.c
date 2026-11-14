#include "../memory/e820.h"
#include "../memory/pages.h"
#include "shell.h"
#include "../drivers/keyboard.h"
#include "../drivers/timer.h"

enum { WIDTH = 80, HEIGHT = 25, TEXT_COLOR = 0x0f00 };
static volatile unsigned short* const vga = (volatile unsigned short*)0xb8000;

static void out(unsigned short port, unsigned char value)
{
    __asm__ volatile ("outb %0, %w1" : : "a"(value), "Nd"(port));
}

static void vga_register(unsigned short port, unsigned char index,
    unsigned char value)
{
    out(port, index);
    out(port + 1, value);
}

static void move_cursor(unsigned int row, unsigned int col)
{
    const unsigned int position = row * WIDTH + col;
    vga_register(0x3d4, 0x0e, position >> 8);
    vga_register(0x3d4, 0x0f, position);
}

static void clear_row(unsigned int row)
{
    for (unsigned int col = 0; col < WIDTH; ++col)
        vga[row * WIDTH + col] = TEXT_COLOR | ' ';
}

enum { FIRST_ROW = 2, PROMPT_WIDTH = 2, MAX_INPUT = WIDTH - PROMPT_WIDTH };

static void next_row(unsigned int* row)
{
    if (*row < HEIGHT - 1) {
        ++*row;
        return;
    }
    for (unsigned int i = FIRST_ROW * WIDTH; i < (HEIGHT - 1) * WIDTH; ++i)
        vga[i] = vga[i + WIDTH];
    clear_row(*row);
}

static void print_line(unsigned int* row, const char* text)
{
    unsigned int col = 0;
    while (*text)
        vga[*row * WIDTH + col++] = TEXT_COLOR | (unsigned char)*text++;
    next_row(row);
}

static int equals(const unsigned char* input, const char* command)
{
    while (*input && *input == (unsigned char)*command) {
        ++input;
        ++command;
    }
    return *input == 0 && *command == 0;
}

static void show_uptime(unsigned int* row)
{
    char text[23];
    char* start = text + sizeof(text) - 1;
    unsigned long long value = timer_uptime_seconds();
    *start = 0;
    *--start = 's';
    *--start = ' ';
    do {
        *--start = '0' + value % 10;
        value /= 10;
    } while (value);
    print_line(row, start);
}

static void hex64(char* text, unsigned long long value)
{
    for (int i = 15; i >= 0; --i) {
        text[i] = "0123456789ABCDEF"[value & 15];
        value >>= 4;
    }
}

static void show_memory(unsigned int* row)
{
    const volatile struct e820_map* const map =
        E820_MAP;
    const unsigned int count = map->count;
    if (map->status == 0 || count > E820_MAX_ENTRIES || !count) {
        print_line(row, "Mapa E820 indisponivel");
        return;
    }
    if (map->status != E820_COMPLETE)
        print_line(row, "Mapa E820 incompleto");

    print_line(row, "Base fisica        Tamanho (bytes)    Tipo E820");
    unsigned long long usable = 0;
    for (unsigned int i = 0; i < count; ++i) {
        const volatile struct e820_entry* entry = &map->entries[i];
        const unsigned long long length = entry->length;
        const unsigned int type = entry->type;
        char line[] = "0x0000000000000000 0x0000000000000000 0x0000000000000000";
        hex64(line + 2, entry->base);
        hex64(line + 21, length);
        hex64(line + 40, type);
        print_line(row, line);
        if (type == E820_USABLE)
            usable += length;
    }
    print_line(row, "Tipo 1 = RAM utilizavel segundo a BIOS");
    char total[] = "RAM utilizavel (MiB):                     ";
    char* end = total + sizeof(total) - 1;
    char* digit = end;
    usable >>= 20;
    do {
        *--digit = '0' + usable % 10;
        usable /= 10;
    } while (usable);
    char* output = total + sizeof("RAM utilizavel (MiB): ") - 1;
    while (digit < end)
        *output++ = *digit++;
    *output = 0;
    print_line(row, total);
    char pages[] = "Paginas livres de 4 KiB: 000000";
    unsigned int free = pages_free_count();
    for (unsigned int i = sizeof(pages) - 2; i >= sizeof(pages) - 7; --i) {
        pages[i] = '0' + free % 10;
        free /= 10;
    }
    print_line(row, pages);
}

static void execute(unsigned int* row, unsigned char* line, unsigned int length)
{
    while (length && *line == ' ') {
        ++line;
        --length;
    }
    while (length && line[length - 1] == ' ')
        --length;
    line[length] = 0;

    if (equals(line, "help")) {
        print_line(row, "help     Lista os comandos");
        print_line(row, "clear    Limpa o terminal");
        print_line(row, "version  Mostra a versao");
        print_line(row, "uptime   Mostra o tempo ligado");
        print_line(row, "mem      Mostra o mapa E820");
    }
    else if (equals(line, "clear")) {
        for (unsigned int i = FIRST_ROW; i < HEIGHT; ++i)
            clear_row(i);
        *row = FIRST_ROW;
    }
    else if (equals(line, "version")) {
        print_line(row, "GPT-R OS v0.2.5");
    }
    else if (equals(line, "uptime")) {
        show_uptime(row);
    }
    else if (equals(line, "mem")) {
        show_memory(row);
    }
    else if (length) {
        print_line(row, "Comando desconhecido");
    }
}

_Noreturn void shell_run(void)
{
    struct keyboard_state keyboard = { 0 };
    unsigned char line[MAX_INPUT + 1];
    unsigned int row = FIRST_ROW;

    for (;;) {
        unsigned int length = 0;
        vga[row * WIDTH] = TEXT_COLOR | '>';
        for (;;) {
            const unsigned int col = PROMPT_WIDTH + length;
            move_cursor(row, col < WIDTH ? col : WIDTH - 1);
            const unsigned char ch = keyboard_read_char(&keyboard);
            if (ch == '\n')
                break;
            if (ch == '\b') {
                if (length)
                    vga[row * WIDTH + PROMPT_WIDTH + --length] = TEXT_COLOR | ' ';
            }
            else if (length < MAX_INPUT) {
                line[length++] = ch;
                vga[row * WIDTH + col] = TEXT_COLOR | ch;
            }
        }
        next_row(&row);
        execute(&row, line, length);
    }
}
