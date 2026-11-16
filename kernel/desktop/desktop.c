#include "desktop.h"
#include "../drivers/keyboard.h"
#include "../drivers/mouse.h"
#include "../shell/shell_internal.h"

#ifdef GPT_R_GRAPHICS

enum {
    SCREEN_WIDTH = VGA_GRAPHICS_WIDTH, SCREEN_HEIGHT = VGA_GRAPHICS_HEIGHT,
    CELL = VGA_OVERLAY_CELL_WIDTH, BAR_HEIGHT = 52, BAR_Y = SCREEN_HEIGHT - BAR_HEIGHT,
    MENU_X = 14, MENU_WIDTH = 300, MENU_HEIGHT = 176, MENU_Y = BAR_Y - MENU_HEIGHT - 8,
    WORKSPACE = 0x101c30, PANEL = 0x142338, ACTIVE = 0x1c3850, BORDER = 0x364e6a,
    SHADOW = 0x080e19, INK = 0xe7eef8, MUTED = 0x91a8c1, ACCENT = 0x61dcf5
};

enum window_id { TERMINAL, EDITOR, NO_WINDOW };

struct window {
    unsigned int x, y, width, height;
    const char *title;
    int open, minimized;
};

static struct window windows[2];
static enum window_id focus, menu_choice, dragging;
static int menu_open;
static unsigned int pointer_x, pointer_y, drag_dx, drag_dy, buttons;
static const unsigned char *editor_name;
static enum window_id painted_focus;
static unsigned int painted_visible, painted_open;
static int painted_menu;

static void text(unsigned int x, unsigned int y, const char *label, unsigned int color)
{
    for (; *label; ++label, x += CELL)
        vga_graphics_overlay_glyph(x, y, (unsigned char)*label, color);
}

static void terminal_icon(unsigned int x, unsigned int y, unsigned int color)
{
    vga_graphics_fill(x, y, 24, 17, color);
    vga_graphics_fill(x + 2, y + 2, 20, 13, PANEL);
    vga_graphics_fill(x + 5, y + 5, 4, 2, color);
    vga_graphics_fill(x + 8, y + 7, 3, 2, color);
    vga_graphics_fill(x + 5, y + 9, 4, 2, color);
    vga_graphics_fill(x + 13, y + 10, 6, 2, color);
}

static void editor_icon(unsigned int x, unsigned int y, unsigned int color)
{
    vga_graphics_fill(x + 4, y, 16, 18, color);
    vga_graphics_fill(x + 7, y + 3, 10, 2, PANEL);
    vga_graphics_fill(x + 7, y + 7, 10, 2, PANEL);
    vga_graphics_fill(x + 7, y + 11, 7, 2, PANEL);
}

static void logo(unsigned int x, unsigned int y)
{
    vga_graphics_fill(x, y, 20, 6, ACCENT);
    vga_graphics_fill(x, y + 8, 6, 12, ACCENT);
    vga_graphics_fill(x + 8, y + 8, 12, 6, ACCENT);
    vga_graphics_fill(x + 14, y + 16, 6, 4, ACCENT);
}

static void desktop_background(void)
{
    vga_graphics_fill(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, WORKSPACE);
    /* Stepped bands form a subdued, lower-right wallpaper. */
    for (unsigned int band = 0; band < 7; ++band) {
        const unsigned int x = 638 + band * 43;
        const unsigned int y = 345 + band * 41;
        vga_graphics_fill(x, y, SCREEN_WIDTH - x, BAR_Y - y, 0x15253a + band * 0x010203);
    }
    logo(23, 16);
    text(55, 15, "GPT-R OS", INK);
}

static void draw_window(const struct window *window, int focused, const unsigned char *name)
{
    const unsigned int x = window->x, y = window->y;
    const unsigned int width = window->width + 2 * DESKTOP_FRAME;
    const unsigned int height = window->height + DESKTOP_TITLE_HEIGHT + DESKTOP_FRAME;
    vga_graphics_fill(x + 7, y + 8, width, height, SHADOW);
    vga_graphics_fill(x, y, width, height, focused ? ACCENT : BORDER);
    vga_graphics_fill(x + 1, y + 1, width - 2, height - 2, PANEL);
    vga_graphics_fill(x + 2, y + 2, width - 4, DESKTOP_TITLE_HEIGHT - 3,
        focused ? ACTIVE : PANEL);
    if (window == &windows[TERMINAL])
        terminal_icon(x + 12, y + 9, focused ? ACCENT : MUTED);
    else
        editor_icon(x + 12, y + 9, ACCENT);
    text(x + 46, y + 8, window->title, focused ? INK : MUTED);
    if (name) {
        text(x + 151, y + 8, "/", MUTED);
        text(x + 181, y + 8, (const char *)name, INK);
    }
    vga_graphics_fill(x + width - 75, y + 7, 30, 23, PANEL);
    vga_graphics_fill(x + width - 65, y + 22, 10, 2, focused ? INK : MUTED);
    vga_graphics_fill(x + width - 39, y + 7, 30, 23, PANEL);
    text(x + width - 32, y + 8, "x", focused ? INK : MUTED);
}

static void draw_taskbar(void)
{
    const unsigned int top = BAR_Y + 7;
    vga_graphics_fill(0, BAR_Y, SCREEN_WIDTH, BAR_HEIGHT, PANEL);
    vga_graphics_fill(0, BAR_Y, SCREEN_WIDTH, 1, BORDER);
    vga_graphics_fill(14, top, 158, 38, menu_open ? ACTIVE : WORKSPACE);
    logo(26, top + 9);
    text(55, top + 8, "F1 Menu", INK);
    if (windows[TERMINAL].open) {
        vga_graphics_fill(181, top, 180, 38, focus == TERMINAL && !menu_open ? ACTIVE : WORKSPACE);
        terminal_icon(194, top + 10, focus == TERMINAL && !menu_open ? ACCENT : MUTED);
        text(232, top + 8, "Terminal", INK);
        if (focus == TERMINAL && !menu_open)
            vga_graphics_fill(181, top + 35, 180, 3, ACCENT);
    }
    if (windows[EDITOR].open) {
        vga_graphics_fill(371, top, 152, 38, focus == EDITOR && !menu_open ? ACTIVE : WORKSPACE);
        editor_icon(383, top + 10, focus == EDITOR && !menu_open ? ACCENT : MUTED);
        text(415, top + 8, "Editor", INK);
        if (focus == EDITOR && !menu_open)
            vga_graphics_fill(371, top + 35, 152, 3, ACCENT);
    }
    text(SCREEN_WIDTH - 213, top + 8, "F2 / Alt+Tab", MUTED);
}

static void draw_menu_items(enum window_id choice)
{
    for (unsigned int item = 0; item < 2; ++item) {
        const unsigned int y = MENU_Y + 47 + item * 40;
        const int selected = item == (unsigned int)choice;
        vga_graphics_fill(MENU_X + 8, y, MENU_WIDTH - 16, 35,
            selected ? ACTIVE : PANEL);
        if (selected)
            vga_graphics_fill(MENU_X + 8, y + 4, 3, 27, ACCENT);
        if (item == TERMINAL)
            terminal_icon(MENU_X + 22, y + 9, selected ? ACCENT : MUTED);
        else
            editor_icon(MENU_X + 22, y + 9, selected ? ACCENT : MUTED);
        text(MENU_X + 60, y + 6, item == TERMINAL ? "Terminal" : "Editor", INK);
    }
}

static void draw_menu(enum window_id choice)
{
    vga_graphics_fill(MENU_X + 6, MENU_Y + 6, MENU_WIDTH, MENU_HEIGHT, SHADOW);
    vga_graphics_fill(MENU_X, MENU_Y, MENU_WIDTH, MENU_HEIGHT, BORDER);
    vga_graphics_fill(MENU_X + 1, MENU_Y + 1, MENU_WIDTH - 2, MENU_HEIGHT - 2, PANEL);
    text(MENU_X + 15, MENU_Y + 14, "Aplicativos", ACCENT);
    draw_menu_items(choice);
    text(MENU_X + 16, MENU_Y + 139, "Setas / Enter", MUTED);
}

static int visible(enum window_id id)
{
    return id != NO_WINDOW && windows[id].open && !windows[id].minimized;
}

static unsigned int full_width(const struct window *window)
{
    return window->width + 2 * DESKTOP_FRAME;
}

static unsigned int full_height(const struct window *window)
{
    return window->height + DESKTOP_TITLE_HEIGHT + DESKTOP_FRAME;
}

struct region {
    unsigned int x, y, width, height;
};

static int intersects(struct region a, struct region b)
{
    return a.x < b.x + b.width && b.x < a.x + a.width &&
        a.y < b.y + b.height && b.y < a.y + a.height;
}

static struct region window_region(enum window_id id)
{
    const struct window *window = &windows[id];
    return (struct region){window->x, window->y, full_width(window) + 7, full_height(window) + 8};
}

static void paint_app(enum window_id id, struct region damage)
{
    if (!visible(id) || !intersects(damage, window_region(id)))
        return;
    const struct window *window = &windows[id];
    draw_window(window, focus == id, id == EDITOR ? editor_name : 0);
    const unsigned int x = window->x + DESKTOP_FRAME;
    const unsigned int y = window->y + DESKTOP_TITLE_HEIGHT;
    if (!intersects(damage, (struct region){x, y, window->width, window->height}))
        return;
    if (id == TERMINAL) {
        vga_graphics_place_console(x, y);
        vga_graphics_snapshot_console();
    } else {
        shell_editor_draw(x, y, 1);
    }
}

static void compose_region(struct region damage)
{
    vga_graphics_clip(damage.x, damage.y, damage.width, damage.height);
    desktop_background();
    if (focus != TERMINAL)
        paint_app(TERMINAL, damage);
    if (focus != EDITOR)
        paint_app(EDITOR, damage);
    if (focus != NO_WINDOW)
        paint_app(focus, damage);
    if (intersects(damage, (struct region){0, BAR_Y, SCREEN_WIDTH, BAR_HEIGHT}))
        draw_taskbar();
    if (menu_open && intersects(damage,
        (struct region){MENU_X, MENU_Y, MENU_WIDTH + 6, MENU_HEIGHT + 6}))
        draw_menu(menu_choice);
}

static void begin_scene(void)
{
    vga_graphics_begin_overlay();
    vga_graphics_begin_frame();
}

static void present_scene(void)
{
    vga_graphics_clip(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    vga_graphics_end_frame();
    if (focus == TERMINAL && !menu_open)
        vga_graphics_resume_console();
    vga_graphics_mouse_cursor(pointer_x, pointer_y, mouse_available());
}

static unsigned int visibility(void)
{
    return visible(TERMINAL) | (visible(EDITOR) << 1);
}

static unsigned int opened_windows(void)
{
    return windows[TERMINAL].open | (windows[EDITOR].open << 1);
}

static void remember_scene(void)
{
    painted_focus = focus;
    painted_visible = visibility();
    painted_open = opened_windows();
    painted_menu = menu_open;
}

static void recompose_overlap(void)
{
    const struct region terminal = window_region(TERMINAL);
    const struct region editor = window_region(EDITOR);
    if (!intersects(terminal, editor))
        return;
    const unsigned int left = terminal.x > editor.x ? terminal.x : editor.x;
    const unsigned int top = terminal.y > editor.y ? terminal.y : editor.y;
    unsigned int right = terminal.x + terminal.width;
    unsigned int bottom = terminal.y + terminal.height;
    if (editor.x + editor.width < right)
        right = editor.x + editor.width;
    if (editor.y + editor.height < bottom)
        bottom = editor.y + editor.height;
    compose_region((struct region){left, top, right - left, bottom - top});
}

static void repaint_changed(int editor_content)
{
    const unsigned int now_visible = visibility();
    const unsigned int changed = painted_visible ^ now_visible;
    const int focus_changed = painted_focus != focus;
    const int menu_changed = painted_menu != menu_open;
    const int taskbar_changed = painted_open != opened_windows() || focus_changed || menu_changed;
    if (!changed && !taskbar_changed && !editor_content)
        return;

    begin_scene();
    for (unsigned int id = 0; id < 2; ++id) {
        if ((changed & (1u << id)) || (editor_content && id == EDITOR)) {
            compose_region(window_region(id));
        } else if (focus_changed && visible(id) && (id == painted_focus || id == focus)) {
            const struct window *window = &windows[id];
            const unsigned int width = full_width(window), height = full_height(window);
            const unsigned int x = window->x, y = window->y;
            compose_region((struct region){x, y, width, DESKTOP_TITLE_HEIGHT});
            compose_region((struct region){x, y + DESKTOP_TITLE_HEIGHT, 1, height - DESKTOP_TITLE_HEIGHT});
            compose_region((struct region){x + width - 1, y + DESKTOP_TITLE_HEIGHT,
                1, height - DESKTOP_TITLE_HEIGHT});
            compose_region((struct region){x, y + height - 1, width, 1});
        }
    }
    if (focus_changed && !editor_content && now_visible == 3 && painted_visible == 3)
        recompose_overlap();
    if (menu_changed)
        compose_region((struct region){MENU_X, MENU_Y, MENU_WIDTH + 6, MENU_HEIGHT + 6});
    if (taskbar_changed)
        compose_region((struct region){14, BAR_Y + 7, 510, 38});
    present_scene();
    remember_scene();
}

void desktop_repaint(void)
{
    begin_scene();
    compose_region((struct region){0, 0, SCREEN_WIDTH, SCREEN_HEIGHT});
    present_scene();
    remember_scene();
}

void desktop_init(void)
{
    windows[TERMINAL] = (struct window) {
        DESKTOP_TERMINAL_X, DESKTOP_TERMINAL_Y,
        CONSOLE_WIDTH * VGA_CELL_WIDTH, CONSOLE_HEIGHT * VGA_LINE_HEIGHT, "Terminal", 1, 0
    };
    windows[EDITOR] = (struct window) {
        DESKTOP_EDITOR_X, DESKTOP_EDITOR_Y,
        DESKTOP_EDITOR_WIDTH, DESKTOP_EDITOR_HEIGHT, "Editor", 0, 0
    };
    focus = menu_choice = TERMINAL;
    dragging = NO_WINDOW;
    buttons = menu_open = 0;
    pointer_x = SCREEN_WIDTH / 2;
    pointer_y = SCREEN_HEIGHT / 2;
    desktop_repaint();
}

static void cancel_drag(void)
{
    if (dragging != NO_WINDOW) {
        vga_graphics_drag_outline(0, 0, 0, 0);
        dragging = NO_WINDOW;
    }
}

void desktop_open_editor(const unsigned char *name)
{
    cancel_drag();
    editor_name = name;
    windows[EDITOR].open = 1;
    windows[EDITOR].minimized = 0;
    focus = EDITOR;
    repaint_changed(1);
}

static void focus_window(enum window_id id)
{
    windows[id].open = 1;
    windows[id].minimized = 0;
    cancel_drag();
    focus = id;
    repaint_changed(0);
}

static void hide_window(enum window_id id, int close)
{
    if (close && id == EDITOR && !shell_editor_key(KEY_ESCAPE)) {
        vga_graphics_begin_frame();
        shell_editor_draw(windows[EDITOR].x + DESKTOP_FRAME,
            windows[EDITOR].y + DESKTOP_TITLE_HEIGHT, 0);
        vga_graphics_end_frame();
        return;
    }
    windows[id].open = !close;
    windows[id].minimized = !close;
    const enum window_id other = id == TERMINAL ? EDITOR : TERMINAL;
    if (focus == id)
        focus = visible(other) ? other : NO_WINDOW;
    cancel_drag();
    repaint_changed(0);
}

static void launch(enum window_id choice, unsigned int *row)
{
    menu_open = 0;
    if (choice == TERMINAL || windows[EDITOR].open) {
        focus_window(choice);
    } else {
        shell_edit(row, (const unsigned char *)"ola.asm");
        if (!windows[EDITOR].open)
            repaint_changed(0);
    }
}

static void toggle_menu(void)
{
    cancel_drag();
    menu_open = !menu_open;
    menu_choice = TERMINAL;
    repaint_changed(0);
}

static int hit(unsigned int x, unsigned int y,
    unsigned int left, unsigned int top, unsigned int width, unsigned int height)
{
    return x >= left && y >= top && x - left < width && y - top < height;
}

static int hit_window(enum window_id id)
{
    if (!visible(id))
        return 0;
    const struct window *w = &windows[id];
    return hit(pointer_x, pointer_y, w->x, w->y, full_width(w), full_height(w));
}

static void click(unsigned int *row)
{
    const unsigned int x = pointer_x, y = pointer_y;
    if (hit(x, y, 14, BAR_Y + 7, 158, 38)) {
        toggle_menu();
        return;
    }
    if (menu_open) {
        for (unsigned int item = 0; item < 2; ++item) {
            if (hit(x, y, MENU_X + 8, MENU_Y + 47 + item * 40, MENU_WIDTH - 16, 35)) {
                launch(item, row);
                return;
            }
        }
        toggle_menu();
        return;
    }
    for (unsigned int id = 0; id < 2; ++id) {
        if (windows[id].open && hit(x, y, id == TERMINAL ? 181 : 371, BAR_Y + 7,
                                   id == TERMINAL ? 180 : 152, 38)) {
            if (focus == id)
                hide_window(id, 0);
            else
                focus_window(id);
            return;
        }
    }

    enum window_id id = focus;
    if (!hit_window(id)) {
        if (hit_window(EDITOR))
            id = EDITOR;
        else if (hit_window(TERMINAL))
            id = TERMINAL;
        else
            return;
    }
    struct window *w = &windows[id];
    if (focus != id)
        focus_window(id);
    if (y - w->y >= DESKTOP_TITLE_HEIGHT)
        return;
    const unsigned int right = w->x + full_width(w);
    if (hit(x, y, right - 39, w->y + 7, 30, 23)) {
        hide_window(id, 1);
    } else if (hit(x, y, right - 75, w->y + 7, 30, 23)) {
        hide_window(id, 0);
    } else {
        dragging = id;
        drag_dx = x - w->x;
        drag_dy = y - w->y;
    }
}

static void drag_window(int commit)
{
    struct window *w = &windows[dragging];
    const unsigned int limit_x = SCREEN_WIDTH - full_width(w) - 8;
    const unsigned int limit_y = BAR_Y - full_height(w) - 8;
    unsigned int x = pointer_x > drag_dx ? pointer_x - drag_dx : 0;
    unsigned int y = pointer_y > drag_dy ? pointer_y - drag_dy : 0;
    if (x > limit_x)
        x = limit_x;
    if (y > limit_y)
        y = limit_y;
    if (!commit) {
        if (x == w->x && y == w->y)
            vga_graphics_drag_outline(0, 0, 0, 0);
        else
            vga_graphics_drag_outline(x, y, full_width(w), full_height(w));
        return;
    }
    cancel_drag();
    if (x == w->x && y == w->y)
        return;
    const unsigned int left = x < w->x ? x : w->x;
    const unsigned int top = y < w->y ? y : w->y;
    const unsigned int width = (x > w->x ? x : w->x) - left + full_width(w) + 7;
    const unsigned int height = (y > w->y ? y : w->y) - top + full_height(w) + 8;
    w->x = x;
    w->y = y;
    begin_scene();
    compose_region((struct region){left, top, width, height});
    present_scene();
}

static void mouse_event(const struct mouse_event *event, unsigned int *row)
{
    pointer_x = event->x;
    pointer_y = event->y;
    vga_graphics_mouse_cursor(pointer_x, pointer_y, 1);
    if (event->buttons & MOUSE_LEFT) {
        if (!(buttons & MOUSE_LEFT))
            click(row);
        else if (dragging != NO_WINDOW)
            drag_window(0);
    } else if (dragging != NO_WINDOW) {
        drag_window(1);
    }
    buttons = event->buttons;
}

unsigned int desktop_wait_key(struct keyboard_state *keyboard, unsigned int *row)
{
    for (;;) {
        struct mouse_event event;
        __asm__ volatile ("cli" : : : "memory");
        const unsigned int key = keyboard_poll(keyboard);
        if (!key && mouse_poll(&event)) {
            __asm__ volatile ("sti" : : : "memory");
            mouse_event(&event, row);
            continue;
        }
        if (!key) {
            __asm__ volatile ("sti; hlt" : : : "memory");
            continue;
        }
        __asm__ volatile ("sti" : : : "memory");

        if (key == KEY_F1) {
            toggle_menu();
        } else if (key == KEY_SWITCH_WINDOW) {
            enum window_id other = focus == EDITOR ? TERMINAL : EDITOR;
            if (focus == NO_WINDOW && !windows[other].open)
                other = TERMINAL;
            if (windows[other].open) {
                menu_open = 0;
                focus_window(other);
            }
        } else if (menu_open) {
            if (key == KEY_ESCAPE) {
                toggle_menu();
            } else if (key == KEY_UP || key == KEY_DOWN) {
                menu_choice = menu_choice == TERMINAL ? EDITOR : TERMINAL;
                vga_graphics_begin_frame();
                draw_menu_items(menu_choice);
                vga_graphics_end_frame();
            } else if (key == '\n') {
                launch(menu_choice, row);
            }
        } else if (focus == EDITOR) {
            if (shell_editor_key(key)) {
                cancel_drag();
                windows[EDITOR].open = 0;
                focus = visible(TERMINAL) ? TERMINAL : NO_WINDOW;
                repaint_changed(0);
            } else {
                vga_graphics_begin_frame();
                shell_editor_draw(windows[EDITOR].x + DESKTOP_FRAME,
                    windows[EDITOR].y + DESKTOP_TITLE_HEIGHT, 0);
                vga_graphics_end_frame();
            }
        } else if (focus == TERMINAL) {
            if (key == KEY_ESCAPE)
                hide_window(TERMINAL, 1);
            else {
                if (key == '\n')
                    cancel_drag();
                return key;
            }
        }
    }
}

#endif
