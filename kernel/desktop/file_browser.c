#include "file_browser.h"

#ifdef GPT_R_GRAPHICS
#include "desktop.h"
#include "shortcuts.h"
#include "tool_view.h"
#include "../drivers/keyboard.h"
#include "../drivers/timer.h"
#include "../shell/shell_internal.h"

enum {
    PATH_DEPTH = 16, FILE_ROWS = 9, FILE_ROW_HEIGHT = 36, TABLE_TOP = 122,
    SCAN_BUDGET = 16, NO_SELECTION = FILE_ROWS, DOUBLE_CLICK_TICKS = 35,
    CONTEXT_WIDTH = 238, CONTEXT_HEIGHT = 40
};

struct folder {
    unsigned int cluster;
    char name[13];
};

struct browser_path {
    unsigned int depth;
    struct folder folders[PATH_DEPTH];
};

static struct {
    struct ata_disk disk;
    struct fat16_volume volume;
    struct browser_path here, history[PATH_DEPTH];
    struct fat16_dir cursor, next_page;
    struct fat16_file files[FILE_ROWS];
    unsigned int count, selected, previous_click, click_tick;
    unsigned int page, skip, seen_mutations, history_start, history_count;
    unsigned int name_length, context_x, context_y;
    unsigned char new_name[9];
    const char *notice;
    int mounted, loading, more, naming, context_open;
} browser;

static unsigned int current_folder_cluster(void)
{
    return browser.here.depth ? browser.here.folders[browser.here.depth - 1].cluster : 0;
}

static void scan_page(unsigned int page)
{
    const int advance = browser.more && page == browser.page + 1;
    browser.cursor = advance ? browser.next_page : (struct fat16_dir){ .first_cluster = current_folder_cluster() };
    browser.skip = advance ? 0 : page * FILE_ROWS;
    browser.page = page;
    browser.count = 0;
    browser.selected = browser.previous_click = NO_SELECTION;
    browser.more = browser.context_open = 0;
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

static void open_path(const struct browser_path *path)
{
    browser.here = *path;
    browser.notice = 0;
    browser.more = 0;
    scan_page(0);
}

static void navigate(const struct browser_path *path)
{
    const unsigned int next = (browser.history_start + browser.history_count) % PATH_DEPTH;
    browser.history[next] = browser.here;
    if (browser.history_count == PATH_DEPTH)
        browser.history_start = (browser.history_start + 1) % PATH_DEPTH;
    else
        ++browser.history_count;
    open_path(path);
}

static int has_extension(const char *name, const char *extension)
{
    while (*name && *name != '.')
        ++name;
    for (; *name && *extension; ++name, ++extension) {
        const char ch = *name >= 'a' && *name <= 'z' ? *name - ('a' - 'A') : *name;
        if (ch != *extension)
            return 0;
    }
    return !*name && !*extension;
}

static int path_add(struct browser_path *path, const struct fat16_file *file)
{
    if (!file->first_cluster || path->depth == PATH_DEPTH)
        return 0;
    for (unsigned int i = 0; i < path->depth; ++i)
        if (path->folders[i].cluster == file->first_cluster)
            return 0;
    struct folder *folder = &path->folders[path->depth++];
    folder->cluster = file->first_cluster;
    for (unsigned int i = 0; i < sizeof(folder->name); ++i)
        folder->name[i] = file->name[i];
    return 1;
}

static const char *open_file(const struct fat16_file *file, unsigned int directory)
{
    if (has_extension(file->name, ".APP")) {
        unsigned int length = 0;
        while (file->name[length])
            ++length;
        struct app_launch launch = {
            .args = (const unsigned char *)file->name, .args_size = length + 1, .argc = 1
        };
        const enum app_status status = desktop_open_app(&browser.volume, file, &launch);
        if (status == APP_STARTED)
            return 0;
        return status == APP_LIMIT ? "Feche uma janela de APP" :
            status == APP_INVALID ? "Formato APP invalido" : "Falha ao abrir o APP";
    }
    if (has_extension(file->name, ".TXT") || has_extension(file->name, ".ASM"))
        return shell_edit_file(&browser.volume, directory,
            (const unsigned char *)file->name, file);
    return "Abra arquivos TXT, ASM ou APP";
}

static void open_selected(void)
{
    if (browser.loading || browser.selected >= browser.count)
        return;
    const struct fat16_file *file = &browser.files[browser.selected];
    browser.notice = 0;
    if (file->is_directory) {
        struct browser_path path = browser.here;
        if (path_add(&path, file))
            navigate(&path);
        else
            browser.notice = "Pasta invalida ou muito profunda";
    } else {
        browser.notice = open_file(file, current_folder_cluster());
    }
}

int browser_open_target(const char *target)
{
    if (!browser.mounted) {
        browser.notice = "Volume FAT16 indisponivel";
        return 1;
    }
    struct browser_path path = {0};
    unsigned int directory = 0;
    for (const char *p = target; *p == '/'; ) {
        unsigned char name[13];
        unsigned int size = 0;
        ++p;
        while (*p && *p != '/' && size + 1 < sizeof(name))
            name[size++] = *p++;
        name[size] = 0;
        struct fat16_file file;
        if (!size || (*p && *p != '/') ||
            fat16_find_at(&browser.volume, directory, name, &file) != 1)
            break;
        if (!file.is_directory) {
            if (*p)
                break;
            browser.notice = open_file(&file, directory);
            return browser.notice != 0;
        }
        if (!path_add(&path, &file))
            break;
        if (!*p) {
            navigate(&path);
            return 1;
        }
        directory = file.first_cluster;
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
        has_extension(file->name, ".APP") ? SHORTCUT_APP : SHORTCUT_FILE;
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

static int create_folder(void)
{
    browser.naming = 0;
    const enum fat16_edit_result result = fat16_mkdir(&browser.volume, current_folder_cluster(),
        browser.new_name, browser.name_length);
    switch (result) {
    case FAT16_EDIT_OK:
        browser.notice = "Pasta criada";
        scan_page(0);
        break;
    case FAT16_EDIT_EXISTS:
        browser.notice = "Esse nome ja existe";
        break;
    case FAT16_EDIT_NAME:
        browser.notice = "Use 1 a 8 caracteres validos";
        break;
    case FAT16_EDIT_FULL:
        browser.notice = "Disco ou pasta cheia";
        break;
    default:
        browser.notice = "Falha ao criar pasta";
        break;
    }
    return 1;
}

static int begin_name(void)
{
    if (!browser.mounted || browser.loading)
        return 0;
    browser.naming = 1;
    browser.name_length = 0;
    browser.new_name[0] = 0;
    browser.previous_click = NO_SELECTION;
    return 1;
}

int browser_key(unsigned int key)
{
    if (browser.context_open) {
        if (key == '\n') {
            browser.context_open = 0;
            create_shortcut();
        } else if (key == KEY_ESCAPE) {
            browser.context_open = 0;
        } else {
            return 0;
        }
        return 1;
    }
    if (browser.naming) {
        if (key == '\n')
            return create_folder();
        if (key == KEY_ESCAPE) {
            browser.naming = 0;
        } else if (key == '\b' && browser.name_length) {
            browser.new_name[--browser.name_length] = 0;
        } else if (key >= '!' && key <= '~' && browser.name_length < 8) {
            browser.new_name[browser.name_length++] = key;
            browser.new_name[browser.name_length] = 0;
        } else {
            return 0;
        }
        return 1;
    }
    if (browser.loading)
        return 0;
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
        return begin_name();
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
    browser.context_x = x + CONTEXT_WIDTH + 8 < BROWSER_WIDTH ? x : BROWSER_WIDTH - CONTEXT_WIDTH - 8;
    browser.context_y = y + CONTEXT_HEIGHT + 8 < BROWSER_HEIGHT ? y : BROWSER_HEIGHT - CONTEXT_HEIGHT - 8;
    browser.context_open = 1;
    return 1;
}

int browser_click(unsigned int x, unsigned int y)
{
    if (browser.context_open) {
        browser.context_open = 0;
        if (tool_hit(x, y, browser.context_x, browser.context_y, CONTEXT_WIDTH, CONTEXT_HEIGHT))
            create_shortcut();
        return 1;
    }
    if (browser.naming) {
        if (tool_hit(x, y, 500, 448, 92, 33))
            return create_folder();
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
        struct browser_path parent = browser.here;
        --parent.depth;
        navigate(&parent);
        return 1;
    }
    if (tool_hit(x, y, 526, 12, 196, 33))
        return begin_name();
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
    const int program = has_extension(file->name, ".APP");
    const unsigned int accent = folder ? TOOL_GOLD : program ? TOOL_BLUE : TOOL_MUTED;
    vga_graphics_fill(x, y, BROWSER_WIDTH - 28, FILE_ROW_HEIGHT, background);
    if (index == browser.selected)
        vga_graphics_fill(x, y + 5, 3, FILE_ROW_HEIGHT - 10, TOOL_BLUE);
    if (folder) {
        vga_graphics_fill(x + 13, y + 9, 10, 4, accent);
        vga_graphics_fill(x + 13, y + 13, 24, 14, accent);
    } else {
        vga_graphics_fill(x + 16, y + 8, 15, 20, accent);
        vga_graphics_fill(x + 19, y + 13, 9, 2, background);
        vga_graphics_fill(x + 19, y + 18, 7, 2, background);
    }
    tool_label(x + 54, y + 7, file->name, TOOL_TEXT);
    tool_label(x + 320, y + 7, folder ? "Pasta" : program ? "Programa" : "Arquivo", accent);
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
    tool_button(x + 526, y + 12, 196, "Nova pasta", TOOL_BLUE, browser.mounted && !browser.loading);

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
        tool_label(x + 14, y + 455, "Nome (8 caracteres)", TOOL_MUTED);
        vga_graphics_fill(x + 334, y + 448, 154, 34, TOOL_BG);
        tool_label(x + 344, y + 455, (const char *)browser.new_name, TOOL_TEXT);
        vga_graphics_fill(x + 344 + browser.name_length * TOOL_CELL, y + 475, 12, 2, TOOL_BLUE);
        tool_button(x + 500, y + 448, 92, "Criar", TOOL_BLUE, 1);
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
        vga_graphics_fill(left + 3, top + 4, CONTEXT_WIDTH, CONTEXT_HEIGHT, 0x080e19);
        vga_graphics_fill(left, top, CONTEXT_WIDTH, CONTEXT_HEIGHT, TOOL_BORDER);
        vga_graphics_fill(left + 1, top + 1, CONTEXT_WIDTH - 2, CONTEXT_HEIGHT - 2, TOOL_HILITE);
        tool_label(left + 14, top + 10, "Criar atalho", TOOL_TEXT);
    }
}

#endif
