#include "../fs/fat16.h"
#include "../drivers/console.h"
#include "../memory/e820.h"
#include "../memory/pages.h"
#include "shell.h"
#include "../drivers/keyboard.h"
#include "../drivers/timer.h"

enum { WIDTH = CONSOLE_WIDTH, HEIGHT = CONSOLE_HEIGHT };
static unsigned short text_color;

static void out(unsigned short port, unsigned char value)
{
    __asm__ volatile ("outb %0, %w1" : : "a"(value), "Nd"(port));
}

#ifndef GPT_R_GRAPHICS
static void vga_register(unsigned short port, unsigned char index,
    unsigned char value)
{
    out(port, index);
    out(port + 1, value);
}

#endif

static void move_cursor(unsigned int row, unsigned int col)
{
    const unsigned int position = row * WIDTH + col;
#ifdef GPT_R_GRAPHICS
    vga_graphics_cursor(position);
#else
    vga_register(0x3d4, 0x0e, position >> 8);
    vga_register(0x3d4, 0x0f, position);
#endif
}

static void clear_row(unsigned int row)
{
    for (unsigned int col = 0; col < WIDTH; ++col)
        console_put(row * WIDTH + col, text_color | ' ');
}

enum { FIRST_ROW = 2, PROMPT_WIDTH = 2, MAX_INPUT = WIDTH - PROMPT_WIDTH };

static void next_row(unsigned int *row)
{
    if (*row < HEIGHT - 1) {
        ++*row;
        return;
    }
    for (unsigned int i = FIRST_ROW * WIDTH; i < (HEIGHT - 1) * WIDTH; ++i)
        console_put(i, console_get(i + WIDTH));
    clear_row(*row);
}

static void print_line(unsigned int *row, const char *text)
{
    unsigned int col = 0;
    while (*text) {
        if (col == WIDTH) {
            next_row(row);
            col = 0;
        }
        console_put(*row * WIDTH + col++, text_color | (unsigned char)*text++);
    }
    next_row(row);
}

static int equals(const unsigned char *input, const char *command)
{
    while (*input && *input == (unsigned char)*command) {
        ++input;
        ++command;
    }
    return *input == 0 && *command == 0;
}

static void print_number(unsigned int *row, const char *label,
                         unsigned long long value, const char *unit)
{
    char text[80];
    char digits[20];
    unsigned int length = 0;
    char *output = text;
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

static void hex64(char *text, unsigned long long value)
{
    for (int i = 15; i >= 0; --i) {
        text[i] = hex_digit(value);
        value >>= 4;
    }
}

static void show_memory(unsigned int *row)
{
    const volatile struct e820_map *const map =
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
        const volatile struct e820_entry *entry = &map->entries[i];
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

static void show_address(unsigned int *row, unsigned long long address)
{
    char line[] = "Endereco: 0x0000000000000000";
    hex64(line + 12, address);
    print_line(row, line);
}

static void test_memory(unsigned int *row)
{
    volatile unsigned char *page = page_alloc();
    if (!page) {
        print_line(row, "memtest: nenhuma pagina livre");
        return;
    }
    show_address(row, (unsigned long long)page);
    static const unsigned char patterns[] = {0xaa, 0x55};
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
    if (!page_free((void *)page))
        print_line(row, "memtest: falha ao liberar pagina");
    else if (ok)
        print_line(row, "memtest: 4 KiB OK (0xAA e 0x55). Pagina liberada.");
}

static int parse_address(const unsigned char *text, unsigned long long *address)
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

static void print_bytes(unsigned int *row, const volatile unsigned char *bytes)
{
    char line[16 * 3];
    for (unsigned int i = 0; i < 16; ++i) {
        const unsigned int byte = bytes[i];
        line[i * 3] = hex_digit(byte >> 4);
        line[i * 3 + 1] = hex_digit(byte);
        line[i * 3 + 2] = ' ';
    }
    line[sizeof(line) - 1] = 0;
    print_line(row, line);
}

static void peek(unsigned int *row, const unsigned char *argument)
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
    const volatile unsigned char *bytes = (const volatile unsigned char *)address;
    print_bytes(row, bytes);
}

static unsigned int color_index(const unsigned char **argument)
{
    const unsigned char *text = *argument;
    while (*text == ' ')
        ++text;
    if (*text < '0' || *text > '9')
        return 256;
    unsigned int value = 0;
    do {
        value = value * 10 + (*text++ - '0');
        if (value > 15)
            return 256;
    } while (*text >= '0' && *text <= '9');

    if (*text && *text != ' ')
        return 256;
    *argument = text;
    return value;
}

static void change_color(unsigned int *row, const unsigned char *argument)
{
    const unsigned int foreground = color_index(&argument);
    const unsigned int background = color_index(&argument);
    if (foreground > 15 || background > 7 || *argument) {
        print_line(row, "Uso: color <frente 0-15> <fundo 0-7>");
        return;
    }

    text_color = (background << 12) | (foreground << 8);
    for (unsigned int i = FIRST_ROW * WIDTH; i < HEIGHT * WIDTH; ++i)
        console_put(i, text_color | (console_get(i) & 0xff));
}

static void exhaust_pages(unsigned int *row)
{
    const unsigned int initial = pages_free_count();
    void **head = 0;
    void **page;
    unsigned int count = 0;
    /* Chain the test pages themselves so only those pages are freed. */
    while ((page = page_alloc())) {
        *page = head;
        head = page;
        ++count;
    }
    print_number(row, "Paginas alocadas ate esgotar: ", count, "");
    while (head) {
        page = head;
        head = *page;
        if (!page_free(page)) {
            print_line(row, "crash alloc: falha ao liberar pagina");
            return;
        }
    }
    if (count == initial && pages_free_count() == initial)
        print_line(row, "Alocador retornou NULL. Contagem restaurada.");
    else
        print_line(row, "crash alloc: contagem inesperada");
}

static void crash_test(unsigned int *row, const unsigned char *type)
{
    while (*type == ' ')
        ++type;
    if (equals(type, "ud")) {
        __asm__ volatile ("ud2");
    } else if (equals(type, "div")) {
        __asm__ volatile ("xor %%ecx, %%ecx; xor %%edx, %%edx; "
                          "mov $1, %%eax; div %%ecx"
                          : : : "rax", "rcx", "rdx", "cc");
    } else if (equals(type, "pf")) {
        __asm__ volatile ("movb (%0), %%al" : : "r"(0x40000000ull) : "rax", "memory");
    } else if (equals(type, "gp")) {
        /* Noncanonical in both 48-bit and 57-bit address modes. */
        __asm__ volatile ("movb (%0), %%al" : : "r"(0x8000000000000000ull)
                          : "rax", "memory");
    } else if (equals(type, "int3")) {
        __asm__ volatile ("int3");
    } else if (equals(type, "alloc")) {
        exhaust_pages(row);
    } else if (equals(type, "hang")) {
        __asm__ volatile ("cli; 1: pause; jmp 1b" : : : "memory");
        __builtin_unreachable();
    } else if (equals(type, "triple")) {
        const unsigned char empty_idtr[10] = {0};
        __asm__ volatile ("cli; lidt %0; ud2" : : "m"(empty_idtr) : "memory");
        __builtin_unreachable();
    } else {
        print_line(row, "Uso: crash <ud|div|pf|gp|int3|alloc|hang|triple>");
    }
}

static const unsigned char *argument_for(const unsigned char *input, const char *name)
{
    while (*name && *input == (unsigned char)*name) {
        ++input;
        ++name;
    }
    return !*name && (!*input || *input == ' ') ? input : 0;
}

static int open_disk(unsigned int *row, struct ata_disk *disk)
{
    if (ata_identify(disk))
        return 1;
    print_line(row, "Disco ATA ausente ou sem resposta");
    return 0;
}

static void disk_info(unsigned int *row)
{
    struct ata_disk disk;
    if (!open_disk(row, &disk))
        return;
    print_line(row, disk.model);
    print_number(row, "Setores acessiveis: ", disk.sectors, " de 512 bytes");
    print_number(row, "Capacidade acessivel: ", (unsigned long long)disk.sectors * 512, " bytes");
}

static const unsigned char *skip_spaces(const unsigned char *text)
{
    while (*text == ' ')
        ++text;
    return text;
}

static void show_sector(unsigned int *row, const unsigned char *argument)
{
    unsigned int lba = 0;
    const unsigned char *text = skip_spaces(argument);
    const unsigned char *start = text;
    while (*text >= '0' && *text <= '9') {
        const unsigned int digit = *text++ - '0';
        if (lba > (0xffffffffu - digit) / 10) {
            print_line(row, "LBA fora do intervalo");
            return;
        }
        lba = lba * 10 + digit;
    }
    if (text == start || *text) {
        print_line(row, "Uso: sector <LBA decimal>");
        return;
    }

    struct ata_disk disk;
    unsigned char data[512];
    if (!open_disk(row, &disk))
        return;
    if (lba >= disk.sectors) {
        print_line(row, "LBA fora do disco");
        return;
    }
    if (!ata_read_sector(&disk, lba, data)) {
        print_line(row, "Falha ao ler setor ATA");
        return;
    }
    print_number(row, "Setor ", lba, " (primeiros 16 bytes)");
    print_bytes(row, data);
}

static int open_volume(unsigned int *row, struct ata_disk *disk,
                       struct fat16_volume *volume)
{
    if (!open_disk(row, disk))
        return 0;
    if (fat16_mount(volume, disk))
        return 1;
    print_line(row, "Volume FAT16 indisponivel");
    return 0;
}

static void list_files(unsigned int *row)
{
    struct ata_disk disk;
    struct fat16_volume volume;
    if (!open_volume(row, &disk, &volume))
        return;

    struct fat16_dir dir;
    dir.index = 0;
    struct fat16_file file;
    int result, found = 0;
    while ((result = fat16_next(&volume, &dir, &file)) > 0) {
        char label[14];
        unsigned int i = 0;
        while (file.name[i]) {
            label[i] = file.name[i];
            ++i;
        }
        label[i++] = ' ';
        label[i] = 0;
        print_number(row, label, file.size, " bytes");
        found = 1;
    }
    if (result < 0)
        print_line(row, "Falha ao ler diretorio");
    else if (!found)
        print_line(row, "Diretorio raiz vazio");
}

static unsigned char upper_ascii(unsigned char ch)
{
    return ch >= 'a' && ch <= 'z' ? ch - 'a' + 'A' : ch;
}

static int same_filename(const unsigned char *input, const char *filename)
{
    while (*input && upper_ascii(*input) == upper_ascii(*filename)) {
        ++input;
        ++filename;
    }
    return !*input && !*filename;
}

struct cat_output {
    unsigned int *row;
    unsigned int col;
};

static void cat_byte(struct cat_output *output, unsigned char ch)
{
    if (output->col == WIDTH) {
        next_row(output->row);
        output->col = 0;
    }
    console_put(*output->row * WIDTH + output->col++, text_color | ch);
}

static void cat_chunk(void *context, const unsigned char *data, unsigned int size)
{
    struct cat_output *output = context;
    for (unsigned int i = 0; i < size; ++i) {
        const unsigned char ch = data[i];
        if (ch == '\n') {
            next_row(output->row);
            output->col = 0;
        } else if (ch == '\t') {
            const unsigned int spaces = 4 - output->col % 4;
            for (unsigned int j = 0; j < spaces; ++j)
                cat_byte(output, ' ');
        } else if (ch != '\r') {
            cat_byte(output, ch >= 32 && ch <= 126 ? ch : '?');
        }
    }
}

static void cat_file(unsigned int *row, const unsigned char *argument)
{
    const unsigned char *name = skip_spaces(argument);
    if (!*name) {
        print_line(row, "Uso: cat <arquivo>");
        return;
    }

    struct ata_disk disk;
    struct fat16_volume volume;
    if (!open_volume(row, &disk, &volume))
        return;

    struct fat16_dir dir;
    dir.index = 0;
    struct fat16_file file;
    int result;
    while ((result = fat16_next(&volume, &dir, &file)) > 0) {
        if (!same_filename(name, file.name))
            continue;
        struct cat_output output = { row, 0 };
        const int read_ok = fat16_read(&volume, &file, cat_chunk, &output);
        if (output.col)
            next_row(row);
        if (!read_ok)
            print_line(row, "Falha ao ler arquivo ou cadeia FAT16");
        return;
    }
    print_line(row, result < 0 ? "Falha ao ler diretorio" : "Arquivo nao encontrado");
}

static void execute(unsigned int *row, unsigned char *line, unsigned int length
#ifdef GPT_R_GRAPHICS
                    , struct keyboard_state *keyboard
#endif
)
{
    while (length && *line == ' ') {
        ++line;
        --length;
    }
    while (length && line[length - 1] == ' ')
        --length;
    line[length] = 0;

    const unsigned char *argument;
    if (equals(line, "help")) {
        print_line(row, "help     Lista os comandos");
        print_line(row, "clear    Limpa o terminal");
        print_line(row, "version  Mostra a versao");
        print_line(row, "uptime   Mostra o tempo ligado");
        print_line(row, "mem      Mostra o mapa e o uso da memoria");
        print_line(row, "memtest  Testa uma pagina de 4 KiB");
        print_line(row, "peek     Le 16 bytes de um endereco hexadecimal");
        print_line(row, "reboot   Reinicia o sistema");
        print_line(row, "diskinfo Mostra o disco ATA");
        print_line(row, "sector   Mostra 16 bytes de um setor");
        print_line(row, "ls       Lista arquivos FAT16");
        print_line(row, "cat      Mostra o texto de um arquivo");
        print_line(row, "color    Muda as cores do terminal");
        print_line(row, "crash    Executa testes de erro e de limite");
#ifdef GPT_R_GRAPHICS
        print_line(row, "demo     Abre a demonstracao VGA");
    } else if (equals(line, "demo")) {
        vga_graphics_demo(keyboard);
#endif
    } else if (equals(line, "clear")) {
        for (unsigned int i = FIRST_ROW; i < HEIGHT; ++i)
            clear_row(i);
        *row = FIRST_ROW;
    } else if (equals(line, "version")) {
        print_line(row, "GPT-R OS v0.3.5");
    } else if (equals(line, "uptime")) {
        print_number(row, "", timer_uptime_seconds(), " s");
    } else if (equals(line, "mem")) {
        show_memory(row);
    } else if (equals(line, "memtest")) {
        test_memory(row);
    } else if ((argument = argument_for(line, "peek"))) {
        peek(row, argument);
    } else if (equals(line, "diskinfo")) {
        disk_info(row);
    } else if ((argument = argument_for(line, "sector"))) {
        show_sector(row, argument);
    } else if (equals(line, "ls")) {
        list_files(row);
    } else if ((argument = argument_for(line, "cat"))) {
        cat_file(row, argument);
    } else if (equals(line, "reboot")) {
        /* System reset control on the QEMU PC chipset. */
        out(0xcf9, 0x02);
        out(0xcf9, 0x06);
        print_line(row, "reboot: controlador nao reiniciou");
    } else if ((argument = argument_for(line, "color"))) {
        change_color(row, argument);
    } else if ((argument = argument_for(line, "crash"))) {
        crash_test(row, argument);
    } else if (length) {
        print_line(row, "Comando desconhecido");
    }
}

_Noreturn void shell_run(void)
{
    struct keyboard_state keyboard = {0};
    unsigned char line[MAX_INPUT + 1];
    unsigned int row = FIRST_ROW;
    text_color = 0x0f00;

    for (;;) {
        unsigned int length = 0;
        console_put(row * WIDTH, text_color | '>');
        for (;;) {
            const unsigned int col = PROMPT_WIDTH + length;
            move_cursor(row, col < WIDTH ? col : WIDTH - 1);
            const unsigned char ch = keyboard_read_char(&keyboard);
#ifdef GPT_R_GRAPHICS
            vga_graphics_cursor(WIDTH * HEIGHT);
#endif
            if (ch == '\n')
                break;
            if (ch == '\b') {
                if (length)
                    console_put(row * WIDTH + PROMPT_WIDTH + --length, text_color | ' ');
            } else if (length < MAX_INPUT) {
                line[length++] = ch;
                console_put(row * WIDTH + col, text_color | ch);
            }
        }
        next_row(&row);
#ifdef GPT_R_GRAPHICS
        execute(&row, line, length, &keyboard);
#else
        execute(&row, line, length);
#endif
    }
}
