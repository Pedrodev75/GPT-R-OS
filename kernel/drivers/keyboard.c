#include "keyboard.h"
#include "../apps/app.h"

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

static void write_port(unsigned short port, unsigned char value)
{
    __asm__ volatile ("outb %0, %w1" : : "a"(value), "Nd"(port));
}

enum { QUEUE_MASK = 63 };
static volatile unsigned char queue[QUEUE_MASK + 1];
static volatile unsigned int head;
static volatile unsigned int tail;

static unsigned int ctrl_keys, scan_pause;
static int scan_extended, suppress_c;

enum scan_action { QUEUE_SCAN, DROP_SCAN, INTERRUPT_APP };

static enum scan_action scan_shortcut(unsigned char byte)
{
    if (scan_pause) {
        --scan_pause;
        return QUEUE_SCAN;
    }
    if (byte == 0xe1) {
        scan_pause = 5;
        scan_extended = 0;
        return QUEUE_SCAN;
    }
    if (byte == 0xe0) {
        scan_extended = 1;
        return QUEUE_SCAN;
    }
    const int extended = scan_extended;
    scan_extended = 0;
    const unsigned int code = byte & 0x7f;
    const int released = byte & 0x80;
    if (code == 0x1d) {
        const unsigned int mask = extended ? 2 : 1;
        if (released)
            ctrl_keys &= ~mask;
        else
            ctrl_keys |= mask;
    }
    if (extended || code != 0x2e)
        return QUEUE_SCAN;
    if (suppress_c) {
        if (released)
            suppress_c = 0;
        return DROP_SCAN;
    }
    if (!released && ctrl_keys) {
        suppress_c = 1;
        return INTERRUPT_APP;
    }
    return QUEUE_SCAN;
}

static enum scan_action receive_byte(unsigned char status)
{
    const unsigned char byte = read_port(0x60);
    if (status & 0xe0)
        return DROP_SCAN;
    /* Recognize Ctrl+C even if a CPU-bound app has filled the input queue. */
    const enum scan_action action = scan_shortcut(byte);
    const unsigned int next = (head + 1) & QUEUE_MASK;
    if (action == QUEUE_SCAN && next != tail) {
        queue[head] = byte;
        head = next;
    }
    return action;
}

int keyboard_irq_init(void)
{
    head = 0;
    tail = 0;
    ctrl_keys = scan_pause = 0;
    scan_extended = suppress_c = 0;

    /* Remap the two 8259 PICs, then unmask IRQ0 and IRQ1. */
    write_port(0x21, 0xff);
    write_port(0xa1, 0xff);
    write_port(0x20, 0x11);
    write_port(0xa0, 0x11);
    write_port(0x21, 0x20);
    write_port(0xa1, 0x28);
    write_port(0x21, 0x04);
    write_port(0xa1, 0x02);
    write_port(0x21, 0x01);
    write_port(0xa1, 0x01);
    write_port(0xa1, 0xff);

    /* The BIOS configured the PS/2 keyboard port and scan-code translation. */
    write_port(0x21, 0xfc);
    unsigned char status = read_port(0x64);
    for (unsigned int i = 0; i < 64 && status != 0xff && (status & 1); ++i) {
        receive_byte(status);
        status = read_port(0x64);
    }
    return status != 0xff && !(status & 1) && !(read_port(0x21) & 2);
}

void keyboard_irq_handler(unsigned long long cs)
{
    const unsigned char status = read_port(0x64);
    const enum scan_action action = (status & 1) ? receive_byte(status) : DROP_SCAN;
    write_port(0x20, 0x20);
    if (action == INTERRUPT_APP)
        app_interrupt(cs);
}

unsigned int keyboard_poll(struct keyboard_state *keyboard)
{
    if (keyboard->pending) {
        const unsigned char ch = keyboard->pending;
        keyboard->pending = 0;
        return ch;
    }
    /* IRQ1 owns head. Only the foreground consumer advances tail. */
    while (head != tail) {
        const unsigned char key = queue[tail];
        tail = (tail + 1) & QUEUE_MASK;

        if (keyboard->pause_bytes) {
            --keyboard->pause_bytes;
            continue;
        }
        if (key == 0xe1) {
            keyboard->extended = 0;
            keyboard->pause_bytes = 5;
            continue;
        }
        if (key == 0xe0) {
            keyboard->extended = 1;
            continue;
        }
        if (keyboard->extended) {
            keyboard->extended = 0;
            switch (key) {
            case 0x48: return KEY_UP;
            case 0x50: return KEY_DOWN;
            case 0x4b: return KEY_LEFT;
            case 0x4d: return KEY_RIGHT;
            default: continue;
            }
        }
        if (key == 1) {
            keyboard->accent = 0;
            return KEY_ESCAPE;
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
    return 0;
}

unsigned int keyboard_wait(struct keyboard_state *keyboard)
{
    for (;;) {
        if (app_interrupt_pending())
            return KEY_INTERRUPT;
        const unsigned int key = keyboard_poll(keyboard);
        if (key)
            return key;

        __asm__ volatile ("cli" : : : "memory");
        if (head == tail && !app_interrupt_pending())
            __asm__ volatile ("sti; hlt" : : : "memory");
        else
            __asm__ volatile ("sti" : : : "memory");
    }
}

