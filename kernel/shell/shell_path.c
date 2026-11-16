#include "shell_internal.h"

static struct fat16_path cwd;

const struct fat16_path *shell_directory(void)
{
    return &cwd;
}

int shell_word(const unsigned char **argument, unsigned char *word, unsigned int capacity)
{
    const unsigned char *p = shell_skip_spaces(*argument);
    unsigned int length = 0;
    while (*p && *p != ' ') {
        if (length + 1 >= capacity)
            return 0;
        word[length++] = *p++;
    }
    word[length] = 0;
    *argument = shell_skip_spaces(p);
    return length;
}

void shell_pwd(unsigned int *row, const unsigned char *argument)
{
    (void)argument;
    char path[FAT16_PATH_CAPACITY];
    fat16_format_path(&cwd, path);
    char line[WIDTH + 1];
    unsigned int used = 0;
    for (const char *p = path; *p; ++p) {
        line[used++] = *p;
        if (used == WIDTH) {
            line[used] = 0;
            shell_print_line(row, line);
            used = 0;
        }
    }
    if (used) {
        line[used] = 0;
        shell_print_line(row, line);
    }
}

void shell_cd(unsigned int *row, const unsigned char *argument)
{
    unsigned char path[FAT16_PATH_CAPACITY];
    if (!shell_word(&argument, path, sizeof(path)) || *argument) {
        shell_print_line(row, "Uso: cd <pasta>");
        return;
    }
    struct ata_disk disk;
    struct fat16_volume volume;
    if (!shell_open_volume(row, &disk, &volume))
        return;
    struct fat16_path next;
    const int result = fat16_resolve_dir(&volume, &cwd, path, &next);
    if (result == 1)
        cwd = next;
    else
        shell_print_line(row, result < 0 ? "Falha ao ler pasta" : "Pasta nao encontrada");
}

static unsigned int word_length(const unsigned char *text)
{
    unsigned int length = 0;
    while (text[length])
        ++length;
    return length;
}

void shell_mkdir(unsigned int *row, const unsigned char *argument)
{
    unsigned char path[FAT16_PATH_CAPACITY];
    if (!shell_word(&argument, path, sizeof(path)) || *argument) {
        shell_print_line(row, "Uso: mkdir <pasta>");
        return;
    }
    struct ata_disk disk;
    struct fat16_volume volume;
    struct fat16_location entry;
    if (!shell_open_volume(row, &disk, &volume))
        return;
    if (fat16_resolve_parent(&volume, &cwd, path, &entry) != 1) {
        shell_print_line(row, "Caminho invalido");
        return;
    }
    const enum fat16_edit_result result =
        fat16_mkdir(&volume, entry.directory, entry.name, word_length(entry.name));
    shell_report_file_edit(row, result, "Pasta criada");
}

static void transfer(unsigned int *row, const unsigned char *argument, int move)
{
    unsigned char source[FAT16_PATH_CAPACITY], target[FAT16_PATH_CAPACITY];
    if (!shell_word(&argument, source, sizeof(source)) || !shell_word(&argument, target, sizeof(target)) ||
        *argument) {
        shell_print_line(row, move ? "Uso: mv <arquivo> <destino>" : "Uso: cp <arquivo> <destino>");
        return;
    }
    struct ata_disk disk;
    struct fat16_volume volume;
    struct fat16_location from, to;
    struct fat16_path folder;
    if (!shell_open_volume(row, &disk, &volume))
        return;
    if (fat16_resolve_parent(&volume, &cwd, source, &from) != 1) {
        shell_print_line(row, "Caminho de origem invalido");
        return;
    }
    const int is_dir = fat16_resolve_dir(&volume, &cwd, target, &folder);
    if (is_dir == 1) {
        to = from;
        to.directory = fat16_path_cluster(&folder);
    } else if (is_dir < 0 || fat16_resolve_parent(&volume, &cwd, target, &to) != 1) {
        shell_print_line(row, "Caminho de destino invalido");
        return;
    }
    const enum fat16_edit_result result = fat16_transfer(&volume,
        from.directory, from.name, to.directory, to.name, move);
#ifdef GPT_R_GRAPHICS
    if (move && (result == FAT16_EDIT_OK || result == FAT16_EDIT_CLEANUP))
        shell_editor_moved(from.directory, from.name, to.directory, to.name);
#endif
    shell_report_file_edit(row, result, move ? "Arquivo movido" : "Arquivo copiado");
}

void shell_cp(unsigned int *row, const unsigned char *argument)
{
    transfer(row, argument, 0);
}

void shell_mv(unsigned int *row, const unsigned char *argument)
{
    transfer(row, argument, 1);
}
