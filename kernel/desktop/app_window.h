#ifndef GPT_R_APP_WINDOW_H
#define GPT_R_APP_WINDOW_H

#ifdef GPT_R_GRAPHICS

#include "../apps/app.h"
#include "../drivers/vga_graphics.h"

enum {
    APP_WINDOW_COLUMNS = 28, APP_WINDOW_ROWS = 12,
    APP_WINDOW_WIDTH = APP_WINDOW_COLUMNS * VGA_OVERLAY_CELL_WIDTH,
    APP_WINDOW_HEIGHT = APP_WINDOW_ROWS * VGA_LINE_HEIGHT
};

void app_window_reset(unsigned int slot);
void app_window_write(void *context, const unsigned char *data, unsigned int count);
void *app_window_context(unsigned int slot);
void app_window_update(unsigned int slot);
void app_window_key(unsigned int slot, unsigned int key);
unsigned int app_window_dirty(unsigned int slot);
void app_window_focus(unsigned int slot);
void app_window_draw(unsigned int slot, unsigned int x, unsigned int y, int focused,
    unsigned int clip_y, unsigned int clip_height);

#endif
#endif
