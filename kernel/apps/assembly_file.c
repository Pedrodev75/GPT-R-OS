#include "assembler.h"
#include "app_files.h"
#include "../fs/file_type.h"
#include "../memory/pages.h"

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

static int fail(struct asm_diagnostic *diagnostic, int status, const char *message)
{
    diagnostic->message = message;
    return status;
}

static int write_result(struct asm_diagnostic *diagnostic, enum fat16_edit_result result)
{
    switch (result) {
    case FAT16_EDIT_OK:
        return 0;
    case FAT16_EDIT_CLEANUP:
        return fail(diagnostic, 0, "Concluido. Falha ao liberar clusters antigos");
    case FAT16_EDIT_UNCERTAIN:
        return fail(diagnostic, APP_ERR_UNCERTAIN, "Falha de disco. Estado do destino incerto");
    case FAT16_EDIT_IO:
        return fail(diagnostic, APP_ERR_WRITE, "Falha de disco ou FAT inconsistente");
    case FAT16_EDIT_FULL:
        return fail(diagnostic, APP_ERR_LIMIT, "Disco ou diretorio cheio");
    case FAT16_EDIT_SIZE:
        return fail(diagnostic, APP_ERR_LIMIT, "Arquivo maior que a capacidade do FAT16");
    case FAT16_EDIT_NAME: case FAT16_EDIT_TEXT_EXTENSION:
        return fail(diagnostic, APP_ERR_INVALID, "Use nome 8.3 sem misturar a caixa em cada parte");
    case FAT16_EDIT_CASE:
        return fail(diagnostic, APP_ERR_ACCESS, "Ja existe um nome com outra capitalizacao");
    default:
        return fail(diagnostic, APP_ERR_ACCESS, "Entrada protegida ou com nome longo");
    }
}

static int assemble_and_write(const struct fat16_volume *volume, const struct fat16_file *file,
    const struct fat16_location *target, unsigned int name_size, unsigned int version,
    struct assembly *work, struct asm_diagnostic *diagnostic)
{
    if (fat16_read(volume, file, load_source, work) != FAT16_READ_OK)
        return work->no_memory ? fail(diagnostic, APP_ERR_MEMORY, "Sem memoria para o fonte") :
            fail(diagnostic, APP_ERR_IO, "Falha ao ler o fonte");

    const struct asm_io io = { work, source_byte, emit_byte };
    struct app_info info, inspected;
    const int result = app_assemble(&io, work->source_size, version, &info, diagnostic);
    if (result)
        return result;
    work->header_size = info.header_size;
    app_make_header(work->header, &info);
    const unsigned int length = info.header_size + info.code_size + info.data_size;
    if (!app_validate_header(work->header, work->header_size, length, &inspected))
        return fail(diagnostic, APP_ERR_INVALID, "Falha ao validar APP montado");

    return write_result(diagnostic, fat16_write_stream_at(volume, target->directory,
        target->name, name_size, length, read_output, work));
}

int app_assemble_at(const struct fat16_volume *volume, const struct fat16_path *cwd,
    const unsigned char *source, const unsigned char *target, unsigned int version,
    struct asm_diagnostic *diagnostic)
{
    *diagnostic = (struct asm_diagnostic){0};
    if ((version != APP_FORMAT_VERSION && version != APP_V11_VERSION) ||
        !file_has_extension((const char *)source, ".ASM") ||
        !file_has_extension((const char *)target, ".APP"))
        return fail(diagnostic, APP_ERR_INVALID, "Formato ou extensao invalida");

    struct fat16_file file;
    struct fat16_location input, output;
    const int found = fat16_resolve_file(volume, cwd, source, &input, &file);
    if (found != 1)
        return found < 0 ? fail(diagnostic, APP_ERR_IO, "Falha ao ler o fonte") :
            fail(diagnostic, APP_ERR_MISSING, "Arquivo nao encontrado");
    const int resolved = fat16_resolve_parent(volume, cwd, target, &output);
    if (resolved != 1)
        return resolved < 0 ? fail(diagnostic, APP_ERR_WRITE, "Falha ao ler destino") :
            fail(diagnostic, APP_ERR_INVALID, "Caminho invalido");

    const unsigned int limit = version == APP_V11_VERSION ? ASM_V11_SOURCE_LIMIT : ASM_V1_SOURCE_LIMIT;
    if (file.size > limit)
        return fail(diagnostic, APP_ERR_LIMIT, "fonte excede o limite");
    if (app_file_in_use(volume, &input, 1) || app_file_in_use(volume, &output, 0))
        return fail(diagnostic, APP_ERR_ACCESS, "Fonte ou destino em uso por um aplicativo");

    unsigned int name_size = 0;
    while (output.name[name_size])
        ++name_size;
    const enum fat16_edit_result check = fat16_check_write(volume, output.directory, output.name, name_size);
    if (check != FAT16_EDIT_OK)
        return write_result(diagnostic, check);
    struct assembly work = {0};
    const int status = assemble_and_write(volume, &file, &output, name_size, version, &work, diagnostic);
    release_buffer(&work.program);
    release_buffer(&work.source);
    return status;
}
