#include "assembler.h"
#include "../memory/pages.h"

enum section { TEXT, DATA, BSS, SECTIONS };

struct token {
    const struct asm_io *io;
    unsigned int start, length;
};

struct parser {
    const struct asm_io *io;
    unsigned int at, end;
};

struct label {
    unsigned int start, length, offset;
    enum section section;
};

enum {
    LABEL_SLOTS = 2 * ASM_V11_LABEL_LIMIT, LABELS_PER_PAGE = PAGE_SIZE / sizeof(struct label),
    LABEL_PAGES = (LABEL_SLOTS + LABELS_PER_PAGE - 1) / LABELS_PER_PAGE
};

struct assembler {
    const struct asm_io *io;
    struct label *labels[LABEL_PAGES];
    unsigned int label_limit, label_count, image_limit;
    unsigned int offsets[SECTIONS], code_size, data_size, total, entry;
    enum section section;
    int extended, emit, entry_seen;
    const char *error;
};

static unsigned char byte(const struct parser *parser)
{
    return parser->io->read(parser->io->context, parser->at);
}

static void skip_space(struct parser *parser)
{
    while (parser->at < parser->end &&
        (byte(parser) == ' ' || byte(parser) == '\t' || byte(parser) == '\r'))
        ++parser->at;
}

static int take(struct parser *parser, unsigned char expected)
{
    skip_space(parser);
    if (parser->at == parser->end || byte(parser) != expected)
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
    const unsigned int start = parser->at;
    if (start < parser->end && letter(byte(parser))) {
        ++parser->at;
        while (parser->at < parser->end &&
            (letter(byte(parser)) || (byte(parser) >= '0' && byte(parser) <= '9')))
            ++parser->at;
    }
    return (struct token){ parser->io, start, parser->at - start };
}

static int equals(struct token word, const char *text)
{
    unsigned int i = 0;
    while (i < word.length && word.io->read(word.io->context, word.start + i) == (unsigned char)text[i])
        ++i;
    return i == word.length && !text[i];
}

static int number(struct parser *parser, unsigned int *value)
{
    skip_space(parser);
    unsigned int radix = 10;
    if (parser->end - parser->at >= 2 && byte(parser) == '0' &&
        (parser->io->read(parser->io->context, parser->at + 1) == 'x' || parser->io->read(parser->io->context, parser->at + 1) == 'X')) {
        radix = 16;
        parser->at += 2;
    }
    const unsigned int start = parser->at;
    *value = 0;
    while (parser->at < parser->end) {
        const unsigned char ch = byte(parser);
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

static unsigned char token_byte(struct token token, unsigned int index)
{
    return token.io->read(token.io->context, token.start + index);
}

static struct label *label_at(const struct assembler *a, unsigned int slot)
{
    struct label *page = a->labels[slot / LABELS_PER_PAGE];
    return page ? &page[slot % LABELS_PER_PAGE] : 0;
}

/* The table is at most half full. Probe only within the current format's capacity. */
static unsigned int label_slot(const struct assembler *a, struct token token)
{
    unsigned int hash = 2166136261u;
    for (unsigned int i = 0; i < token.length; ++i)
        hash = (hash ^ token_byte(token, i)) * 16777619u;
    const unsigned int mask = 2 * a->label_limit - 1;
    for (unsigned int slot = hash & mask;; slot = (slot + 1) & mask) {
        const struct label *label = label_at(a, slot);
        if (!label || !label->length)
            return slot;
        if (label->length == token.length) {
            unsigned int i = 0;
            while (i < token.length && token_byte(token, i) == a->io->read(a->io->context, label->start + i))
                ++i;
            if (i == token.length)
                return slot;
        }
    }
}

static const char *define_label(struct assembler *a, struct token token)
{
    const unsigned int slot = label_slot(a, token), page = slot / LABELS_PER_PAGE;
    struct label *label = label_at(a, slot);
    if (label && label->length)
        return "rotulo duplicado";
    if (a->label_count == a->label_limit)
        return "rotulos demais";
    if (!label) {
        a->labels[page] = page_alloc();
        if (!a->labels[page])
            return "sem memoria para rotulos";
        for (unsigned int i = 0; i < LABELS_PER_PAGE; ++i)
            a->labels[page][i] = (struct label){0};
        label = label_at(a, slot);
    }
    *label = (struct label){ token.start, token.length, a->offsets[a->section], a->section };
    ++a->label_count;
    return 0;
}

/* Return an offset relative to the virtual base of .text. */
static int label_value(const struct assembler *a, struct parser *parser,
    unsigned int *value, int require_text)
{
    const struct token name = word(parser);
    if (!name.length)
        return 0;
    *value = 0;
    if (!a->emit)
        return 1;
    const struct label *label = label_at(a, label_slot(a, name));
    if (!label || !label->length ||
        (require_text && (label->section != TEXT || label->offset >= a->code_size)))
        return 0;
    *value = label->offset;
    if (label->section != TEXT)
        *value += (unsigned int)(APP_V11_DATA_BASE - APP_CODE_BASE);
    if (label->section == BSS)
        *value += a->data_size;
    return 1;
}

static int immediate(const struct assembler *a, struct parser *parser,
    unsigned int *value)
{
    skip_space(parser);
    if (parser->at < parser->end && letter(byte(parser))) {
        unsigned int base = 0;
        if (!label_value(a, parser, value, 0))
            return 0;
        if (take(parser, '-') && !label_value(a, parser, &base, 0))
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

static int reserve(struct assembler *a, unsigned int bytes)
{
    if (bytes > a->image_limit - a->total) {
        a->error = "imagem excede o limite";
        return 0;
    }
    a->total += bytes;
    a->offsets[a->section] += bytes;
    return 1;
}

static int emit(struct assembler *a, unsigned int value, unsigned int bytes)
{
    const unsigned int offset = a->offsets[a->section] + (a->section == DATA ? a->code_size : 0);
    if (!reserve(a, bytes))
        return 0;
    if (a->emit) {
        for (unsigned int i = 0; i < bytes; ++i) {
            if (!a->io->write(a->io->context, offset + i, value >> (i * 8))) {
                a->error = "sem memoria para a imagem";
                return 0;
            }
        }
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
        while (parser->at < parser->end && byte(parser) != '"') {
            unsigned char ch = byte(parser);
            ++parser->at;
            if (ch == '\\') {
                if (parser->at == parser->end)
                    return 0;
                ch = byte(parser);
                ++parser->at;
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

    const unsigned int start = parser->at;
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
    const unsigned int start = parser->at;
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
    return label_value(a, parser, &target, a->extended) &&
        emit(a, opcode, opcode > 255 ? 2 : 1) &&
        emit(a, target - (a->offsets[TEXT] + 4), 4);
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
    if (equals(op, "db"))
        return a->section != BSS && data_items(a, parser) ? 0 : bad_operand;
    if (a->extended && equals(op, "resb")) {
        unsigned int bytes;
        if (a->section != BSS)
            return "resb so e permitido em .bss";
        return number(parser, &bytes) && reserve(a, bytes) ? 0 : bad_operand;
    }
    if (a->section != TEXT)
        return "instrucao fora de .text";
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
            const unsigned int start = parser->at;
            if (!equals(word(parser), "ptr"))
                parser->at = start;
            ok = take(parser, '[') && read_memory(parser, &memory) && prefix(a, dest) &&
                emit(a, 0xb60f, 2) && emit_memory(a, dest, &memory);
        }
    } else if (equals(op, "lea")) {
        const int dest = read_register(parser);
        ok = dest >= WIDE && take(parser, ',') && take(parser, '[') &&
            equals(word(parser), "rip") && take(parser, '+') &&
            label_value(a, parser, &value, 0) && take(parser, ']') &&
            emit(a, 0x8d48, 2) && emit(a, ((dest & 7) << 3) | 5, 1) &&
            emit(a, value - (a->offsets[TEXT] + 4), 4);
    } else if (equals(op, "int")) {
        ok = number(parser, &value) && value == 0x80 && emit(a, 0x80cd, 2);
    } else if (equals(op, "entry")) {
        if (a->entry_seen)
            return "entry duplicado";
        a->entry_seen = 1;
        ok = label_value(a, parser, &a->entry, a->extended);
    } else {
        return "instrucao desconhecida";
    }
    return ok ? 0 : bad_operand;
}

static int at_end(struct parser *parser)
{
    skip_space(parser);
    return parser->at == parser->end || byte(parser) == ';';
}

static const char *select_section(struct assembler *a, struct parser *parser)
{
    if (!a->extended || !equals(word(parser), "section") || !take(parser, '.'))
        return "diretiva de secao invalida";
    const struct token name = word(parser);
    if (equals(name, "text"))
        a->section = TEXT;
    else if (equals(name, "data"))
        a->section = DATA;
    else if (equals(name, "bss"))
        a->section = BSS;
    else
        return "secao desconhecida";
    return at_end(parser) ? 0 : "texto inesperado no fim da linha";
}

static const char *assemble_line(struct assembler *a, struct parser *parser)
{
    if (at_end(parser))
        return 0;
    if (take(parser, '.'))
        return select_section(a, parser);
    struct token op = word(parser);
    if (op.length && take(parser, ':')) {
        if (!a->emit) {
            const char *error = define_label(a, op);
            if (error)
                return error;
        }
        if (at_end(parser))
            return 0;
        op = word(parser);
    }
    const char *error = instruction(a, parser, op);
    if (error)
        return a->error ? a->error : error;
    return at_end(parser) ? 0 : "texto inesperado no fim da linha";
}

static const char *assemble_passes(struct assembler *a, unsigned int source_size,
    struct app_info *info, unsigned int *error_line)
{
    for (a->emit = 0; a->emit <= 1; ++a->emit) {
        a->section = TEXT;
        a->offsets[TEXT] = a->offsets[DATA] = a->offsets[BSS] = 0;
        a->entry = a->total = 0;
        a->entry_seen = 0;
        *error_line = 1;
        unsigned int line = 0;
        while (line < source_size) {
            unsigned int next = line;
            while (next < source_size && a->io->read(a->io->context, next) != '\n')
                ++next;
            struct parser parser = { a->io, line, next };
            const char *error = assemble_line(a, &parser);
            if (error)
                return error;
            line = next < source_size ? next + 1 : source_size;
            ++*error_line;
        }
        a->code_size = a->offsets[TEXT];
        a->data_size = a->offsets[DATA];
    }
    if (a->entry >= a->code_size)
        return "programa vazio ou entry fora do codigo";
    info->code_size = a->code_size;
    info->data_size = a->data_size;
    info->bss_size = a->offsets[BSS];
    info->entry = a->entry;
    return 0;
}

const char *app_assemble(const struct asm_io *io, unsigned int source_size,
    unsigned int version, struct app_info *info, unsigned int *error_line)
{
    *error_line = 1;
    if (version != APP_FORMAT_VERSION && version != APP_V11_VERSION)
        return "versao GTRA incompativel";
    const int extended = version == APP_V11_VERSION;
    if (source_size > (unsigned int)(extended ? ASM_V11_SOURCE_LIMIT : ASM_V1_SOURCE_LIMIT))
        return "fonte excede o limite";
    struct assembler a = {
        .io = io, .extended = extended,
        .label_limit = extended ? ASM_V11_LABEL_LIMIT : ASM_V1_LABEL_LIMIT,
        .image_limit = extended ? APP_V11_IMAGE_LIMIT : APP_IMAGE_LIMIT
    };
    *info = (struct app_info){
        .version = version, .header_size = extended ? APP_V11_HEADER_SIZE : APP_HEADER_SIZE
    };
    const char *error = assemble_passes(&a, source_size, info, error_line);
    for (unsigned int i = 0; i < LABEL_PAGES; ++i)
        if (a.labels[i])
            page_free(a.labels[i]);
    return error;
}
