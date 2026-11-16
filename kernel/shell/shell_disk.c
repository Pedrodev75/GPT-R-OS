#include "../desktop/desktop.h"
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

int shell_open_volume(unsigned int *row, struct ata_disk *disk,
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
    if (file->is_directory) {
        for (unsigned int i = 14; i < 26; ++i)
            line[i] = ' ';
        line[21] = 'D';
        line[22] = 'I';
        line[23] = 'R';
    }
    shell_print_line(row, line);
}

void shell_ls(unsigned int *row, const unsigned char *argument)
{
    (void)argument;
    struct ata_disk disk;
    struct fat16_volume volume;
    if (!shell_open_volume(row, &disk, &volume))
        return;

    struct fat16_path path;
    if (fat16_resolve_dir(&volume, shell_directory(), argument, &path) != 1) {
        shell_print_line(row, "Pasta nao encontrada");
        return;
    }
    struct fat16_dir dir = { .first_cluster = fat16_path_cluster(&path) };
    struct fat16_file file;
    int result, found = 0;
    while ((result = fat16_next_at(&volume, &dir, &file)) > 0) {
        if (!found)
            shell_print_line(row, "Nome               Bytes   Modificado");
        list_file(row, &file);
        found = 1;
    }
    if (result < 0)
        shell_print_line(row, "Falha ao ler diretorio");
    else if (!found)
        shell_print_line(row, "Pasta vazia");
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

unsigned char shell_codepoint_glyph(unsigned int codepoint)
{
    if (codepoint >= 32 && codepoint <= 126)
        return codepoint;
    for (unsigned int i = 0; i < sizeof(disk_glyphs) / sizeof(disk_glyphs[0]); ++i)
        if (codepoint == disk_glyphs[i])
            return 0x80 + i;
    return codepoint == 0x00b4 ? 0xf0 : '?';
}

unsigned int shell_encode_glyph(unsigned char *bytes, unsigned char glyph)
{
    const unsigned int cp = glyph_codepoint(glyph);
    if (cp < 0x80) {
        bytes[0] = cp;
        return 1;
    }
    if (cp < 0x800) {
        bytes[0] = 0xc0 | (cp >> 6);
        bytes[1] = 0x80 | (cp & 63);
        return 2;
    }
    bytes[0] = 0xe0 | (cp >> 12);
    bytes[1] = 0x80 | ((cp >> 6) & 63);
    bytes[2] = 0x80 | (cp & 63);
    return 3;
}

static unsigned int encode_text(unsigned char *bytes, const unsigned char *text)
{
    unsigned int size = 0;
    while (*text)
        size += shell_encode_glyph(bytes + size, *text++);
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
                    text_byte(output, cp >= output->minimum ? shell_codepoint_glyph(cp) : '?');
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

int shell_lookup_file(const struct fat16_volume *volume,
    const unsigned char *name, struct fat16_file *file)
{
    return fat16_resolve_file(volume, shell_directory(), name, 0, file);
}

int shell_find_file(unsigned int *row, const struct fat16_volume *volume,
    const unsigned char *name, struct fat16_file *file)
{
    const int result = shell_lookup_file(volume, name, file);
    if (result == 1)
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
    if (!shell_open_volume(row, &disk, &volume) || !shell_find_file(row, &volume, name, &file))
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

#ifndef GPT_R_GRAPHICS
static unsigned int read_app_line(void *context, unsigned char *buffer,
    unsigned int capacity)
{
    struct text_output *output = context;
    if (output->continuation) {
        text_byte(output, '?');
        output->continuation = 0;
    }
    if (output->col)
        text_next_row(output);

    unsigned int length = 0;
    for (;;) {
        shell_move_cursor(*output->row, output->col);
        const unsigned int key = shell_wait_key();
        if (key == KEY_INTERRUPT)
            return capacity;
        if (key == '\n')
            break;
        if (key == '\b' && length) {
            do {
                --length;
            } while (length && (buffer[length] & 0xc0) == 0x80);
            console_put(*output->row * WIDTH + --output->col, console_color | ' ');
        } else if (key >= ' ' && key <= 255 && output->col < WIDTH - 1) {
            unsigned char bytes[3];
            const unsigned int size = shell_encode_glyph(bytes, key);
            if (size < capacity - length) {
                for (unsigned int i = 0; i < size; ++i)
                    buffer[length++] = bytes[i];
                text_byte(output, key);
            }
        }
    }
    buffer[length] = 0;
    text_next_row(output);
    return length;
}

#endif

static int pack_arguments(const unsigned char *text, unsigned char *args,
    struct app_launch *launch)
{
    for (text = shell_skip_spaces(text); *text; text = shell_skip_spaces(text)) {
        if (launch->argc == APP_ARG_LIMIT)
            return 0;
        ++launch->argc;
        while (*text && *text != ' ') {
            unsigned char bytes[3];
            const unsigned int size = shell_encode_glyph(bytes, *text++);
            if (size >= APP_ARG_BYTES - launch->args_size)
                return 0;
            for (unsigned int i = 0; i < size; ++i)
                args[launch->args_size++] = bytes[i];
        }
        args[launch->args_size++] = 0;
    }
    return 1;
}

void shell_run_app(unsigned int *row, const unsigned char *argument)
{
    unsigned char args[APP_ARG_BYTES];
    struct app_launch launch = { .args = args, .directory = shell_directory() };
    if (!pack_arguments(argument, args, &launch)) {
        shell_print_line(row, "Argumentos demais para run");
        return;
    }
    if (!launch.argc) {
        shell_print_line(row, "Uso: run <arquivo.APP> [argumentos]");
        return;
    }

    struct ata_disk disk;
    struct fat16_volume volume;
    struct fat16_file file;
    if (!shell_open_volume(row, &disk, &volume) || !shell_find_file(row, &volume, args, &file))
        return;

#ifdef GPT_R_GRAPHICS
    const enum app_status status = desktop_open_app(&volume, &file, &launch);
    switch (status) {
#else
    struct text_output output = { .row = row };
    launch.output = text_chunk;
    launch.input = read_app_line;
    launch.context = &output;
    const struct app_result result = app_run(&volume, &file, &launch);
    finish_text(&output);
    switch (result.status) {
    case APP_SAVE_ERROR:
        shell_print_line(row, "Falha ao salvar arquivo");
        break;
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
    case APP_INTERRUPTED:
        shell_discard_input();
        shell_print_line(row, "Programa interrompido (Ctrl+C)");
        break;
#endif
    case APP_INVALID:
        shell_print_line(row, "Formato APP invalido");
        break;
    case APP_READ_ERROR:
        shell_print_line(row, "Falha ao ler arquivo ou cadeia FAT16");
        break;
    case APP_BAD_ARGS:
        shell_print_line(row, "Argumentos invalidos");
        break;
    case APP_NO_MEMORY:
        shell_print_line(row, "Sem memoria para executar o programa");
        break;
    case APP_LIMIT:
        shell_print_line(row, "Limite de janelas APP. Feche uma janela.");
        break;
    default:
        break;
    }
}

const char *shell_file_error(enum fat16_edit_result result)
{
    static const char *const errors[] = {
        [FAT16_EDIT_NAME] = "Use nome 8.3 sem misturar a caixa em cada parte",
        [FAT16_EDIT_TEXT_EXTENSION] = "Use .txt em minusculas",
        [FAT16_EDIT_CASE] = "Ja existe um nome com outra capitalizacao",
        [FAT16_EDIT_SIZE] = "Arquivo maior que a capacidade do FAT16",
        [FAT16_EDIT_MISSING] = "Arquivo nao encontrado",
        [FAT16_EDIT_PROTECTED] = "Entrada protegida ou com nome longo",
        [FAT16_EDIT_FULL] = "Disco ou diretorio cheio",
        [FAT16_EDIT_IO] = "Falha de disco ou FAT inconsistente",
        [FAT16_EDIT_CLEANUP] = "Concluido. Falha ao liberar clusters antigos",
        [FAT16_EDIT_EXISTS] = "O destino ja existe",
        [FAT16_EDIT_PARTIAL] = "Destino salvo. Falha ao remover a origem"
    };
    return result > FAT16_EDIT_OK && (unsigned int)result < sizeof(errors) / sizeof(errors[0]) ?
        errors[result] : 0;
}

void shell_report_file_edit(unsigned int *row, enum fat16_edit_result result, const char *success)
{
    const char *error = shell_file_error(result);
    shell_print_line(row, error ? error : success);
}

static void edit_file(unsigned int *row, const unsigned char *argument, int remove)
{
    unsigned char path[FAT16_PATH_CAPACITY];
    if (!shell_word(&argument, path, sizeof(path)) || (remove ? *argument != 0 : *argument == 0)) {
        shell_print_line(row, remove ? "Uso: rm <arquivo>" : "Uso: write <arquivo> <texto>");
        return;
    }
    unsigned char data[MAX_INPUT * 3];
    const unsigned int length = remove ? 0 : encode_text(data, argument);

    struct ata_disk disk;
    struct fat16_volume volume;
    struct fat16_location entry;
    if (!shell_open_volume(row, &disk, &volume))
        return;
    if (fat16_resolve_parent(&volume, shell_directory(), path, &entry) != 1) {
        shell_print_line(row, "Caminho invalido");
        return;
    }
    unsigned int name_size = 0;
    while (entry.name[name_size])
        ++name_size;
    const enum fat16_edit_result result = remove ?
        fat16_remove_at(&volume, entry.directory, entry.name, name_size) :
        fat16_write_at(&volume, entry.directory, entry.name, name_size, data, length);
    shell_report_file_edit(row, result, remove ? "Arquivo removido" : "Arquivo salvo");
}

void shell_write(unsigned int *row, const unsigned char *argument)
{
    edit_file(row, argument, 0);
}

void shell_rm(unsigned int *row, const unsigned char *argument)
{
    edit_file(row, argument, 1);
}
