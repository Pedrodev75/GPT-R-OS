#include "file_browser.h"

#ifdef GPT_R_GRAPHICS
#include "desktop.h"
#include "tool_view.h"
#include "../drivers/keyboard.h"
#include "../drivers/timer.h"
#include "../shell/shell_internal.h"

enum {
    PATH_DEPTH = 16, FILE_ROWS = 9, FILE_ROW_HEIGHT = 36, TABLE_TOP = 122,
    SCAN_BUDGET = 16, NO_SELECTION = FILE_ROWS, DOUBLE_CLICK_TICKS = 35
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
    unsigned int name_length;
    unsigned char new_name[9];
    const char *notice;
    int mounted, loading, more, naming;
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
    browser.more = 0;
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
    if (browser.seen_mutations != fat16_mutations())
        scan_page(0);
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

static void open_selected(void)
{
    if (browser.loading || browser.selected >= browser.count)
        return;
    const struct fat16_file *file = &browser.files[browser.selected];
    browser.notice = 0;
    if (file->is_directory) {
        if (browser.here.depth == PATH_DEPTH) {
            browser.notice = "Limite de 16 niveis na navegacao";
            return;
        }
        if (!file->first_cluster) {
            browser.notice = "Pasta invalida";
            return;
        }
        for (unsigned int i = 0; i < browser.here.depth; ++i) {
            if (browser.here.folders[i].cluster == file->first_cluster) {
                browser.notice = "Pasta invalida";
                return;
            }
        }
        struct browser_path path = browser.here;
        struct folder *folder = &path.folders[path.depth++];
        folder->cluster = file->first_cluster;
        for (unsigned int i = 0; i < sizeof(folder->name); ++i)
            folder->name[i] = file->name[i];
        navigate(&path);
    } else if (has_extension(file->name, ".APP")) {
        unsigned int length = 0;
        while (file->name[length])
            ++length;
        struct app_launch launch = {
            .args = (const unsigned char *)file->name, .args_size = length + 1, .argc = 1
        };
        const enum app_status status = desktop_open_app(&browser.volume, file, &launch);
        if (status != APP_STARTED)
            browser.notice = status == APP_LIMIT ? "Feche uma janela de APP" :
                status == APP_INVALID ? "Formato APP invalido" : "Falha ao abrir o APP";
    } else if (has_extension(file->name, ".TXT") || has_extension(file->name, ".ASM")) {
        browser.notice = shell_edit_file(&browser.volume, current_folder_cluster(),
            (const unsigned char *)file->name, file);
    } else {
        browser.notice = "Abra arquivos TXT, ASM ou APP";
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

int browser_click(unsigned int x, unsigned int y)
{
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

void browser_draw(unsigned int x, unsigned int y)
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
        for (unsigned int i = 0; i < browser.count; ++i)
            draw_row(x + 14, y + TABLE_TOP + i * FILE_ROW_HEIGHT, i);

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
}

#endif
