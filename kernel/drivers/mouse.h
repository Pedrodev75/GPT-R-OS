#ifndef GPT_R_MOUSE_H
#define GPT_R_MOUSE_H

struct mouse_event {
    unsigned short x, y;
    unsigned char buttons;
};

enum { MOUSE_LEFT = 1 };

int mouse_init(void);
int mouse_poll(struct mouse_event *event);
int mouse_available(void);

#endif
