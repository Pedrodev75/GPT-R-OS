#include "app.h"
#include "assembler.h"

struct token {
    const unsigned char *text;
    unsigned int length;
};

struct parser {
    const unsigned char *at, *end;
};

struct label {
    struct token name;
    unsigned int offset;
};

struct assembler {
    struct label labels[64];
    unsigned int label_count, offset, entry;
    int emit, entry_seen;
    unsigned char *code;
};

static void skip_space(struct parser *parser)
{
    while (parser->at < parser->end &&
        (*parser->at == ' ' || *parser->at == '\t' || *parser->at == '\r'))
        ++parser->at;
}

static int take(struct parser *parser, unsigned char expected)
{
    skip_space(parser);
    if (parser->at == parser->end || *parser->at != expected)
        return 0;
    ++parser->at;
    return 1;
}

static int letter(unsigned char ch)
{
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_';
}

static struct token word(struct parser *parser)
{
    skip_space(parser);
    const unsigned char *start = parser->at;
    if (start < parser->end && letter(*start)) {
        ++parser->at;
        while (parser->at < parser->end &&
            (letter(*parser->at) || (*parser->at >= '0' && *parser->at <= '9')))
            ++parser->at;
    }
    return (struct token){ start, parser->at - start };
}

static int equals(struct token word, const char *text)
{
    unsigned int i = 0;
    while (i < word.length && word.text[i] == (unsigned char)text[i])
        ++i;
    return i == word.length && !text[i];
}

static int number(struct parser *parser, unsigned int *value)
{
    skip_space(parser);
    unsigned int radix = 10;
    if (parser->end - parser->at >= 2 && parser->at[0] == '0' &&
        (parser->at[1] == 'x' || parser->at[1] == 'X')) {
        radix = 16;
        parser->at += 2;
    }
    const unsigned char *start = parser->at;
    *value = 0;
    while (parser->at < parser->end) {
        const unsigned char ch = *parser->at;
        unsigned int digit;
        if (ch >= '0' && ch <= '9')
            digit = ch - '0';
        else if ((ch | 32) >= 'a' && (ch | 32) <= 'f')
            digit = (ch | 32) - 'a' + 10;
        else
            break;
        if (digit >= radix)
            break;
        if (*value > (0xffffffffu - digit) / radix)
            return 0;
        *value = *value * radix + digit;
        ++parser->at;
    }
    return parser->at != start;
}

static const struct label *find_label(const struct assembler *a, struct token token)
{
    for (unsigned int i = 0; i < a->label_count; ++i) {
        const struct token name = a->labels[i].name;
        if (name.length != token.length)
            continue;
        unsigned int j = 0;
        while (j < name.length && name.text[j] == token.text[j])
            ++j;
        if (j == name.length)
            return &a->labels[i];
    }
    return 0;
}

static int label_value(const struct assembler *a, struct parser *parser,
    unsigned int *value)
{
    const struct token name = word(parser);
    if (!name.length)
        return 0;
    *value = 0;
    if (!a->emit)
        return 1;
    const struct label *label = find_label(a, name);
    if (!label)
        return 0;
    *value = label->offset;
    return 1;
}

static int immediate(const struct assembler *a, struct parser *parser,
    unsigned int *value)
{
    skip_space(parser);
    if (parser->at < parser->end && letter(*parser->at)) {
        unsigned int base = 0;
        if (!label_value(a, parser, value))
            return 0;
        if (take(parser, '-') && !label_value(a, parser, &base))
            return 0;
        *value -= base;
        return 1;
    }
    const int negative = take(parser, '-');
    if (!number(parser, value))
        return 0;
    if (negative)
        *value = 0u - *value;
    return 1;
}

static int emit(struct assembler *a, unsigned int value, unsigned int bytes)
{
    if (bytes > APP_IMAGE_LIMIT - a->offset)
        return 0;
    for (unsigned int i = 0; i < bytes; ++i) {
        if (a->emit)
            a->code[a->offset] = value >> (i * 8);
        ++a->offset;
    }
    return 1;
}

static int data_items(struct assembler *a, struct parser *parser)
{
    do {
        if (!take(parser, '"')) {
            unsigned int byte;
            if (!number(parser, &byte) || byte > 255 || !emit(a, byte, 1))
                return 0;
            continue;
        }
        while (parser->at < parser->end && *parser->at != '"') {
            unsigned char ch = *parser->at++;
            if (ch == '\\') {
                if (parser->at == parser->end)
                    return 0;
                ch = *parser->at++;
                if (ch == 'n')
                    ch = '\n';
                else if (ch == 'r')
                    ch = '\r';
                else if (ch == 't')
                    ch = '\t';
                else if (ch != '"' && ch != '\\')
                    return 0;
            }
            if (!emit(a, ch, 1))
                return 0;
        }
        if (!take(parser, '"'))
            return 0;
    } while (take(parser, ','));
    return 1;
}

static unsigned int mov_opcode(struct token reg)
{
    if (equals(reg, "eax"))
        return 0xb8;
    if (equals(reg, "edx"))
        return 0xba;
    if (equals(reg, "esi"))
        return 0xbe;
    if (equals(reg, "edi"))
        return 0xbf;
    return 0;
}

static const char *instruction(struct assembler *a, struct parser *parser,
    struct token op)
{
    unsigned int value;
    int ok;
    if (equals(op, "mov")) {
        const unsigned int opcode = mov_opcode(word(parser));
        ok = opcode && take(parser, ',') && immediate(a, parser, &value) &&
            emit(a, opcode, 1) && emit(a, value, 4);
    } else if (equals(op, "lea")) {
        ok = equals(word(parser), "rdi") && take(parser, ',') && take(parser, '[') &&
            equals(word(parser), "rip") && take(parser, '+') &&
            label_value(a, parser, &value) && take(parser, ']') &&
            emit(a, 0x3d8d48, 3) && emit(a, value - (a->offset + 4), 4);
    } else if (equals(op, "jmp")) {
        ok = label_value(a, parser, &value) && emit(a, 0xe9, 1) &&
            emit(a, value - (a->offset + 4), 4);
    } else if (equals(op, "int")) {
        ok = number(parser, &value) && value == 0x80 && emit(a, 0x80cd, 2);
    } else if (equals(op, "nop")) {
        ok = emit(a, 0x90, 1);
    } else if (equals(op, "ud2")) {
        ok = emit(a, 0x0b0f, 2);
    } else if (equals(op, "db")) {
        ok = data_items(a, parser);
    } else if (equals(op, "entry")) {
        if (a->entry_seen)
            return "entry duplicado";
        a->entry_seen = 1;
        ok = label_value(a, parser, &a->entry);
    } else {
        return "instrucao desconhecida";
    }
    return ok ? 0 : "operando, rotulo ou tamanho invalido";
}

static int at_end(struct parser *parser)
{
    skip_space(parser);
    return parser->at == parser->end || *parser->at == ';';
}

static const char *assemble_line(struct assembler *a, struct parser *parser)
{
    if (at_end(parser))
        return 0;
    struct token op = word(parser);
    if (op.length && take(parser, ':')) {
        if (!a->emit) {
            if (find_label(a, op))
                return "rotulo duplicado";
            if (a->label_count == sizeof(a->labels) / sizeof(a->labels[0]))
                return "rotulos demais";
            a->labels[a->label_count++] = (struct label){ op, a->offset };
        }
        if (at_end(parser))
            return 0;
        op = word(parser);
    }
    const char *error = instruction(a, parser, op);
    if (error)
        return error;
    return at_end(parser) ? 0 : "texto inesperado no fim da linha";
}

const char *app_assemble(const unsigned char *source, unsigned int source_size,
    unsigned char *code, unsigned int *code_size, unsigned int *entry,
    unsigned int *error_line)
{
    struct assembler assembler;
    assembler.label_count = 0;
    assembler.code = code;
    const unsigned char *end = source + source_size;
    for (assembler.emit = 0; assembler.emit <= 1; ++assembler.emit) {
        assembler.offset = assembler.entry = 0;
        assembler.entry_seen = 0;
        *error_line = 1;
        const unsigned char *line = source;
        while (line < end) {
            const unsigned char *next = line;
            while (next < end && *next != '\n')
                ++next;
            struct parser parser = { line, next };
            const char *error = assemble_line(&assembler, &parser);
            if (error)
                return error;
            line = next < end ? next + 1 : end;
            ++*error_line;
        }
    }
    if (assembler.entry >= assembler.offset)
        return "programa vazio ou entry fora do codigo";
    *code_size = assembler.offset;
    *entry = assembler.entry;
    return 0;
}
