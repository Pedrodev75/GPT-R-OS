#include "desktop.h"
#include "app_window.h"
#include "file_browser.h"
#include "task_manager.h"
#include "../drivers/timer.h"
#include "../drivers/keyboard.h"
#include "../drivers/mouse.h"
#include "../shell/shell_internal.h"

#ifdef GPT_R_GRAPHICS

enum {
    SCREEN_WIDTH = VGA_GRAPHICS_WIDTH, SCREEN_HEIGHT = VGA_GRAPHICS_HEIGHT,
    CELL = VGA_OVERLAY_CELL_WIDTH, BAR_HEIGHT = 52, BAR_Y = SCREEN_HEIGHT - BAR_HEIGHT,
    MENU_X = 14, MENU_WIDTH = 300, MENU_HEIGHT = 256, MENU_Y = BAR_Y - MENU_HEIGHT - 8,
    WORKSPACE = 0x101c30, PANEL = 0x142338, ACTIVE = 0x1c3850, BORDER = 0x364e6a,
    SHADOW = 0x080e19, INK = 0xe7eef8, MUTED = 0x91a8c1, ACCENT = 0x61dcf5,
    APP_TAB_X = 731, APP_TAB_STEP = 71, APP_TAB_WIDTH = 68
};

enum window_id {
    TERMINAL, EDITOR, EXPLORER, TASKS, FIRST_APP, NO_WINDOW = FIRST_APP + APP_PROCESS_LIMIT
};

struct taskbar_item {
    unsigned int x, width;
    const char *label;
};

static const struct taskbar_item builtins[FIRST_APP] = {
    [TERMINAL] = {181, 138, "Terminal"},
    [EDITOR] = {328, 110, "Editor"},
    [EXPLORER] = {447, 140, "Arquivos"},
    [TASKS] = {596, 125, "Tarefas"}
};

static struct taskbar_item taskbar_item(enum window_id id)
{
    if (id < FIRST_APP)
        return builtins[id];
    return (struct taskbar_item){APP_TAB_X + (id - FIRST_APP) * APP_TAB_STEP, APP_TAB_WIDTH, 0};
}

struct window {
    unsigned int x, y, width, height;
    const char *title;
    int open, minimized;
};

static struct window windows[NO_WINDOW];
static enum window_id window_order[NO_WINDOW];
static char app_titles[APP_PROCESS_LIMIT][sizeof(((struct fat16_file *)0)->name)];
static unsigned int last_app_paint, pending_app_paint[APP_PROCESS_LIMIT];
static unsigned int events_since_slice;
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

static void window_icon(enum window_id id, unsigned int x, unsigned int y, unsigned int color)
{
    if (id == EDITOR) {
        editor_icon(x, y, color);
    } else if (id == EXPLORER) {
        vga_graphics_fill(x + 1, y + 1, 10, 5, color);
        vga_graphics_fill(x + 1, y + 6, 23, 12, color);
    } else if (id == TASKS) {
        vga_graphics_fill(x + 2, y + 9, 4, 9, color);
        vga_graphics_fill(x + 9, y + 4, 4, 14, color);
        vga_graphics_fill(x + 16, y, 4, 18, color);
    } else {
        terminal_icon(x, y, color);
    }
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

static void draw_window(enum window_id id, int focused, const unsigned char *name)
{
    const struct window *window = &windows[id];
    const unsigned int x = window->x, y = window->y;
    const unsigned int width = window->width + 2 * DESKTOP_FRAME;
    const unsigned int height = window->height + DESKTOP_TITLE_HEIGHT + DESKTOP_FRAME;
    vga_graphics_fill(x + 7, y + 8, width, height, SHADOW);
    vga_graphics_fill(x, y, width, height, focused ? ACCENT : BORDER);
    vga_graphics_fill(x + 1, y + 1, width - 2, height - 2, PANEL);
    vga_graphics_fill(x + 2, y + 2, width - 4, DESKTOP_TITLE_HEIGHT - 3,
        focused ? ACTIVE : PANEL);
    window_icon(id, x + 12, y + 9, focused ? ACCENT : MUTED);
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
    for (unsigned int id = 0; id < NO_WINDOW; ++id) {
        if (!windows[id].open)
            continue;
        const struct taskbar_item item = taskbar_item(id);
        const int focused = focus == id && !menu_open;
        vga_graphics_fill(item.x, top, item.width, 38, focused ? ACTIVE : WORKSPACE);
        if (id < FIRST_APP) {
            unsigned int length = 0;
            while (item.label[length])
                ++length;
            text(item.x + (item.width - CELL * length) / 2, top + 8, item.label, INK);
        } else {
            const char *title = windows[id].title;
            for (unsigned int i = 0; i < 4 && title[i]; ++i)
                vga_graphics_overlay_glyph(item.x + 3 + i * CELL, top + 8, title[i], INK);
        }
        if (focused)
            vga_graphics_fill(item.x, top + 35, item.width, 3, ACCENT);
    }
}

static void draw_menu_items(enum window_id choice)
{
    for (unsigned int item = 0; item < FIRST_APP; ++item) {
        const unsigned int y = MENU_Y + 47 + item * 40;
        const int selected = item == (unsigned int)choice;
        vga_graphics_fill(MENU_X + 8, y, MENU_WIDTH - 16, 35,
            selected ? ACTIVE : PANEL);
        if (selected)
            vga_graphics_fill(MENU_X + 8, y + 4, 3, 27, ACCENT);
        window_icon(item, MENU_X + 22, y + 9, selected ? ACCENT : MUTED);
        text(MENU_X + 60, y + 6, builtins[item].label, INK);
    }
}

static void draw_menu(enum window_id choice)
{
    vga_graphics_fill(MENU_X + 6, MENU_Y + 6, MENU_WIDTH, MENU_HEIGHT, SHADOW);
    vga_graphics_fill(MENU_X, MENU_Y, MENU_WIDTH, MENU_HEIGHT, BORDER);
    vga_graphics_fill(MENU_X + 1, MENU_Y + 1, MENU_WIDTH - 2, MENU_HEIGHT - 2, PANEL);
    text(MENU_X + 15, MENU_Y + 14, "Aplicativos", ACCENT);
    draw_menu_items(choice);
    text(MENU_X + 16, MENU_Y + 219, "Setas / Enter", MUTED);
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
    draw_window(id, focus == id, id == EDITOR ? editor_name : 0);
    const unsigned int x = window->x + DESKTOP_FRAME;
    const unsigned int y = window->y + DESKTOP_TITLE_HEIGHT;
    if (!intersects(damage, (struct region){x, y, window->width, window->height}))
        return;
    if (id == TERMINAL) {
        vga_graphics_place_console(x, y);
        vga_graphics_snapshot_console();
    } else if (id == EDITOR) {
        shell_editor_draw(x, y, 1);
    } else if (id == EXPLORER) {
        browser_draw(x, y);
    } else if (id == TASKS) {
        tasks_draw(x, y);
    } else {
        app_window_draw(id - FIRST_APP, x, y, focus == id && !menu_open,
            damage.y, damage.height);
    }
}

static void compose_region(struct region damage)
{
    vga_graphics_clip(damage.x, damage.y, damage.width, damage.height);
    desktop_background();
    for (unsigned int i = 0; i < NO_WINDOW; ++i)
        paint_app(window_order[i], damage);
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
    unsigned int mask = 0;
    for (unsigned int id = 0; id < NO_WINDOW; ++id)
        if (visible(id))
            mask |= 1u << id;
    return mask;
}

static unsigned int opened_windows(void)
{
    unsigned int mask = 0;
    for (unsigned int id = 0; id < NO_WINDOW; ++id)
        if (windows[id].open)
            mask |= 1u << id;
    return mask;
}

static void remember_scene(void)
{
    painted_focus = focus;
    painted_visible = visibility();
    painted_open = opened_windows();
    painted_menu = menu_open;
}

static void recompose_overlap(enum window_id id)
{
    const struct region raised = window_region(id);
    unsigned int left = SCREEN_WIDTH, top = SCREEN_HEIGHT, right = 0, bottom = 0;
    for (unsigned int other = 0; other < NO_WINDOW; ++other) {
        if (other == id || !visible(other))
            continue;
        const struct region w = window_region(other);
        if (!intersects(raised, w))
            continue;
        const unsigned int x = raised.x > w.x ? raised.x : w.x;
        const unsigned int y = raised.y > w.y ? raised.y : w.y;
        const unsigned int edge_x = raised.x + raised.width < w.x + w.width ?
            raised.x + raised.width : w.x + w.width;
        const unsigned int edge_y = raised.y + raised.height < w.y + w.height ?
            raised.y + raised.height : w.y + w.height;
        if (x < left)
            left = x;
        if (y < top)
            top = y;
        if (edge_x > right)
            right = edge_x;
        if (edge_y > bottom)
            bottom = edge_y;
    }
    if (left < right)
        compose_region((struct region){left, top, right - left, bottom - top});
}

static void repaint_client(enum window_id id)
{
    if (!visible(id))
        return;
    const struct window *window = &windows[id];
    begin_scene();
    compose_region((struct region){window->x + DESKTOP_FRAME, window->y + DESKTOP_TITLE_HEIGHT,
        window->width, window->height});
    present_scene();
}

static void service_tools(void)
{
    if (visible(EXPLORER) && browser_busy() && browser_step())
        repaint_client(EXPLORER);
    if (windows[TASKS].open && tasks_refresh())
        repaint_client(TASKS);
}

static void repaint_app_output(int immediate)
{
    int repainting = 0;
    const unsigned int tick = timer_ticks();
    const int ready_to_paint = immediate || tick - last_app_paint >= 3;
    for (unsigned int slot = 0; slot < APP_PROCESS_LIMIT; ++slot) {
        const enum window_id id = FIRST_APP + slot;
        app_window_update(slot);
        pending_app_paint[slot] |= app_window_dirty(slot);
        if (!visible(id)) {
            pending_app_paint[slot] = 0;
            continue;
        }
        const unsigned int mask = pending_app_paint[slot];
        if (!mask || !ready_to_paint)
            continue;
        pending_app_paint[slot] = 0;
        const unsigned int first = __builtin_ctz(mask);
        const unsigned int last = sizeof(mask) * 8 - 1 - __builtin_clz(mask);
        if (!repainting) {
            begin_scene();
            repainting = 1;
        }
        const struct window *w = &windows[id];
        compose_region((struct region){w->x + DESKTOP_FRAME,
            w->y + DESKTOP_TITLE_HEIGHT + first * VGA_LINE_HEIGHT,
            w->width, (last + 1 - first) * VGA_LINE_HEIGHT});
    }
    if (repainting) {
        present_scene();
        last_app_paint = tick;
    }
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
    for (unsigned int id = 0; id < NO_WINDOW; ++id) {
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
    if (focus_changed && visible(focus) && !(changed & (1u << focus)))
        recompose_overlap(focus);
    if (focus_changed || menu_changed) {
        if (visible(painted_focus) && painted_focus >= FIRST_APP)
            app_window_focus(painted_focus - FIRST_APP);
        if (visible(focus) && focus >= FIRST_APP)
            app_window_focus(focus - FIRST_APP);
    }
    if (menu_changed)
        compose_region((struct region){MENU_X, MENU_Y, MENU_WIDTH + 6, MENU_HEIGHT + 6});
    if (taskbar_changed)
        compose_region((struct region){14, BAR_Y + 7, SCREEN_WIDTH - 14, 38});
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
    windows[EXPLORER] = (struct window){112, 93, BROWSER_WIDTH, BROWSER_HEIGHT, "Arquivos", 0, 0};
    windows[TASKS] = (struct window){196, 198, TASKS_WIDTH, TASKS_HEIGHT, "Gerenciador de tarefas", 0, 0};
    for (unsigned int i = 0; i < NO_WINDOW; ++i)
        window_order[i] = i;
    for (unsigned int slot = 0; slot < APP_PROCESS_LIMIT; ++slot) {
        windows[FIRST_APP + slot].open = 0;
        pending_app_paint[slot] = 0;
        app_window_reset(slot);
    }
    last_app_paint = timer_ticks();
    events_since_slice = 0;
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

static void raise_window(enum window_id id)
{
    unsigned int index = 0;
    while (window_order[index] != id)
        ++index;
    for (; index + 1 < NO_WINDOW; ++index)
        window_order[index] = window_order[index + 1];
    window_order[index] = id;
    focus = id;
}

static void focus_top_window(void)
{
    focus = NO_WINDOW;
    for (unsigned int i = NO_WINDOW; i; --i) {
        if (visible(window_order[i - 1])) {
            focus = window_order[i - 1];
            return;
        }
    }
}

enum app_status desktop_open_app(const struct fat16_volume *volume, const struct fat16_file *file,
    struct app_launch *launch)
{
    unsigned int slot = 0;
    while (slot < APP_PROCESS_LIMIT && windows[FIRST_APP + slot].open)
        ++slot;
    if (slot == APP_PROCESS_LIMIT)
        return APP_LIMIT;

    launch->output = app_window_write;
    launch->context = app_window_context(slot);
    const enum app_status result = app_start(slot, volume, file, launch);
    if (result != APP_STARTED)
        return result;

    app_window_reset(slot);
    pending_app_paint[slot] = 0;
    for (unsigned int i = 0; i < sizeof(file->name); ++i)
        app_titles[slot][i] = file->name[i];
    const enum window_id id = FIRST_APP + slot;
    windows[id] = (struct window){
        272 + slot * 54, 153 + slot * 57, APP_WINDOW_WIDTH, APP_WINDOW_HEIGHT,
        app_titles[slot], 1, 0
    };
    cancel_drag();
    raise_window(id);
    repaint_changed(0);
    return APP_STARTED;
}

void desktop_open_editor(const unsigned char *name)
{
    cancel_drag();
    editor_name = name;
    windows[EDITOR].open = 1;
    windows[EDITOR].minimized = 0;
    raise_window(EDITOR);
    repaint_changed(1);
}

static void focus_window(enum window_id id)
{
    windows[id].open = 1;
    windows[id].minimized = 0;
    cancel_drag();
    raise_window(id);
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
    if (close && id >= FIRST_APP)
        app_release(id - FIRST_APP);
    if (focus == id)
        focus_top_window();
    cancel_drag();
    repaint_changed(0);
}

void desktop_get_tasks(struct desktop_task tasks[APP_PROCESS_LIMIT])
{
    for (unsigned int slot = 0; slot < APP_PROCESS_LIMIT; ++slot) {
        const struct window *window = &windows[FIRST_APP + slot];
        tasks[slot] = window->open ? (struct desktop_task){
            .name = window->title, .app = app_task_info(slot), .minimized = window->minimized
        } : (struct desktop_task){0};
    }
}

int desktop_end_task(unsigned int pid)
{
    for (unsigned int slot = 0; slot < APP_PROCESS_LIMIT; ++slot) {
        const struct app_task_info info = app_task_info(slot);
        if (info.pid && info.pid == pid && info.state != APP_TASK_FINISHED) {
            app_cancel(slot);
            repaint_app_output(1);
            return 1;
        }
    }
    return 0;
}

static void launch(enum window_id choice, unsigned int *row)
{
    menu_open = 0;
    if (choice == EDITOR && !windows[EDITOR].open) {
        shell_edit(row, (const unsigned char *)"ola.asm");
        if (!windows[EDITOR].open)
            repaint_changed(0);
        return;
    }
    if (choice == EXPLORER && !windows[EXPLORER].open)
        browser_init();
    if (choice == TASKS && !windows[TASKS].open)
        tasks_init();
    focus_window(choice);
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
        for (unsigned int item = 0; item < FIRST_APP; ++item) {
            if (hit(x, y, MENU_X + 8, MENU_Y + 47 + item * 40, MENU_WIDTH - 16, 35)) {
                launch(item, row);
                return;
            }
        }
        toggle_menu();
        return;
    }
    for (unsigned int id = 0; id < NO_WINDOW; ++id) {
        const struct taskbar_item item = taskbar_item(id);
        if (windows[id].open && hit(x, y, item.x, BAR_Y + 7, item.width, 38)) {
            if (focus == id)
                hide_window(id, 0);
            else
                focus_window(id);
            return;
        }
    }

    enum window_id id = NO_WINDOW;
    for (unsigned int i = NO_WINDOW; i; --i) {
        if (hit_window(window_order[i - 1])) {
            id = window_order[i - 1];
            break;
        }
    }
    if (id == NO_WINDOW)
        return;
    struct window *w = &windows[id];
    if (focus != id)
        focus_window(id);
    if (y - w->y >= DESKTOP_TITLE_HEIGHT) {
        if (x - w->x < DESKTOP_FRAME || x - w->x >= DESKTOP_FRAME + w->width ||
            y - w->y >= DESKTOP_TITLE_HEIGHT + w->height)
            return;
        const unsigned int local_x = x - w->x - DESKTOP_FRAME;
        const unsigned int local_y = y - w->y - DESKTOP_TITLE_HEIGHT;
        if ((id == EXPLORER && browser_click(local_x, local_y)) ||
            (id == TASKS && tasks_click(local_x, local_y)))
            repaint_client(id);
        return;
    }
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

static void switch_window(void)
{
    const unsigned int start = focus < NO_WINDOW ? focus + 1u : 0u;
    for (unsigned int offset = 0; offset < NO_WINDOW; ++offset) {
        const enum window_id id = (start + offset) % NO_WINDOW;
        if (windows[id].open) {
            menu_open = 0;
            focus_window(id);
            return;
        }
    }
}

static void run_slice(void)
{
    events_since_slice = 0;
    app_schedule();
    repaint_app_output(0);
}

unsigned int desktop_wait_key(struct keyboard_state *keyboard, unsigned int *row)
{
    for (;;) {
        struct mouse_event event;
        service_tools();
        if (events_since_slice >= 8 && app_ready())
            run_slice();

        __asm__ volatile ("cli" : : : "memory");
        if (mouse_poll(&event)) {
            __asm__ volatile ("sti" : : : "memory");
            ++events_since_slice;
            mouse_event(&event, row);
            repaint_app_output(0);
            continue;
        }
        const unsigned int key = keyboard_poll(keyboard);
        if (!key) {
            if (app_ready()) {
                __asm__ volatile ("sti" : : : "memory");
                run_slice();
            } else if (visible(EXPLORER) && browser_busy()) {
                __asm__ volatile ("sti" : : : "memory");
            } else {
                __asm__ volatile ("sti; hlt" : : : "memory");
                repaint_app_output(0);
            }
            continue;
        }
        __asm__ volatile ("sti" : : : "memory");
        ++events_since_slice;

        if (key == KEY_F1) {
            toggle_menu();
        } else if (key == KEY_SWITCH_WINDOW) {
            switch_window();
        } else if (menu_open) {
            if (key == KEY_ESCAPE) {
                toggle_menu();
            } else if (key == KEY_UP || key == KEY_DOWN) {
                menu_choice = (menu_choice + (key == KEY_UP ? FIRST_APP - 1 : 1)) % FIRST_APP;
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
                focus_top_window();
                repaint_changed(0);
            } else {
                vga_graphics_begin_frame();
                shell_editor_draw(windows[EDITOR].x + DESKTOP_FRAME,
                    windows[EDITOR].y + DESKTOP_TITLE_HEIGHT, 0);
                vga_graphics_end_frame();
            }
        } else if (focus == EXPLORER) {
            if (browser_key(key))
                repaint_client(EXPLORER);
        } else if (focus == TASKS) {
            if (key == KEY_ESCAPE)
                hide_window(TASKS, 1);
            else if (tasks_key(key))
                repaint_client(TASKS);
        } else if (focus >= FIRST_APP && focus < NO_WINDOW) {
            app_window_key(focus - FIRST_APP, key);
            repaint_app_output(1);
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
