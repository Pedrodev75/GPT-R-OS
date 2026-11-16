#include "app_canvas.h"

#ifdef GPT_R_GRAPHICS
#include "app_window.h"

#define CANVAS_ROWS ((1u << APP_WINDOW_ROWS) - 1)
enum { RGB_MAX = 0xffffff, CLICK_MASK = 7 };

struct drawing {
    unsigned int color;
    unsigned short x, y, width, height, first_glyph, glyphs;
};

struct picture {
    unsigned int count, used, background;
    struct drawing commands[APP_GUI_COMMANDS];
    unsigned char text[APP_GUI_GLYPHS];
};

struct canvas {
    struct picture pictures[2];
    unsigned int front, dirty, click_head, click_tail;
    unsigned int clicks[CLICK_MASK + 1];
    int visible, building;
};

static struct canvas canvases[APP_PROCESS_LIMIT];

void app_canvas_reset(unsigned int slot)
{
    struct canvas *canvas = &canvases[slot];
    canvas->visible = canvas->building = 0;
    canvas->front = canvas->dirty = canvas->click_head = canvas->click_tail = 0;
}

int app_canvas_visible(unsigned int slot)
{
    return canvases[slot].visible;
}

int app_canvas_begin(unsigned int slot, unsigned long long color)
{
    if (color > RGB_MAX)
        return -1;
    struct canvas *canvas = &canvases[slot];
    struct picture *picture = &canvas->pictures[canvas->front ^ 1];
    picture->count = picture->used = 0;
    picture->background = color;
    canvas->building = 1;
    return 0;
}

static struct picture *stage_picture(unsigned int slot, unsigned long long x,
    unsigned long long y, unsigned long long width, unsigned long long height,
    unsigned long long color)
{
    struct canvas *canvas = &canvases[slot];
    struct picture *picture = &canvas->pictures[canvas->front ^ 1];
    if (!canvas->building || color > RGB_MAX || !width || !height ||
        x >= APP_WINDOW_WIDTH || y >= APP_WINDOW_HEIGHT ||
        width > APP_WINDOW_WIDTH - x || height > APP_WINDOW_HEIGHT - y ||
        picture->count == APP_GUI_COMMANDS)
        return 0;
    return picture;
}

int app_canvas_rect(unsigned int slot, unsigned long long x, unsigned long long y,
    unsigned long long width, unsigned long long height, unsigned long long color)
{
    struct picture *picture = stage_picture(slot, x, y, width, height, color);
    if (!picture)
        return -1;
    picture->commands[picture->count++] = (struct drawing){
        .color = color, .x = x, .y = y, .width = width, .height = height
    };
    return 0;
}

int app_canvas_text(unsigned int slot, unsigned long long x, unsigned long long y,
    const unsigned char *text, unsigned long long size, unsigned long long color)
{
    if (!size || size > APP_WINDOW_COLUMNS)
        return -1;
    const unsigned int width = size * VGA_OVERLAY_CELL_WIDTH;
    struct picture *picture = stage_picture(slot, x, y, width, VGA_OVERLAY_GLYPH_HEIGHT, color);
    if (!picture || size > APP_GUI_GLYPHS - picture->used)
        return -1;
    picture->commands[picture->count++] = (struct drawing){
        .color = color, .x = x, .y = y, .width = width, .height = VGA_OVERLAY_GLYPH_HEIGHT,
        .first_glyph = picture->used, .glyphs = size
    };
    for (unsigned int i = 0; i < size; ++i)
        picture->text[picture->used++] = text[i] >= 32 && text[i] <= 126 ? text[i] : '?';
    return 0;
}

static int same_drawing(const struct picture *old, const struct picture *next, unsigned int i)
{
    const struct drawing *a = &old->commands[i], *b = &next->commands[i];
    if (a->x != b->x || a->y != b->y || a->width != b->width || a->height != b->height ||
        a->color != b->color || a->glyphs != b->glyphs)
        return 0;
    for (unsigned int j = 0; j < a->glyphs; ++j)
        if (old->text[a->first_glyph + j] != next->text[b->first_glyph + j])
            return 0;
    return 1;
}

static unsigned int rows(const struct drawing *drawing)
{
    const unsigned int first = drawing->y / VGA_LINE_HEIGHT;
    const unsigned int last = (drawing->y + drawing->height - 1) / VGA_LINE_HEIGHT;
    return ((1u << (last + 1)) - 1) & ~((1u << first) - 1);
}

int app_canvas_present(unsigned int slot)
{
    struct canvas *canvas = &canvases[slot];
    if (!canvas->building)
        return -1;
    const struct picture *old = &canvas->pictures[canvas->front];
    const struct picture *next = &canvas->pictures[canvas->front ^ 1];
    if (!canvas->visible || old->background != next->background) {
        canvas->dirty = CANVAS_ROWS;
    } else {
        const unsigned int common = old->count < next->count ? old->count : next->count;
        for (unsigned int i = 0; i < common; ++i)
            if (!same_drawing(old, next, i))
                canvas->dirty |= rows(&old->commands[i]) | rows(&next->commands[i]);
        for (unsigned int i = common; i < old->count; ++i)
            canvas->dirty |= rows(&old->commands[i]);
        for (unsigned int i = common; i < next->count; ++i)
            canvas->dirty |= rows(&next->commands[i]);
    }
    canvas->building = 0;
    canvas->visible = 1;
    canvas->front ^= 1;
    return 0;
}

void app_canvas_click(unsigned int slot, unsigned int x, unsigned int y)
{
    struct canvas *canvas = &canvases[slot];
    if (!canvas->visible || x >= APP_WINDOW_WIDTH || y >= APP_WINDOW_HEIGHT)
        return;
    const unsigned int next = (canvas->click_head + 1) & CLICK_MASK;
    if (next == canvas->click_tail)
        canvas->click_tail = (canvas->click_tail + 1) & CLICK_MASK;
    canvas->clicks[canvas->click_head] = x | (y << 16);
    canvas->click_head = next;
}

unsigned long long app_canvas_poll(unsigned int slot)
{
    struct canvas *canvas = &canvases[slot];
    if (canvas->click_head == canvas->click_tail)
        return 0;
    const unsigned int packed = canvas->clicks[canvas->click_tail];
    canvas->click_tail = (canvas->click_tail + 1) & CLICK_MASK;
    return (1ull << 32) | packed;
}

unsigned int app_canvas_dirty(unsigned int slot)
{
    struct canvas *canvas = &canvases[slot];
    const unsigned int dirty = canvas->dirty;
    canvas->dirty = 0;
    return dirty;
}

void app_canvas_draw(unsigned int slot, unsigned int x, unsigned int y,
    unsigned int clip_y, unsigned int clip_height)
{
    const struct canvas *canvas = &canvases[slot];
    const struct picture *picture = &canvas->pictures[canvas->front];
    vga_graphics_fill(x, y, APP_WINDOW_WIDTH, APP_WINDOW_HEIGHT, picture->background);
    for (unsigned int i = 0; i < picture->count; ++i) {
        const struct drawing *draw = &picture->commands[i];
        const unsigned int top = y + draw->y;
        if (top + draw->height <= clip_y || top >= clip_y + clip_height)
            continue;
        const unsigned int left = x + draw->x;
        if (!draw->glyphs) {
            vga_graphics_fill(left, top, draw->width, draw->height, draw->color);
        } else {
            for (unsigned int c = 0; c < draw->glyphs; ++c)
                vga_graphics_overlay_glyph(left + c * VGA_OVERLAY_CELL_WIDTH, top,
                    picture->text[draw->first_glyph + c], draw->color);
        }
    }
}

#endif
