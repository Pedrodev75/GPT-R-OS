#ifndef GPT_R_SHELL_INTERNAL_H
#define GPT_R_SHELL_INTERNAL_H

#include "../drivers/console.h"

enum {
    WIDTH = CONSOLE_WIDTH, HEIGHT = CONSOLE_HEIGHT,
    FIRST_ROW = 2, PROMPT_WIDTH = 2, MAX_INPUT = WIDTH - PROMPT_WIDTH
};

void shell_clear_row(unsigned int row);
void shell_next_row(unsigned int *row);
void shell_print_line(unsigned int *row, const char *text);
void shell_print_number(unsigned int *row, const char *label,
                        unsigned long long value, const char *unit);
void shell_print_bytes(unsigned int *row, const volatile unsigned char *bytes);
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
void shell_ls(unsigned int *row, const unsigned char *argument);
void shell_cat(unsigned int *row, const unsigned char *argument);
void shell_write(unsigned int *row, const unsigned char *argument);
void shell_rm(unsigned int *row, const unsigned char *argument);
void shell_crash(unsigned int *row, const unsigned char *argument);

#endif
