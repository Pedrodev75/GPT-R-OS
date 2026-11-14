#include "keyboard.h"

/* PS/2 scan code set 1, using the BIOS controller's translation. */
static const char normal[] =
"\0\0331234567890-=\b\tqwertyuiop[]\n\0asdfghjkl;'\x60\0\\zxcvbnm,./\0*\0 ";
static const char shifted[] =
"\0\033!@#$%^&*()_+\b\tQWERTYUIOP{}\n\0ASDFGHJKL:\"~\0|ZXCVBNM<>?\0*\0 ";

static unsigned char read_port(unsigned short port)
{
    unsigned char value;
    __asm__ volatile ("inb %w1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

char keyboard_read_char(struct keyboard_state* keyboard)
{
    unsigned int skip = 0;

    for (;;) {
        const unsigned char status = read_port(0x64);
        if (!(status & 1))
            continue;
        const unsigned char key = read_port(0x60);
        if (status & 0xe0)
            continue;

        if (skip) {
            --skip;
            continue;
        }
        if (key == 0xe0 || key == 0xe1) {
            skip = key == 0xe0 ? 1 : 5;
            continue;
        }

        const unsigned int code = key & 0x7f;
        const int released = key & 0x80;
        if (code == 0x2a || code == 0x36) {
            const unsigned int mask = code == 0x2a ? 1 : 2;
            if (released)
                keyboard->shifts &= ~mask;
            else
                keyboard->shifts |= mask;
            continue;
        }
        if (code == 0x3a) {
            if (!released && !keyboard->caps_down)
                keyboard->caps = !keyboard->caps;
            keyboard->caps_down = !released;
            continue;
        }
        if (released || code >= sizeof(normal) - 1)
            continue;

        const char plain = normal[code];
        const int letter = plain >= 'a' && plain <= 'z';
        const char ch = (!!keyboard->shifts ^ (letter && keyboard->caps)) ? shifted[code] : plain;
        if (ch == '\n' || ch == '\b' || (ch >= ' ' && ch <= '~'))
            return ch;
    }
}
