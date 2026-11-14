#ifndef GPT_R_KEYBOARD_H
#define GPT_R_KEYBOARD_H

struct keyboard_state {
    unsigned int shifts;
    int caps;
    int caps_down;
};

/* Wait for a printable character, Enter or Backspace. */
char keyboard_read_char(struct keyboard_state* keyboard);

#endif
