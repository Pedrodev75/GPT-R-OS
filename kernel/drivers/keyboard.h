#ifndef GPT_R_KEYBOARD_H
#define GPT_R_KEYBOARD_H

void keyboard_irq_init(void);

struct keyboard_state {
    unsigned int shifts;
    int caps;
    int caps_down;
    unsigned char accent;
    unsigned char pending;
};

/* Wait for a printable character, Enter or Backspace. */
unsigned char keyboard_read_char(struct keyboard_state* keyboard);

#endif
