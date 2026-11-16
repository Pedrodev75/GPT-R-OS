#include "shortcuts.h"

#ifdef GPT_R_GRAPHICS
#include "tool_view.h"
#include "file_icons.h"
#include "../fs/fat16.h"

enum {
    CONFIG_HEADER = 5, CONFIG_RECORD = 6,
    CONFIG_LIMIT = CONFIG_HEADER + SHORTCUT_COUNT * (CONFIG_RECORD + SHORTCUT_PATH_LIMIT - 1),
    ICON_TOP = 52, ICON_BOTTOM = VGA_GRAPHICS_HEIGHT - 52
};

static const unsigned char config_name[] = "ATALHOS.DAT";
static struct desktop_shortcut items[SHORTCUT_COUNT];
static struct ata_disk config_disk;
static struct fat16_volume config_volume;
static unsigned int config_directory, data_size;
static unsigned char data[CONFIG_LIMIT];

static int equal(const char *first, const char *second)
{
    while (*first && *first == *second) {
        ++first;
        ++second;
    }
    return *first == *second;
}

static unsigned int length(const char *text)
{
    unsigned int n = 0;
    while (text[n])
        ++n;
    return n;
}

static void copy_target(char *target, const char *text)
{
    for (unsigned int i = 0; ; ++i) {
        target[i] = text[i];
        if (!text[i])
            return;
    }
}

static int valid_path(const char *target)
{
    if (target[0] != '/')
        return 0;
    unsigned int component = 0;
    for (unsigned int i = 1; target[i]; ++i) {
        const unsigned char c = target[i];
        if (c == '/') {
            if (!component)
                return 0;
            component = 0;
        } else if (++component > 12 ||
            !((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
              (c >= 'A' && c <= 'Z') || c == '-' || c == '_' || c == '.')) {
            return 0;
        }
    }
    return component != 0;
}

static int valid_position(unsigned int x, unsigned int y)
{
    return x <= VGA_GRAPHICS_WIDTH - SHORTCUT_WIDTH &&
        y >= ICON_TOP && y <= ICON_BOTTOM - SHORTCUT_HEIGHT;
}

static int position_taken(unsigned int index, unsigned int x, unsigned int y)
{
    for (unsigned int i = 0; i < SHORTCUT_COUNT; ++i) {
        const struct desktop_shortcut *other = &items[i];
        if (i != index && other->target[0] &&
            x < (unsigned int)other->x + SHORTCUT_WIDTH && other->x < x + SHORTCUT_WIDTH &&
            y < (unsigned int)other->y + SHORTCUT_HEIGHT && other->y < y + SHORTCUT_HEIGHT)
            return 1;
    }
    return 0;
}

static int collect(void *context, const unsigned char *bytes, unsigned int size)
{
    (void)context;
    if (size > sizeof(data) - data_size)
        return 0;
    for (unsigned int i = 0; i < size; ++i)
        data[data_size++] = bytes[i];
    return 1;
}

static int load_items(void)
{
    struct fat16_file file;
    if (fat16_lookup_at(&config_volume, config_directory, config_name, &file) != 1 ||
        file.size < CONFIG_HEADER || file.size > sizeof(data))
        return 0;
    data_size = 0;
    if (fat16_read(&config_volume, &file, collect, 0) != FAT16_READ_OK ||
        data[0] != 'G' || data[1] != 'T' || data[2] != 'S' || data[3] != '1' ||
        data[4] < 2 || data[4] > SHORTCUT_COUNT)
        return 0;
    unsigned int cursor = CONFIG_HEADER;
    for (unsigned int i = 0; i < data[4]; ++i) {
        if (data_size - cursor < CONFIG_RECORD)
            return 0;
        struct desktop_shortcut *item = &items[i];
        item->x = data[cursor] | (data[cursor + 1] << 8);
        item->y = data[cursor + 2] | (data[cursor + 3] << 8);
        item->kind = data[cursor + 4];
        const unsigned int size = data[cursor + 5];
        cursor += CONFIG_RECORD;
        if (size >= sizeof(item->target) || size > data_size - cursor ||
            item->kind > SHORTCUT_APP || !valid_position(item->x, item->y))
            return 0;
        for (unsigned int j = 0; j < size; ++j) {
            const char ch = data[cursor++];
            if (!ch)
                return 0;
            item->target[j] = ch;
        }
        item->target[size] = 0;
        if (!valid_path(item->target) || position_taken(i, item->x, item->y))
            return 0;
    }
    return cursor == data_size && equal(items[0].target, "/HOME") &&
        equal(items[1].target, "/APPS") && items[0].kind == SHORTCUT_FOLDER &&
        items[1].kind == SHORTCUT_FOLDER;
}

void shortcuts_init(void)
{
    for (unsigned int i = 0; i < SHORTCUT_COUNT; ++i)
        items[i].target[0] = 0;
    config_directory = 0;
    struct fat16_file folder;
    if (ata_identify(&config_disk) && fat16_mount(&config_volume, &config_disk) &&
        fat16_find_at(&config_volume, 0, (const unsigned char *)"CONFIG", &folder) == 1 &&
        folder.is_directory && folder.first_cluster) {
        config_directory = folder.first_cluster;
        if (load_items())
            return;
    }
    for (unsigned int i = 2; i < SHORTCUT_COUNT; ++i)
        items[i].target[0] = 0;
    items[0] = (struct desktop_shortcut){932, 63, SHORTCUT_FOLDER, "/HOME"};
    items[1] = (struct desktop_shortcut){932, 147, SHORTCUT_FOLDER, "/APPS"};
}

static int save_items(void)
{
    if (!config_directory)
        return 0;
    data[0] = 'G';
    data[1] = 'T';
    data[2] = 'S';
    data[3] = '1';
    data[4] = 0;
    unsigned int cursor = CONFIG_HEADER;
    for (unsigned int i = 0; i < SHORTCUT_COUNT; ++i) {
        const struct desktop_shortcut *item = &items[i];
        if (!item->target[0])
            continue;
        const unsigned int size = length(item->target);
        data[cursor++] = item->x;
        data[cursor++] = item->x >> 8;
        data[cursor++] = item->y;
        data[cursor++] = item->y >> 8;
        data[cursor++] = item->kind;
        data[cursor++] = size;
        for (unsigned int j = 0; j < size; ++j)
            data[cursor++] = item->target[j];
        ++data[4];
    }
    const enum fat16_edit_result result = fat16_write_at(&config_volume, config_directory,
        config_name, sizeof(config_name) - 1, data, cursor);
    return result == FAT16_EDIT_OK || result == FAT16_EDIT_CLEANUP;
}

enum shortcut_result shortcuts_add(const char *path, enum shortcut_kind kind, unsigned int *index)
{
    unsigned int available = SHORTCUT_COUNT;
    for (unsigned int i = 0; i < SHORTCUT_COUNT; ++i) {
        if (!items[i].target[0]) {
            if (available == SHORTCUT_COUNT)
                available = i;
        } else if (equal(path, items[i].target)) {
            return SHORTCUT_EXISTS;
        }
    }
    if (available == SHORTCUT_COUNT || length(path) >= SHORTCUT_PATH_LIMIT || !valid_path(path))
        return SHORTCUT_FULL;
    for (unsigned int col = 0; col < 5; ++col) {
        for (unsigned int row = 0; row < 7; ++row) {
            const unsigned int x = 932 - col * 96, y = 63 + row * 84;
            if (position_taken(available, x, y))
                continue;
            struct desktop_shortcut *item = &items[available];
            item->x = x;
            item->y = y;
            item->kind = kind;
            copy_target(item->target, path);
            *index = available;
            return save_items() ? SHORTCUT_SAVED : SHORTCUT_SESSION;
        }
    }
    return SHORTCUT_FULL;
}

int shortcuts_move(unsigned int index, unsigned int x, unsigned int y)
{
    struct desktop_shortcut *item = &items[index];
    if (!valid_position(x, y) || position_taken(index, x, y) || (item->x == x && item->y == y))
        return 0;
    item->x = x;
    item->y = y;
    save_items();
    return 1;
}

const struct desktop_shortcut *shortcuts_at(unsigned int index)
{
    return index < SHORTCUT_COUNT && items[index].target[0] ? &items[index] : 0;
}

int shortcuts_pick(unsigned int x, unsigned int y)
{
    for (unsigned int i = 0; i < SHORTCUT_COUNT; ++i)
        if (items[i].target[0] && tool_hit(x, y, items[i].x, items[i].y, SHORTCUT_WIDTH, SHORTCUT_HEIGHT))
            return i;
    return -1;
}

void shortcuts_draw(unsigned int selected)
{
    for (unsigned int i = 0; i < SHORTCUT_COUNT; ++i) {
        const struct desktop_shortcut *item = &items[i];
        if (!item->target[0])
            continue;
        const unsigned int x = item->x, y = item->y;
        const int marked = i == selected;
        if (marked)
            vga_graphics_fill(x, y, SHORTCUT_WIDTH, SHORTCUT_HEIGHT, TOOL_HILITE);
        const unsigned int background = marked ? TOOL_HILITE : 0x15253a;
        const char *title = item->target;
        for (const char *ch = title; *ch; ++ch)
            if (*ch == '/')
                title = ch + 1;
        file_icon_draw(file_icon_kind(title, item->kind == SHORTCUT_FOLDER),
            x + 21, y + 1, 2, background);
        title = i == 0 ? "Home" : i == 1 ? "Apps" : title;
        unsigned int width = 0;
        while (width < 5 && title[width] && (item->kind == SHORTCUT_FOLDER || title[width] != '.'))
            ++width;
        const unsigned int left = x + (SHORTCUT_WIDTH - width * TOOL_CELL) / 2;
        for (unsigned int j = 0; j < width; ++j)
            vga_graphics_overlay_glyph(left + j * TOOL_CELL, y + 48, title[j], TOOL_TEXT);
    }
}

#endif
