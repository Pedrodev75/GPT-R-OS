#include "desktop.h"
#include "../drivers/keyboard.h"

#ifdef GPT_R_GRAPHICS

enum {
    SCREEN_WIDTH = VGA_GRAPHICS_WIDTH, SCREEN_HEIGHT = VGA_GRAPHICS_HEIGHT,
    CELL = VGA_OVERLAY_CELL_WIDTH, BAR_HEIGHT = 52, BAR_Y = SCREEN_HEIGHT - BAR_HEIGHT,
    MENU_X = 14, MENU_WIDTH = 300, MENU_HEIGHT = 176, MENU_Y = BAR_Y - MENU_HEIGHT - 8,
    WORKSPACE = 0x101c30, PANEL = 0x142338, ACTIVE = 0x1c3850, BORDER = 0x364e6a,
    SHADOW = 0x080e19, INK = 0xe7eef8, MUTED = 0x91a8c1, ACCENT = 0x61dcf5
};

struct window {
    unsigned int x, y, width, height;
    const char *title;
};

static const struct window terminal = {
    DESKTOP_TERMINAL_X, DESKTOP_TERMINAL_Y, CONSOLE_WIDTH * VGA_CELL_WIDTH,
    CONSOLE_HEIGHT * VGA_LINE_HEIGHT, "Terminal"
};
static const struct window editor = {
    DESKTOP_EDITOR_X, DESKTOP_EDITOR_Y, DESKTOP_EDITOR_WIDTH, DESKTOP_EDITOR_HEIGHT, "Editor"
};

static int terminal_open, terminal_focused;

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
    if (window == &terminal)
        terminal_icon(x + 12, y + 9, focused ? ACCENT : MUTED);
    else
        editor_icon(x + 12, y + 9, ACCENT);
    text(x + 46, y + 8, window->title, focused ? INK : MUTED);
    if (name) {
        text(x + 151, y + 8, "/", MUTED);
        text(x + 181, y + 8, (const char *)name, INK);
    }
    /* Esc activates this window's close control. */
    vga_graphics_fill(x + width - 39, y + 7, 30, 23, PANEL);
    text(x + width - 32, y + 8, "x", focused ? INK : MUTED);
}

static void draw_taskbar(int editor_active, int menu_open)
{
    const unsigned int top = BAR_Y + 7;
    vga_graphics_fill(0, BAR_Y, SCREEN_WIDTH, BAR_HEIGHT, PANEL);
    vga_graphics_fill(0, BAR_Y, SCREEN_WIDTH, 1, BORDER);
    vga_graphics_fill(14, top, 158, 38, menu_open ? ACTIVE : WORKSPACE);
    logo(26, top + 9);
    text(55, top + 8, editor_active ? "Menu" : "F1 Menu", editor_active ? MUTED : INK);
    if (terminal_open) {
        vga_graphics_fill(181, top, 180, 38, editor_active || menu_open ? WORKSPACE : ACTIVE);
        terminal_icon(194, top + 10, editor_active || menu_open ? MUTED : ACCENT);
        text(232, top + 8, "Terminal", INK);
        if (!editor_active && !menu_open)
            vga_graphics_fill(181, top + 35, 180, 3, ACCENT);
    }
    if (editor_active) {
        vga_graphics_fill(371, top, 152, 38, ACTIVE);
        editor_icon(383, top + 10, ACCENT);
        text(415, top + 8, "Editor", INK);
        vga_graphics_fill(371, top + 35, 152, 3, ACCENT);
    }
    if (terminal_open || editor_active)
        text(SCREEN_WIDTH - 171, top + 8, "Esc Fecha", MUTED);
}

static void draw_menu_items(enum desktop_action choice)
{
    for (unsigned int item = 0; item < 2; ++item) {
        const unsigned int y = MENU_Y + 47 + item * 40;
        const int selected = item == (unsigned int)choice;
        vga_graphics_fill(MENU_X + 8, y, MENU_WIDTH - 16, 35,
            selected ? ACTIVE : PANEL);
        if (selected)
            vga_graphics_fill(MENU_X + 8, y + 4, 3, 27, ACCENT);
        if (item == DESKTOP_TERMINAL)
            terminal_icon(MENU_X + 22, y + 9, selected ? ACCENT : MUTED);
        else
            editor_icon(MENU_X + 22, y + 9, selected ? ACCENT : MUTED);
        text(MENU_X + 60, y + 6, item == DESKTOP_TERMINAL ? "Terminal" : "Editor", INK);
    }
}

static void draw_menu(enum desktop_action choice)
{
    vga_graphics_fill(MENU_X + 6, MENU_Y + 6, MENU_WIDTH, MENU_HEIGHT, SHADOW);
    vga_graphics_fill(MENU_X, MENU_Y, MENU_WIDTH, MENU_HEIGHT, BORDER);
    vga_graphics_fill(MENU_X + 1, MENU_Y + 1, MENU_WIDTH - 2, MENU_HEIGHT - 2, PANEL);
    text(MENU_X + 15, MENU_Y + 14, "Aplicativos", ACCENT);
    draw_menu_items(choice);
    text(MENU_X + 16, MENU_Y + 139, "Setas / Enter", MUTED);
}

static void paint_desktop(int editor_active, const unsigned char *filename, int menu_open)
{
    vga_graphics_begin_overlay();
    vga_graphics_begin_frame();
    desktop_background();
    terminal_focused = terminal_open && !editor_active && !menu_open;
    if (terminal_open) {
        draw_window(&terminal, terminal_focused, 0);
        vga_graphics_place_console(terminal.x + DESKTOP_FRAME, terminal.y + DESKTOP_TITLE_HEIGHT);
        vga_graphics_snapshot_console();
    }
    if (editor_active)
        draw_window(&editor, 1, filename);
    draw_taskbar(editor_active, menu_open);
}

void desktop_show_terminal(void)
{
    terminal_open = 1;
    paint_desktop(0, 0, 0);
    vga_graphics_end_frame();
    vga_graphics_resume_console();
}

void desktop_restore_terminal(void)
{
    if (!terminal_focused)
        desktop_show_terminal();
}

void desktop_begin_editor(const unsigned char *name)
{
    paint_desktop(1, name, 0);
}

enum desktop_action desktop_launcher(struct keyboard_state *keyboard, int close_terminal)
{
    if (close_terminal)
        terminal_open = 0;
    int menu_open = !close_terminal;
    enum desktop_action choice = DESKTOP_TERMINAL;
    paint_desktop(0, 0, menu_open);
    if (menu_open)
        draw_menu(choice);
    vga_graphics_end_frame();

    for (;;) {
        const unsigned int key = keyboard_wait(keyboard);
        if (key == KEY_F1 || (menu_open && key == KEY_ESCAPE)) {
            if (menu_open && terminal_open)
                return DESKTOP_TERMINAL;
            menu_open = !menu_open;
            paint_desktop(0, 0, menu_open);
            if (menu_open)
                draw_menu(choice);
            vga_graphics_end_frame();
        } else if (menu_open && (key == KEY_UP || key == KEY_DOWN)) {
            choice = choice == DESKTOP_EDITOR ? DESKTOP_TERMINAL : DESKTOP_EDITOR;
            vga_graphics_begin_frame();
            draw_menu_items(choice);
            vga_graphics_end_frame();
        } else if (menu_open && key == '\n') {
            return choice;
        }
    }
}

#endif
