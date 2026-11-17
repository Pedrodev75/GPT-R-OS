#include "app_center.h"

#ifdef GPT_R_GRAPHICS
#include "desktop.h"
#include "tool_view.h"
#include "../drivers/keyboard.h"
#include "../packages/fat_storage.h"
#include "../shell/shell_internal.h"

enum { CENTER_ROWS = 6, ROW_TOP = 58, ROW_HEIGHT = 40, FOOTER = 359, NONE = CENTER_ROWS };

struct center_item {
    struct pkg_manifest pkg;
    unsigned int directory;
    enum pkg_state state;
};

static struct {
    struct pkg_fat fat;
    struct pkg_storage storage;
    struct center_item items[CENTER_ROWS];
    struct pkg_manifest preview;
    struct fat16_location source;
    unsigned int page, count, selected, seen;
    int mounted, more, viewing, confirm;
    const char *notice;
} center;

static void scan(unsigned int page)
{
    center.page = page;
    center.count = center.more = 0;
    center.selected = NONE;
    if (!center.mounted)
        return;
    struct fat16_dir dir = { .first_cluster = center.fat.apps };
    struct fat16_file entry;
    unsigned int skip = page * CENTER_ROWS;
    int next;
    while ((next = fat16_next_at(&center.fat.volume, &dir, &entry)) > 0) {
        if (!entry.is_directory || !entry.first_cluster || !pkg_valid_id(entry.name))
            continue;
        struct center_item item = { .directory = entry.first_cluster };
        const enum pkg_status status = pkg_load(&center.storage, item.directory, entry.name,
            &item.pkg, &item.state);
        if (status == PKG_IO) {
            center.notice = pkg_error(status);
            continue;
        }
        if (status != PKG_OK || item.state == PKG_INACTIVE)
            continue;
        if (skip) {
            --skip;
            continue;
        }
        if (center.count == CENTER_ROWS) {
            center.more = 1;
            return;
        }
        center.items[center.count++] = item;
    }
    if (next < 0)
        center.notice = "Falha ao listar /APPS";
}

void center_init(void)
{
    center.notice = 0;
    center.viewing = center.confirm = 0;
    const enum pkg_status status = pkg_fat_open(&center.fat);
    center.mounted = status == PKG_OK;
    center.storage = pkg_fat_storage(&center.fat);
    if (!center.mounted)
        center.notice = status == PKG_MISSING ? "Pasta /APPS ausente" : pkg_error(status);
    scan(0);
}

void center_open_package(unsigned int directory, const struct fat16_file *file)
{
    center_init();
    if (!center.mounted)
        return;
    struct pkg_fat_source reader = { .volume = &center.fat.volume, .file = *file };
    const struct pkg_source source = pkg_fat_reader(&reader);
    const enum pkg_status status = pkg_validate(&source, &center.preview);
    if (status != PKG_OK) {
        center.notice = pkg_error(status);
        return;
    }
    center.source.directory = directory;
    for (unsigned int i = 0; i < sizeof(center.source.name); ++i)
        center.source.name[i] = file->name[i];
    center.seen = fat16_mutations();
    center.viewing = 1;
}

static void install_preview(void)
{
    struct pkg_fat_source reader = { .volume = &center.fat.volume };
    const int found = fat16_lookup_at(reader.volume, center.source.directory, center.source.name, &reader.file);
    if (found != 1 || fat16_file_changed(center.source.directory, center.source.name, center.seen)) {
        center.notice = found < 0 ? pkg_error(PKG_IO) : "O pacote mudou. Abra novamente.";
        return;
    }
    const struct pkg_source source = pkg_fat_reader(&reader);
    const enum pkg_status status = pkg_install(&center.storage, &source);
    center.notice = status == PKG_OK ? "Aplicativo instalado" : pkg_error(status);
    if (status == PKG_OK || status == PKG_PENDING || status == PKG_IO) {
        center.viewing = 0;
        scan(0);
    }
}

static int selected_folder_valid(const struct center_item *item)
{
    unsigned int directory;
    const enum pkg_status status = center.storage.folder(center.storage.context, item->pkg.id, 0, &directory);
    if (status == PKG_OK && directory == item->directory)
        return 1;
    scan(center.page);
    center.confirm = 0;
    center.notice = status == PKG_IO ? pkg_error(status) : "A pasta mudou. Selecione novamente.";
    return 0;
}

static void run_selected(void)
{
    const struct center_item *item = &center.items[center.selected];
    if (!selected_folder_valid(item))
        return;
    struct pkg_manifest pkg;
    enum pkg_state state;
    const enum pkg_status status = pkg_load(&center.storage, item->directory, item->pkg.id, &pkg, &state);
    if (status != PKG_OK || state != PKG_INSTALLED) {
        center.notice = pkg_error(status == PKG_OK ? PKG_PENDING : status);
        return;
    }
    const char *name = pkg.files[pkg.main].name;
    struct fat16_file file;
    const int found = fat16_lookup_at(&center.fat.volume, item->directory, (const unsigned char *)name, &file);
    if (found != 1) {
        center.notice = pkg_error(found < 0 ? PKG_IO : PKG_MISSING);
        return;
    }
    unsigned char path[20] = "/APPS/";
    unsigned int end = 6;
    for (unsigned int i = 0; pkg.id[i]; ++i)
        path[end++] = pkg.id[i];
    struct fat16_path cwd;
    const struct fat16_path root = {0};
    if (fat16_resolve_dir(&center.fat.volume, &root, path, &cwd) != 1) {
        center.notice = pkg_error(PKG_IO);
        return;
    }
    unsigned int size = 0;
    while (name[size])
        ++size;
    struct app_launch launch = {
        .args = (const unsigned char *)name, .argc = 1, .args_size = size + 1,
        .directory = &cwd, .executable_directory = item->directory
    };
    const enum app_status result = desktop_open_app(&center.fat.volume, &file, &launch);
    center.notice = result == APP_STARTED ? 0 : result == APP_LIMIT ?
        "Feche uma janela de aplicativo" : "Falha ao executar o aplicativo";
}

static void remove_selected(void)
{
    const struct center_item *item = &center.items[center.selected];
    if (!center.confirm) {
        center.confirm = 1;
        center.notice = "Confirme com Enter ou no botao";
        return;
    }
    if (!selected_folder_valid(item))
        return;
    unsigned int preserved;
    const enum pkg_status status = pkg_remove(&center.storage, item->directory, item->pkg.id, &preserved);
    center.confirm = 0;
    center.notice = status != PKG_OK ? pkg_error(status) : preserved ?
        "Removido. Arquivos alterados preservados" : "Aplicativo removido";
    if (status != PKG_RUNNING) {
        scan(center.page);
        if (!center.count && center.page)
            scan(center.page - 1);
    }
}

int center_key(unsigned int key)
{
    if (center.viewing) {
        if (key == '\n')
            install_preview();
        else if (key == KEY_ESCAPE) {
            center.viewing = 0;
            center.notice = 0;
        } else {
            return 0;
        }
        return 1;
    }
    if (key == KEY_ESCAPE && center.confirm) {
        center.confirm = 0;
        center.notice = 0;
    } else if ((key == KEY_LEFT && center.page) || (key == KEY_RIGHT && center.more)) {
        scan(key == KEY_LEFT ? center.page - 1 : center.page + 1);
    } else if (key == KEY_DOWN && center.count && center.selected != center.count - 1) {
        center.selected = center.selected == NONE ? 0 : center.selected + 1;
    } else if (key == KEY_UP && center.selected < center.count && center.selected) {
        --center.selected;
    } else if (center.selected < center.count && (key == '\n' || key == KEY_DELETE)) {
        if (key == KEY_DELETE || center.confirm || center.items[center.selected].state != PKG_INSTALLED)
            remove_selected();
        else
            run_selected();
        return 1;
    } else {
        return 0;
    }
    center.confirm = 0;
    center.notice = 0;
    return 1;
}

int center_click(unsigned int x, unsigned int y)
{
    if (center.viewing) {
        if (tool_hit(x, y, 16, FOOTER, 126, 33))
            return center_key(KEY_ESCAPE);
        return tool_hit(x, y, 642, FOOTER, 170, 33) ? center_key('\n') : 0;
    }
    if (tool_hit(x, y, 16, FOOTER, 44, 33))
        return center_key(KEY_LEFT);
    if (tool_hit(x, y, 69, FOOTER, 44, 33))
        return center_key(KEY_RIGHT);
    if (tool_hit(x, y, 456, FOOTER, 155, 33) && center.selected < center.count &&
        center.items[center.selected].state == PKG_INSTALLED && !center.confirm)
        return center_key('\n');
    if (tool_hit(x, y, 622, FOOTER, 190, 33))
        return center_key(KEY_DELETE);
    if (!tool_hit(x, y, 16, ROW_TOP, 349, center.count * ROW_HEIGHT))
        return 0;
    center.selected = (y - ROW_TOP) / ROW_HEIGHT;
    center.notice = 0;
    center.confirm = 0;
    return 1;
}

static void utf8_lines(unsigned int x, unsigned int y, const char *text, unsigned int columns,
    unsigned int rows, unsigned int color)
{
    const unsigned char *bytes = (const unsigned char *)text;
    unsigned int size = 0;
    while (bytes[size])
        ++size;
    for (unsigned int row = 0; row < rows && size; ++row) {
        unsigned int length = 0, space = 0;
        for (unsigned int col = 0; col < columns && length < size; ++col) {
            unsigned int cp;
            const unsigned int count = pkg_utf8(bytes + length, size - length, &cp);
            if (!count)
                return;
            if (cp == ' ')
                space = length;
            length += count;
        }
        if (row + 1 < rows && length < size && bytes[length] != ' ' && space)
            length = space;
        for (unsigned int col = 0; length; ++col) {
            unsigned int cp;
            const unsigned int count = pkg_utf8(bytes, length, &cp);
            bytes += count;
            size -= count;
            length -= count;
            if (col + 1 == columns && row + 1 == rows && size)
                cp = '.';
            vga_graphics_overlay_glyph(x + col * TOOL_CELL, y + row * 24, shell_codepoint_glyph(cp), color);
        }
        while (size && *bytes == ' ') {
            ++bytes;
            --size;
        }
    }
}

static void draw_details(unsigned int x, unsigned int y, const struct pkg_manifest *pkg, unsigned int width)
{
    utf8_lines(x, y, pkg->name, width, 2, TOOL_BLUE);
    tool_label(x, y + 56, "Versao", TOOL_MUTED);
    utf8_lines(x + 112, y + 56, pkg->version, width - 7, 1, TOOL_TEXT);
    tool_label(x, y + 88, "ID", TOOL_MUTED);
    tool_label(x + 112, y + 88, pkg->id, TOOL_TEXT);
    tool_label(x, y + 120, "APP", TOOL_MUTED);
    tool_label(x + 112, y + 120, pkg->files[pkg->main].name, TOOL_TEXT);
    utf8_lines(x, y + 162, pkg->description, width, 4, TOOL_MUTED);
}

void center_draw(unsigned int x, unsigned int y)
{
    vga_graphics_fill(x, y, CENTER_WIDTH, CENTER_HEIGHT, TOOL_BG);
    vga_graphics_fill(x, y, CENTER_WIDTH, 46, TOOL_PANEL);
    if (center.viewing) {
        tool_label(x + 24, y + 11, "Instalar pacote", TOOL_TEXT);
        draw_details(x + 24, y + ROW_TOP, &center.preview, 47);
        tool_button(x + 16, y + FOOTER, 126, "Voltar", TOOL_TEXT, 1);
        tool_button(x + 642, y + FOOTER, 170, "Instalar", TOOL_BLUE, 1);
    } else {
        tool_label(x + 24, y + 11, "Instalados", TOOL_TEXT);
        tool_label(x + 386, y + 11, "Detalhes", TOOL_TEXT);
        vga_graphics_fill(x + 369, y + 46, 1, 250, TOOL_BORDER);
        for (unsigned int i = 0; i < center.count; ++i) {
            const struct center_item *item = &center.items[i];
            const unsigned int top = y + ROW_TOP + i * ROW_HEIGHT;
            vga_graphics_fill(x + 16, top, 349, ROW_HEIGHT - 3,
                center.selected == i ? TOOL_HILITE : TOOL_ROW);
            utf8_lines(x + 26, top + 7, item->pkg.name, 20, 1,
                item->state == PKG_INSTALLED ? TOOL_TEXT : TOOL_GOLD);
        }
        const int selected = center.selected < center.count;
        const int installed = selected && center.items[center.selected].state == PKG_INSTALLED;
        if (selected) {
            draw_details(x + 386, y + ROW_TOP, &center.items[center.selected].pkg, 26);
            if (!installed)
                tool_label(x + 386, y + 307, "Operacao incompleta", TOOL_GOLD);
        } else if (!center.count) {
            tool_label(x + 24, y + 68, "Nenhum aplicativo", TOOL_MUTED);
            tool_label(x + 24, y + 102, "Abra um .PKG", TOOL_MUTED);
        }
        tool_button(x + 16, y + FOOTER, 44, "<", TOOL_TEXT, center.page != 0);
        tool_button(x + 69, y + FOOTER, 44, ">", TOOL_TEXT, center.more);
        tool_number(x + 126, y + FOOTER + 6, center.page + 1, TOOL_MUTED);
        tool_button(x + 456, y + FOOTER, 155, "Executar", TOOL_BLUE, installed && !center.confirm);
        tool_button(x + 622, y + FOOTER, 190,
            center.confirm ? "Confirmar" : installed || !selected ? "Desinstalar" : "Limpar",
            TOOL_RED, selected);
    }
    if (center.notice)
        utf8_lines(x + 16, y + 332, center.notice, 49, 1, TOOL_GOLD);
}
#endif
