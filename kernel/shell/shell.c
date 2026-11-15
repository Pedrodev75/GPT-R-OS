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

static void print_number(unsigned int* row, const char* label,
    unsigned long long value, const char* unit)
{
    char text[80];
    char digits[20];
    unsigned int length = 0;
    char* output = text;
    while (*label)
        *output++ = *label++;
    do {
        digits[length++] = '0' + value % 10;
        value /= 10;
    } while (value);
    while (length)
        *output++ = digits[--length];
    while (*unit)
        *output++ = *unit++;
    *output = 0;
    print_line(row, text);
}

static char hex_digit(unsigned int digit)
{
    return "0123456789ABCDEF"[digit & 15];
}

static void hex64(char* text, unsigned long long value)
{
    for (int i = 15; i >= 0; --i) {
        text[i] = hex_digit(value);
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
    print_number(row, "RAM utilizavel (E820): ", usable / 1024, " KiB");
    print_number(row, "RAM mapeada para alocacao: ",
        pages_managed_count() * (PAGE_SIZE / 1024), " KiB");
    print_number(row, "Paginas livres de 4 KiB: ", pages_free_count(), "");
}

static void show_address(unsigned int* row, unsigned long long address)
{
    char line[] = "Endereco: 0x0000000000000000";
    hex64(line + 12, address);
    print_line(row, line);
}

static void test_memory(unsigned int* row)
{
    volatile unsigned char* page = page_alloc();
    if (!page) {
        print_line(row, "memtest: nenhuma pagina livre");
        return;
    }
    show_address(row, (unsigned long long)page);
    static const unsigned char patterns[] = { 0xaa, 0x55 };
    unsigned int ok = 1;
    for (unsigned int p = 0; p < sizeof(patterns) && ok; ++p) {
        for (unsigned int i = 0; i < PAGE_SIZE; ++i)
            page[i] = patterns[p];
        for (unsigned int i = 0; i < PAGE_SIZE; ++i) {
            if (page[i] != patterns[p]) {
                print_number(row, "memtest: falha no byte ", i, "");
                ok = 0;
                break;
            }
        }
    }
    if (!page_free((void*)page))
        print_line(row, "memtest: falha ao liberar pagina");
    else if (ok)
        print_line(row, "memtest: 4 KiB OK (0xAA e 0x55). Pagina liberada.");
}

static int parse_address(const unsigned char* text, unsigned long long* address)
{
    while (*text == ' ')
        ++text;
    if (text[0] == '0' && (text[1] | 32) == 'x')
        text += 2;
    if (!*text)
        return 0;

    unsigned long long value = 0;
    for (unsigned int count = 0; *text; ++count, ++text) {
        unsigned int digit = *text;
        if (digit >= '0' && digit <= '9')
            digit -= '0';
        else if ((digit | 32) >= 'a' && (digit | 32) <= 'f')
            digit = (digit | 32) - 'a' + 10;
        else
            return 0;
        if (count == 16)
            return 0;
        value = (value << 4) | digit;
    }
    *address = value;
    return 1;
}

static void peek(unsigned int* row, const unsigned char* argument)
{
    enum { BYTE_COUNT = 16 };
    unsigned long long address;
    if (!parse_address(argument, &address)) {
        print_line(row, "Uso: peek <endereco hexadecimal>");
        return;
    }
    if (!pages_contains_range(address, BYTE_COUNT)) {
        print_line(row, "peek: faixa fora da RAM gerenciada");
        return;
    }
    show_address(row, address);
    const volatile unsigned char* bytes = (const volatile unsigned char*)address;
    char line[BYTE_COUNT * 3];
    for (unsigned int i = 0; i < BYTE_COUNT; ++i) {
        const unsigned int byte = bytes[i];
        line[i * 3] = hex_digit(byte >> 4);
        line[i * 3 + 1] = hex_digit(byte);
        line[i * 3 + 2] = ' ';
    }
    line[sizeof(line) - 1] = 0;
    print_line(row, line);
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
        print_line(row, "mem      Mostra o mapa e o uso da memoria");
        print_line(row, "memtest  Testa uma pagina de 4 KiB");
        print_line(row, "peek     Le 16 bytes de um endereco hexadecimal");
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
        print_number(row, "", timer_uptime_seconds(), " s");
    }
    else if (equals(line, "mem")) {
        show_memory(row);
    }
    else if (equals(line, "memtest")) {
        test_memory(row);
    }
    else if (length >= 4 && line[0] == 'p' && line[1] == 'e' &&
        line[2] == 'e' && line[3] == 'k' &&
        (line[4] == 0 || line[4] == ' ')) {
        peek(row, line + 4);
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
