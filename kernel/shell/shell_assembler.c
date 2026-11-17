#include "shell_internal.h"
#include "../apps/assembler.h"
#include "../fs/file_type.h"

enum { ASM_BUFFER_PAGES = ASM_V11_SOURCE_LIMIT / PAGE_SIZE };

struct assembly_buffer {
    unsigned char *pages[ASM_BUFFER_PAGES];
};

struct assembly {
    struct assembly_buffer source, program;
    unsigned char header[APP_V11_HEADER_SIZE];
    unsigned int source_size, header_size, written;
    int no_memory;
};

static int buffer_write(struct assembly_buffer *buffer, unsigned int at, unsigned char byte)
{
    if (at / PAGE_SIZE >= ASM_BUFFER_PAGES)
        return 0;
    unsigned char **page = &buffer->pages[at / PAGE_SIZE];
    if (!*page)
        *page = page_alloc();
    if (!*page)
        return 0;
    (*page)[at % PAGE_SIZE] = byte;
    return 1;
}

static unsigned char buffer_read(const struct assembly_buffer *buffer, unsigned int at)
{
    return buffer->pages[at / PAGE_SIZE][at % PAGE_SIZE];
}

static void release_buffer(struct assembly_buffer *buffer)
{
    for (unsigned int i = 0; i < ASM_BUFFER_PAGES; ++i)
        if (buffer->pages[i])
            page_free(buffer->pages[i]);
}

static int load_source(void *context, const unsigned char *data, unsigned int size)
{
    struct assembly *work = context;
    for (unsigned int i = 0; i < size; ++i) {
        if (!buffer_write(&work->source, work->source_size, data[i])) {
            work->no_memory = 1;
            return 0;
        }
        ++work->source_size;
    }
    return 1;
}

static unsigned char source_byte(void *context, unsigned int at)
{
    const struct assembly *work = context;
    return buffer_read(&work->source, at);
}

static int emit_byte(void *context, unsigned int at, unsigned char byte)
{
    struct assembly *work = context;
    return buffer_write(&work->program, at, byte);
}

static int read_output(void *context, unsigned char sector[512], unsigned int size)
{
    struct assembly *work = context;
    for (unsigned int i = 0; i < size; ++i) {
        const unsigned int at = work->written++;
        sector[i] = at < work->header_size ? work->header[at] :
            buffer_read(&work->program, at - work->header_size);
    }
    return 1;
}

void shell_assemble(unsigned int *row, const unsigned char *argument)
{
    unsigned char input_name[FAT16_PATH_CAPACITY], output_name[FAT16_PATH_CAPACITY];
    unsigned int version = APP_FORMAT_VERSION;
    int input_length = shell_word(&argument, input_name, sizeof(input_name));
    if (input_length && shell_equals(input_name, "-f")) {
        unsigned char format[8];
        if (!shell_word(&argument, format, sizeof(format)) || !shell_equals(format, "v1.1")) {
            shell_print_error(row, "Formato de montagem esperado: v1.1");
            return;
        }
        version = APP_V11_VERSION;
        input_length = shell_word(&argument, input_name, sizeof(input_name));
    }
    const int output_length = shell_word(&argument, output_name, sizeof(output_name));
    if (!input_length || !output_length || *argument ||
        !file_has_extension((const char *)input_name, ".ASM") ||
        !file_has_extension((const char *)output_name, ".APP")) {
        shell_print_line(row, "Uso: asm [-f v1.1] <fonte.asm> <saida.APP>");
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
    const unsigned int source_limit = version == APP_V11_VERSION ? ASM_V11_SOURCE_LIMIT : ASM_V1_SOURCE_LIMIT;
    if (file.size > source_limit) {
        shell_print_number(row, "Limite do fonte: ", source_limit, " bytes");
        return;
    }

    struct assembly work = {0};
    if (fat16_read(&volume, &file, load_source, &work) != FAT16_READ_OK) {
        shell_print_error(row, work.no_memory ? "Sem memoria para o fonte" : "Falha ao ler o fonte");
        goto done;
    }
    const struct asm_io io = { &work, source_byte, emit_byte };
    struct app_info info, inspected;
    unsigned int line;
    const char *error = app_assemble(&io, work.source_size, version, &info, &line);
    if (error) {
        shell_print_number(row, "Erro na linha ", line, "");
        shell_print_error(row, error);
        goto done;
    }

    work.header_size = info.header_size;
    app_make_header(work.header, &info);
    const unsigned int length = info.header_size + info.code_size + info.data_size;
    if (!app_validate_header(work.header, work.header_size, length, &inspected)) {
        shell_print_error(row, "Falha ao validar APP montado");
        goto done;
    }
    unsigned int size = 0;
    while (output.name[size])
        ++size;
    const enum fat16_edit_result result = fat16_write_stream_at(&volume, output.directory,
        output.name, size, length, read_output, &work);
    shell_report_file_edit(row, result, "APP montado e salvo");

done:
    release_buffer(&work.program);
    release_buffer(&work.source);
}

void shell_appinfo(unsigned int *row, const unsigned char *argument)
{
    unsigned char name[FAT16_PATH_CAPACITY];
    if (!shell_word(&argument, name, sizeof(name)) || *argument ||
        !file_has_extension((const char *)name, ".APP")) {
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
        shell_print_error(row, valid == -2 ? "Versao GTRA incompativel" :
            valid == -1 ? "Falha ao ler arquivo ou cadeia FAT16" :
            "Formato APP invalido");
        return;
    }

    const int extended = info.version == APP_V11_VERSION;
    shell_print_line(row, extended ? "Formato GTRA v1.1" : "Formato GTRA v1");
    shell_print_number(row, "Cabecalho: ", info.header_size, " bytes");
    shell_print_number(row, extended ? "Codigo e constantes: " : "Codigo e dados: ", info.code_size, " bytes");
    if (extended) {
        shell_print_number(row, "Dados inicializados: ", info.data_size, " bytes");
        shell_print_number(row, "BSS: ", info.bss_size, " bytes");
        shell_print_number(row, "Total da imagem: ", info.code_size + info.data_size + info.bss_size, " bytes");
    }
    char entry[] = "Entrada: 0x0000000000000000";
    shell_hex64(entry + 11, info.entry);
    shell_print_line(row, entry);
}
