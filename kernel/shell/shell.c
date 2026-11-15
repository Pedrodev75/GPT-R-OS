#include "../fs/fat16.h"
#include "../drivers/console.h"
#include "../memory/e820.h"
#include "../memory/pages.h"
#include "../memory/heap.h"
#include "shell.h"
#include "../drivers/keyboard.h"
#include "../drivers/timer.h"

enum { WIDTH = CONSOLE_WIDTH, HEIGHT = CONSOLE_HEIGHT };

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
        console_put(row * WIDTH + col, console_color | ' ');
}

enum { FIRST_ROW = 2, PROMPT_WIDTH = 2, MAX_INPUT = WIDTH - PROMPT_WIDTH };

static void next_row(unsigned int* row)
{
    if (*row < HEIGHT - 1) {
        ++*row;
        return;
    }
    for (unsigned int i = FIRST_ROW * WIDTH; i < (HEIGHT - 1) * WIDTH; ++i)
        console_put(i, console_get(i + WIDTH));
    clear_row(*row);
}

static void print_line(unsigned int* row, const char* text)
{
    unsigned int col = 0;
    while (*text) {
        if (col == WIDTH) {
            next_row(row);
            col = 0;
        }
        console_put(*row * WIDTH + col++, console_color | (unsigned char)*text++);
    }
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

static void print_bytes(unsigned int* row, const volatile unsigned char* bytes)
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
    print_bytes(row, bytes);
}

static unsigned int color_index(const unsigned char** argument)
{
    const unsigned char* text = *argument;
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

static void change_color(unsigned int* row, const unsigned char* argument)
{
    const unsigned int foreground = color_index(&argument);
    const unsigned int background = color_index(&argument);
    if (foreground > 15 || background > 7 || *argument) {
        print_line(row, "Uso: color <frente 0-15> <fundo 0-7>");
        return;
    }

    if (foreground == background) {
        print_line(row, "Frente e fundo precisam ser diferentes");
        return;
    }
    const unsigned short color = (background << 12) | (foreground << 8);
    unsigned long long flags;
    /* The PIT redraws the header from IRQ0. Recolor it atomically. */
    __asm__ volatile ("pushfq; pop %0; cli" : "=r"(flags) : : "memory");
    console_color = color;
    for (unsigned int i = 0; i < FIRST_ROW * WIDTH; ++i)
        console_put(i, color | (console_get(i) & 0xff));
    if (flags & 0x200)
        __asm__ volatile ("sti" : : : "memory");
    for (unsigned int i = FIRST_ROW * WIDTH; i < HEIGHT * WIDTH; ++i)
        console_put(i, color | (console_get(i) & 0xff));
}

static void exhaust_pages(unsigned int* row)
{
    const unsigned int initial = pages_free_count();
    void** head = 0;
    void** page;
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

static void crash_test(unsigned int* row, const unsigned char* type)
{
    while (*type == ' ')
        ++type;
    if (equals(type, "ud")) {
        __asm__ volatile ("ud2");
    }
    else if (equals(type, "div")) {
        __asm__ volatile ("xor %%ecx, %%ecx; xor %%edx, %%edx; "
            "mov $1, %%eax; div %%ecx"
            : : : "rax", "rcx", "rdx", "cc");
    }
    else if (equals(type, "pf")) {
        __asm__ volatile ("movb (%0), %%al" : : "r"(0x40000000ull) : "rax", "memory");
    }
    else if (equals(type, "gp")) {
        /* Noncanonical in both 48-bit and 57-bit address modes. */
        __asm__ volatile ("movb (%0), %%al" : : "r"(0x8000000000000000ull)
            : "rax", "memory");
    }
    else if (equals(type, "int3")) {
        __asm__ volatile ("int3");
    }
    else if (equals(type, "alloc")) {
        exhaust_pages(row);
    }
    else if (equals(type, "heap")) {
        print_line(row, heap_self_test() ? "crash heap: PASS" : "crash heap: FAIL");
    }
    else if (equals(type, "hang")) {
        __asm__ volatile ("cli; 1: pause; jmp 1b" : : : "memory");
        __builtin_unreachable();
    }
    else if (equals(type, "triple")) {
        const unsigned char empty_idtr[10] = { 0 };
        __asm__ volatile ("cli; lidt %0; ud2" : : "m"(empty_idtr) : "memory");
        __builtin_unreachable();
    }
    else {
        print_line(row, "Uso: crash <ud|div|pf|gp|int3|alloc|heap|hang|triple>");
    }
}

static const unsigned char* argument_for(const unsigned char* input, const char* name)
{
    while (*name && *input == (unsigned char)*name) {
        ++input;
        ++name;
    }
    return !*name && (!*input || *input == ' ') ? input : 0;
}

static int open_disk(unsigned int* row, struct ata_disk* disk)
{
    if (ata_identify(disk))
        return 1;
    print_line(row, "Disco ATA ausente ou sem resposta");
    return 0;
}

static void disk_info(unsigned int* row)
{
    struct ata_disk disk;
    if (!open_disk(row, &disk))
        return;
    print_line(row, disk.model);
    print_number(row, "Setores acessiveis: ", disk.sectors, " de 512 bytes");
    print_number(row, "Capacidade acessivel: ", (unsigned long long)disk.sectors * 512, " bytes");
}

static const unsigned char* skip_spaces(const unsigned char* text)
{
    while (*text == ' ')
        ++text;
    return text;
}

/* Returns -1 on overflow, 0 on invalid input, and 1 on success. */
static int parse_decimal(const unsigned char* argument, unsigned int* number)
{
    unsigned int value = 0;
    const unsigned char* text = skip_spaces(argument);
    const unsigned char* start = text;
    while (*text >= '0' && *text <= '9') {
        const unsigned int digit = *text++ - '0';
        if (value > (0xffffffffu - digit) / 10)
            return -1;
        value = value * 10 + digit;
    }
    if (text == start || *text)
        return 0;
    *number = value;
    return 1;
}

static void show_sector(unsigned int* row, const unsigned char* argument)
{
    unsigned int lba;
    const int parsed = parse_decimal(argument, &lba);
    if (parsed != 1) {
        print_line(row, parsed < 0 ? "LBA fora do intervalo" : "Uso: sector <LBA decimal>");
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

static int open_volume(unsigned int* row, struct ata_disk* disk,
    struct fat16_volume* volume)
{
    if (!open_disk(row, disk))
        return 0;
    if (fat16_mount(volume, disk))
        return 1;
    print_line(row, "Volume FAT16 indisponivel");
    return 0;
}

static void list_files(unsigned int* row)
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

static int same_filename(const unsigned char* input, const char* filename)
{
    while (*input && upper_ascii(*input) == upper_ascii(*filename)) {
        ++input;
        ++filename;
    }
    return !*input && !*filename;
}

/* ABNT2 console glyph order, with Unicode values for UTF-8 on disk. */
static const unsigned short disk_glyphs[] = {
    0x00e7, 0x00c7,
    0x00e1, 0x00e9, 0x00ed, 0x00f3, 0x00fa, 0x00c1, 0x00c9, 0x00cd, 0x00d3, 0x00da,
    0x00e0, 0x00e8, 0x00ec, 0x00f2, 0x00f9, 0x00c0, 0x00c8, 0x00cc, 0x00d2, 0x00d9,
    0x00e3, 0x1ebd, 0x0129, 0x00f5, 0x0169, 0x00c3, 0x1ebc, 0x0128, 0x00d5, 0x0168,
    0x00e2, 0x00ea, 0x00ee, 0x00f4, 0x00fb, 0x00c2, 0x00ca, 0x00ce, 0x00d4, 0x00db
};

static const unsigned short accent_marks[] = { 0x00b4, 0x0060, 0x007e, 0x005e };

static unsigned int glyph_codepoint(unsigned char glyph)
{
    if (glyph >= 0x80 && glyph < 0x80 + sizeof(disk_glyphs) / sizeof(disk_glyphs[0]))
        return disk_glyphs[glyph - 0x80];
    if (glyph >= 0xf0 && glyph <= 0xf3)
        return accent_marks[glyph - 0xf0];
    return glyph >= 32 && glyph <= 126 ? glyph : '?';
}

static unsigned char codepoint_glyph(unsigned int codepoint)
{
    if (codepoint >= 32 && codepoint <= 126)
        return codepoint;
    for (unsigned int i = 0; i < sizeof(disk_glyphs) / sizeof(disk_glyphs[0]); ++i)
        if (codepoint == disk_glyphs[i])
            return 0x80 + i;
    return codepoint == 0x00b4 ? 0xf0 : '?';
}

static unsigned int encode_text(unsigned char* bytes, const unsigned char* text)
{
    unsigned int size = 0;
    while (*text) {
        const unsigned int cp = glyph_codepoint(*text++);
        if (cp < 0x80) {
            bytes[size++] = cp;
        }
        else if (cp < 0x800) {
            bytes[size++] = 0xc0 | (cp >> 6);
            bytes[size++] = 0x80 | (cp & 63);
        }
        else {
            bytes[size++] = 0xe0 | (cp >> 12);
            bytes[size++] = 0x80 | ((cp >> 6) & 63);
            bytes[size++] = 0x80 | (cp & 63);
        }
    }
    return size;
}

struct cat_output {
    unsigned int* row;
    unsigned int col, codepoint, continuation, minimum;
};

static void cat_byte(struct cat_output* output, unsigned char ch)
{
    if (output->col == WIDTH) {
        next_row(output->row);
        output->col = 0;
    }
    console_put(*output->row * WIDTH + output->col++, console_color | ch);
}

static void cat_chunk(void* context, const unsigned char* data, unsigned int size)
{
    struct cat_output* output = context;
    for (unsigned int i = 0; i < size; ++i) {
        const unsigned char ch = data[i];
        if (output->continuation) {
            if ((ch & 0xc0) == 0x80) {
                output->codepoint = (output->codepoint << 6) | (ch & 63);
                if (!--output->continuation) {
                    unsigned int cp = output->codepoint;
                    cat_byte(output, cp >= output->minimum ? codepoint_glyph(cp) : '?');
                }
                continue;
            }
            output->continuation = 0;
            cat_byte(output, '?');
        }
        if (ch >= 0xc2 && ch <= 0xdf) {
            output->continuation = 1;
            output->minimum = 0x80;
            output->codepoint = ch & 31;
        }
        else if (ch >= 0xe0 && ch <= 0xef) {
            output->continuation = 2;
            output->minimum = 0x800;
            output->codepoint = ch & 15;
        }
        else if (ch >= 0xf0 && ch <= 0xf4) {
            output->continuation = 3;
            output->minimum = 0x10000;
            output->codepoint = ch & 7;
        }
        else if (ch == '\n') {
            next_row(output->row);
            output->col = 0;
        }
        else if (ch == '\t') {
            const unsigned int spaces = 4 - output->col % 4;
            for (unsigned int j = 0; j < spaces; ++j)
                cat_byte(output, ' ');
        }
        else if (ch != '\r') {
            cat_byte(output, ch >= 32 && ch <= 126 ? ch : '?');
        }
    }
}

static void cat_file(unsigned int* row, const unsigned char* argument)
{
    const unsigned char* name = skip_spaces(argument);
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
        struct cat_output output = { .row = row };
        const int read_ok = fat16_read(&volume, &file, cat_chunk, &output);
        if (output.continuation)
            cat_byte(&output, '?');
        if (output.col)
            next_row(row);
        if (!read_ok)
            print_line(row, "Falha ao ler arquivo ou cadeia FAT16");
        return;
    }
    print_line(row, result < 0 ? "Falha ao ler diretorio" : "Arquivo nao encontrado");
}

static void edit_file(unsigned int* row, const unsigned char* argument, int remove)
{
    static const char* const errors[] = {
        [FAT16_EDIT_NAME] = "Nome 8.3 invalido",
        [FAT16_EDIT_SIZE] = "Texto maior que um setor",
        [FAT16_EDIT_MISSING] = "Arquivo nao encontrado",
        [FAT16_EDIT_PROTECTED] = "Entrada protegida ou com nome longo",
        [FAT16_EDIT_FULL] = "Disco ou diretorio cheio",
        [FAT16_EDIT_IO] = "Falha de disco ou FAT inconsistente",
        [FAT16_EDIT_CLEANUP] = "Diretorio atualizado; falha ao liberar clusters antigos"
    };
    const unsigned char* name = skip_spaces(argument);
    const unsigned char* end = name;
    while (*end && *end != ' ')
        ++end;
    const unsigned char* content = skip_spaces(end);
    if (name == end || (remove ? *content != 0 : *content == 0)) {
        print_line(row, remove ? "Uso: rm <arquivo>" : "Uso: write <arquivo> <texto>");
        return;
    }
    unsigned char data[MAX_INPUT * 3];
    const unsigned int length = remove ? 0 : encode_text(data, content);

    struct ata_disk disk;
    struct fat16_volume volume;
    if (!open_volume(row, &disk, &volume))
        return;
    const enum fat16_edit_result result = remove ?
        fat16_remove(&volume, name, end - name) :
        fat16_write(&volume, name, end - name, data, length);
    if (result == FAT16_EDIT_OK)
        print_line(row, remove ? "Arquivo removido" : "Arquivo salvo");
    else
        print_line(row, errors[result]);
}

enum { SHELL_HEAP_SLOTS = 32 };
static void* shell_blocks[SHELL_HEAP_SLOTS];

static void allocate_block(unsigned int* row, const unsigned char* argument)
{
    unsigned int bytes;
    if (parse_decimal(argument, &bytes) != 1 || !bytes) {
        print_line(row, "Uso: kmalloc <bytes em decimal>");
        return;
    }
    for (unsigned int i = 0; i < SHELL_HEAP_SLOTS; ++i) {
        if (shell_blocks[i])
            continue;
        void* block = kmalloc(bytes);
        if (!block) {
            print_line(row, "kmalloc: sem espaco para esse bloco");
            return;
        }
        shell_blocks[i] = block;
        show_address(row, (unsigned long long)block);
        return;
    }
    print_line(row, "kmalloc: limite de 32 blocos ativos no shell");
}

static void free_block(unsigned int* row, const unsigned char* argument)
{
    unsigned long long address;
    if (!parse_address(argument, &address)) {
        print_line(row, "Uso: kfree <endereco hexadecimal>");
        return;
    }
    for (unsigned int i = 0; i < SHELL_HEAP_SLOTS; ++i) {
        void* block = shell_blocks[i];
        if ((unsigned long long)block != address || !block)
            continue;
        shell_blocks[i] = 0;
        kfree(block);
        print_line(row, "Bloco liberado");
        return;
    }
    print_line(row, "kfree: bloco ativo nao encontrado no shell");
}

static void execute(unsigned int* row, unsigned char* line, unsigned int length
#ifdef GPT_R_GRAPHICS
    , struct keyboard_state* keyboard
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

    const unsigned char* argument;
    if (equals(line, "help")) {
        print_line(row, "help     Lista os comandos");
        print_line(row, "clear    Limpa o terminal");
        print_line(row, "version  Mostra a versao");
        print_line(row, "uptime   Mostra o tempo ligado");
        print_line(row, "mem      Mostra o mapa e o uso da memoria");
        print_line(row, "memtest  Testa uma pagina de 4 KiB");
        print_line(row, "kmalloc <bytes>    Aloca um bloco do heap");
        print_line(row, "kfree <endereco>   Libera um bloco do heap");
        print_line(row, "peek     Le 16 bytes de um endereco hexadecimal");
        print_line(row, "reboot   Reinicia o sistema");
        print_line(row, "diskinfo Mostra o disco ATA");
        print_line(row, "sector   Mostra 16 bytes de um setor");
        print_line(row, "ls       Lista arquivos FAT16");
        print_line(row, "cat      Mostra o texto de um arquivo");
        print_line(row, "write    Cria ou sobrescreve um arquivo");
        print_line(row, "rm       Remove um arquivo");
        print_line(row, "color    Muda as cores do terminal");
        print_line(row, "crash    Executa testes de erro e de limite");
        print_line(row, "crash heap   Testa o alocador do kernel");
#ifdef GPT_R_GRAPHICS
        print_line(row, "demo     Abre a demonstracao VGA");
    }
    else if (equals(line, "demo")) {
        vga_graphics_demo(keyboard);
#endif
    }
    else if (equals(line, "clear")) {
        for (unsigned int i = FIRST_ROW; i < HEIGHT; ++i)
            clear_row(i);
        *row = FIRST_ROW;
    }
    else if (equals(line, "version")) {
        print_line(row, "GPT-R OS v0.4.0");
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
    else if ((argument = argument_for(line, "kmalloc"))) {
        allocate_block(row, argument);
    }
    else if ((argument = argument_for(line, "kfree"))) {
        free_block(row, argument);
    }
    else if ((argument = argument_for(line, "peek"))) {
        peek(row, argument);
    }
    else if (equals(line, "diskinfo")) {
        disk_info(row);
    }
    else if ((argument = argument_for(line, "sector"))) {
        show_sector(row, argument);
    }
    else if (equals(line, "ls")) {
        list_files(row);
    }
    else if ((argument = argument_for(line, "cat"))) {
        cat_file(row, argument);
    }
    else if ((argument = argument_for(line, "write"))) {
        edit_file(row, argument, 0);
    }
    else if ((argument = argument_for(line, "rm"))) {
        edit_file(row, argument, 1);
    }
    else if (equals(line, "reboot")) {
        /* System reset control on the QEMU PC chipset. */
        out(0xcf9, 0x02);
        out(0xcf9, 0x06);
        print_line(row, "reboot: controlador nao reiniciou");
    }
    else if ((argument = argument_for(line, "color"))) {
        change_color(row, argument);
    }
    else if ((argument = argument_for(line, "crash"))) {
        crash_test(row, argument);
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
    for (unsigned int i = 0; i < SHELL_HEAP_SLOTS; ++i)
        shell_blocks[i] = 0;

    for (;;) {
        unsigned int length = 0;
        console_put(row * WIDTH, console_color | '>');
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
                    console_put(row * WIDTH + PROMPT_WIDTH + --length, console_color | ' ');
            }
            else if (length < MAX_INPUT) {
                line[length++] = ch;
                console_put(row * WIDTH + col, console_color | ch);
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
