#include "shell.h"
#include "shell_internal.h"
#include "../drivers/keyboard.h"
#include "../drivers/timer.h"

static void out(unsigned short port, unsigned char value)
{
    __asm__ volatile ("outb %0, %w1" : : "a"(value), "Nd"(port));
}

#ifndef GPT_R_GRAPHICS
static void vga_register(unsigned short port, unsigned char index,
    unsigned char value)
{
    out(port, index);
    out(port + 1, value);
}

#endif

static void move_cursor(unsigned int row, unsigned int col)
{
    const unsigned int position = row * WIDTH + col;
#ifdef GPT_R_GRAPHICS
    vga_graphics_cursor(position);
#else
    vga_register(0x3d4, 0x0e, position >> 8);
    vga_register(0x3d4, 0x0f, position);
#endif
}

static unsigned int color_index(const unsigned char **argument)
{
    const unsigned char *text = *argument;
    while (*text == ' ')
        ++text;
    if (*text < '0' || *text > '9')
        return 256;
    unsigned int value = 0;
    do {
        value = value * 10 + (*text++ - '0');
        if (value > 15)
            return 256;
    } while (*text >= '0' && *text <= '9');

    if (*text && *text != ' ')
        return 256;
    *argument = text;
    return value;
}

static void command_color(unsigned int *row, const unsigned char *argument)
{
    const unsigned int foreground = color_index(&argument);
    const unsigned int background = color_index(&argument);
    if (foreground > 15 || background > 7 || *argument) {
        shell_print_line(row, "Uso: color <frente 0-15> <fundo 0-7>");
        return;
    }

    if (foreground == background) {
        shell_print_line(row, "Frente e fundo precisam ser diferentes");
        return;
    }
    const unsigned short color = (background << 12) | (foreground << 8);
    unsigned long long flags;
    /* The PIT redraws the header from IRQ0. Recolor it atomically. */
    __asm__ volatile ("pushfq; pop %0; cli" : "=r"(flags) : : "memory");
    console_color = color;
    for (unsigned int i = 0; i < FIRST_ROW * WIDTH; ++i)
        console_put(i, color | (console_get(i) & 0xff));
    if (flags & 0x200)
        __asm__ volatile ("sti" : : : "memory");
    for (unsigned int i = FIRST_ROW * WIDTH; i < HEIGHT * WIDTH; ++i)
        console_put(i, color | (console_get(i) & 0xff));
}

static const unsigned char *argument_for(const unsigned char *input, const char *name)
{
    while (*name && *input == (unsigned char)*name) {
        ++input;
        ++name;
    }
    return !*name && (!*input || *input == ' ') ? input : 0;
}


static struct keyboard_state keyboard;

static void command_clear(unsigned int *row, const unsigned char *argument)
{
    (void)argument;
    for (unsigned int i = FIRST_ROW; i < HEIGHT; ++i)
        shell_clear_row(i);
    *row = FIRST_ROW;
}

static void command_version(unsigned int *row, const unsigned char *argument)
{
    (void)argument;
    shell_print_line(row, "GPT-R OS v0.4.5");
}

static void command_uptime(unsigned int *row, const unsigned char *argument)
{
    (void)argument;
    shell_print_number(row, "", timer_uptime_seconds(), " s");
}

static void command_reboot(unsigned int *row, const unsigned char *argument)
{
    (void)argument;
    out(0xcf9, 0x02);
    out(0xcf9, 0x06);
    shell_print_line(row, "reboot: controlador nao reiniciou");
}

#ifdef GPT_R_GRAPHICS
static void command_demo(unsigned int *row, const unsigned char *argument)
{
    (void)row;
    (void)argument;
    vga_graphics_demo(&keyboard);
}
#endif

static void command_help(unsigned int *row, const unsigned char *argument);

static const struct command {
    const char *name;
    const char *help;
    void (*run)(unsigned int *, const unsigned char *);
    int takes_args;
} commands[] = {
    {"help", "Lista os comandos", command_help, 0},
    {"clear", "Limpa o terminal", command_clear, 0},
    {"version", "Mostra a versao", command_version, 0},
    {"uptime", "Mostra o tempo ligado", command_uptime, 0},
    {"mem", "Mostra o mapa e o uso da memoria", shell_mem, 0},
    {"memtest", "Testa uma pagina de 4 KiB", shell_memtest, 0},
    {"kmalloc", "Aloca um bloco (bytes em decimal)", shell_kmalloc, 1},
    {"kfree", "Libera um bloco (endereco hexadecimal)", shell_kfree, 1},
    {"peek", "Le 16 bytes de um endereco hexadecimal", shell_peek, 1},
    {"reboot", "Reinicia o sistema", command_reboot, 0},
    {"diskinfo", "Mostra o disco ATA", shell_diskinfo, 0},
    {"sector", "Mostra 16 bytes de um setor", shell_sector, 1},
    {"ls", "Lista arquivos FAT16", shell_ls, 0},
    {"cat", "Mostra o texto de um arquivo", shell_cat, 1},
    {"write", "Cria ou sobrescreve um arquivo", shell_write, 1},
    {"rm", "Remove um arquivo", shell_rm, 1},
    {"color", "Muda as cores do terminal", command_color, 1},
    {"crash", "Executa testes, incluindo crash heap", shell_crash, 1},
#ifdef GPT_R_GRAPHICS
    {"demo", "Abre a demonstracao VGA", command_demo, 0},
#endif
};

static void command_help(unsigned int *row, const unsigned char *argument)
{
    (void)argument;
    for (unsigned int i = 0; i < sizeof(commands) / sizeof(commands[0]); ++i) {
        char line[80];
        unsigned int n = 0;
        while (commands[i].name[n]) {
            line[n] = commands[i].name[n];
            ++n;
        }
        while (n < 9)
            line[n++] = ' ';
        const char *description = commands[i].help;
        while (*description && n + 1 < sizeof(line))
            line[n++] = *description++;
        line[n] = 0;
        shell_print_line(row, line);
    }
}

static void execute(unsigned int *row, unsigned char *line, unsigned int length)
{
    while (length && *line == ' ') {
        ++line;
        --length;
    }
    while (length && line[length - 1] == ' ')
        --length;
    line[length] = 0;
    if (!length)
        return;

    for (unsigned int i = 0; i < sizeof(commands) / sizeof(commands[0]); ++i) {
        const unsigned char *argument = argument_for(line, commands[i].name);
        if (argument && (commands[i].takes_args || !*argument)) {
            commands[i].run(row, argument);
            return;
        }
    }
    shell_print_line(row, "Comando desconhecido");
}

_Noreturn void shell_run(unsigned int row)
{
    keyboard = (struct keyboard_state){0};
    unsigned char line[MAX_INPUT + 1];
    shell_memory_init();

    for (;;) {
        unsigned int length = 0;
        console_put(row * WIDTH, console_color | '>');
        for (;;) {
            const unsigned int col = PROMPT_WIDTH + length;
            move_cursor(row, col < WIDTH ? col : WIDTH - 1);
            const unsigned char ch = keyboard_read_char(&keyboard);
#ifdef GPT_R_GRAPHICS
            vga_graphics_cursor(WIDTH * HEIGHT);
#endif
            if (ch == '\n')
                break;
            if (ch == '\b') {
                if (length)
                    console_put(row * WIDTH + PROMPT_WIDTH + --length, console_color | ' ');
            } else if (length < MAX_INPUT) {
                line[length++] = ch;
                console_put(row * WIDTH + col, console_color | ch);
            }
        }
        shell_next_row(&row);
        execute(&row, line, length);
    }
}
