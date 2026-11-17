#include "shell_internal.h"
#include "../apps/assembler.h"
#include "../fs/file_type.h"

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
    if (!shell_open_volume(row, &disk, &volume))
        return;
    struct asm_diagnostic diagnostic;
    app_assemble_at(&volume, shell_directory(), input_name, output_name, version, &diagnostic);
    if (diagnostic.line)
        shell_print_number(row, "Erro na linha ", diagnostic.line, "");
    shell_print_line(row, diagnostic.message ? diagnostic.message : "APP montado e salvo");
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
