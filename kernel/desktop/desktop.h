#ifndef GPT_R_DESKTOP_H
#define GPT_R_DESKTOP_H

#include "../drivers/console.h"

#ifdef GPT_R_GRAPHICS
enum {
    DESKTOP_TITLE_HEIGHT = 36, DESKTOP_FRAME = 4,
    DESKTOP_TERMINAL_X = 22, DESKTOP_TERMINAL_Y = 50,
    DESKTOP_EDITOR_X = 120, DESKTOP_EDITOR_Y = 85,
    DESKTOP_EDITOR_WIDTH = 870, DESKTOP_EDITOR_HEIGHT = 567
};

struct keyboard_state;
void desktop_init(void);
void desktop_repaint(void);
void desktop_open_editor(const unsigned char *name);
unsigned int desktop_wait_key(struct keyboard_state *keyboard, unsigned int *row);
#endif

#endif
