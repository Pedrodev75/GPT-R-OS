#include "idt.h"
#include "timer.h"
#include "keyboard.h"
#include "shell.h"

enum { WIDTH = 80, HEIGHT = 25, TEXT_COLOR = 0x0f00 };
static volatile unsigned short* const vga = (volatile unsigned short*)0xb8000;

static void out(unsigned short port, unsigned char value)
{
    __asm__ volatile ("outb %0, %w1" : : "a"(value), "Nd"(port));
}

static void vga_register(unsigned short port, unsigned char index,
    unsigned char value)
{
    out(port, index);
    out(port + 1, value);
}

static void load_accents(void)
{
    static const unsigned char marks[4][2] = {
        { 0x0c, 0x18 },{ 0x30, 0x18 },{ 0x00, 0x32 },{ 0x10, 0x28 }
    };
    static const char vowels[] = "aeiouAEIOU";
    volatile unsigned char* const font = (volatile unsigned char*)0xa0000;

    /* Map VGA font memory to plane 2 in BIOS video mode 3. */
    vga_register(0x3c4, 2, 4);
    vga_register(0x3c4, 4, 7);
    vga_register(0x3ce, 4, 2);
    vga_register(0x3ce, 5, 0);
    vga_register(0x3ce, 6, 4);

    for (unsigned int mark = 0; mark < 4; ++mark) {
        volatile unsigned char* const symbol = font + (0xf0 + mark) * 32;
        for (unsigned int y = 0; y < 16; ++y)
            symbol[y] = y == 5 ? marks[mark][0] :
            y == 6 ? marks[mark][1] : 0;

        for (unsigned int i = 0; i < 10; ++i) {
            volatile unsigned char* const glyph = font + (0x82 + mark * 10 + i) * 32;
            volatile unsigned char* const base = font + vowels[i] * 32;
            for (unsigned int y = 0; y < 16; ++y)
                glyph[y] = base[y];
            glyph[0] = marks[mark][0];
            glyph[1] = marks[mark][1];
        }
    }
    for (unsigned int i = 0; i < 2; ++i) {
        volatile unsigned char* const glyph = font + (0x80 + i) * 32;
        volatile unsigned char* const base = font + (i ? 'C' : 'c') * 32;
        for (unsigned int y = 0; y < 16; ++y)
            glyph[y] = base[y];
        glyph[13] |= 0x10;
        glyph[14] |= 0x18;
    }

    vga_register(0x3c4, 2, 3);
    vga_register(0x3c4, 4, 3);
    vga_register(0x3ce, 4, 0);
    vga_register(0x3ce, 5, 0x10);
    vga_register(0x3ce, 6, 0x0e);
}

static void clear_row(unsigned int row)
{
    for (unsigned int col = 0; col < WIDTH; ++col)
        vga[row * WIDTH + col] = TEXT_COLOR | ' ';
}

void kernel_main(void)
{
    const char title[] = "GPT-R OS";

    idt_init();
#ifdef GPT_R_TEST_INVALID_OPCODE
    __asm__ volatile ("ud2");
#endif
    load_accents();
    vga_register(0x3d4, 0x0a, 14);
    vga_register(0x3d4, 0x0b, 15);
    for (unsigned int i = 0; i < HEIGHT; ++i)
        clear_row(i);
    for (unsigned int i = 0; title[i]; ++i)
        vga[i] = TEXT_COLOR | title[i];

    timer_init();
    keyboard_irq_init();
    __asm__ volatile ("sti" : : : "memory");

    shell_run();
}
