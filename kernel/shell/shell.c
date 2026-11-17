#include "shell.h"
#include "shell_internal.h"
#include "../apps/app.h"
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
    while (*name) {
        const unsigned char ch = *input >= 'A' && *input <= 'Z' ? *input + ('a' - 'A') : *input;
        if (ch != (unsigned char)*name)
            return 0;
        ++input;
        ++name;
    }
    return !*input || *input == ' ' ? input : 0;
}


static struct keyboard_state keyboard;
static unsigned int deferred_keys[MAX_INPUT + 1];
static unsigned int deferred_first, deferred_count;

static unsigned int take_deferred_key(void)
{
    if (!deferred_count)
        return 0;
    const unsigned int key = deferred_keys[deferred_first];
    deferred_first = (deferred_first + 1) % (MAX_INPUT + 1);
    --deferred_count;
    return key;
}

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
    shell_print_line(row, "GPT-R OS v0.7.0");
}

static void command_uptime(unsigned int *row, const unsigned char *argument)
{
    (void)argument;
    shell_print_number(row, "", timer_uptime_seconds(), " s");
}

static void command_echo(unsigned int *row, const unsigned char *argument)
{
    shell_print_line(row, (const char *)shell_skip_spaces(argument));
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

enum { COMMAND_OUTPUT = 1, COMMAND_INPUT = 2 };

static const struct command {
    const char *name;
    const char *help;
    void (*run)(unsigned int *, const unsigned char *);
    const char *usage;
    unsigned int streams;
} commands[] = {
    {"help", "Lista os comandos", command_help, 0, COMMAND_OUTPUT},
    {"clear", "Limpa o terminal", command_clear, 0, 0},
    {"version", "Mostra a versao", command_version, 0, COMMAND_OUTPUT},
    {"echo", "Escreve texto na saida", command_echo, "[texto]", COMMAND_OUTPUT},
    {"uptime", "Mostra o tempo ligado", command_uptime, 0, COMMAND_OUTPUT},
    {"mem", "Mostra o mapa e o uso da memoria", shell_mem, 0, COMMAND_OUTPUT},
    {"memtest", "Testa uma pagina de 4 KiB", shell_memtest, 0, 0},
    {"kmalloc", "Aloca um bloco (bytes em decimal)", shell_kmalloc, "<bytes>", 0},
    {"kfree", "Libera um bloco (endereco hexadecimal)", shell_kfree, "<endereco>", 0},
    {"peek", "Le 16 bytes de um endereco hexadecimal", shell_peek, "<endereco>", 0},
    {"reboot", "Reinicia o sistema", command_reboot, 0, 0},
    {"diskinfo", "Mostra o disco ATA", shell_diskinfo, 0, COMMAND_OUTPUT},
    {"sector", "Mostra 16 bytes de um setor", shell_sector, "<LBA>", COMMAND_OUTPUT},
    {"ls", "Lista pastas e arquivos FAT16", shell_ls, "[pasta]", COMMAND_OUTPUT},
    {"cd", "Troca a pasta atual", shell_cd, "<pasta>", 0},
    {"pwd", "Mostra o caminho da pasta atual", shell_pwd, 0, COMMAND_OUTPUT},
    {"mkdir", "Cria uma pasta", shell_mkdir, "<pasta>", 0},
    {"cp", "Copia um arquivo", shell_cp, "<arquivo> <destino>", 0},
    {"mv", "Move ou renomeia um arquivo", shell_mv, "<arquivo> <destino>", 0},
    {"cat", "Le texto com paginacao", shell_cat, "<arquivo>", COMMAND_OUTPUT | COMMAND_INPUT},
    {"run", "Executa um .APP com argumentos", shell_run_app, "<arquivo.APP> [argumentos]", COMMAND_OUTPUT | COMMAND_INPUT},
    {"sysinstall", "Instala um APP no SYS64", shell_sysinstall, "[-u] <origem.APP> [NOME.APP]", 0},
    {"asm", "Monta um fonte para .APP", shell_assemble, "[-f v1.1] <fonte.asm> <saida.APP>", 0},
    {"appinfo", "Mostra o cabecalho de um .APP", shell_appinfo, "<arquivo.APP>", COMMAND_OUTPUT},
#ifdef GPT_R_GRAPHICS
    {"edit", "Abre o editor grafico de texto", shell_edit, "<arquivo>", 0},
    {"pkg", "Abre um pacote na Central de Aplicativos", shell_open_package, "<arquivo.PKG>", 0},
#endif
    {"write", "Cria ou sobrescreve um arquivo", shell_write, "<arquivo> <texto>", 0},
    {"rm", "Remove um arquivo", shell_rm, "<arquivo>", 0},
    {"color", "Muda as cores do terminal", command_color, "<frente> <fundo>", 0},
    {"crash", "Executa testes, incluindo crash heap", shell_crash, "<tipo>", 0},
#ifdef GPT_R_GRAPHICS
    {"demo", "Abre a demonstracao VGA", command_demo, 0, 0},
#endif
    {"man", "Mostra a sintaxe e a descricao", command_man, "<comando>", COMMAND_OUTPUT},
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
    shell_print_line(row, "man pipe       Mostra como usar | e >");
}

static void command_man(unsigned int *row, const unsigned char *argument)
{
    const unsigned char *name = shell_skip_spaces(argument);
    if (!*name) {
        shell_print_error(row, "Uso: man <comando>");
        return;
    }
    if (shell_equals(name, "pipe")) {
        shell_print_line(row, "comando | cat         Passa a saida ao proximo");
        shell_print_line(row, "comando > arquivo    Grava a saida no FAT16");
        shell_print_line(row, "Ate 8 comandos e 64 KiB por etapa capturada");
        shell_print_line(row, "Use \\| e \\> para digitar os sinais como texto");
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
    shell_print_error(row, "Comando desconhecido. Use help.");
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

static unsigned int recall_command(unsigned char *line, const unsigned char *source)
{
    unsigned int length = 0;
    for (; source[length]; ++length)
        line[length] = source[length];
    return length;
}

static unsigned int show_prompt(unsigned int row)
{
    enum { PATH_COLUMNS = WIDTH - 22 };
    char path[FAT16_PATH_CAPACITY];
    fat16_format_path(shell_directory(), path);
    unsigned int length = 0, column = 0;
    while (path[length])
        ++length;
    unsigned int start = 0;
    if (length > PATH_COLUMNS) {
        for (; column < 3; ++column)
            console_put(row * WIDTH + column, console_color | '.');
        start = length - (PATH_COLUMNS - 3);
    }
    for (unsigned int i = start; i < length; ++i)
        console_put(row * WIDTH + column++, console_color | (unsigned char)path[i]);
    console_put(row * WIDTH + column++, console_color | '>');
    console_put(row * WIDTH + column++, console_color | ' ');
    return column;
}

static unsigned int show_input(unsigned int row, unsigned int prompt,
    const unsigned char *line, unsigned int length)
{
    const unsigned int visible = WIDTH - prompt;
    const unsigned int first = length > visible ? length - visible : 0;
    for (unsigned int i = 0; i < visible; ++i)
        console_put(row * WIDTH + prompt + i,
            console_color | (first + i < length ? line[first + i] : ' '));
    return prompt + length - first;
}

enum { PIPE_STAGES = APP_JOB_LIMIT };

struct shell_stage {
    const struct command *command;
    const unsigned char *argument;
};

static const struct command native_command = {
    .run = shell_run_native, .streams = COMMAND_OUTPUT | COMMAND_INPUT
};

static const struct command *lookup_command(const unsigned char *start, const unsigned char **argument)
{
    for (unsigned int i = 0; i < sizeof(commands) / sizeof(commands[0]); ++i) {
        const unsigned char *found = argument_for(start, commands[i].name);
        if (found) {
            *argument = found;
            return &commands[i];
        }
    }
    *argument = start;
    return &native_command;
}

static int is_app_command(const struct command *command)
{
    return command == &native_command || command->run == shell_run_app;
}

static int stage_uses_app_args(const unsigned char *start)
{
    const unsigned char *argument;
    return is_app_command(lookup_command(shell_skip_spaces(start), &argument));
}

static int parse_stage(unsigned int *row, unsigned char *text, struct shell_stage *stage)
{
    const unsigned char *start = shell_skip_spaces(text);
    unsigned char *end = text;
    while (*end)
        ++end;
    while (end > text && end[-1] == ' ')
        --end;
    *end = 0;
    if (!*start) {
        shell_print_error(row, "Falta um comando na linha");
        return 0;
    }
    const unsigned char *argument;
    const struct command *command = lookup_command(start, &argument);
    if (is_app_command(command)) {
        const char *error = shell_check_run_args(argument);
        if (error) {
            shell_print_error(row, error);
            return 0;
        }
    } else if (!command->usage && *argument) {
        shell_print_error(row, "Comando interno nao aceita argumentos");
        return 0;
    }
    *stage = (struct shell_stage){command, argument};
    return 1;
}

static int redirect_sector(void *context, unsigned char sector[512], unsigned int size)
{
    return shell_buffer_read(context, sector, size) == size;
}

static void run_pipeline(unsigned int *row, const struct shell_stage *stages,
    unsigned int count, const unsigned char *redirect)
{
    for (unsigned int i = 0; i < count; ++i) {
        const unsigned int streams = stages[i].command->streams;
        if (!streams || (i && !(streams & COMMAND_INPUT))) {
            shell_print_error(row, "Comando incompativel com essa etapa do pipe ou >");
            return;
        }
    }

    struct ata_disk disk;
    struct fat16_volume volume;
    struct fat16_location target;
    struct fat16_file original;
    int existed = 0;
    unsigned int name_size = 0, target_seen = 0;
    if (redirect) {
        unsigned char path[FAT16_PATH_CAPACITY];
        if (!shell_word(&redirect, path, sizeof(path)) || *redirect) {
            shell_print_error(row, "Uso: comando > arquivo");
            return;
        }
        if (!shell_open_volume(row, &disk, &volume))
            return;
        const int resolved = fat16_resolve_parent(&volume, shell_directory(), path, &target);
        if (resolved != 1) {
            shell_print_error(row, resolved < 0 ? "Falha ao ler pasta de destino" :
                "Caminho de destino invalido");
            return;
        }
        while (target.name[name_size])
            ++name_size;
        const enum fat16_edit_result result = fat16_check_write(&volume,
            target.directory, target.name, name_size);
        if (result != FAT16_EDIT_OK) {
            shell_report_file_edit(row, result, "Falha ao abrir destino");
            return;
        }
        existed = fat16_lookup_at(&volume, target.directory, target.name, &original);
        if (existed < 0) {
            shell_print_error(row, "Falha ao ler o destino");
            return;
        }
        target_seen = fat16_mutations();
    }

    struct shell_buffer buffers[2] = {0};
    struct shell_buffer *input = 0, *output = 0;
    int failed = 0;
    for (unsigned int i = 0; i < count;) {
        struct shell_app_command apps[APP_JOB_LIMIT];
        unsigned int app_count = 0, next = i + 1;
        if (is_app_command(stages[i].command)) {
            apps[app_count++] = (struct shell_app_command){ stages[i].argument, stages[i].command == &native_command };
            for (; next < count; ++next) {
                if (is_app_command(stages[next].command)) {
                    apps[app_count++] = (struct shell_app_command){ stages[next].argument, stages[next].command == &native_command };
                } else if (stages[next].command->run != shell_cat ||
                    *shell_skip_spaces(stages[next].argument)) {
                    break;
                }
            }
        }
        output = next < count || redirect ? (input == &buffers[0] ? &buffers[1] : &buffers[0]) : 0;
        struct shell_stream stream = { .input = input, .output = output };
        shell_set_stream(&stream);
        if (app_count)
            shell_run_apps(row, apps, app_count, &stream);
        else
            stages[i].command->run(row, stages[i].argument);
        shell_set_stream(0);
        i = next;
        if (input)
            shell_buffer_clear(input);
        if (output && output->error)
            shell_print_error(row, output->error);
        failed = stream.failed || (output && output->error);
        if (failed)
            break;
        input = output;
    }
    if (redirect && !failed) {
        struct fat16_file current;
        const int found = fat16_lookup_at(&volume, target.directory, target.name, &current);
        if (found < 0) {
            shell_print_error(row, "Falha ao ler o destino");
        } else if (fat16_file_changed(target.directory, target.name, target_seen) ||
            found != existed || (found &&
            (original.first_cluster != current.first_cluster || original.size != current.size ||
             original.modified_date != current.modified_date))) {
            shell_print_error(row, "O destino mudou durante o comando");
        } else {
            const enum fat16_edit_result result = fat16_write_stream_at(&volume,
                target.directory, target.name, name_size, output->size, redirect_sector, output);
            if (result != FAT16_EDIT_OK)
                shell_report_file_edit(row, result, "Falha ao gravar destino");
        }
    }
    shell_buffer_clear(&buffers[0]);
    shell_buffer_clear(&buffers[1]);
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

    struct shell_stage stages[PIPE_STAGES];
    unsigned int count = 0;
    unsigned char *part = line, *next = line, *redirect = 0;
    int app_args = stage_uses_app_args(line), quoted = 0;
    for (const unsigned char *p = line; *p; ++p) {
        if (*p == '\\' && (app_args ? shell_run_escape(quoted, p[1]) : p[1] == '|' || p[1] == '>')) {
            if (app_args)
                *next++ = *p;
            *next++ = *++p;
            continue;
        }
        if (app_args && *p == '"')
            quoted = !quoted;
        if (quoted || (*p != '|' && *p != '>')) {
            *next++ = *p;
            continue;
        }
        const unsigned char op = *p;
        if (redirect || count == PIPE_STAGES) {
            shell_print_error(row, "Pipe invalido ou com comandos demais");
            return;
        }
        *next++ = 0;
        if (!parse_stage(row, part, &stages[count++]))
            return;
        part = next;
        app_args = op == '|' && stage_uses_app_args(p + 1);
        if (op == '>')
            redirect = part;
    }
    if (quoted) {
        shell_print_error(row, "Aspas sem fechamento");
        return;
    }
    *next = 0;
    if (!redirect) {
        if (count == PIPE_STAGES) {
            shell_print_error(row, "Pipe aceita ate 8 comandos");
            return;
        }
        if (!parse_stage(row, part, &stages[count++]))
            return;
    }
    if (count == 1 && !redirect)
        stages[0].command->run(row, stages[0].argument);
    else
        run_pipeline(row, stages, count, redirect);
}

unsigned int shell_wait_key(void)
{
    const unsigned int key = take_deferred_key();
    return key ? key : keyboard_wait(&keyboard);
}

unsigned int shell_poll_job_key(unsigned int *row, int reading)
{
#ifdef GPT_R_GRAPHICS
    const unsigned int key = desktop_poll_key(&keyboard, row);
#else
    (void)row;
    if (app_ready())
        app_schedule();
    const unsigned int key = keyboard_poll(&keyboard);
#endif
    if (key == KEY_INTERRUPT) {
        deferred_count = 0;
        return key;
    }
    if (key && deferred_count < MAX_INPUT + 1) {
        const unsigned int tail = (deferred_first + deferred_count) % (MAX_INPUT + 1);
        deferred_keys[tail] = key;
        ++deferred_count;
    }
    return reading ? take_deferred_key() : 0;
}

_Noreturn void shell_run(unsigned int row)
{
    keyboard = (struct keyboard_state){0};
    deferred_first = deferred_count = 0;
    history.next = history.count = 0;
    unsigned char line[MAX_INPUT + 1], draft[MAX_INPUT + 1];
    shell_memory_init();
#ifdef GPT_R_GRAPHICS
    desktop_init();
#endif

    for (;;) {
        unsigned int length = 0, distance = 0;
        const unsigned int prompt = show_prompt(row);
        for (;;) {
            const unsigned int col = show_input(row, prompt, line, length);
            shell_move_cursor(row, col < WIDTH ? col : WIDTH - 1);
#ifdef GPT_R_GRAPHICS
            unsigned int key = take_deferred_key();
            if (!key)
                key = desktop_wait_key(&keyboard, &row);
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
                length = recall_command(line, distance ? history.entries[at] : draft);
            } else if (key == '\b') {
                if (length)
                    --length;
            } else if (key >= ' ' && key <= 255 && length < MAX_INPUT) {
                line[length++] = key;
            }
        }
        shell_next_row(&row);
        execute(&row, line, length);
    }
}
