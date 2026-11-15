#ifndef GPT_R_KEYBOARD_H
#define GPT_R_KEYBOARD_H

void keyboard_irq_init(void);

enum {
    KEY_ESCAPE = 0x1b,
    KEY_UP = 0x100,
    KEY_DOWN,
    KEY_LEFT,
    KEY_RIGHT
};

struct keyboard_state {
    unsigned int shifts;
    int caps;
    int caps_down;
    unsigned char accent;
    unsigned char pending;
    unsigned char extended;
    unsigned char pause_bytes;
};

/* Return zero when no decoded event is ready. */
unsigned int keyboard_poll(struct keyboard_state* keyboard);

/* Wait for a printable character, Enter or Backspace. */
unsigned char keyboard_read_char(struct keyboard_state* keyboard);

#endif
