#include "shell_internal.h"

void shell_clear_row(unsigned int row)
{
    for (unsigned int col = 0; col < WIDTH; ++col)
        console_put(row * WIDTH + col, console_color | ' ');
}

void shell_next_row(unsigned int *row)
{
    if (*row < HEIGHT - 1) {
        ++*row;
        return;
    }
    for (unsigned int i = FIRST_ROW * WIDTH; i < (HEIGHT - 1) * WIDTH; ++i)
        console_put(i, console_get(i + WIDTH));
    shell_clear_row(*row);
}

void shell_print_line(unsigned int *row, const char *text)
{
    unsigned int col = 0;
    while (*text) {
        if (col == WIDTH) {
            shell_next_row(row);
            col = 0;
        }
        console_put(*row * WIDTH + col++, console_color | (unsigned char)*text++);
    }
    shell_next_row(row);
}

int shell_equals(const unsigned char *input, const char *command)
{
    while (*input && *input == (unsigned char)*command) {
        ++input;
        ++command;
    }
    return *input == 0 && *command == 0;
}

void shell_print_number(unsigned int *row, const char *label,
                         unsigned long long value, const char *unit)
{
    char text[80];
    char digits[20];
    unsigned int length = 0;
    char *output = text;
    while (*label)
        *output++ = *label++;
    do {
        digits[length++] = '0' + value % 10;
        value /= 10;
    } while (value);
    while (length)
        *output++ = digits[--length];
    while (*unit)
        *output++ = *unit++;
    *output = 0;
    shell_print_line(row, text);
}

static char hex_digit(unsigned int digit)
{
    return "0123456789ABCDEF"[digit & 15];
}

void shell_hex64(char *text, unsigned long long value)
{
    for (int i = 15; i >= 0; --i) {
        text[i] = hex_digit(value);
        value >>= 4;
    }
}

int shell_parse_address(const unsigned char *text, unsigned long long *address)
{
    while (*text == ' ')
        ++text;
    if (text[0] == '0' && (text[1] | 32) == 'x')
        text += 2;
    if (!*text)
        return 0;

    unsigned long long value = 0;
    for (unsigned int count = 0; *text; ++count, ++text) {
        unsigned int digit = *text;
        if (digit >= '0' && digit <= '9')
            digit -= '0';
        else if ((digit | 32) >= 'a' && (digit | 32) <= 'f')
            digit = (digit | 32) - 'a' + 10;
        else
            return 0;
        if (count == 16)
            return 0;
        value = (value << 4) | digit;
    }
    *address = value;
    return 1;
}

void shell_print_bytes(unsigned int *row, const volatile unsigned char *bytes)
{
    char line[16 * 3];
    for (unsigned int i = 0; i < 16; ++i) {
        const unsigned int byte = bytes[i];
        line[i * 3] = hex_digit(byte >> 4);
        line[i * 3 + 1] = hex_digit(byte);
        line[i * 3 + 2] = ' ';
    }
    line[sizeof(line) - 1] = 0;
    shell_print_line(row, line);
}

const unsigned char *shell_skip_spaces(const unsigned char *text)
{
    while (*text == ' ')
        ++text;
    return text;
}

/* Returns -1 on overflow, 0 on invalid input, and 1 on success. */
int shell_parse_decimal(const unsigned char *argument, unsigned int *number)
{
    unsigned int value = 0;
    const unsigned char *text = shell_skip_spaces(argument);
    const unsigned char *start = text;
    while (*text >= '0' && *text <= '9') {
        const unsigned int digit = *text++ - '0';
        if (value > (0xffffffffu - digit) / 10)
            return -1;
        value = value * 10 + digit;
    }
    if (text == start || *text)
        return 0;
    *number = value;
    return 1;
}
