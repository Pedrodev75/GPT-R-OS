#include "shell_internal.h"
#include "../apps/app.h"
#include "../apps/app_files.h"
#include "../fs/file_type.h"

static unsigned char upper(unsigned char ch)
{
    return ch >= 'a' && ch <= 'z' ? ch - ('a' - 'A') : ch;
}

static int name_equal(const unsigned char *a, const unsigned char *b)
{
    while (*a && upper(*a) == upper(*b)) {
        ++a;
        ++b;
    }
    return !*a && !*b;
}

/* A command is an 8.3 basename without its extension. */
static unsigned int app_name(const unsigned char *base, int has_extension, unsigned char name[13])
{
    unsigned int length = 0;
    while (*base && (!has_extension || *base != '.')) {
        const unsigned char ch = upper(*base++);
        if (length == 8 || !((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
            ch == '_' || ch == '-'))
            return 0;
        name[length++] = ch;
    }
    if (!length || (has_extension && !name_equal(base, (const unsigned char *)".APP")))
        return 0;
    for (const unsigned char *ext = (const unsigned char *)".APP"; *ext;)
        name[length++] = *ext++;
    name[length] = 0;
    return length;
}

static int find_app(const struct fat16_volume *volume, unsigned int directory,
    const unsigned char *name, struct fat16_file *file)
{
    struct fat16_dir scan = { .first_cluster = directory };
    int result;
    while ((result = fat16_next_at(volume, &scan, file)) == 1)
        if (name_equal(name, (const unsigned char *)file->name))
            return 1;
    return result;
}

const char *shell_sys64_app(const struct fat16_volume *volume, const unsigned char *name,
    struct fat16_file *file, unsigned int *directory, unsigned char path[SYS64_APP_PATH])
{
    unsigned char filename[13];
    if (!app_name(name, 0, filename))
        return "Comando desconhecido";
    struct fat16_file folder;
    const enum fat16_edit_result status = fat16_sys64_directory(volume, &folder);
    if (status != FAT16_EDIT_OK)
        return status == FAT16_EDIT_MISSING ? "Comando desconhecido" : "SYS64 indisponivel";
    const int found = find_app(volume, folder.first_cluster, filename, file);
    if (found != 1)
        return found < 0 ? "Falha ao ler SYS64" : "Comando desconhecido";
    if (file->is_directory)
        return "O comando no SYS64 nao e um APP";
    *directory = folder.first_cluster;
    unsigned int length = 0;
    path[length++] = '/';
    for (unsigned int i = 0; folder.name[i]; ++i)
        path[length++] = folder.name[i];
    path[length++] = '/';
    for (unsigned int i = 0; file->name[i]; ++i)
        path[length++] = file->name[i];
    path[length] = 0;
    return 0;
}

void shell_sysinstall(unsigned int *row, const unsigned char *argument)
{
    unsigned char source[FAT16_PATH_CAPACITY], target[13] = {0}, filename[13];
    int size = shell_word(&argument, source, sizeof(source));
    const int replace = size && shell_equals(source, "-u");
    if (replace)
        size = shell_word(&argument, source, sizeof(source));
    if (!size || !file_has_extension((const char *)source, ".APP") ||
        (*argument && !shell_word(&argument, target, sizeof(target))) || *argument) {
        shell_print_error(row, "Uso: sysinstall [-u] <origem.APP> [NOME.APP]");
        return;
    }
    struct ata_disk disk;
    struct fat16_volume volume;
    if (!shell_open_volume(row, &disk, &volume))
        return;
    struct fat16_location input;
    struct fat16_file app;
    if (fat16_resolve_file(&volume, shell_directory(), source, &input, &app) != 1) {
        shell_print_error(row, "Falha ao abrir o APP de origem");
        return;
    }
    unsigned int length = app_name(target[0] ? target : input.name, 1, filename);
    if (!length) {
        shell_print_error(row, "Use um nome APP curto para o destino");
        return;
    }
    if (app_file_in_use(&volume, &input, 1)) {
        shell_print_error(row, "APP de origem em uso para escrita");
        return;
    }
    struct app_info info;
    const int valid = app_inspect(&volume, &app, &info);
    if (valid != 1) {
        shell_print_error(row, valid == -1 ? "Falha ao ler o APP de origem" : "Formato APP invalido");
        return;
    }

    struct fat16_file folder, existing;
    const enum fat16_edit_result status = fat16_sys64_ensure(&volume, &folder);
    if (status != FAT16_EDIT_OK) {
        shell_report_file_edit(row, status, "Falha ao abrir SYS64");
        return;
    }
    const int found = find_app(&volume, folder.first_cluster, filename, &existing);
    if (found < 0) {
        shell_print_error(row, "Falha ao ler SYS64");
        return;
    }
    if (found && !replace) {
        shell_print_error(row, "Destino ja existe. Use sysinstall -u");
        return;
    }
    if (replace && !found) {
        shell_print_error(row, "APP de destino ausente no SYS64");
        return;
    }
    if (found) {
        struct fat16_location target_entry = { .directory = folder.first_cluster };
        length = 0;
        while (existing.name[length]) {
            target_entry.name[length] = filename[length] = existing.name[length];
            ++length;
        }
        filename[length] = 0;
        if (existing.is_directory || app_file_in_use(&volume, &target_entry, 0) ||
            app_directory_running(&volume, folder.first_cluster)) {
            shell_print_error(row, "Destino protegido ou SYS64 em uso");
            return;
        }
        if (input.directory == folder.first_cluster && name_equal(input.name, filename)) {
            shell_print_error(row, "Origem e destino sao o mesmo APP");
            return;
        }
    }
    const enum fat16_edit_result result = fat16_sys64_install(&volume, &app, filename, length, replace);
    shell_report_file_edit(row, result, replace ? "APP atualizado no SYS64" : "APP instalado no SYS64");
}
