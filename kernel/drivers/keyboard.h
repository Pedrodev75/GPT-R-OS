#ifndef GPT_R_KEYBOARD_H
#define GPT_R_KEYBOARD_H

int keyboard_irq_init(void);

enum {
    KEY_ESCAPE = 0x1b,
    KEY_UP = 0x100,
    KEY_DOWN,
    KEY_LEFT,
    KEY_RIGHT,
    KEY_INTERRUPT,
    KEY_SAVE,
    KEY_HOME,
    KEY_END,
    KEY_DELETE
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
unsigned int keyboard_poll(struct keyboard_state *keyboard);

/* Wait for any decoded event. */
unsigned int keyboard_wait(struct keyboard_state *keyboard);

#endif
