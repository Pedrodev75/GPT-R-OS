#include "trash.h"

#ifdef GPT_R_GRAPHICS
#include "tool_view.h"
#include "../drivers/keyboard.h"
#include "../drivers/timer.h"
#include "../shell/shell_internal.h"

enum { RECORD_NAME = 12, ROWS = 7, ROW_TOP = 57, ROW_HEIGHT = 33, FOOTER = 346 };
static const unsigned char receipt_magic[] = {'G', 'T', 'L', '1'};
static const unsigned char trash_folder[] = "LIXEIRA";

struct trash_entry {
    unsigned char receipt[RECORD_NAME + 1];
    char path[FAT16_PATH_CAPACITY];
};

static struct {
    struct ata_disk disk;
    struct fat16_volume volume;
    struct trash_entry entries[ROWS];
    unsigned int directory, seen, page, count, selected;
    int more, mounted;
    const char *notice;
} bin;

static enum fat16_edit_result open_folder(const struct fat16_volume *volume,
    unsigned int *directory, int create)
{
    struct fat16_file file;
    const int config = fat16_find_at(volume, 0, (const unsigned char *)"CONFIG", &file);
    if (config <= 0)
        return config < 0 ? FAT16_EDIT_IO : FAT16_EDIT_MISSING;
    if (!file.is_directory || !file.first_cluster)
        return FAT16_EDIT_PROTECTED;
    const unsigned int parent = file.first_cluster;
    int found = fat16_find_at(volume, parent, trash_folder, &file);
    if (!found && create) {
        const enum fat16_edit_result result = fat16_mkdir(volume, parent,
            trash_folder, sizeof(trash_folder) - 1);
        if (result != FAT16_EDIT_OK)
            return result;
        found = fat16_find_at(volume, parent, trash_folder, &file);
    }
    if (found <= 0)
        return found < 0 ? FAT16_EDIT_IO : FAT16_EDIT_MISSING;
    if (!file.is_directory || !file.first_cluster)
        return FAT16_EDIT_PROTECTED;
    *directory = file.first_cluster;
    return FAT16_EDIT_OK;
}

static void payload_name(unsigned char *payload, const unsigned char *receipt)
{
    for (unsigned int i = 0; i < 9; ++i)
        payload[i] = receipt[i];
    payload[9] = 'D';
    payload[10] = 'A';
    payload[11] = 'T';
    payload[12] = 0;
}

static int receipt_name(const char *name)
{
    if (name[0] != 'T')
        return 0;
    for (unsigned int i = 1; i < 8; ++i)
        if (!((name[i] >= '0' && name[i] <= '9') || (name[i] >= 'A' && name[i] <= 'F')))
            return 0;
    return shell_equals((const unsigned char *)name + 8, ".INF");
}

static enum fat16_edit_result reserve_receipt(const struct fat16_volume *volume,
    unsigned int directory, unsigned char *receipt, unsigned char *payload)
{
    const unsigned int first = timer_ticks();
    for (unsigned int attempt = 0; attempt < 256; ++attempt) {
        unsigned int id = first + attempt;
        receipt[0] = 'T';
        for (unsigned int i = 7; i; --i, id >>= 4)
            receipt[i] = "0123456789ABCDEF"[id & 15];
        receipt[8] = '.';
        receipt[9] = 'I';
        receipt[10] = 'N';
        receipt[11] = 'F';
        receipt[12] = 0;
        payload_name(payload, receipt);
        struct fat16_file existing;
        const int found = fat16_find_at(volume, directory, payload, &existing);
        if (found < 0)
            return FAT16_EDIT_IO;
        if (found)
            continue;
        const enum fat16_edit_result result =
            fat16_create(volume, directory, receipt, RECORD_NAME);
        if (result != FAT16_EDIT_EXISTS)
            return result;
    }
    return FAT16_EDIT_FULL;
}

static int restorable_path(const char *path, unsigned int length)
{
    if (length < 2 || path[0] != '/' || path[length - 1] == '/')
        return 0;
    for (unsigned int i = 0; i < length; ++i)
        if ((unsigned char)path[i] <= ' ' || (unsigned char)path[i] == 0x7f)
            return 0;
    return 1;
}

const char *trash_send(const struct fat16_volume *volume, const struct fat16_path *directory,
    const struct fat16_file *file)
{
    const unsigned char *name = (const unsigned char *)file->name;
    const unsigned int from = fat16_path_cluster(directory);
    if (file->is_directory)
        return "Selecione um arquivo";
    if (shell_editor_has_file(from, name))
        return "Feche o arquivo no Editor";
    struct fat16_file current;
    const int origin = fat16_lookup_at(volume, from, name, &current);
    if (origin < 0)
        return "Falha ao ler a origem";
    if (!origin || current.first_cluster != file->first_cluster || current.size != file->size ||
        current.modified_date != file->modified_date)
        return "Origem mudou. Selecione de novo";

    char path[FAT16_PATH_CAPACITY];
    fat16_format_path(directory, path);
    unsigned int length = 0;
    while (path[length])
        ++length;
    if (length > 1)
        path[length++] = '/';
    for (unsigned int i = 0; name[i]; ++i) {
        if (length == sizeof(path) - 1)
            return "Caminho longo demais";
        path[length++] = name[i];
    }
    path[length] = 0;
    const struct fat16_path root = {0};
    struct fat16_location original;
    if (!restorable_path(path, length) ||
        fat16_resolve_parent(volume, &root, (const unsigned char *)path, &original) != 1 ||
        original.directory != from || !shell_equals(original.name, (const char *)name))
        return "Caminho original nao restauravel";

    unsigned int storage;
    enum fat16_edit_result result = open_folder(volume, &storage, 1);
    if (result != FAT16_EDIT_OK)
        return shell_file_error(result);
    if (from == storage)
        return "Arquivo ja esta na Lixeira";
    unsigned char receipt[RECORD_NAME + 1], payload[RECORD_NAME + 1];
    result = reserve_receipt(volume, storage, receipt, payload);
    if (result != FAT16_EDIT_OK)
        return shell_file_error(result);

    unsigned char record[sizeof(receipt_magic) + FAT16_PATH_CAPACITY];
    for (unsigned int i = 0; i < sizeof(receipt_magic); ++i)
        record[i] = receipt_magic[i];
    for (unsigned int i = 0; i < length; ++i)
        record[sizeof(receipt_magic) + i] = path[i];
    result = fat16_write_at(volume, storage, receipt, RECORD_NAME,
        record, sizeof(receipt_magic) + length);
    if (result == FAT16_EDIT_OK || result == FAT16_EDIT_CLEANUP) {
        result = fat16_transfer(volume, from, name, storage, payload, 1);
        if (result == FAT16_EDIT_OK)
            return 0;
        if (result == FAT16_EDIT_PARTIAL || result == FAT16_EDIT_CLEANUP)
            return shell_file_error(result);
        struct fat16_file copied;
        if (result != FAT16_EDIT_EXISTS && fat16_lookup_at(volume, storage, payload, &copied) != 0)
            return shell_file_error(result);
    }
    fat16_remove_at(volume, storage, receipt, RECORD_NAME);
    return shell_file_error(result);
}

static int read_entry(const struct fat16_file *file, struct trash_entry *entry)
{
    unsigned char record[sizeof(receipt_magic) + FAT16_PATH_CAPACITY];
    if (file->is_directory || !receipt_name(file->name) || file->size <= sizeof(receipt_magic) ||
        file->size >= sizeof(record) || !fat16_read_range(&bin.volume, file, 0, record, file->size))
        return 0;
    for (unsigned int i = 0; i < sizeof(receipt_magic); ++i)
        if (record[i] != receipt_magic[i])
            return 0;
    const unsigned int length = file->size - sizeof(receipt_magic);
    for (unsigned int i = 0; i < length; ++i)
        entry->path[i] = record[sizeof(receipt_magic) + i];
    entry->path[length] = 0;
    if (!restorable_path(entry->path, length))
        return 0;
    for (unsigned int i = 0; i <= RECORD_NAME; ++i)
        entry->receipt[i] = file->name[i];
    unsigned char payload[RECORD_NAME + 1];
    payload_name(payload, entry->receipt);
    struct fat16_file value;
    return fat16_lookup_at(&bin.volume, bin.directory, payload, &value) == 1;
}

static void scan(unsigned int page)
{
    bin.seen = fat16_mutations();
    bin.page = page;
    bin.selected = ROWS;
    bin.count = bin.more = 0;
    if (!bin.mounted)
        return;
    const enum fat16_edit_result folder = open_folder(&bin.volume, &bin.directory, 0);
    if (folder != FAT16_EDIT_OK) {
        bin.directory = 0;
        if (folder != FAT16_EDIT_MISSING)
            bin.notice = shell_file_error(folder);
        return;
    }
    struct fat16_dir dir = { .first_cluster = bin.directory };
    struct fat16_file file;
    unsigned int skip = page * ROWS;
    int result;
    while ((result = fat16_next_at(&bin.volume, &dir, &file)) > 0) {
        struct trash_entry entry;
        if (!read_entry(&file, &entry))
            continue;
        if (skip) {
            --skip;
            continue;
        }
        if (bin.count == ROWS) {
            bin.more = 1;
            return;
        }
        bin.entries[bin.count++] = entry;
    }
    if (result < 0)
        bin.notice = "Falha ao ler a Lixeira";
}

void trash_init(void)
{
    bin.notice = 0;
    bin.mounted = ata_identify(&bin.disk) && fat16_mount(&bin.volume, &bin.disk);
    if (!bin.mounted)
        bin.notice = "Volume FAT16 indisponivel";
    scan(0);
}

int trash_refresh(void)
{
    if (!bin.mounted || bin.seen == fat16_mutations())
        return 0;
    if (bin.directory && !fat16_dir_changed(bin.directory, bin.seen)) {
        bin.seen = fat16_mutations();
        return 0;
    }
    scan(bin.page);
    if (!bin.count && bin.page)
        scan(0);
    return 1;
}

static int restore(void)
{
    if (bin.selected >= bin.count)
        return 0;
    if (trash_refresh()) {
        bin.notice = "Lixeira mudou. Selecione de novo";
        return 1;
    }
    const struct trash_entry *entry = &bin.entries[bin.selected];
    const struct fat16_path root = {0};
    struct fat16_location target;
    const int found = fat16_resolve_parent(&bin.volume, &root,
        (const unsigned char *)entry->path, &target);
    if (found != 1 || target.directory == bin.directory) {
        bin.notice = found < 0 ? "Falha ao ler a pasta original" : "Pasta original indisponivel";
        return 1;
    }
    unsigned char payload[RECORD_NAME + 1];
    payload_name(payload, entry->receipt);
    const enum fat16_edit_result result = fat16_restore_file(&bin.volume,
        bin.directory, payload, target.directory, target.name);
    bin.notice = shell_file_error(result);
    if (result == FAT16_EDIT_OK || result == FAT16_EDIT_CLEANUP) {
        const enum fat16_edit_result metadata =
            fat16_remove_at(&bin.volume, bin.directory, entry->receipt, RECORD_NAME);
        if (metadata != FAT16_EDIT_OK && metadata != FAT16_EDIT_CLEANUP)
            bin.notice = "Restaurado. Falha ao limpar registro";
        else if (!bin.notice)
            bin.notice = "Arquivo restaurado";
        scan(bin.page);
        if (!bin.count && bin.page)
            scan(bin.page - 1);
    }
    return 1;
}

int trash_key(unsigned int key)
{
    if (key == '\n')
        return restore();
    if ((key == KEY_LEFT && bin.page) || (key == KEY_RIGHT && bin.more)) {
        bin.notice = 0;
        scan(key == KEY_LEFT ? bin.page - 1 : bin.page + 1);
    } else if (key == KEY_DOWN && bin.count && bin.selected != bin.count - 1) {
        bin.selected = bin.selected == ROWS ? 0 : bin.selected + 1;
    } else if (key == KEY_UP && bin.selected && bin.selected < bin.count) {
        --bin.selected;
    } else {
        return 0;
    }
    return 1;
}

int trash_click(unsigned int x, unsigned int y)
{
    if (tool_hit(x, y, 564, FOOTER, 220, 33))
        return restore();
    if (tool_hit(x, y, 16, FOOTER, 44, 33))
        return trash_key(KEY_LEFT);
    if (tool_hit(x, y, 69, FOOTER, 44, 33))
        return trash_key(KEY_RIGHT);
    if (!tool_hit(x, y, 16, ROW_TOP, TRASH_WIDTH - 32, bin.count * ROW_HEIGHT))
        return 0;
    bin.selected = (y - ROW_TOP) / ROW_HEIGHT;
    return 1;
}

static void draw_path(unsigned int x, unsigned int y, const char *path, unsigned int max)
{
    for (unsigned int i = 0; i < max && path[i]; ++i)
        vga_graphics_overlay_glyph(x + i * TOOL_CELL, y, path[i], TOOL_TEXT);
}

void trash_draw(unsigned int x, unsigned int y)
{
    vga_graphics_fill(x, y, TRASH_WIDTH, TRASH_HEIGHT, TOOL_BG);
    vga_graphics_fill(x, y, TRASH_WIDTH, 46, TOOL_PANEL);
    tool_label(x + 24, y + 12, "Arquivo", TOOL_MUTED);
    tool_label(x + 251, y + 12, "Caminho original", TOOL_MUTED);
    for (unsigned int i = 0; i < bin.count; ++i) {
        const unsigned int top = y + ROW_TOP + i * ROW_HEIGHT;
        vga_graphics_fill(x + 16, top, TRASH_WIDTH - 32, ROW_HEIGHT,
            bin.selected == i ? TOOL_HILITE : i % 2 ? TOOL_ROW : TOOL_BG);
        const char *path = bin.entries[i].path;
        const char *base = path;
        for (const char *p = path; *p; ++p)
            if (*p == '/')
                base = p + 1;
        draw_path(x + 24, top + 5, base, 12);
        draw_path(x + 251, top + 5, path, 32);
    }
    if (!bin.count)
        tool_label(x + 24, y + ROW_TOP + 8, "Lixeira vazia", TOOL_MUTED);
    if (bin.notice)
        tool_label(x + 16, y + 310, bin.notice, TOOL_GOLD);
    tool_button(x + 16, y + FOOTER, 44, "<", TOOL_TEXT, bin.page != 0);
    tool_button(x + 69, y + FOOTER, 44, ">", TOOL_TEXT, bin.more);
    tool_number(x + 126, y + FOOTER + 5, bin.page + 1, TOOL_MUTED);
    tool_button(x + 564, y + FOOTER, 220, "Restaurar", TOOL_BLUE, bin.selected < bin.count);
}
#endif
