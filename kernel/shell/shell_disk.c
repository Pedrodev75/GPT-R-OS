#include "../apps/app.h"
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
        char label[14];
        unsigned int i = 0;
        while (file.name[i]) {
            label[i] = file.name[i];
            ++i;
        }
        label[i++] = ' ';
        label[i] = 0;
        shell_print_number(row, label, file.size, " bytes");
        found = 1;
    }
    if (result < 0)
        shell_print_line(row, "Falha ao ler diretorio");
    else if (!found)
        shell_print_line(row, "Diretorio raiz vazio");
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
};

static void text_byte(struct text_output *output, unsigned char ch)
{
    if (output->col == WIDTH) {
        shell_next_row(output->row);
        output->col = 0;
    }
    console_put(*output->row * WIDTH + output->col++, console_color | ch);
}

static void text_chunk(void *context, const unsigned char *data, unsigned int size)
{
    struct text_output *output = context;
    for (unsigned int i = 0; i < size; ++i) {
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
            shell_next_row(output->row);
            output->col = 0;
        } else if (ch == '\t') {
            const unsigned int spaces = 4 - output->col % 4;
            for (unsigned int j = 0; j < spaces; ++j)
                text_byte(output, ' ');
        } else if (ch != '\r') {
            text_byte(output, ch >= 32 && ch <= 126 ? ch : '?');
        }
    }
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
        if (same_filename(name, file->name))
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

    struct text_output output = { .row = row };
    const int read_ok = fat16_read(&volume, &file, text_chunk, &output);
    finish_text(&output);
    if (!read_ok)
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
        [FAT16_EDIT_NAME] = "Nome 8.3 invalido",
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
