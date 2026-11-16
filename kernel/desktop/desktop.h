#ifndef GPT_R_DESKTOP_H
#define GPT_R_DESKTOP_H

#include "../drivers/console.h"

#ifdef GPT_R_GRAPHICS
enum {
    DESKTOP_TITLE_HEIGHT = 36, DESKTOP_FRAME = 4,
    DESKTOP_TERMINAL_X = 22, DESKTOP_TERMINAL_Y = 50,
    DESKTOP_EDITOR_X = 120, DESKTOP_EDITOR_Y = 85,
    DESKTOP_EDITOR_CLIENT_X = DESKTOP_EDITOR_X + DESKTOP_FRAME,
    DESKTOP_EDITOR_CLIENT_Y = DESKTOP_EDITOR_Y + DESKTOP_TITLE_HEIGHT,
    DESKTOP_EDITOR_WIDTH = 870, DESKTOP_EDITOR_HEIGHT = 567
};

enum desktop_action { DESKTOP_TERMINAL, DESKTOP_EDITOR };

struct keyboard_state;
void desktop_show_terminal(void);
void desktop_restore_terminal(void);
/* The editor presents this frame after drawing its first contents. */
void desktop_begin_editor(const unsigned char *name);
enum desktop_action desktop_launcher(struct keyboard_state *keyboard, int close_terminal);
#endif

#endif
