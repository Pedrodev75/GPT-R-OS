#include "shell_internal.h"
#include "../apps/app.h"
#include "../apps/assembler.h"

enum { SOURCE_LIMIT = 4096 };
static unsigned char source[SOURCE_LIMIT];
static unsigned char program[APP_HEADER_SIZE + APP_IMAGE_LIMIT];

struct source_reader {
    unsigned int written;
};

static int load_source(void *context, const unsigned char *data, unsigned int size)
{
    struct source_reader *reader = context;
    if (size > sizeof(source) - reader->written)
        return 0;
    for (unsigned int i = 0; i < size; ++i)
        source[reader->written++] = data[i];
    return 1;
}

void shell_assemble(unsigned int *row, const unsigned char *argument)
{
    unsigned char input_name[FAT16_PATH_CAPACITY], output_name[FAT16_PATH_CAPACITY];
    const int input_length = shell_word(&argument, input_name, sizeof(input_name));
    const int output_length = shell_word(&argument, output_name, sizeof(output_name));
    if (!input_length || !output_length || *argument) {
        shell_print_line(row, "Uso: asm <fonte.asm> <saida.APP>");
        return;
    }
    if (shell_equals(input_name, (const char *)output_name)) {
        shell_print_line(row, "Escolha outro nome para o executavel");
        return;
    }

    struct ata_disk disk;
    struct fat16_volume volume;
    struct fat16_file file;
    if (!shell_open_volume(row, &disk, &volume) ||
        !shell_find_file(row, &volume, input_name, &file))
        return;
    struct fat16_location output, input;
    if (fat16_resolve_parent(&volume, shell_directory(), input_name, &input) != 1 ||
        fat16_resolve_parent(&volume, shell_directory(), output_name, &output) != 1) {
        shell_print_line(row, "Caminho invalido");
        return;
    }
    if (input.directory == output.directory && shell_equals(input.name, (const char *)output.name)) {
        shell_print_line(row, "Escolha outro nome para o executavel");
        return;
    }
    if (file.size > SOURCE_LIMIT) {
        shell_print_number(row, "Limite do fonte: ", SOURCE_LIMIT, " bytes");
        return;
    }

    struct source_reader reader = {0};
    if (fat16_read(&volume, &file, load_source, &reader) != FAT16_READ_OK) {
        shell_print_line(row, "Falha ao ler o fonte");
        return;
    }
    unsigned int code_size, entry, line;
    const char *error = app_assemble(source, reader.written, program + APP_HEADER_SIZE,
        &code_size, &entry, &line);
    if (error) {
        shell_print_number(row, "Erro na linha ", line, "");
        shell_print_line(row, error);
        return;
    }

    app_make_header(program, code_size, entry);
    unsigned int size = 0;
    while (output.name[size])
        ++size;
    const enum fat16_edit_result result =
        fat16_write_at(&volume, output.directory, output.name, size, program, APP_HEADER_SIZE + code_size);
    shell_report_file_edit(row, result, "APP montado e salvo");
}

void shell_appinfo(unsigned int *row, const unsigned char *argument)
{
    unsigned char name[FAT16_PATH_CAPACITY];
    if (!shell_word(&argument, name, sizeof(name)) || *argument) {
        shell_print_error(row, "Uso: appinfo <arquivo.APP>");
        return;
    }

    struct ata_disk disk;
    struct fat16_volume volume;
    struct fat16_file file;
    if (!shell_open_volume(row, &disk, &volume) ||
        !shell_find_file(row, &volume, name, &file))
        return;
    struct app_info info;
    const int valid = app_inspect(&volume, &file, &info);
    if (valid <= 0) {
        shell_print_error(row, valid < 0 ? "Falha ao ler arquivo ou cadeia FAT16" :
            "Formato APP invalido");
        return;
    }

    shell_print_number(row, "Formato GTRA v", APP_FORMAT_VERSION, "");
    shell_print_number(row, "Cabecalho: ", APP_HEADER_SIZE, " bytes");
    shell_print_number(row, "Codigo e dados: ", info.image_size, " bytes");
    char entry[] = "Entrada: 0x0000000000000000";
    shell_hex64(entry + 11, info.entry);
    shell_print_line(row, entry);
}
