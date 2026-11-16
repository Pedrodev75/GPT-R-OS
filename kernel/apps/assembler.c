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

enum { WIDE = 8 };

static int read_register(struct parser *parser)
{
    static const char *const names[] = {
        "eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi",
        "rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi"
    };
    const struct token name = word(parser);
    for (unsigned int i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
        if (equals(name, names[i]))
            return i;
    return -1;
}

static int prefix(struct assembler *a, int reg)
{
    return !(reg & WIDE) || emit(a, 0x48, 1);
}

static int reg_pair(struct assembler *a, int field, int operand)
{
    return emit(a, 0xc0 | ((field & 7) << 3) | (operand & 7), 1);
}

struct memory {
    unsigned int base, offset;
};

/* The opening bracket has already been consumed. */
static int read_memory(struct parser *parser, struct memory *memory)
{
    const int base = read_register(parser);
    if (base < WIDE)
        return 0;
    memory->base = base & 7;
    memory->offset = 0;
    if (take(parser, '+')) {
        if (!number(parser, &memory->offset) || memory->offset > 0x7fffffffu)
            return 0;
    } else if (take(parser, '-')) {
        if (!number(parser, &memory->offset) || memory->offset > 0x80000000u)
            return 0;
        memory->offset = 0u - memory->offset;
    }
    return take(parser, ']');
}

static int emit_memory(struct assembler *a, int field, const struct memory *memory)
{
    const unsigned int base = memory->base;
    const unsigned int offset = memory->offset;
    unsigned int mode = 0;
    if (offset || base == 5)
        mode = offset <= 127 || offset >= 0xffffff80u ? 1 : 2;
    return emit(a, (mode << 6) | ((field & 7) << 3) | base, 1) &&
        (base != 4 || emit(a, 0x24, 1)) &&
        (!mode || emit(a, offset, mode == 1 ? 1 : 4));
}

static int assemble_mov(struct assembler *a, struct parser *parser)
{
    struct memory memory;
    if (take(parser, '[')) {
        if (!read_memory(parser, &memory) || !take(parser, ','))
            return 0;
        const int reg = read_register(parser);
        return reg >= 0 && prefix(a, reg) && emit(a, 0x89, 1) &&
            emit_memory(a, reg, &memory);
    }

    const int dest = read_register(parser);
    if (dest < 0 || !take(parser, ','))
        return 0;
    if (take(parser, '['))
        return read_memory(parser, &memory) && prefix(a, dest) &&
            emit(a, 0x8b, 1) && emit_memory(a, dest, &memory);

    const unsigned char *start = parser->at;
    const int src = read_register(parser);
    if (src >= 0)
        return (dest & WIDE) == (src & WIDE) && prefix(a, dest) &&
            emit(a, 0x89, 1) && reg_pair(a, src, dest);

    parser->at = start;
    unsigned int value;
    if (!immediate(a, parser, &value))
        return 0;
    if (dest & WIDE)
        return prefix(a, dest) && emit(a, 0xc7, 1) &&
            reg_pair(a, 0, dest) && emit(a, value, 4);
    return emit(a, 0xb8 + dest, 1) && emit(a, value, 4);
}

struct binary_opcode {
    const char *name;
    unsigned int opcode, immediate_opcode, group;
};

static int assemble_binary(struct assembler *a, struct parser *parser,
    const struct binary_opcode *op)
{
    const int dest = read_register(parser);
    if (dest < 0 || !take(parser, ',') || !prefix(a, dest))
        return 0;
    const unsigned char *start = parser->at;
    const int src = read_register(parser);
    if (src >= 0) {
        if ((dest & WIDE) != (src & WIDE))
            return 0;
        if (op->opcode == 0xaf0f)
            return emit(a, op->opcode, 2) && reg_pair(a, dest, src);
        return emit(a, op->opcode, 1) && reg_pair(a, src, dest);
    }
    parser->at = start;
    unsigned int value;
    return op->immediate_opcode && immediate(a, parser, &value) &&
        emit(a, op->immediate_opcode, 1) && reg_pair(a, op->group, dest) &&
        emit(a, value, 4);
}

struct named_opcode {
    const char *name;
    unsigned int opcode;
};

struct unary_opcode {
    const char *name;
    unsigned int opcode;
    int group;
};

static int assemble_jump(struct assembler *a, struct parser *parser, unsigned int opcode)
{
    unsigned int target;
    return label_value(a, parser, &target) &&
        emit(a, opcode, opcode > 255 ? 2 : 1) &&
        emit(a, target - (a->offset + 4), 4);
}

static const char *instruction(struct assembler *a, struct parser *parser,
    struct token op)
{
    static const struct binary_opcode binary[] = {
        {"add", 0x01, 0x81, 0}, {"or", 0x09, 0x81, 1},
        {"and", 0x21, 0x81, 4}, {"sub", 0x29, 0x81, 5},
        {"xor", 0x31, 0x81, 6}, {"cmp", 0x39, 0x81, 7},
        {"test", 0x85, 0xf7, 0}, {"imul", 0xaf0f, 0, 0}
    };
    static const struct named_opcode jumps[] = {
        {"jmp", 0xe9}, {"call", 0xe8},
        {"je", 0x840f}, {"jz", 0x840f}, {"jne", 0x850f}, {"jnz", 0x850f},
        {"jl", 0x8c0f}, {"jge", 0x8d0f}, {"jle", 0x8e0f}, {"jg", 0x8f0f},
        {"jb", 0x820f}, {"jae", 0x830f}, {"jbe", 0x860f}, {"ja", 0x870f}
    };
    static const struct named_opcode no_operand[] = {
        {"nop", 0x90}, {"ud2", 0x0b0f}, {"int3", 0xcc}, {"hlt", 0xf4}, {"ret", 0xc3}
    };
    static const struct unary_opcode unary[] = {
        {"inc", 0xff, 0}, {"dec", 0xff, 1}, {"div", 0xf7, 6}, {"idiv", 0xf7, 7},
        {"push", 0x50, -1}, {"pop", 0x58, -1}
    };
    const char *bad_operand = "operando, rotulo ou tamanho invalido";
    for (unsigned int i = 0; i < sizeof(binary) / sizeof(binary[0]); ++i)
        if (equals(op, binary[i].name))
            return assemble_binary(a, parser, &binary[i]) ? 0 : bad_operand;
    for (unsigned int i = 0; i < sizeof(jumps) / sizeof(jumps[0]); ++i)
        if (equals(op, jumps[i].name))
            return assemble_jump(a, parser, jumps[i].opcode) ? 0 : bad_operand;
    for (unsigned int i = 0; i < sizeof(no_operand) / sizeof(no_operand[0]); ++i)
        if (equals(op, no_operand[i].name))
            return emit(a, no_operand[i].opcode, no_operand[i].opcode > 255 ? 2 : 1) ?
                0 : bad_operand;
    for (unsigned int i = 0; i < sizeof(unary) / sizeof(unary[0]); ++i) {
        if (!equals(op, unary[i].name))
            continue;
        const int reg = read_register(parser);
        const struct unary_opcode *operation = &unary[i];
        if (reg < 0)
            return bad_operand;
        if (operation->group < 0)
            return reg >= WIDE && emit(a, operation->opcode + (reg & 7), 1) ?
                0 : bad_operand;
        return prefix(a, reg) && emit(a, operation->opcode, 1) &&
            reg_pair(a, operation->group, reg) ? 0 : bad_operand;
    }

    unsigned int value;
    int ok;
    if (equals(op, "mov")) {
        ok = assemble_mov(a, parser);
    } else if (equals(op, "movzx")) {
        const int dest = read_register(parser);
        struct memory memory;
        ok = dest >= 0 && take(parser, ',') && equals(word(parser), "byte");
        if (ok) {
            const unsigned char *start = parser->at;
            if (!equals(word(parser), "ptr"))
                parser->at = start;
            ok = take(parser, '[') && read_memory(parser, &memory) && prefix(a, dest) &&
                emit(a, 0xb60f, 2) && emit_memory(a, dest, &memory);
        }
    } else if (equals(op, "lea")) {
        const int dest = read_register(parser);
        ok = dest >= WIDE && take(parser, ',') && take(parser, '[') &&
            equals(word(parser), "rip") && take(parser, '+') &&
            label_value(a, parser, &value) && take(parser, ']') &&
            emit(a, 0x8d48, 2) && emit(a, ((dest & 7) << 3) | 5, 1) &&
            emit(a, value - (a->offset + 4), 4);
    } else if (equals(op, "int")) {
        ok = number(parser, &value) && value == 0x80 && emit(a, 0x80cd, 2);
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
    return ok ? 0 : bad_operand;
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
