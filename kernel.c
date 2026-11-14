enum { WIDTH = 80, HEIGHT = 25, TEXT_COLOR = 0x0f00 };
static volatile unsigned short* const vga = (volatile unsigned short*)0xb8000;

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

struct keyboard_state {
    unsigned int shifts;
    int caps;
    int caps_down;
};

static char read_key(struct keyboard_state* keyboard)
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
        const char ch = read_key(&keyboard);
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
