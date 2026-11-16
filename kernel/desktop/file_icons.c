#include "file_icons.h"

#ifdef GPT_R_GRAPHICS
#include "tool_view.h"

static int extension(const char *name, const char *wanted)
{
    while (*name && *name != '.')
        ++name;
    for (; *name && *wanted; ++name, ++wanted) {
        const char ch = *name >= 'a' && *name <= 'z' ? *name - ('a' - 'A') : *name;
        if (ch != *wanted)
            return 0;
    }
    return !*name && !*wanted;
}

enum file_icon_kind file_icon_kind(const char *name, int folder)
{
    if (folder)
        return FILE_ICON_FOLDER;
    if (extension(name, ".ASM"))
        return FILE_ICON_ASM;
    if (extension(name, ".APP"))
        return FILE_ICON_APP;
    if (extension(name, ".TXT"))
        return FILE_ICON_TEXT;
    return FILE_ICON_GENERIC;
}

unsigned int file_icon_color(enum file_icon_kind kind)
{
    static const unsigned int colors[] = {
        [FILE_ICON_GENERIC] = TOOL_MUTED, [FILE_ICON_FOLDER] = TOOL_GOLD,
        [FILE_ICON_ASM] = 0xceb3ff, [FILE_ICON_APP] = TOOL_BLUE, [FILE_ICON_TEXT] = TOOL_TEXT
    };
    return colors[kind];
}

void file_icon_draw(enum file_icon_kind kind, unsigned int x, unsigned int y,
    unsigned int scale, unsigned int background)
{
    const unsigned int color = file_icon_color(kind);
#define PIXELS(a, b, w, h, ink) vga_graphics_fill(x + (a) * scale, y + (b) * scale, \
    (w) * scale, (h) * scale, (ink))
    if (kind == FILE_ICON_FOLDER) {
        PIXELS(0, 3, 9, 4, color);
        PIXELS(0, 7, 22, 14, 0xb98143);
        PIXELS(1, 9, 20, 11, color);
    } else if (kind == FILE_ICON_APP) {
        PIXELS(0, 2, 22, 19, color);
        PIXELS(2, 6, 18, 13, background);
        PIXELS(8, 8, 2, 9, color);
        PIXELS(10, 9, 2, 7, color);
        PIXELS(12, 10, 2, 5, color);
        PIXELS(14, 11, 2, 3, color);
    } else {
        PIXELS(3, 0, 12, 5, color);
        PIXELS(3, 5, 16, 17, color);
        PIXELS(15, 3, 2, 2, color);
        if (kind == FILE_ICON_ASM) {
            PIXELS(7, 10, 2, 2, background);
            PIXELS(6, 12, 2, 2, background);
            PIXELS(7, 14, 2, 2, background);
            PIXELS(13, 10, 2, 2, background);
            PIXELS(14, 12, 2, 2, background);
            PIXELS(13, 14, 2, 2, background);
        } else {
            PIXELS(6, 8, 10, 2, background);
            PIXELS(6, 12, 10, 2, background);
            PIXELS(6, 16, 7, 2, background);
        }
    }
#undef PIXELS
}

#endif
