#include "drivers/console.h"
#include "memory/pages.h"
#include "memory/heap.h"
#include "interrupts/idt.h"
#include "drivers/timer.h"
#include "drivers/keyboard.h"
#include "shell/shell.h"

volatile unsigned short console_color;

#ifndef GPT_R_GRAPHICS

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

#endif

void kernel_main(void)
{
    const char title[] = "GPT-R OS";

#ifdef GPT_R_GRAPHICS
    vga_graphics_init();
#endif
    idt_init();
#ifdef GPT_R_TEST_INVALID_OPCODE
    __asm__ volatile ("ud2");
#endif
#ifndef GPT_R_GRAPHICS
    load_accents();
    vga_register(0x3d4, 0x0a, 14);
    vga_register(0x3d4, 0x0b, 15);
#endif
    console_color = 0x0f00;
    for (unsigned int i = 0; i < CONSOLE_WIDTH * CONSOLE_HEIGHT; ++i)
        console_put(i, console_color | ' ');
    for (unsigned int i = 0; title[i]; ++i)
        console_put(i, console_color | title[i]);

    pages_init(E820_MAP);
    heap_init();
#ifdef GPT_R_TEST_PAGE_FAULT
    volatile unsigned char* page = page_alloc();
    if ((unsigned long long)page < 0x200000)
        __asm__ volatile ("ud2");
    page[0] = 0xaa;
    page[4095] = 0x55;
    if (page[0] != 0xaa || page[4095] != 0x55 || !page_free((void*)page))
        __asm__ volatile ("ud2");

    /* PDPT entry 1 is absent, even with 1 GiB or more installed. */
    __asm__ volatile ("movb (%0), %%al" : : "r"(0x40000000ull) : "rax", "memory");
#endif
    timer_init();
    keyboard_irq_init();
    __asm__ volatile ("sti" : : : "memory");

    shell_run();
}
