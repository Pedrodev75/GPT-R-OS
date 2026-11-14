#include "keyboard.h"

/* PS/2 scan code set 1, using the BIOS controller's translation. */
/* ABNT2 scan codes, translated by the PS/2 controller to set 1. */
static const unsigned char normal[128] = {
    [2] = '1', '2','3','4','5','6','7','8','9','0','-','=',
    '\b',[16] = 'q','w','e','r','t','y','u','i','o','p', 0xf0,'[',
    '\n',[30] = 'a','s','d','f','g','h','j','k','l', 0x80,0xf2,'\'',
    [43] = ']', 'z','x','c','v','b','n','m',',','.',';',[55] = '*',
    [57] = ' ',[0x56] = '\\',[0x73] = '/',[0x7e] = '.'
};
static const unsigned char shifted[128] = {
    [2] = '!', '@','#','$','%','^','&','*','(',')','_','+',
    '\b',[16] = 'Q','W','E','R','T','Y','U','I','O','P',0xf1,'{',
    '\n',[30] = 'A','S','D','F','G','H','J','K','L',0x81,0xf3,'"',
    [43] = '}', 'Z','X','C','V','B','N','M','<','>',':',[55] = '*',
    [57] = ' ',[0x56] = '|',[0x73] = '?',[0x7e] = '.'
};

static unsigned char compose(unsigned char accent, unsigned char ch)
{
    static const char vowels[] = "aeiouAEIOU";
    for (unsigned int i = 0; vowels[i]; ++i)
        if (ch == vowels[i])
            return 0x82 + (accent - 0xf0) * 10 + i;
    return 0;
}

static unsigned char read_port(unsigned short port)
{
    unsigned char value;
    __asm__ volatile ("inb %w1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

unsigned char keyboard_read_char(struct keyboard_state* keyboard)
{
    if (keyboard->pending) {
        const unsigned char ch = keyboard->pending;
        keyboard->pending = 0;
        return ch;
    }
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
        if (released || code >= sizeof(normal))
            continue;

        const unsigned char plain = normal[code];
        const int letter = (plain >= 'a' && plain <= 'z') || plain == 0x80;
        unsigned char ch = (!!keyboard->shifts ^ (letter && keyboard->caps))
            ? shifted[code] : plain;
        if (!ch)
            continue;
        if (ch >= 0xf0 && !keyboard->accent) {
            keyboard->accent = ch;
            continue;
        }
        if (keyboard->accent) {
            const unsigned char accent = keyboard->accent;
            keyboard->accent = 0;
            if (ch == '\b')
                continue;
            const unsigned char combined = compose(accent, ch);
            if (combined)
                return combined;
            if (ch != ' ')
                keyboard->pending = ch;
            return accent;
        }
        return ch;
    }
}
