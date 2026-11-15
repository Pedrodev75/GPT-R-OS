#include "timer.h"
#include "console.h"

enum {
    PIT_HZ = 1193182, TICKS_PER_SECOND = 100,
    DIVISOR = (PIT_HZ + TICKS_PER_SECOND / 2) / TICKS_PER_SECOND
};
#ifdef GPT_R_GRAPHICS
enum { COUNTER_CELL = CONSOLE_WIDTH };
#else
enum { COUNTER_CELL = 54 };
#endif

static unsigned int ticks;
static volatile unsigned int total_ticks;
static volatile unsigned long long seconds;

static void out(unsigned short port, unsigned char value)
{
    __asm__ volatile ("outb %0, %w1" : : "a"(value), "Nd"(port));
}

static void show_uptime(void)
{
    const char label[] = "Tempo ligado: ";
    char digits[10];
    unsigned long long value = seconds;
    unsigned int length = 0;
    unsigned int cell = COUNTER_CELL;
    const unsigned short color = console_color;

    for (unsigned int i = 0; label[i]; ++i)
        console_put(cell++, color | label[i]);
    do {
        digits[length++] = '0' + value % 10;
        value /= 10;
    } while (value && length < sizeof(digits));
    while (length)
        console_put(cell++, color | digits[--length]);
    console_put(cell++, color | ' ');
    console_put(cell, color | 's');
}

void timer_init(void)
{
    ticks = 0;
    total_ticks = 0;
    seconds = 0;
    show_uptime();

    /* Channel 0, low byte then high byte, mode 2 (rate generator). */
    out(0x43, 0x34);
    out(0x40, DIVISOR & 0xff);
    out(0x40, DIVISOR >> 8);
}

void timer_irq_handler(void)
{
    ++total_ticks;
    if (++ticks == TICKS_PER_SECOND) {
        ticks = 0;
        ++seconds;
        show_uptime();
    }
#ifdef GPT_R_GRAPHICS
    if (ticks == 0 || ticks == 50)
        vga_graphics_blink();
#endif
    out(0x20, 0x20);
}

unsigned long long timer_uptime_seconds(void)
{
    return seconds;
}

unsigned int timer_ticks(void)
{
    return total_ticks;
}
