#include "file_browser.h"

#ifdef GPT_R_GRAPHICS
#include "desktop.h"
#include "file_icons.h"
#include "shortcuts.h"
#include "tool_view.h"
#include "trash.h"
#include "../drivers/keyboard.h"
#include "../drivers/timer.h"
#include "../shell/shell_internal.h"

enum {
    PATH_DEPTH = FAT16_PATH_DEPTH, FILE_ROWS = 9, FILE_ROW_HEIGHT = 36, TABLE_TOP = 122,
    SCAN_BUDGET = 16, NO_SELECTION = FILE_ROWS, DOUBLE_CLICK_TICKS = 35,
    CONTEXT_WIDTH = 238, MENU_ROW = 35
};

enum name_action { NAME_NONE, NAME_FOLDER, NAME_TEXT, NAME_SOURCE, NAME_RENAME };
enum menu_type { MENU_NONE, MENU_FILE, MENU_FOLDER, MENU_NEW };

static struct {
    struct fat16_location entry;
    unsigned int first_cluster, size, seen;
    int available, move;
} clipboard;

static struct {
    struct ata_disk disk;
    struct fat16_volume volume;
    struct fat16_path here, history[PATH_DEPTH];
    struct fat16_dir cursor, next_page;
    struct fat16_file files[FILE_ROWS];
    unsigned int count, selected, previous_click, click_tick;
    unsigned int page, skip, seen_mutations, history_start, history_count;
    unsigned int name_length, context_x, context_y, menu_row;
    unsigned char new_name[13];
    const char *notice;
    int mounted, loading, more;
    enum name_action naming;
    enum menu_type context_open;
} browser;

static unsigned int current_folder_cluster(void)
{
    return fat16_path_cluster(&browser.here);
}

static void scan_page(unsigned int page)
{
    const int advance = browser.more && page == browser.page + 1;
    browser.cursor = advance ? browser.next_page : (struct fat16_dir){ .first_cluster = current_folder_cluster() };
    browser.skip = advance ? 0 : page * FILE_ROWS;
    browser.page = page;
    browser.count = 0;
    browser.selected = browser.previous_click = NO_SELECTION;
    browser.more = 0;
    browser.context_open = MENU_NONE;
    browser.naming = NAME_NONE;
    browser.loading = browser.mounted;
    browser.seen_mutations = fat16_mutations();
}

void browser_init(void)
{
    browser = (typeof(browser)){0};
    browser.mounted = ata_identify(&browser.disk) && fat16_mount(&browser.volume, &browser.disk);
    if (!browser.mounted)
        browser.notice = "Volume FAT16 indisponivel";
    scan_page(0);
}

int browser_busy(void)
{
    return browser.loading || (browser.mounted && browser.seen_mutations != fat16_mutations());
}

int browser_step(void)
{
    if (browser.seen_mutations != fat16_mutations()) {
        if (fat16_dir_changed(current_folder_cluster(), browser.seen_mutations))
            scan_page(0);
        else
            browser.seen_mutations = fat16_mutations();
    }
    if (!browser.loading)
        return 0;

    for (unsigned int i = 0; i < SCAN_BUDGET; ++i) {
        if (browser.count == FILE_ROWS)
            browser.next_page = browser.cursor;
        struct fat16_file file;
        const enum fat16_scan_result result = fat16_scan(&browser.volume, &browser.cursor, &file);
        if (result <= FAT16_SCAN_END) {
            if (result == FAT16_SCAN_ERROR)
                browser.notice = "Falha ao ler a pasta";
            browser.loading = 0;
            return 1;
        }
        if (result == FAT16_SCAN_SKIP)
            continue;
        if (browser.skip) {
            --browser.skip;
            continue;
        }
        if (browser.count == FILE_ROWS) {
            browser.more = 1;
            browser.loading = 0;
            return 1;
        }
        browser.files[browser.count++] = file;
    }
    return 0;
}

static void open_path(const struct fat16_path *path)
{
    browser.here = *path;
    browser.notice = 0;
    browser.more = 0;
    scan_page(0);
}

static void navigate(const struct fat16_path *path)
{
    const unsigned int next = (browser.history_start + browser.history_count) % PATH_DEPTH;
    browser.history[next] = browser.here;
    if (browser.history_count == PATH_DEPTH)
        browser.history_start = (browser.history_start + 1) % PATH_DEPTH;
    else
        ++browser.history_count;
    open_path(path);
}

static const char *open_file(const struct fat16_file *file, const struct fat16_path *directory)
{
    const enum file_icon_kind kind = file_icon_kind(file->name, file->is_directory);
    if (kind == FILE_ICON_APP) {
        unsigned int length = 0;
        while (file->name[length])
            ++length;
        struct app_launch launch = {
            .args = (const unsigned char *)file->name, .args_size = length + 1, .argc = 1,
            .directory = directory, .executable_directory = fat16_path_cluster(directory)
        };
        const enum app_status status = desktop_open_app(&browser.volume, file, &launch);
        if (status == APP_STARTED)
            return 0;
        return status == APP_LIMIT ? "Feche uma janela de APP" :
            status == APP_INVALID ? "Formato APP invalido" : "Falha ao abrir o APP";
    }
    if (kind == FILE_ICON_PKG) {
        desktop_open_package(fat16_path_cluster(directory), file);
        return 0;
    }
    if (kind == FILE_ICON_TEXT || kind == FILE_ICON_ASM)
        return shell_edit_file(&browser.volume, fat16_path_cluster(directory),
            (const unsigned char *)file->name, file);
    return "Abra arquivos TXT, ASM, APP ou PKG";
}

static void open_selected(void)
{
    if (browser.loading || browser.selected >= browser.count)
        return;
    const struct fat16_file *file = &browser.files[browser.selected];
    browser.notice = 0;
    if (file->is_directory) {
        struct fat16_path path = browser.here;
        if (fat16_path_enter(&path, file))
            navigate(&path);
        else
            browser.notice = "Pasta invalida ou muito profunda";
    } else {
        browser.notice = open_file(file, &browser.here);
    }
}

int browser_open_target(const char *target)
{
    if (!browser.mounted) {
        browser.notice = "Volume FAT16 indisponivel";
        return 1;
    }
    const struct fat16_path root = {0};
    struct fat16_path path;
    if (fat16_resolve_dir(&browser.volume, &root, (const unsigned char *)target, &path) == 1) {
        navigate(&path);
        return 1;
    }
    unsigned char name[13];
    struct fat16_file file;
    if (fat16_split_path(&browser.volume, &root, (const unsigned char *)target, &path, name) == 1 &&
        fat16_lookup_at(&browser.volume, fat16_path_cluster(&path), name, &file) == 1) {
        browser.notice = open_file(&file, &path);
        return browser.notice != 0;
    }
    browser.notice = "Destino do atalho nao encontrado";
    return 1;
}

static void create_shortcut(void)
{
    const struct fat16_file *file = &browser.files[browser.selected];
    _Static_assert(SHORTCUT_PATH_LIMIT >= 13 * (PATH_DEPTH + 1) + 1, "Shortcut path capacity");
    char path[SHORTCUT_PATH_LIMIT];
    unsigned int used = 0;
    for (unsigned int i = 0; i <= browser.here.depth; ++i) {
        const char *name = i == browser.here.depth ? file->name : browser.here.folders[i].name;
        path[used++] = '/';
        for (; *name; ++name)
            path[used++] = *name;
    }
    path[used] = 0;
    const enum shortcut_kind kind = file->is_directory ? SHORTCUT_FOLDER :
        file_icon_kind(file->name, 0) == FILE_ICON_APP ? SHORTCUT_APP : SHORTCUT_FILE;
    unsigned int index;
    const enum shortcut_result result = shortcuts_add(path, kind, &index);
    switch (result) {
    case SHORTCUT_SAVED:
    case SHORTCUT_SESSION:
        desktop_shortcut_added(index);
        browser.notice = result == SHORTCUT_SAVED ? "Atalho criado no desktop" :
            "Atalho criado nesta sessao";
        break;
    case SHORTCUT_EXISTS:
        browser.notice = "O atalho ja existe";
        break;
    case SHORTCUT_FULL:
        browser.notice = "Sem espaco ou nome invalido";
        break;
    }
}

static int begin_name(enum name_action action)
{
    if (!browser.mounted || browser.loading)
        return 0;
    browser.naming = action;
    browser.name_length = 0;
    browser.new_name[0] = 0;
    browser.previous_click = NO_SELECTION;
    return 1;
}

static int submit_name(void)
{
    const enum name_action action = browser.naming;
    browser.naming = NAME_NONE;
    const unsigned int folder = current_folder_cluster();
    enum fat16_edit_result result;
    if (action == NAME_FOLDER) {
        result = fat16_mkdir(&browser.volume, folder, browser.new_name, browser.name_length);
    } else if (action == NAME_RENAME) {
        const unsigned char *old = (const unsigned char *)browser.files[browser.selected].name;
        result = fat16_transfer(&browser.volume, folder, old, folder, browser.new_name, 1);
        if (result == FAT16_EDIT_OK)
            shell_editor_moved(folder, old, folder, browser.new_name);
    } else {
        const char *extension = action == NAME_TEXT ? ".txt" : ".asm";
        if (!browser.name_length) {
            browser.notice = "Digite um nome";
            return 1;
        }
        for (unsigned int i = 0; i <= 4; ++i)
            browser.new_name[browser.name_length + i] = extension[i];
        result = fat16_create(&browser.volume, folder, browser.new_name, browser.name_length + 4);
    }
    if (result == FAT16_EDIT_OK) {
        scan_page(0);
        browser.notice = action == NAME_FOLDER ? "Pasta criada" :
            action == NAME_RENAME ? "Arquivo renomeado" : "Arquivo criado";
    } else {
        browser.notice = shell_file_error(result);
    }
    return 1;
}

static int take_file(int move)
{
    if (browser.selected >= browser.count || browser.files[browser.selected].is_directory)
        return 0;
    const struct fat16_file *file = &browser.files[browser.selected];
    clipboard.entry.directory = current_folder_cluster();
    for (unsigned int i = 0; i < sizeof(clipboard.entry.name); ++i)
        clipboard.entry.name[i] = file->name[i];
    clipboard.first_cluster = file->first_cluster;
    clipboard.size = file->size;
    clipboard.seen = fat16_mutations();
    clipboard.available = 1;
    clipboard.move = move;
    browser.notice = move ? "Recortado. Escolha onde colar" : "Copiado. Escolha onde colar";
    return 1;
}

static int paste_file(void)
{
    if (!clipboard.available || browser.loading || !browser.mounted)
        return 0;
    const struct fat16_location *from = &clipboard.entry;
    struct fat16_file file;
    const int found = fat16_lookup_at(&browser.volume, from->directory, from->name, &file);
    if (found < 0) {
        browser.notice = "Falha ao ler a origem";
        return 1;
    }
    if (!found || fat16_file_changed(from->directory, from->name, clipboard.seen) ||
        file.first_cluster != clipboard.first_cluster || file.size != clipboard.size) {
        clipboard.available = 0;
        browser.notice = "Origem mudou. Selecione de novo";
        return 1;
    }
    const unsigned int to = current_folder_cluster();
    if (clipboard.move && from->directory == to) {
        browser.notice = "Escolha outra pasta para mover";
        return 1;
    }

    const enum fat16_edit_result result = fat16_transfer(&browser.volume, from->directory,
        from->name, to, from->name, clipboard.move);
    if (result == FAT16_EDIT_OK || result == FAT16_EDIT_CLEANUP) {
        if (clipboard.move) {
            shell_editor_moved(from->directory, from->name, to, from->name);
            clipboard.available = 0;
        }
        scan_page(0);
    }
    const char *error = shell_file_error(result);
    browser.notice = error ? error : clipboard.move ? "Arquivo movido" : "Arquivo copiado";
    return 1;
}

static int send_selected_to_trash(void)
{
    if (browser.selected >= browser.count || browser.files[browser.selected].is_directory)
        return 0;
    const char *error = trash_send(&browser.volume, &browser.here,
        &browser.files[browser.selected]);
    if (!error)
        scan_page(0);
    browser.notice = error ? error : "Arquivo enviado para Lixeira";
    return 1;
}

static unsigned int menu_count(void)
{
    return browser.context_open == MENU_FILE ? 5 : browser.context_open == MENU_NEW ? 3 : 1;
}

static void show_menu(enum menu_type menu, unsigned int x, unsigned int y)
{
    browser.context_open = menu;
    browser.menu_row = 0;
    const unsigned int height = menu_count() * MENU_ROW;
    browser.context_x = x + CONTEXT_WIDTH + 8 < BROWSER_WIDTH ? x : BROWSER_WIDTH - CONTEXT_WIDTH - 8;
    browser.context_y = y + height + 8 < BROWSER_HEIGHT ? y : BROWSER_HEIGHT - height - 8;
}

static int menu_action(unsigned int row)
{
    const enum menu_type type = browser.context_open;
    browser.context_open = MENU_NONE;
    if (type == MENU_NEW)
        return begin_name(NAME_FOLDER + row);
    if (type == MENU_FILE && row == 4)
        return send_selected_to_trash();
    if (type == MENU_FOLDER || row == 3) {
        create_shortcut();
    } else if (row == 2) {
        return begin_name(NAME_RENAME);
    } else {
        take_file(row == 1);
    }
    return 1;
}

int browser_key(unsigned int key)
{
    if (browser.context_open) {
        if (key == '\n') {
            return menu_action(browser.menu_row);
        } else if (key == KEY_UP) {
            browser.menu_row = (browser.menu_row + menu_count() - 1) % menu_count();
        } else if (key == KEY_DOWN) {
            browser.menu_row = (browser.menu_row + 1) % menu_count();
        } else if (key == KEY_ESCAPE) {
            browser.context_open = MENU_NONE;
        } else {
            return 0;
        }
        return 1;
    }
    if (browser.naming) {
        if (key == '\n')
            return submit_name();
        if (key == KEY_ESCAPE) {
            browser.naming = 0;
        } else if (key == '\b' && browser.name_length) {
            browser.new_name[--browser.name_length] = 0;
        } else if (key >= '!' && key <= '~' && browser.name_length < (browser.naming == NAME_RENAME ? 12u : 8u) &&
            (browser.naming == NAME_RENAME || key != '.')) {
            browser.new_name[browser.name_length++] = key;
            browser.new_name[browser.name_length] = 0;
        } else {
            return 0;
        }
        return 1;
    }
    if (browser.loading)
        return 0;
    if (key == KEY_INTERRUPT || key == KEY_CUT) {
        return take_file(key == KEY_CUT);
    }
    if (key == KEY_PASTE)
        return paste_file();
    if (key == KEY_DELETE)
        return send_selected_to_trash();
    if (key == '\n') {
        open_selected();
    } else if (key == KEY_DOWN && browser.count) {
        if (browser.selected == NO_SELECTION)
            browser.selected = 0;
        else if (browser.selected + 1 < browser.count)
            ++browser.selected;
        else
            return 0;
    } else if (key == KEY_UP && browser.selected != NO_SELECTION && browser.selected) {
        --browser.selected;
    } else if (key == KEY_LEFT && browser.page) {
        scan_page(browser.page - 1);
    } else if (key == KEY_RIGHT && browser.more) {
        scan_page(browser.page + 1);
    } else if (key == 'n' || key == 'N') {
        if (!browser.mounted)
            return 0;
        show_menu(MENU_NEW, 484, 48);
    } else {
        return 0;
    }
    browser.previous_click = NO_SELECTION;
    return 1;
}

int browser_context(unsigned int x, unsigned int y)
{
    if (browser.loading || browser.naming)
        return 0;
    if (!tool_hit(x, y, 14, TABLE_TOP, BROWSER_WIDTH - 28, browser.count * FILE_ROW_HEIGHT)) {
        const int closed = browser.context_open;
        browser.context_open = 0;
        return closed;
    }
    browser.selected = (y - TABLE_TOP) / FILE_ROW_HEIGHT;
    browser.previous_click = NO_SELECTION;
    show_menu(browser.files[browser.selected].is_directory ? MENU_FOLDER : MENU_FILE, x, y);
    return 1;
}

int browser_click(unsigned int x, unsigned int y)
{
    if (browser.context_open) {
        if (tool_hit(x, y, browser.context_x, browser.context_y, CONTEXT_WIDTH, menu_count() * MENU_ROW))
            return menu_action((y - browser.context_y) / MENU_ROW);
        browser.context_open = MENU_NONE;
        return 1;
    }
    if (browser.naming) {
        if (tool_hit(x, y, 500, 448, 92, 33))
            return submit_name();
        if (tool_hit(x, y, 602, 448, 112, 33)) {
            browser.naming = 0;
            return 1;
        }
        return 0;
    }
    if (tool_hit(x, y, 14, 12, 116, 33) && browser.history_count) {
        const unsigned int previous =
            (browser.history_start + --browser.history_count) % PATH_DEPTH;
        open_path(&browser.history[previous]);
        return 1;
    }
    if (tool_hit(x, y, 140, 12, 100, 33) && browser.here.depth) {
        struct fat16_path parent = browser.here;
        --parent.depth;
        navigate(&parent);
        return 1;
    }
    if (tool_hit(x, y, 384, 12, 124, 33))
        return paste_file();
    if (tool_hit(x, y, 526, 12, 196, 33) && browser.mounted && !browser.loading) {
        show_menu(MENU_NEW, 484, 48);
        return 1;
    }
    if (browser.loading)
        return 0;
    if (tool_hit(x, y, 14, 452, 42, 33) && browser.page) {
        scan_page(browser.page - 1);
        return 1;
    }
    if (tool_hit(x, y, 62, 452, 42, 33) && browser.more) {
        scan_page(browser.page + 1);
        return 1;
    }
    if (!tool_hit(x, y, 14, TABLE_TOP, BROWSER_WIDTH - 28, browser.count * FILE_ROW_HEIGHT))
        return 0;
    const unsigned int index = (y - TABLE_TOP) / FILE_ROW_HEIGHT;
    const unsigned int now = timer_ticks();
    browser.selected = index;
    if (browser.previous_click == index && now - browser.click_tick <= DOUBLE_CLICK_TICKS) {
        browser.previous_click = NO_SELECTION;
        open_selected();
    } else {
        browser.previous_click = index;
        browser.click_tick = now;
    }
    return 1;
}

static void draw_row(unsigned int x, unsigned int y, unsigned int index)
{
    const struct fat16_file *file = &browser.files[index];
    const unsigned int background = index == browser.selected ? TOOL_HILITE :
        index % 2 ? TOOL_ROW : TOOL_BG;
    const int folder = file->is_directory;
    const enum file_icon_kind kind = file_icon_kind(file->name, folder);
    const unsigned int accent = file_icon_color(kind);
    vga_graphics_fill(x, y, BROWSER_WIDTH - 28, FILE_ROW_HEIGHT, background);
    if (index == browser.selected)
        vga_graphics_fill(x, y + 5, 3, FILE_ROW_HEIGHT - 10, TOOL_BLUE);
    file_icon_draw(kind, x + 13, y + 7, 1, background);
    static const char *const types[] = {
        [FILE_ICON_GENERIC] = "Arquivo", [FILE_ICON_FOLDER] = "Pasta",
        [FILE_ICON_ASM] = "Fonte ASM", [FILE_ICON_APP] = "Programa", [FILE_ICON_TEXT] = "Texto", [FILE_ICON_PKG] = "Pacote"
    };
    tool_label(x + 54, y + 7, file->name, TOOL_TEXT);
    tool_label(x + 320, y + 7, types[kind], accent);
    if (folder)
        tool_label(x + 513, y + 7, "-", TOOL_MUTED);
    else
        tool_number(x + 513, y + 7, file->size, TOOL_MUTED);
}

void browser_draw(unsigned int x, unsigned int y, unsigned int clip_y, unsigned int clip_height)
{
    vga_graphics_fill(x, y, BROWSER_WIDTH, BROWSER_HEIGHT, TOOL_BG);
    vga_graphics_fill(x, y, BROWSER_WIDTH, 55, TOOL_PANEL);
    tool_button(x + 14, y + 12, 116, "Voltar", TOOL_TEXT, browser.history_count != 0);
    tool_button(x + 140, y + 12, 100, "Subir", TOOL_TEXT, browser.here.depth != 0);
    tool_button(x + 384, y + 12, 124, "Colar", TOOL_TEXT, clipboard.available && !browser.loading);
    tool_button(x + 526, y + 12, 196, "Novo   v", TOOL_BLUE, browser.mounted && !browser.loading);

    vga_graphics_fill(x + 14, y + 63, BROWSER_WIDTH - 28, 32, TOOL_ROW);
    tool_label(x + 25, y + 69, browser.here.depth ? "Disco /" : "Disco / FAT16", TOOL_BLUE);
    if (browser.here.depth) {
        unsigned int col = 153;
        if (browser.here.depth > 2) {
            tool_label(x + col, y + 69, "... /", TOOL_MUTED);
            col += 6 * TOOL_CELL;
        }
        if (browser.here.depth > 1) {
            const char *name = browser.here.folders[browser.here.depth - 2].name;
            tool_label(x + col, y + 69, name, TOOL_MUTED);
            for (; *name; ++name)
                col += TOOL_CELL;
            tool_label(x + col + TOOL_CELL, y + 69, "/", TOOL_MUTED);
            col += 3 * TOOL_CELL;
        }
        tool_label(x + col, y + 69, browser.here.folders[browser.here.depth - 1].name, TOOL_TEXT);
    }

    tool_label(x + 68, y + 100, "Nome", TOOL_MUTED);
    tool_label(x + 334, y + 100, "Tipo", TOOL_MUTED);
    tool_label(x + 527, y + 100, "Bytes", TOOL_MUTED);
    if (browser.loading)
        tool_label(x + 60, y + TABLE_TOP + 24, "Lendo a pasta...", TOOL_MUTED);
    else if (!browser.count && browser.mounted)
        tool_label(x + 60, y + TABLE_TOP + 24, "Nenhum item nesta pagina", TOOL_MUTED);
    else
        for (unsigned int i = 0; i < browser.count; ++i) {
            const unsigned int top = y + TABLE_TOP + i * FILE_ROW_HEIGHT;
            if (top < clip_y + clip_height && top + FILE_ROW_HEIGHT > clip_y)
                draw_row(x + 14, top, i);
        }

    vga_graphics_fill(x, y + 446, BROWSER_WIDTH, 52, TOOL_PANEL);
    if (browser.naming) {
        static const char *const names[] = {
            [NAME_FOLDER] = "Nome da pasta", [NAME_TEXT] = "Nome do .txt",
            [NAME_SOURCE] = "Nome do .asm", [NAME_RENAME] = "Novo nome 8.3"
        };
        tool_label(x + 14, y + 455, names[browser.naming], TOOL_MUTED);
        vga_graphics_fill(x + 278, y + 448, 210, 34, TOOL_BG);
        tool_label(x + 286, y + 455, (const char *)browser.new_name, TOOL_TEXT);
        vga_graphics_fill(x + 286 + browser.name_length * TOOL_CELL, y + 475, 10, 2, TOOL_BLUE);
        tool_button(x + 500, y + 448, 92, browser.naming == NAME_RENAME ? "OK" : "Criar", TOOL_BLUE, 1);
        tool_button(x + 602, y + 448, 112, "Sair", TOOL_TEXT, 1);
    } else {
        tool_button(x + 14, y + 452, 42, "<", TOOL_TEXT, browser.page != 0 && !browser.loading);
        tool_button(x + 62, y + 452, 42, ">", TOOL_TEXT, browser.more);
        tool_number(x + 114, y + 458, browser.page + 1, TOOL_MUTED);
        const char *notice = browser.notice ? browser.notice : "Duplo clique ou Enter para abrir";
        for (unsigned int i = 0; i < 35 && notice[i]; ++i)
            vga_graphics_overlay_glyph(x + 150 + i * TOOL_CELL, y + 458, notice[i], TOOL_MUTED);
    }
    if (browser.context_open) {
        const unsigned int left = x + browser.context_x, top = y + browser.context_y;
        const unsigned int height = menu_count() * MENU_ROW;
        vga_graphics_fill(left + 3, top + 4, CONTEXT_WIDTH, height, 0x080e19);
        vga_graphics_fill(left, top, CONTEXT_WIDTH, height, TOOL_BORDER);
        for (unsigned int i = 0; i < menu_count(); ++i) {
            static const char *const actions[] = {"Copiar", "Recortar", "Renomear", "Criar atalho", "Enviar Lixeira"};
            static const char *const types[] = {"Pasta", "Texto .txt", "Fonte .asm"};
            const char *label = browser.context_open == MENU_NEW ? types[i] :
                browser.context_open == MENU_FILE ? actions[i] : actions[3];
            const unsigned int line = top + i * MENU_ROW;
            vga_graphics_fill(left + 1, line + 1, CONTEXT_WIDTH - 2, MENU_ROW - 2,
                i == browser.menu_row ? TOOL_HILITE : TOOL_PANEL);
            tool_label(left + 14, line + 7, label, TOOL_TEXT);
        }
    }
}

#endif
