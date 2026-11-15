#include "../apps/app.h"
#include "../drivers/keyboard.h"
#include "shell_internal.h"
#include "../fs/fat16.h"

static int open_disk(unsigned int *row, struct ata_disk *disk)
{
    if (ata_identify(disk))
        return 1;
    shell_print_line(row, "Disco ATA ausente ou sem resposta");
    return 0;
}

void shell_diskinfo(unsigned int *row, const unsigned char *argument)
{
    (void)argument;
    struct ata_disk disk;
    if (!open_disk(row, &disk))
        return;
    shell_print_line(row, disk.model);
    shell_print_number(row, "Setores acessiveis: ", disk.sectors, " de 512 bytes");
    shell_print_number(row, "Capacidade acessivel: ", (unsigned long long)disk.sectors * 512, " bytes");
    struct fat16_volume volume;
    if (!fat16_mount(&volume, &disk)) {
        shell_print_line(row, "Volume FAT16 indisponivel");
        return;
    }
    unsigned int total, free;
    if (!fat16_space(&volume, &total, &free)) {
        shell_print_line(row, "Falha ao ler tabela FAT16");
        return;
    }
    const unsigned long long cluster_bytes = volume.sectors_per_cluster * 512ull;
    shell_print_number(row, "FAT16 total: ", total * cluster_bytes, " bytes");
    shell_print_number(row, "FAT16 livre: ", free * cluster_bytes, " bytes");
}

void shell_sector(unsigned int *row, const unsigned char *argument)
{
    unsigned int lba;
    const int parsed = shell_parse_decimal(argument, &lba);
    if (parsed != 1) {
        shell_print_line(row, parsed < 0 ? "LBA fora do intervalo" : "Uso: sector <LBA decimal>");
        return;
    }

    struct ata_disk disk;
    unsigned char data[512];
    if (!open_disk(row, &disk))
        return;
    if (lba >= disk.sectors) {
        shell_print_line(row, "LBA fora do disco");
        return;
    }
    if (!ata_read_sector(&disk, lba, data)) {
        shell_print_line(row, "Falha ao ler setor ATA");
        return;
    }
    shell_print_number(row, "Setor ", lba, " (primeiros 16 bytes)");
    shell_print_bytes(row, data);
}

static int open_volume(unsigned int *row, struct ata_disk *disk,
                       struct fat16_volume *volume)
{
    if (!open_disk(row, disk))
        return 0;
    if (fat16_mount(volume, disk))
        return 1;
    shell_print_line(row, "Volume FAT16 indisponivel");
    return 0;
}

static void decimal_field(char *end, unsigned int value, unsigned int width, char fill)
{
    do {
        *--end = '0' + value % 10;
        value /= 10;
    } while (--width && value);
    while (width) {
        *--end = fill;
        --width;
    }
}

static void list_file(unsigned int *row, const struct fat16_file *file)
{
    char line[] = "                           --/--/----";
    for (unsigned int i = 0; file->name[i]; ++i)
        line[i] = file->name[i];
    decimal_field(line + 24, file->size, 10, ' ');
    const unsigned int day = file->modified_date & 31;
    const unsigned int month = (file->modified_date >> 5) & 15;
    if (day && month && month <= 12) {
        decimal_field(line + 29, day, 2, '0');
        decimal_field(line + 32, month, 2, '0');
        decimal_field(line + 37, 1980 + (file->modified_date >> 9), 4, '0');
    }
    shell_print_line(row, line);
}

void shell_ls(unsigned int *row, const unsigned char *argument)
{
    (void)argument;
    struct ata_disk disk;
    struct fat16_volume volume;
    if (!open_volume(row, &disk, &volume))
        return;

    struct fat16_dir dir;
    dir.index = 0;
    struct fat16_file file;
    int result, found = 0;
    while ((result = fat16_next(&volume, &dir, &file)) > 0) {
        if (!found)
            shell_print_line(row, "Nome               Bytes   Modificado");
        list_file(row, &file);
        found = 1;
    }
    if (result < 0)
        shell_print_line(row, "Falha ao ler diretorio");
    else if (!found)
        shell_print_line(row, "Diretorio raiz vazio");
}

/* ABNT2 console glyph order, with Unicode values for UTF-8 on disk. */
static const unsigned short disk_glyphs[] = {
    0x00e7, 0x00c7,
    0x00e1, 0x00e9, 0x00ed, 0x00f3, 0x00fa, 0x00c1, 0x00c9, 0x00cd, 0x00d3, 0x00da,
    0x00e0, 0x00e8, 0x00ec, 0x00f2, 0x00f9, 0x00c0, 0x00c8, 0x00cc, 0x00d2, 0x00d9,
    0x00e3, 0x1ebd, 0x0129, 0x00f5, 0x0169, 0x00c3, 0x1ebc, 0x0128, 0x00d5, 0x0168,
    0x00e2, 0x00ea, 0x00ee, 0x00f4, 0x00fb, 0x00c2, 0x00ca, 0x00ce, 0x00d4, 0x00db
};

static const unsigned short accent_marks[] = {0x00b4, 0x0060, 0x007e, 0x005e};

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

static unsigned int encode_text(unsigned char *bytes, const unsigned char *text)
{
    unsigned int size = 0;
    while (*text) {
        const unsigned int cp = glyph_codepoint(*text++);
        if (cp < 0x80) {
            bytes[size++] = cp;
        } else if (cp < 0x800) {
            bytes[size++] = 0xc0 | (cp >> 6);
            bytes[size++] = 0x80 | (cp & 63);
        } else {
            bytes[size++] = 0xe0 | (cp >> 12);
            bytes[size++] = 0x80 | ((cp >> 6) & 63);
            bytes[size++] = 0x80 | (cp & 63);
        }
    }
    return size;
}

struct text_output {
    unsigned int *row;
    unsigned int col, codepoint, continuation, minimum;
    unsigned int lines_left;
    int paged, stopped;
};

static int page_ready(struct text_output *output)
{
    if (output->stopped)
        return 0;
    if (!output->paged || output->lines_left)
        return 1;

    static const char prompt[] = "Espaco: pagina | Enter: linha | Esc: sair";
    for (unsigned int i = 0; prompt[i] && i < WIDTH; ++i)
        console_put(*output->row * WIDTH + i, console_color | (unsigned char)prompt[i]);

    unsigned int key;
    do {
        key = shell_wait_key();
    } while (key != ' ' && key != '\n' && key != KEY_ESCAPE);
    shell_clear_row(*output->row);
    if (key == KEY_ESCAPE) {
        output->stopped = 1;
        return 0;
    }
    output->lines_left = key == ' ' ? HEIGHT - FIRST_ROW - 1 : 1;
    return 1;
}

static void text_next_row(struct text_output *output)
{
    shell_next_row(output->row);
    output->col = 0;
    if (output->paged)
        --output->lines_left;
}

static void text_byte(struct text_output *output, unsigned char ch)
{
    if (output->col == WIDTH)
        text_next_row(output);
    if (!page_ready(output))
        return;
    console_put(*output->row * WIDTH + output->col++, console_color | ch);
}

static void text_chunk(void *context, const unsigned char *data, unsigned int size)
{
    struct text_output *output = context;
    for (unsigned int i = 0; i < size && !output->stopped; ++i) {
        const unsigned char ch = data[i];
        if (output->continuation) {
            if ((ch & 0xc0) == 0x80) {
                output->codepoint = (output->codepoint << 6) | (ch & 63);
                if (!--output->continuation) {
                    unsigned int cp = output->codepoint;
                    text_byte(output, cp >= output->minimum ? codepoint_glyph(cp) : '?');
                }
                continue;
            }
            output->continuation = 0;
            text_byte(output, '?');
        }
        if (ch >= 0xc2 && ch <= 0xdf) {
            output->continuation = 1;
            output->minimum = 0x80;
            output->codepoint = ch & 31;
        } else if (ch >= 0xe0 && ch <= 0xef) {
            output->continuation = 2;
            output->minimum = 0x800;
            output->codepoint = ch & 15;
        } else if (ch >= 0xf0 && ch <= 0xf4) {
            output->continuation = 3;
            output->minimum = 0x10000;
            output->codepoint = ch & 7;
        } else if (ch == '\n') {
            if (page_ready(output))
                text_next_row(output);
        } else if (ch == '\t') {
            const unsigned int spaces = 4 - output->col % 4;
            for (unsigned int j = 0; j < spaces; ++j)
                text_byte(output, ' ');
        } else if (ch != '\r') {
            text_byte(output, ch >= 32 && ch <= 126 ? ch : '?');
        }
    }
}

static int cat_chunk(void *context, const unsigned char *data, unsigned int size)
{
    struct text_output *output = context;
    text_chunk(output, data, size);
    return !output->stopped;
}

static void finish_text(struct text_output *output)
{
    if (output->continuation)
        text_byte(output, '?');
    if (output->col)
        shell_next_row(output->row);
}

static int find_file(unsigned int *row, const struct fat16_volume *volume,
    const unsigned char *name, struct fat16_file *file)
{
    struct fat16_dir dir;
    dir.index = 0;
    int result;
    while ((result = fat16_next(volume, &dir, file)) > 0)
        if (shell_equals(name, file->name))
            return 1;
    shell_print_line(row, result < 0 ? "Falha ao ler diretorio" : "Arquivo nao encontrado");
    return 0;
}

void shell_cat(unsigned int *row, const unsigned char *argument)
{
    const unsigned char *name = shell_skip_spaces(argument);
    if (!*name) {
        shell_print_line(row, "Uso: cat <arquivo>");
        return;
    }

    struct ata_disk disk;
    struct fat16_volume volume;
    struct fat16_file file;
    if (!open_volume(row, &disk, &volume) || !find_file(row, &volume, name, &file))
        return;

    struct text_output output = {
        .row = row, .lines_left = HEIGHT - FIRST_ROW - 1, .paged = 1
    };
    const enum fat16_read_result result = fat16_read(&volume, &file, cat_chunk, &output);
    if (result != FAT16_READ_STOPPED)
        finish_text(&output);
    if (result == FAT16_READ_ERROR)
        shell_print_line(row, "Falha ao ler arquivo ou cadeia FAT16");
}

void shell_run_app(unsigned int *row, const unsigned char *argument)
{
    const unsigned char *name = shell_skip_spaces(argument);
    if (!*name) {
        shell_print_line(row, "Uso: run <arquivo.APP>");
        return;
    }

    struct ata_disk disk;
    struct fat16_volume volume;
    struct fat16_file file;
    if (!open_volume(row, &disk, &volume) || !find_file(row, &volume, name, &file))
        return;

    struct text_output output = { .row = row };
    const struct app_result result = app_run(&volume, &file, text_chunk, &output);
    finish_text(&output);
    switch (result.status) {
    case APP_EXITED:
        shell_print_number(row, "Codigo de saida: ", result.exit_code, "");
        break;
    case APP_FAULTED: {
        char rip[] = "RIP 0x0000000000000000";
        shell_print_number(row, "Falha do programa, vetor: ", result.vector, "");
        shell_hex64(rip + 6, result.rip);
        shell_print_line(row, rip);
        if (result.vector == 14) {
            char cr2[] = "CR2 0x0000000000000000";
            shell_hex64(cr2 + 6, result.fault_address);
            shell_print_line(row, cr2);
        }
        break;
    }
    case APP_INVALID:
        shell_print_line(row, "Formato APP invalido");
        break;
    case APP_READ_ERROR:
        shell_print_line(row, "Falha ao ler arquivo ou cadeia FAT16");
        break;
    case APP_NO_MEMORY:
        shell_print_line(row, "Sem memoria para executar o programa");
        break;
    }
}

static void edit_file(unsigned int *row, const unsigned char *argument, int remove)
{
    static const char *const errors[] = {
        [FAT16_EDIT_NAME] = "Use nome 8.3 sem misturar a caixa em cada parte",
        [FAT16_EDIT_TEXT_EXTENSION] = "Use .txt em minusculas",
        [FAT16_EDIT_CASE] = "Ja existe um nome com outra capitalizacao",
        [FAT16_EDIT_SIZE] = "Texto maior que um setor",
        [FAT16_EDIT_MISSING] = "Arquivo nao encontrado",
        [FAT16_EDIT_PROTECTED] = "Entrada protegida ou com nome longo",
        [FAT16_EDIT_FULL] = "Disco ou diretorio cheio",
        [FAT16_EDIT_IO] = "Falha de disco ou FAT inconsistente",
        [FAT16_EDIT_CLEANUP] = "Diretorio atualizado; falha ao liberar clusters antigos"
    };
    const unsigned char *name = shell_skip_spaces(argument);
    const unsigned char *end = name;
    while (*end && *end != ' ')
        ++end;
    const unsigned char *content = shell_skip_spaces(end);
    if (name == end || (remove ? *content != 0 : *content == 0)) {
        shell_print_line(row, remove ? "Uso: rm <arquivo>" : "Uso: write <arquivo> <texto>");
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
        shell_print_line(row, remove ? "Arquivo removido" : "Arquivo salvo");
    else
        shell_print_line(row, errors[result]);
}

void shell_write(unsigned int *row, const unsigned char *argument)
{
    edit_file(row, argument, 0);
}

void shell_rm(unsigned int *row, const unsigned char *argument)
{
    edit_file(row, argument, 1);
}
