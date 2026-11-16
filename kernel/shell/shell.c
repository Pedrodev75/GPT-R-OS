#include "shell.h"
#include "shell_internal.h"
#include "../drivers/keyboard.h"
#include "../drivers/timer.h"
#include "../desktop/desktop.h"

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

void shell_move_cursor(unsigned int row, unsigned int col)
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
    shell_print_line(row, "GPT-R OS v0.6.0");
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
    desktop_repaint();
}
#endif

static void command_help(unsigned int *row, const unsigned char *argument);
static void command_man(unsigned int *row, const unsigned char *argument);

static const struct command {
    const char *name;
    const char *help;
    void (*run)(unsigned int *, const unsigned char *);
    const char *usage;
} commands[] = {
    {"help", "Lista os comandos", command_help, 0},
    {"clear", "Limpa o terminal", command_clear, 0},
    {"version", "Mostra a versao", command_version, 0},
    {"uptime", "Mostra o tempo ligado", command_uptime, 0},
    {"mem", "Mostra o mapa e o uso da memoria", shell_mem, 0},
    {"memtest", "Testa uma pagina de 4 KiB", shell_memtest, 0},
    {"kmalloc", "Aloca um bloco (bytes em decimal)", shell_kmalloc, "<bytes>"},
    {"kfree", "Libera um bloco (endereco hexadecimal)", shell_kfree, "<endereco>"},
    {"peek", "Le 16 bytes de um endereco hexadecimal", shell_peek, "<endereco>"},
    {"reboot", "Reinicia o sistema", command_reboot, 0},
    {"diskinfo", "Mostra o disco ATA", shell_diskinfo, 0},
    {"sector", "Mostra 16 bytes de um setor", shell_sector, "<LBA>"},
    {"ls", "Lista arquivos FAT16", shell_ls, 0},
    {"cat", "Le texto com paginacao", shell_cat, "<arquivo>"},
    {"run", "Executa um .APP com argumentos", shell_run_app, "<arquivo.APP> [argumentos]"},
    {"asm", "Monta um fonte para .APP", shell_assemble, "<fonte.asm> <saida.APP>"},
    {"appinfo", "Mostra o cabecalho de um .APP", shell_appinfo, "<arquivo.APP>"},
#ifdef GPT_R_GRAPHICS
    {"edit", "Abre o editor grafico de texto", shell_edit, "<arquivo>"},
#endif
    {"write", "Cria ou sobrescreve um arquivo", shell_write, "<arquivo> <texto>"},
    {"rm", "Remove um arquivo", shell_rm, "<arquivo>"},
    {"color", "Muda as cores do terminal", command_color, "<frente> <fundo>"},
    {"crash", "Executa testes, incluindo crash heap", shell_crash, "<tipo>"},
#ifdef GPT_R_GRAPHICS
    {"demo", "Abre a demonstracao VGA", command_demo, 0},
#endif
    {"man", "Mostra a sintaxe e a descricao", command_man, "<comando>"},
};

static void command_help(unsigned int *row, const unsigned char *argument)
{
    (void)argument;
    char line[WIDTH + 1];
    unsigned int length = 0;
    for (unsigned int i = 0; i < sizeof(commands) / sizeof(commands[0]); ++i) {
        const struct command *command = &commands[i];
        if (command->run == command_man)
            continue;
        const int second = length != 0;
        if (second)
            while (length < WIDTH / 2)
                line[length++] = ' ';
        for (const char *name = command->name; *name && length < WIDTH; ++name)
            line[length++] = *name;
        if (second) {
            line[length] = 0;
            shell_print_line(row, line);
            length = 0;
        }
    }
    if (length) {
        line[length] = 0;
        shell_print_line(row, line);
    }
    shell_print_line(row, "");
    shell_print_line(row, "man <comando>  Mostra como usar um comando");
}

static void command_man(unsigned int *row, const unsigned char *argument)
{
    const unsigned char *name = shell_skip_spaces(argument);
    if (!*name) {
        shell_print_line(row, "Uso: man <comando>");
        return;
    }
    for (unsigned int i = 0; i < sizeof(commands) / sizeof(commands[0]); ++i) {
        const struct command *command = &commands[i];
        if (!shell_equals(name, command->name))
            continue;
        char line[80];
        unsigned int length = 0;
        for (const char *p = command->name; *p; ++p)
            line[length++] = *p;
        if (command->usage) {
            line[length++] = ' ';
            for (const char *p = command->usage; *p && length < sizeof(line) - 1; ++p)
                line[length++] = *p;
        }
        line[length] = 0;
        shell_print_line(row, line);
        shell_print_line(row, command->help);
        return;
    }
    shell_print_line(row, "Comando desconhecido. Use help.");
}

enum { HISTORY_LIMIT = 16 };

static struct {
    unsigned char entries[HISTORY_LIMIT][MAX_INPUT + 1];
    unsigned int next, count;
} history;

static void remember_command(const unsigned char *line, unsigned int length)
{
    const unsigned int previous = (history.next + HISTORY_LIMIT - 1) % HISTORY_LIMIT;
    const char *last = (const char *)history.entries[previous];
    if (history.count && shell_equals(line, last))
        return;
    unsigned char *entry = history.entries[history.next];
    for (unsigned int i = 0; i <= length; ++i)
        entry[i] = line[i];
    history.next = (history.next + 1) % HISTORY_LIMIT;
    if (history.count < HISTORY_LIMIT)
        ++history.count;
}

static unsigned int recall_command(unsigned int row, unsigned char *line,
    unsigned int old_length, const unsigned char *source)
{
    unsigned int length = 0;
    for (; source[length]; ++length) {
        line[length] = source[length];
        console_put(row * WIDTH + PROMPT_WIDTH + length, console_color | line[length]);
    }
    for (unsigned int i = length; i < old_length; ++i)
        console_put(row * WIDTH + PROMPT_WIDTH + i, console_color | ' ');
    return length;
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
    remember_command(line, length);

    for (unsigned int i = 0; i < sizeof(commands) / sizeof(commands[0]); ++i) {
        const unsigned char *argument = argument_for(line, commands[i].name);
        if (argument && (commands[i].usage || !*argument)) {
            commands[i].run(row, argument);
            return;
        }
    }
    shell_print_line(row, "Comando desconhecido");
}

unsigned int shell_wait_key(void)
{
    return keyboard_wait(&keyboard);
}

void shell_discard_input(void)
{
    while (keyboard_poll(&keyboard)) {}
    keyboard.accent = 0;
}

_Noreturn void shell_run(unsigned int row)
{
    keyboard = (struct keyboard_state){0};
    history.next = history.count = 0;
    unsigned char line[MAX_INPUT + 1], draft[MAX_INPUT + 1];
    shell_memory_init();
#ifdef GPT_R_GRAPHICS
    desktop_init();
#endif

    for (;;) {
        unsigned int length = 0, distance = 0;
        console_put(row * WIDTH, console_color | '>');
        for (;;) {
            const unsigned int col = PROMPT_WIDTH + length;
            shell_move_cursor(row, col < WIDTH ? col : WIDTH - 1);
#ifdef GPT_R_GRAPHICS
            const unsigned int key = desktop_wait_key(&keyboard, &row);
            vga_graphics_cursor(WIDTH * HEIGHT);
#else
            const unsigned int key = shell_wait_key();
#endif
            if (key == '\n')
                break;
            if (key == KEY_UP || key == KEY_DOWN) {
                if (key == KEY_UP && distance < history.count) {
                    if (!distance) {
                        for (unsigned int i = 0; i < length; ++i)
                            draft[i] = line[i];
                        draft[length] = 0;
                    }
                    ++distance;
                } else if (key == KEY_DOWN && distance) {
                    --distance;
                } else {
                    continue;
                }
                const unsigned int at = (history.next + HISTORY_LIMIT - distance) % HISTORY_LIMIT;
                length = recall_command(row, line, length, distance ? history.entries[at] : draft);
            } else if (key == '\b') {
                if (length)
                    console_put(row * WIDTH + PROMPT_WIDTH + --length, console_color | ' ');
            } else if (key >= ' ' && key <= 255 && length < MAX_INPUT) {
                line[length++] = key;
                console_put(row * WIDTH + col, console_color | key);
            }
        }
        shell_next_row(&row);
        execute(&row, line, length);
    }
}
