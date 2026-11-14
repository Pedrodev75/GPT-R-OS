#include "keyboard.h"

enum { WIDTH = 80, HEIGHT = 25, TEXT_COLOR = 0x0f00 };
static volatile unsigned short* const vga = (volatile unsigned short*)0xb8000;

static void clear_row(unsigned int row)
{
    for (unsigned int col = 0; col < WIDTH; ++col)
        vga[row * WIDTH + col] = TEXT_COLOR | ' ';
}

void kernel_main(void)
{
    const char title[] = "GPT-R OS";
    struct keyboard_state keyboard = { 0 };
    unsigned int row = 2;
    unsigned int col = 2;

    for (unsigned int i = 0; i < HEIGHT; ++i)
        clear_row(i);
    for (unsigned int i = 0; title[i]; ++i)
        vga[i] = TEXT_COLOR | title[i];
    vga[row * WIDTH] = TEXT_COLOR | '>';

    for (;;) {
        const char ch = keyboard_read_char(&keyboard);
        if (ch == '\b') {
            if (col > 2)
                vga[row * WIDTH + --col] = TEXT_COLOR | ' ';
            continue;
        }
        if (ch != '\n')
            vga[row * WIDTH + col++] = TEXT_COLOR | ch;
        if (ch != '\n' && col < WIDTH)
            continue;

        col = 2;
        ++row;
        if (row == HEIGHT) {
            /* Keep the title and the blank line above the input. */
            for (unsigned int i = 2 * WIDTH; i < (HEIGHT - 1) * WIDTH; ++i)
                vga[i] = vga[i + WIDTH];
            --row;
            clear_row(row);
        }
        vga[row * WIDTH] = TEXT_COLOR | '>';
    }
}
