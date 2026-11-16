#include "apps/app.h"
#include "drivers/console.h"
#include "memory/pages.h"
#include "memory/heap.h"
#include "interrupts/idt.h"
#include "drivers/timer.h"
#include "drivers/keyboard.h"
#include "fs/fat16.h"
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

enum boot_result { BOOT_OK, BOOT_FAILED, BOOT_SKIP };

static void boot_log(unsigned int *row, enum boot_result result, const char *message)
{
    static const char *const status_text[] = {"[  OK  ] ", "[FAILED] ", "[ SKIP ] "};
    static const unsigned short status_color[] = {0x0a00, 0x0c00, 0x0e00};
    const char *status = status_text[result];
    unsigned int cell = (*row)++ * CONSOLE_WIDTH;
    while (*status)
        console_put(cell++, status_color[result] | (unsigned char)*status++);
    while (*message)
        console_put(cell++, console_color | (unsigned char)*message++);
}

static void log_storage(unsigned int *row)
{
    struct ata_disk disk;
    struct fat16_volume volume;
    const enum ata_probe_result ata = ata_probe(&disk);
    if (ata == ATA_MISSING) {
        boot_log(row, BOOT_SKIP, "ATA PIO - sem disco de dados");
        boot_log(row, BOOT_SKIP, "Volume FAT16");
    } else if (ata != ATA_READY) {
        boot_log(row, BOOT_FAILED, "ATA PIO - falha ao identificar");
        boot_log(row, BOOT_SKIP, "Volume FAT16");
    } else {
        boot_log(row, BOOT_OK, "ATA PIO - disco detectado");
        boot_log(row, fat16_mount(&volume, &disk) ? BOOT_OK : BOOT_FAILED, "Volume FAT16");
    }
}

static int timer_responds(void)
{
    for (unsigned int attempt = 0; attempt < 2000000; ++attempt) {
        if (timer_ticks())
            return 1;
        __asm__ volatile ("pause");
    }
    return 0;
}

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

    unsigned int row = 2;
#ifdef GPT_R_GRAPHICS
    boot_log(&row, BOOT_OK, "Video VBE 1024x768");
#else
    boot_log(&row, BOOT_OK, "Video VGA em modo texto");
#endif
    boot_log(&row, BOOT_OK, "IDT carregada");

    const int e820_ok = E820_MAP->status == E820_COMPLETE &&
                        E820_MAP->count > 0 && E820_MAP->count <= E820_MAX_ENTRIES;
    boot_log(&row, e820_ok ? BOOT_OK : BOOT_FAILED, "Mapa E820");
    pages_init(E820_MAP);
    const int pages_ok = pages_managed_count() != 0;
    boot_log(&row, pages_ok ? BOOT_OK : BOOT_FAILED, "Alocador de paginas");
    heap_init();
    const enum boot_result heap = !pages_ok ? BOOT_SKIP :
                                  heap_self_test() ? BOOT_OK : BOOT_FAILED;
    boot_log(&row, heap, "Heap do kernel");
#ifdef GPT_R_TEST_PAGE_FAULT
    volatile unsigned char *page = page_alloc();
    if ((unsigned long long)page < 0x200000)
        __asm__ volatile ("ud2");
    page[0] = 0xaa;
    page[4095] = 0x55;
    if (page[0] != 0xaa || page[4095] != 0x55 || !page_free((void *)page))
        __asm__ volatile ("ud2");

    /* PDPT entry 1 is absent, even with 1 GiB or more installed. */
    __asm__ volatile ("movb (%0), %%al" : : "r"(0x40000000ull) : "rax", "memory");
#endif
    app_init();
    timer_init();
    const int keyboard_ok = keyboard_irq_init();
    __asm__ volatile ("sti" : : : "memory");

    boot_log(&row, timer_responds() ? BOOT_OK : BOOT_FAILED, "Timer PIT");
    boot_log(&row, keyboard_ok ? BOOT_OK : BOOT_FAILED, "Teclado PS/2 - IRQ1");
    log_storage(&row);
    shell_run(row + 1);
}
