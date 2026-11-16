#ifndef GPT_R_APP_CANVAS_H
#define GPT_R_APP_CANVAS_H

#ifdef GPT_R_GRAPHICS

enum {
    APP_GUI_BEGIN = 4, APP_GUI_RECT, APP_GUI_TEXT, APP_GUI_PRESENT, APP_GUI_WAIT,
    APP_GUI_COMMANDS = 96, APP_GUI_GLYPHS = 512
};

void app_canvas_reset(unsigned int slot);
int app_canvas_visible(unsigned int slot);
int app_canvas_begin(unsigned int slot, unsigned long long color);
int app_canvas_rect(unsigned int slot, unsigned long long x, unsigned long long y,
    unsigned long long width, unsigned long long height, unsigned long long color);
int app_canvas_text(unsigned int slot, unsigned long long x, unsigned long long y,
    const unsigned char *text, unsigned long long size, unsigned long long color);
int app_canvas_present(unsigned int slot);
void app_canvas_click(unsigned int slot, unsigned int x, unsigned int y);
unsigned long long app_canvas_poll(unsigned int slot);
unsigned int app_canvas_dirty(unsigned int slot);
void app_canvas_draw(unsigned int slot, unsigned int x, unsigned int y,
    unsigned int clip_y, unsigned int clip_height);

#endif
#endif
