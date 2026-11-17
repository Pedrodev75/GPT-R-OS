#ifndef GPT_R_SHELL_INTERNAL_H
#define GPT_R_SHELL_INTERNAL_H

#include "../drivers/console.h"
#include "../fs/fat16_path.h"
#include "../memory/pages.h"

enum {
    WIDTH = CONSOLE_WIDTH, HEIGHT = CONSOLE_HEIGHT,
    FIRST_ROW = 2, MAX_INPUT = 2 * FAT16_PATH_CAPACITY + 6
};

enum { SHELL_BUFFER_PAGES = 16 };

struct shell_buffer {
    unsigned char *pages[SHELL_BUFFER_PAGES];
    unsigned int size, offset;
    const char *error;
};

struct shell_stream {
    struct shell_buffer *input, *output;
    int failed;
};

void shell_set_stream(struct shell_stream *stream);
int shell_has_input(void);
int shell_has_output(void);
int shell_in_pipeline(void);
int shell_capture(const unsigned char *data, unsigned int size);
unsigned int shell_read_input(unsigned char *data, unsigned int size);
unsigned int shell_buffer_read(struct shell_buffer *buffer, unsigned char *data, unsigned int size);
void shell_buffer_clear(struct shell_buffer *buffer);
void shell_print_error(unsigned int *row, const char *text);
void shell_mark_error(void);

unsigned int shell_wait_key(void);
unsigned int shell_poll_job_key(unsigned int *row, int reading);
void shell_move_cursor(unsigned int row, unsigned int col);
void shell_clear_row(unsigned int row);
void shell_next_row(unsigned int *row);
void shell_print_line(unsigned int *row, const char *text);
void shell_print_number(unsigned int *row, const char *label,
                        unsigned long long value, const char *unit);
void shell_print_bytes(unsigned int *row, const volatile unsigned char *bytes);
unsigned char shell_codepoint_glyph(unsigned int codepoint);
unsigned int shell_encode_glyph(unsigned char *bytes, unsigned char glyph);
void shell_hex64(char *text, unsigned long long value);
int shell_equals(const unsigned char *input, const char *command);
int shell_parse_address(const unsigned char *text, unsigned long long *address);
int shell_parse_decimal(const unsigned char *argument, unsigned int *number);
const unsigned char *shell_skip_spaces(const unsigned char *text);

void shell_memory_init(void);
void shell_mem(unsigned int *row, const unsigned char *argument);
void shell_memtest(unsigned int *row, const unsigned char *argument);
void shell_peek(unsigned int *row, const unsigned char *argument);
void shell_kmalloc(unsigned int *row, const unsigned char *argument);
void shell_kfree(unsigned int *row, const unsigned char *argument);

void shell_diskinfo(unsigned int *row, const unsigned char *argument);
void shell_sector(unsigned int *row, const unsigned char *argument);
const struct fat16_path *shell_directory(void);
int shell_word(const unsigned char **argument, unsigned char *word, unsigned int capacity);
void shell_cd(unsigned int *row, const unsigned char *argument);
void shell_pwd(unsigned int *row, const unsigned char *argument);
void shell_mkdir(unsigned int *row, const unsigned char *argument);
void shell_cp(unsigned int *row, const unsigned char *argument);
void shell_mv(unsigned int *row, const unsigned char *argument);
void shell_ls(unsigned int *row, const unsigned char *argument);
void shell_run_app(unsigned int *row, const unsigned char *argument);
void shell_run_native(unsigned int *row, const unsigned char *argument);
void shell_sysinstall(unsigned int *row, const unsigned char *argument);
enum { SYS64_APP_PATH = 21 };
const char *shell_sys64_app(const struct fat16_volume *volume, const unsigned char *name,
    struct fat16_file *file, unsigned int *directory, unsigned char path[SYS64_APP_PATH]);
struct shell_app_command {
    const unsigned char *argument;
    int search_sys64;
};
int shell_run_escape(int quoted, unsigned char next);
const char *shell_check_run_args(const unsigned char *argument);
void shell_run_apps(unsigned int *row, const struct shell_app_command *commands,
    unsigned int count, struct shell_stream *stream);
int shell_open_volume(unsigned int *row, struct ata_disk *disk, struct fat16_volume *volume);
int shell_lookup_file(const struct fat16_volume *volume,
    const unsigned char *name, struct fat16_file *file);
int shell_find_file(unsigned int *row, const struct fat16_volume *volume,
    const unsigned char *name, struct fat16_file *file);
const char *shell_file_error(enum fat16_edit_result result);
void shell_report_file_edit(unsigned int *row, enum fat16_edit_result result,
    const char *success);
void shell_edit(unsigned int *row, const unsigned char *argument);
void shell_open_package(unsigned int *row, const unsigned char *argument);
const char *shell_edit_file(const struct fat16_volume *volume, unsigned int directory,
    const unsigned char *name, const struct fat16_file *file);
void shell_editor_moved(unsigned int from, const unsigned char *old_name,
    unsigned int to, const unsigned char *new_name);
int shell_editor_has_file(unsigned int directory, const unsigned char *name);
void shell_editor_draw(unsigned int x, unsigned int y, int full);
int shell_editor_key(unsigned int key);
void shell_assemble(unsigned int *row, const unsigned char *argument);
void shell_appinfo(unsigned int *row, const unsigned char *argument);
void shell_cat(unsigned int *row, const unsigned char *argument);
void shell_write(unsigned int *row, const unsigned char *argument);
void shell_rm(unsigned int *row, const unsigned char *argument);
void shell_crash(unsigned int *row, const unsigned char *argument);

#endif
