#include "compiler.h"
#include "../../kernel/apps/app_abi.h"
#include "../../kernel/fs/fat16_path.h"
#include <stdint.h>

enum kind {
    END = 256, NAME, INTEGER, STRING, EQ, NE, LE, GE, AND, OR, PLUS_EQ, MINUS_EQ,
    APP, FUNC, LET, MUT, CONST, IF, ELSE, WHILE, RETURN, TRUE, FALSE, I32, BOOL, ARROW
};
enum type { VOID, INT, BOOLEAN, STR, PRINTABLE };
enum error {
    UNKNOWN = 1, TYPE_ERROR, IMMUTABLE, LEXICAL, SYNTAX, LIMIT, DUPLICATE,
    SEMANTIC, GENERATE
};

struct token {
    unsigned int at, size, line, column;
    int kind;
};

struct lexer {
    unsigned int at, line, column, count;
};

struct symbol {
    struct token name;
    uint32_t value;
    enum type type;
    int slot, mutable, constant;
};

struct function {
    struct token name;
    struct symbol parameters[KUST_PARAMETER_LIMIT];
    unsigned int arity;
    enum type result;
    struct lexer body;
    struct token opening;
};

enum helper { H_PRINT_INT = 1, H_PRINT_BOOL = 2, H_DIV_ZERO = 4, H_READ_BYTE = 8, H_MEMORY = 16 };

struct compiler {
    const unsigned char *source;
    unsigned int size, used, capacity, labels, helpers;
    char *output;
    struct kust_error *error;
    struct lexer lexer;
    struct token token, title;
    unsigned int depth, local_count, global_count, function_count;
    struct symbol locals[KUST_LOCAL_LIMIT + KUST_PARAMETER_LIMIT];
    struct symbol globals[KUST_GLOBAL_LIMIT];
    struct function functions[KUST_FUNCTION_LIMIT];
    struct function *function;
    unsigned int next_slot;
    int evaluating, skip_value;
};

static struct compiler workspace;

struct value {
    enum type type;
    uint32_t number;
    struct token literal;
};

static void diagnose(struct compiler *c, int code, struct token at, const char *message)
{
    if (!c->error->code)
        *c->error = (struct kust_error){code, at.line, at.column, message, 0, 0};
}

static const char *type_name(enum type type)
{
    static const char *const names[] = {"sem retorno", "i32", "bool", "literal string", "i32, bool ou literal string"};
    return names[type];
}

static void expect_type(struct compiler *c, struct token at, enum type actual, enum type expected)
{
    if (c->error->code || actual == expected ||
        (expected == PRINTABLE && (actual == INT || actual == BOOLEAN || actual == STR)))
        return;
    diagnose(c, TYPE_ERROR, at, "tipos incompativeis");
    c->error->expected = type_name(expected);
    c->error->received = type_name(actual);
}

static int same(const struct compiler *c, struct token a, struct token b)
{
    if (a.size != b.size)
        return 0;
    for (unsigned int i = 0; i < a.size; ++i)
        if (c->source[a.at + i] != c->source[b.at + i])
            return 0;
    return 1;
}

static int word_is(const struct compiler *c, struct token a, const char *word)
{
    for (unsigned int i = 0; i < a.size; ++i)
        if (!word[i] || c->source[a.at + i] != (unsigned char)word[i])
            return 0;
    return !word[a.size];
}

static int digit(unsigned int ch) { return ch >= '0' && ch <= '9'; }
static int letter(unsigned int ch)
{
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_';
}

static unsigned int peek(const struct compiler *c, unsigned int ahead)
{
    const unsigned int at = c->lexer.at + ahead;
    return at < c->size ? c->source[at] : 0;
}

static unsigned int consume(struct compiler *c)
{
    const unsigned int ch = c->source[c->lexer.at++];
    if (ch == '\n') {
        ++c->lexer.line;
        c->lexer.column = 1;
    } else {
        ++c->lexer.column;
    }
    return ch;
}

static struct token location(const struct compiler *c, int kind)
{
    return (struct token){c->lexer.at, 0, c->lexer.line, c->lexer.column, kind};
}

static int consume_utf8(struct compiler *c, unsigned int lead)
{
    unsigned int more;
    if (lead >= 0xc2 && lead <= 0xdf)
        more = 1;
    else if (lead >= 0xe0 && lead <= 0xef)
        more = 2;
    else if (lead >= 0xf0 && lead <= 0xf4)
        more = 3;
    else
        return 0;
    const unsigned int next = peek(c, 0);
    if ((lead == 0xe0 && next < 0xa0) || (lead == 0xed && next >= 0xa0) ||
        (lead == 0xf0 && next < 0x90) || (lead == 0xf4 && next >= 0x90))
        return 0;
    while (more--) {
        const unsigned int ch = peek(c, 0);
        if (ch < 0x80 || ch > 0xbf)
            return 0;
        consume(c);
    }
    return 1;
}

static int escape(unsigned int ch)
{
    switch (ch) {
    case 'n': return '\n';
    case 'r': return '\r';
    case 't': return '\t';
    case '"': case '\\': return ch;
    default: return -1;
    }
}

static void scan_string(struct compiler *c, struct token at)
{
    while (c->lexer.at < c->size) {
        const unsigned int ch = consume(c);
        if (ch == '"')
            return;
        if (ch == '\\') {
            if (escape(peek(c, 0)) < 0) {
                diagnose(c, LEXICAL, at, "escape invalido");
                return;
            }
            consume(c);
        } else if (ch < 32 || ch == 127 || (ch >= 128 && !consume_utf8(c, ch))) {
            diagnose(c, LEXICAL, at, "caractere invalido em string");
            return;
        }
        if (c->lexer.at - at.at - 1 > KUST_STRING_LIMIT) {
            diagnose(c, LIMIT, at, "literal string muito longo");
            return;
        }
    }
    diagnose(c, LEXICAL, at, "string sem aspas de fechamento");
}

static void skip_trivia(struct compiler *c)
{
    while (c->lexer.at < c->size) {
        const unsigned int ch = peek(c, 0), next = peek(c, 1);
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
            consume(c);
        } else if (ch == '/' && next == '/') {
            do {
                consume(c);
            } while (c->lexer.at < c->size && peek(c, 0) != '\n');
        } else if (ch == '/' && next == '*') {
            const struct token opening = location(c, END);
            consume(c);
            consume(c);
            while (c->lexer.at < c->size && (peek(c, 0) != '*' || peek(c, 1) != '/'))
                consume(c);
            if (c->lexer.at == c->size) {
                diagnose(c, LEXICAL, opening, "comentario sem */");
                return;
            }
            consume(c);
            consume(c);
        } else {
            return;
        }
    }
}

static void next_token(struct compiler *c)
{
    static const struct { const char *word; int kind; } keywords[] = {
        {"app", APP}, {"func", FUNC}, {"let", LET}, {"mut", MUT}, {"const", CONST},
        {"if", IF}, {"else", ELSE}, {"while", WHILE}, {"return", RETURN},
        {"true", TRUE}, {"false", FALSE}, {"i32", I32}, {"bool", BOOL}
    };
    skip_trivia(c);
    struct token t = location(c, END);
    if (!c->error->code && c->lexer.at < c->size) {
        const unsigned int ch = consume(c), next = peek(c, 0);
        t.kind = ch;
        if (++c->lexer.count > KUST_TOKEN_LIMIT)
            diagnose(c, LIMIT, t, "limite de tokens excedido");
        if (letter(ch)) {
            while (letter(peek(c, 0)) || digit(peek(c, 0)))
                consume(c);
            t.kind = NAME;
        } else if (digit(ch)) {
            while (digit(peek(c, 0)))
                consume(c);
            t.kind = INTEGER;
        } else if (ch == '"') {
            scan_string(c, t);
            t.kind = STRING;
        } else if (next == '=' && (ch == '=' || ch == '!' || ch == '<' ||
            ch == '>' || ch == '+' || ch == '-')) {
            consume(c);
            t.kind = ch == '=' ? EQ : ch == '!' ? NE : ch == '<' ? LE :
                ch == '>' ? GE : ch == '+' ? PLUS_EQ : MINUS_EQ;
        } else if ((ch == '&' || ch == '|') && ch == next) {
            consume(c);
            t.kind = ch == '&' ? AND : OR;
        } else if (ch == '-' && next == '>') {
            consume(c);
            t.kind = ARROW;
        } else {
            switch (ch) {
            case '(': case ')': case '{': case '}': case ':': case ';': case ',':
            case '.': case '=': case '!': case '<': case '>': case '+': case '-':
            case '*': case '/': case '%':
                break;
            default: diagnose(c, LEXICAL, t, "caractere inesperado");
            }
        }
        t.size = c->lexer.at - t.at;
        if (t.kind == NAME) {
            if (t.size > KUST_NAME_LIMIT)
                diagnose(c, LIMIT, t, "identificador muito longo");
            for (unsigned int i = 0; i < sizeof(keywords) / sizeof(keywords[0]); ++i)
                if (word_is(c, t, keywords[i].word)) {
                    t.kind = keywords[i].kind;
                    break;
                }
        }
    }
    if (c->error->code)
        t.kind = END;
    c->token = t;
}

static int take(struct compiler *c, int kind)
{
    if (c->token.kind != kind)
        return 0;
    next_token(c);
    return 1;
}

static struct token require(struct compiler *c, int kind, const char *message)
{
    const struct token at = c->token;
    if (!take(c, kind))
        diagnose(c, SYNTAX, at, message);
    return at;
}

static enum type parse_type(struct compiler *c)
{
    if (take(c, I32))
        return INT;
    if (take(c, BOOL))
        return BOOLEAN;
    diagnose(c, SYNTAX, c->token, "esperado tipo i32 ou bool");
    return VOID;
}

static int enter(struct compiler *c)
{
    if (c->depth >= KUST_DEPTH_LIMIT) {
        diagnose(c, LIMIT, c->token, "expressoes ou blocos aninhados demais");
        return 0;
    }
    ++c->depth;
    return 1;
}

static void emit(struct compiler *c, const char *text)
{
    if (c->evaluating || c->error->code)
        return;
    while (*text && c->used < c->capacity)
        c->output[c->used++] = *text++;
    if (*text)
        diagnose(c, GENERATE, c->token, "Assembly gerado excede 64 KiB");
}

static void number(struct compiler *c, uint32_t n)
{
    char digits[11];
    unsigned int at = sizeof(digits) - 1;
    digits[at] = 0;
    do {
        digits[--at] = '0' + n % 10;
        n /= 10;
    } while (n);
    emit(c, digits + at);
}

static unsigned int label(struct compiler *c)
{
    if (c->evaluating || c->error->code)
        return 0;
    /* Leave space for fixed entry and runtime helper labels. */
    if (c->labels + 3 * c->function_count + 48 >= KUST_LABEL_LIMIT) {
        diagnose(c, GENERATE, c->token, "rotulos demais para o assembler");
        return 0;
    }
    return ++c->labels;
}

static void jump(struct compiler *c, const char *instruction, unsigned int n)
{
    emit(c, instruction);
    emit(c, " L");
    number(c, n);
    emit(c, "\n");
}

static void define(struct compiler *c, unsigned int n)
{
    emit(c, "L");
    number(c, n);
    emit(c, ":\n");
}

static void immediate(struct compiler *c, const char *prefix, uint32_t value)
{
    emit(c, prefix);
    number(c, value);
    emit(c, "\n");
}

static void slot_address(struct compiler *c, int slot)
{
    emit(c, slot < 0 ? "[rbp - " : "[rbp + ");
    number(c, slot < 0 ? -slot : slot);
    emit(c, "]");
}

static struct symbol *lookup(struct compiler *c, struct token name)
{
    for (unsigned int i = c->local_count; i > 0; --i)
        if (same(c, name, c->locals[i - 1].name))
            return &c->locals[i - 1];
    for (unsigned int i = 0; i < c->global_count; ++i)
        if (same(c, name, c->globals[i].name))
            return &c->globals[i];
    return 0;
}

static int find_function(const struct compiler *c, struct token name)
{
    for (unsigned int i = 0; i < c->function_count; ++i)
        if (same(c, name, c->functions[i].name))
            return (int)i;
    return -1;
}

static void load(struct compiler *c, const struct symbol *symbol)
{
    if (symbol->constant) {
        immediate(c, "mov eax, ", symbol->value);
    } else {
        emit(c, "mov eax, ");
        slot_address(c, symbol->slot);
        emit(c, "\n");
    }
}

static void store(struct compiler *c, const struct symbol *symbol)
{
    emit(c, "mov ");
    slot_address(c, symbol->slot);
    emit(c, ", eax\n");
}

static uint32_t integer(struct compiler *c, struct token t, uint32_t max)
{
    uint32_t n = 0;
    for (unsigned int i = 0; i < t.size; ++i) {
        unsigned int digit_value = c->source[t.at + i] - '0';
        if (n > (max - digit_value) / 10) {
            diagnose(c, SEMANTIC, t, "literal fora do intervalo i32");
            return 0;
        }
        n = n * 10 + digit_value;
    }
    return n;
}

/* Literals go in .data as decimal bytes, even if they contain Assembly syntax. */
static unsigned int literal_bytes(struct compiler *c, struct token t)
{
    unsigned int count = 0;
    for (unsigned int i = 1; i + 1 < t.size; ++i) {
        unsigned int byte = c->source[t.at + i];
        if (byte == '\\')
            byte = escape(c->source[t.at + ++i]);
        immediate(c, "db ", byte);
        ++count;
    }
    return count;
}

static unsigned int emit_literal(struct compiler *c, struct token t, int tail)
{
    const unsigned int id = label(c);
    emit(c, ".section .data\n");
    define(c, id);
    const unsigned int count = literal_bytes(c, t);
    if (tail >= 0)
        immediate(c, "db ", tail);
    emit(c, ".section .text\nlea rax, [rip + L");
    number(c, id);
    emit(c, "]\n");
    return count;
}

static struct value expression(struct compiler *c, int min);

enum native_id {
    PRINT, PRINTLN, EXIT_APP, OPEN_READ, OPEN_WRITE, READ_BYTE, WRITE_LITERAL, CLOSE_FILE,
    MEM_ALLOC, MEM_READ, MEM_WRITE, MEM_FREE, GFX_BEGIN, GFX_RECT, GFX_PRESENT, GFX_WAIT
};

static const struct native {
    const char *group, *name;
    enum native_id id;
    enum type result;
    unsigned int arity;
    enum type args[5];
} natives[] = {
    {"term", "print", PRINT, VOID, 1, {PRINTABLE}},
    {"term", "println", PRINTLN, VOID, 1, {PRINTABLE}},
    {"app", "exit", EXIT_APP, VOID, 1, {INT}},
    {"file", "openRead", OPEN_READ, INT, 1, {STR}},
    {"file", "openWrite", OPEN_WRITE, INT, 1, {STR}},
    {"file", "readByte", READ_BYTE, INT, 1, {INT}},
    {"file", "write", WRITE_LITERAL, INT, 2, {INT, STR}},
    {"file", "close", CLOSE_FILE, INT, 1, {INT}},
    {"mem", "alloc", MEM_ALLOC, INT, 1, {INT}},
    {"mem", "read", MEM_READ, INT, 1, {INT}},
    {"mem", "write", MEM_WRITE, INT, 2, {INT, INT}},
    {"mem", "free", MEM_FREE, INT, 0, {VOID}},
    {"gfx", "begin", GFX_BEGIN, INT, 1, {INT}},
    {"gfx", "rect", GFX_RECT, INT, 5, {INT, INT, INT, INT, INT}},
    {"gfx", "present", GFX_PRESENT, INT, 0, {VOID}},
    {"gfx", "wait", GFX_WAIT, INT, 0, {VOID}},
};

/* The declaration token "app" can also name the application API before a dot. */
static const struct native *find_native(struct compiler *c, struct token group, struct token name)
{
    for (unsigned int i = 0; i < sizeof(natives) / sizeof(natives[0]); ++i)
        if (word_is(c, group, natives[i].group) && word_is(c, name, natives[i].name))
            return &natives[i];
    return 0;
}

static void syscall(struct compiler *c, unsigned int id)
{
    immediate(c, "mov eax, ", id);
    emit(c, "int 0x80\n");
}

static void print_value(struct compiler *c, enum type type, unsigned int length, int newline)
{
    if (type == STR) {
        emit(c, "mov rdi, rax\n");
        immediate(c, "mov esi, ", length + newline);
        syscall(c, 1);
        return;
    }
    if (type == INT) {
        c->helpers |= H_PRINT_INT;
        emit(c, "call k_print_int\n");
    } else {
        c->helpers |= H_PRINT_BOOL;
        emit(c, "call k_print_bool\n");
    }
    if (newline) {
        const unsigned int id = label(c);
        emit(c, ".section .data\n");
        define(c, id);
        emit(c, "db 10\n.section .text\nlea rdi, [rip + L");
        number(c, id);
        emit(c, "]\nmov esi, 1\n");
        syscall(c, 1);
    }
}

static void native_code(struct compiler *c, const struct native *native,
    enum type first, unsigned int length)
{
    switch (native->id) {
    case PRINT: case PRINTLN:
        print_value(c, first, length, native->id == PRINTLN);
        break;
    case EXIT_APP:
        emit(c, "mov edi, eax\n");
        syscall(c, 2);
        break;
    case OPEN_READ: case OPEN_WRITE:
        emit(c, "mov rdi, rax\n");
        immediate(c, "mov esi, ", native->id == OPEN_WRITE);
        syscall(c, APP_SYS_OPEN);
        break;
    case READ_BYTE:
        c->helpers |= H_READ_BYTE;
        emit(c, "call k_read_byte\n");
        break;
    case WRITE_LITERAL:
        emit(c, "mov rsi, rax\npop rdi\n");
        immediate(c, "mov edx, ", length);
        syscall(c, APP_SYS_FILE_WRITE);
        break;
    case CLOSE_FILE:
        emit(c, "mov edi, eax\n");
        syscall(c, APP_SYS_CLOSE);
        break;
    case MEM_ALLOC: case MEM_READ: case MEM_WRITE: case MEM_FREE:
        c->helpers |= H_MEMORY;
        if (native->id == MEM_WRITE)
            emit(c, "pop rcx\n");
        emit(c, native->id == MEM_ALLOC ? "call k_mem_alloc\n" :
            native->id == MEM_READ ? "call k_mem_read\n" :
            native->id == MEM_WRITE ? "call k_mem_write\n" : "call k_mem_free\n");
        break;
    case GFX_BEGIN:
        emit(c, "mov edi, eax\n");
        syscall(c, APP_GUI_BEGIN);
        break;
    case GFX_RECT:
        /* The user assembler has no r8 operand. This is mov r8, rax. */
        emit(c, "db 73,137,192\npop rcx\npop rdx\npop rsi\npop rdi\n");
        syscall(c, APP_GUI_RECT);
        break;
    case GFX_PRESENT:
        syscall(c, APP_GUI_PRESENT);
        break;
    case GFX_WAIT:
        syscall(c, APP_GUI_WAIT);
        break;
    }
}

static struct value call(struct compiler *c, struct token name, int *exits)
{
    const struct native *native = 0;
    const struct function *function = 0;
    int index = -1;
    *exits = 0;
    if (take(c, '.')) {
        const struct token member = require(c, NAME, "esperado nome da API");
        native = find_native(c, name, member);
    } else {
        index = find_function(c, name);
        if (index >= 0 && name.kind == NAME)
            function = &c->functions[index];
    }
    if (!native && !function)
        diagnose(c, UNKNOWN, name, "funcao ou API desconhecida");
    if (c->evaluating)
        diagnose(c, SEMANTIC, name, "chamada em expressao constante");
    if (c->error->code)
        return (struct value){0};
    const unsigned int arity = native ? native->arity : function->arity;
    require(c, '(', "esperado '(' na chamada");
    unsigned int length = 0, count = 0;
    enum type first = VOID;
    if (c->token.kind != ')') {
        do {
            const struct token argument = c->token;
            if (count >= arity) {
                diagnose(c, SEMANTIC, argument, "argumentos demais");
                break;
            }
            const struct value arg = expression(c, 1);
            const enum type expected = native ? native->args[count] : function->parameters[count].type;
            expect_type(c, argument, arg.type, expected);
            if (!count)
                first = arg.type;
            if (arg.type == STR && !c->error->code) {
                const int path = native->id == OPEN_READ || native->id == OPEN_WRITE;
                const int tail = path ? 0 : native->id == PRINTLN ? 10 : -1;
                length = emit_literal(c, arg.literal, tail);
                if (path && (!length || length >= FAT16_PATH_CAPACITY))
                    diagnose(c, SEMANTIC, argument, "caminho vazio ou longo demais");
            }
            ++count;
            if (!native || count < arity)
                emit(c, "push rax\n");
        } while (!c->error->code && take(c, ','));
    }
    require(c, ')', "esperado ')' na chamada");
    if (count != arity)
        diagnose(c, SEMANTIC, name, "quantidade incorreta de argumentos");
    if (c->error->code)
        return (struct value){0};
    if (native) {
        native_code(c, native, first, length);
        if (native->result == INT)
            emit(c, "mov eax, eax\n");
        *exits = native->id == EXIT_APP;
        return (struct value){.type = native->result};
    }
    immediate(c, "call F", index);
    if (count)
        immediate(c, "add rsp, ", count * 8);
    return (struct value){.type = function->result};
}

static struct value primary(struct compiler *c)
{
    const struct token t = c->token;
    if (take(c, '(')) {
        const struct value v = expression(c, 1);
        require(c, ')', "esperado ')'");
        return v;
    }
    if (take(c, '-') || take(c, '!')) {
        const enum type type = t.kind == '-' ? INT : BOOLEAN;
        if (type == INT && c->token.kind == INTEGER) {
            const struct token literal = c->token;
            next_token(c);
            const uint32_t n = 0u - integer(c, literal, 0x80000000u);
            immediate(c, "mov eax, ", n);
            return (struct value){.type = INT, .number = n};
        }
        if (!enter(c))
            return (struct value){0};
        const struct value operand = primary(c);
        --c->depth;
        expect_type(c, t, operand.type, type);
        if (type == INT)
            emit(c, "xor ecx, ecx\nsub ecx, eax\nmov eax, ecx\n");
        else
            emit(c, "xor eax, 1\n");
        return (struct value){.type = type, .number = type == INT ?
            0u - operand.number : !operand.number};
    }
    next_token(c);
    switch (t.kind) {
    case INTEGER: {
        const uint32_t n = integer(c, t, 0x7fffffffu);
        immediate(c, "mov eax, ", n);
        return (struct value){.type = INT, .number = n};
    }
    case TRUE: case FALSE:
        immediate(c, "mov eax, ", t.kind == TRUE);
        return (struct value){.type = BOOLEAN, .number = t.kind == TRUE};
    case STRING:
        return (struct value){.type = STR, .literal = t};
    case NAME: case APP:
        if (c->token.kind == '(' || c->token.kind == '.') {
            int exits;
            const struct value result = call(c, t, &exits);
            if (result.type == VOID)
                diagnose(c, TYPE_ERROR, t, "funcao sem retorno usada como valor");
            return result;
        }
        break;
    default:
        diagnose(c, SYNTAX, t, "expressao esperada");
        return (struct value){0};
    }
    const struct symbol *s = lookup(c, t);
    if (!s) {
        diagnose(c, UNKNOWN, t, "identificador desconhecido");
        return (struct value){0};
    }
    if (c->evaluating && !s->constant)
        diagnose(c, SEMANTIC, t, "variavel em expressao constante");
    load(c, s);
    return (struct value){.type = s->type, .number = s->value};
}

static int precedence(int kind)
{
    switch (kind) {
    case OR: return 1;
    case AND: return 2;
    case EQ: case NE: return 3;
    case '<': case '>': case LE: case GE: return 4;
    case '+': case '-': return 5;
    case '*': case '/': case '%': return 6;
    default: return 0;
    }
}

static void division(struct compiler *c, int remainder)
{
    const unsigned int normal = label(c), sign = label(c), done = label(c);
    c->helpers |= H_DIV_ZERO;
    emit(c, "mov edx, eax\nmov eax, ecx\nmov ecx, edx\ntest ecx, ecx\njz k_div_zero\n"
        "cmp eax, 2147483648\n");
    jump(c, "jne", normal);
    emit(c, "cmp ecx, 4294967295\n");
    jump(c, "jne", normal);
    if (remainder)
        emit(c, "xor eax, eax\n");
    jump(c, "jmp", done);
    define(c, normal);
    emit(c, "xor edx, edx\ntest eax, eax\n");
    jump(c, "jge", sign);
    emit(c, "mov edx, 4294967295\n");
    define(c, sign);
    emit(c, "idiv ecx\n");
    if (remainder)
        emit(c, "mov eax, edx\n");
    define(c, done);
}

static void comparison(struct compiler *c, const char *condition)
{
    const unsigned int yes = label(c), done = label(c);
    emit(c, "cmp ecx, eax\n");
    jump(c, condition, yes);
    emit(c, "xor eax, eax\n");
    jump(c, "jmp", done);
    define(c, yes);
    emit(c, "mov eax, 1\n");
    define(c, done);
}

static uint32_t constant_binary(struct compiler *c, struct token op, uint32_t a, uint32_t b)
{
    /* Sign extension in int64_t avoids signed 32-bit division overflow. */
    const int64_t left = (int64_t)(a ^ 0x80000000u) - 0x80000000ll;
    const int64_t right = (int64_t)(b ^ 0x80000000u) - 0x80000000ll;
    switch (op.kind) {
    case OR: return a || b;
    case AND: return a && b;
    case EQ: return a == b;
    case NE: return a != b;
    case '<': return left < right;
    case '>': return left > right;
    case LE: return left <= right;
    case GE: return left >= right;
    case '+': return a + b;
    case '-': return a - b;
    case '*': return a * b;
    case '/': case '%':
        if (!right) {
            if (!c->skip_value)
                diagnose(c, SEMANTIC, op, "divisao por zero em constante");
            return 0;
        }
        return (uint32_t)(op.kind == '/' ? left / right : left % right);
    }
    return 0;
}

static enum type binary_type(struct compiler *c, struct token op, enum type left, enum type right)
{
    const int rank = precedence(op.kind);
    if (rank == 3) {
        if (left != INT && left != BOOLEAN)
            expect_type(c, op, left, INT);
        expect_type(c, op, right, left);
    } else {
        const enum type expected = rank <= 2 ? BOOLEAN : INT;
        expect_type(c, op, left, expected);
        expect_type(c, op, right, expected);
    }
    return rank <= 4 ? BOOLEAN : INT;
}

static void binary_code(struct compiler *c, int kind)
{
    emit(c, "pop rcx\n");
    switch (kind) {
    case '+': emit(c, "add eax, ecx\n"); break;
    case '-': emit(c, "sub ecx, eax\nmov eax, ecx\n"); break;
    case '*': emit(c, "imul eax, ecx\n"); break;
    case '/': case '%': division(c, kind == '%'); break;
    case EQ: comparison(c, "je"); break;
    case NE: comparison(c, "jne"); break;
    case '<': comparison(c, "jl"); break;
    case '>': comparison(c, "jg"); break;
    case LE: comparison(c, "jle"); break;
    case GE: comparison(c, "jge"); break;
    }
}

static struct value expression(struct compiler *c, int min)
{
    if (!enter(c))
        return (struct value){0};
    struct value left = primary(c);
    while (!c->error->code && precedence(c->token.kind) >= min) {
        const struct token op = c->token;
        const int logic = op.kind == OR || op.kind == AND;
        unsigned int done = 0;
        if (logic) {
            done = label(c);
            emit(c, "test eax, eax\n");
            jump(c, op.kind == OR ? "jnz" : "jz", done);
        } else {
            emit(c, "push rax\n");
        }
        next_token(c);
        const int previous = c->skip_value;
        if (c->evaluating && logic && (op.kind == OR ? left.number != 0 : left.number == 0))
            c->skip_value = 1;
        const struct value right = expression(c, precedence(op.kind) + 1);
        c->skip_value = previous;
        left.type = binary_type(c, op, left.type, right.type);
        if (c->evaluating && !c->error->code)
            left.number = constant_binary(c, op, left.number, right.number);
        if (logic)
            define(c, done);
        else if (!c->evaluating)
            binary_code(c, op.kind);
    }
    --c->depth;
    return left;
}

static struct symbol declaration(struct compiler *c, int kind)
{
    struct symbol symbol = {.name = require(c, NAME, "esperado identificador"),
        .mutable = kind == MUT, .constant = kind == CONST};
    require(c, ':', "esperado ':' antes do tipo");
    symbol.type = parse_type(c);
    require(c, '=', "esperado '=' na declaracao");
    const struct token at = c->token;
    c->evaluating = symbol.constant;
    const struct value init = expression(c, 1);
    c->evaluating = 0;
    expect_type(c, at, init.type, symbol.type);
    symbol.value = init.number;
    return symbol;
}

static int block(struct compiler *c, unsigned int scope);

static int control(struct compiler *c, int kind)
{
    const unsigned int start = kind == WHILE ? label(c) : 0;
    const unsigned int other = label(c);
    if (kind == WHILE)
        define(c, start);
    const struct token at = c->token;
    const struct value cond = expression(c, 1);
    expect_type(c, at, cond.type, BOOLEAN);
    emit(c, "test eax, eax\n");
    jump(c, "jz", other);
    const int first = block(c, c->local_count);
    if (kind == WHILE)
        jump(c, "jmp", start);
    if (kind == IF && take(c, ELSE)) {
        const unsigned int done = label(c);
        jump(c, "jmp", done);
        define(c, other);
        const int second = block(c, c->local_count);
        define(c, done);
        return first && second;
    }
    define(c, other);
    return 0;
}

static int statement(struct compiler *c, unsigned int scope)
{
    const struct token at = c->token;
    next_token(c);
    switch (at.kind) {
    case ';':
        return 0;
    case LET: case MUT: case CONST: {
        struct symbol symbol = declaration(c, at.kind);
        for (unsigned int i = scope; i < c->local_count; ++i)
            if (same(c, symbol.name, c->locals[i].name))
                diagnose(c, DUPLICATE, symbol.name, "identificador duplicado no escopo");
        if (c->local_count == sizeof(c->locals) / sizeof(c->locals[0]) ||
            (!symbol.constant && c->next_slot == KUST_LOCAL_LIMIT)) {
            diagnose(c, LIMIT, symbol.name, "variaveis locais demais");
            return 0;
        }
        if (!symbol.constant) {
            symbol.slot = -(int)(++c->next_slot * 8);
            store(c, &symbol);
        }
        c->locals[c->local_count++] = symbol;
        return 0;
    }
    case IF: case WHILE:
        return control(c, at.kind);
    case RETURN:
        if (c->function->result != VOID || (c->token.line == at.line &&
            c->token.kind != '}' && c->token.kind != ';' && c->token.kind != END)) {
            const struct token value_at = c->token;
            const struct value result = expression(c, 1);
            expect_type(c, value_at, result.type, c->function->result);
        }
        emit(c, "mov rsp, rbp\npop rbp\nret\n");
        return 1;
    case NAME: case APP: {
        if (c->token.kind == '(' || c->token.kind == '.') {
            int exits;
            call(c, at, &exits);
            return exits;
        }
        struct symbol *symbol = lookup(c, at);
        if (!symbol)
            diagnose(c, UNKNOWN, at, "identificador desconhecido");
        else if (!symbol->mutable)
            diagnose(c, IMMUTABLE, at, "tentativa de modificar variavel imutavel");
        if (c->error->code)
            return 0;
        const int op = c->token.kind;
        if (op != '=' && op != PLUS_EQ && op != MINUS_EQ) {
            diagnose(c, SYNTAX, c->token, "esperada atribuicao ou chamada");
            return 0;
        }
        next_token(c);
        if (op != '=') {
            expect_type(c, at, symbol->type, INT);
            load(c, symbol);
            emit(c, "push rax\n");
        }
        const struct token right = c->token;
        const struct value value = expression(c, 1);
        expect_type(c, right, value.type, symbol->type);
        if (op != '=')
            binary_code(c, op == PLUS_EQ ? '+' : '-');
        store(c, symbol);
        return 0;
    }
    default:
        diagnose(c, SYNTAX, at, "instrucao inesperada");
        return 0;
    }
}

static int block(struct compiler *c, unsigned int scope)
{
    require(c, '{', "esperado '{'");
    if (!enter(c))
        return 0;
    const unsigned int saved = c->local_count;
    int returns = 0;
    while (!c->error->code && c->token.kind != '}' && c->token.kind != END) {
        if (statement(c, scope))
            returns = 1;
    }
    require(c, '}', "esperado '}'");
    c->local_count = saved;
    --c->depth;
    return returns;
}

static void signature(struct compiler *c)
{
    const struct token name = require(c, NAME, "esperado nome da funcao");
    if (lookup(c, name) || find_function(c, name) >= 0)
        diagnose(c, DUPLICATE, name, "nome global duplicado");
    if (c->function_count == KUST_FUNCTION_LIMIT)
        diagnose(c, LIMIT, name, "funcoes demais");
    if (c->error->code)
        return;
    struct function *function = &c->functions[c->function_count++];
    function->name = name;
    function->arity = 0;
    require(c, '(', "esperado '(' na declaracao");
    if (c->token.kind != ')') {
        do {
            const struct token param = require(c, NAME, "esperado nome do parametro");
            if (function->arity == KUST_PARAMETER_LIMIT)
                diagnose(c, LIMIT, param, "parametros demais");
            for (unsigned int i = 0; i < function->arity; ++i)
                if (same(c, param, function->parameters[i].name))
                    diagnose(c, DUPLICATE, param, "parametro duplicado");
            if (c->error->code)
                return;
            require(c, ':', "esperado ':' antes do tipo");
            function->parameters[function->arity++] = (struct symbol){.name = param, .type = parse_type(c)};
        } while (take(c, ','));
    }
    require(c, ')', "esperado ')' na declaracao");
    function->result = take(c, ARROW) ? parse_type(c) : VOID;
    function->body = c->lexer;
    function->opening = require(c, '{', "esperado corpo da funcao");
    unsigned int braces = 1;
    while (!c->error->code && braces && c->token.kind != END) {
        if (c->token.kind == '{')
            ++braces;
        else if (c->token.kind == '}')
            --braces;
        next_token(c);
    }
    if (braces)
        diagnose(c, SYNTAX, c->token, "esperado '}'");
}

static int collect(struct compiler *c)
{
    require(c, APP, "esperado app e nome entre aspas");
    const struct token title = require(c, STRING, "esperado nome do aplicativo");
    c->title = title;
    if (title.size == 2)
        diagnose(c, SEMANTIC, title, "nome do aplicativo vazio");
    int main = -1;
    while (!c->error->code && c->token.kind != END) {
        if (take(c, ';'))
            continue;
        if (take(c, FUNC)) {
            signature(c);
            if (!c->error->code && word_is(c, c->functions[c->function_count - 1].name, "main"))
                main = (int)c->function_count - 1;
        } else if (take(c, CONST)) {
            const struct symbol symbol = declaration(c, CONST);
            if (lookup(c, symbol.name) || find_function(c, symbol.name) >= 0)
                diagnose(c, DUPLICATE, symbol.name, "nome global duplicado");
            if (c->global_count == KUST_GLOBAL_LIMIT)
                diagnose(c, LIMIT, symbol.name, "constantes globais demais");
            if (!c->error->code)
                c->globals[c->global_count++] = symbol;
        } else {
            diagnose(c, SYNTAX, c->token, "esperado func ou const global");
        }
    }
    if (main < 0)
        diagnose(c, SEMANTIC, title, "func main() ausente");
    else if (c->functions[main].arity || c->functions[main].result != VOID)
        diagnose(c, SEMANTIC, c->functions[main].name, "main nao aceita parametros nem retorno");
    return main;
}

static void function_body(struct compiler *c, unsigned int index)
{
    struct function *function = c->function = &c->functions[index];
    c->lexer = function->body;
    c->token = function->opening;
    c->local_count = function->arity;
    c->next_slot = 0;
    for (unsigned int i = 0; i < function->arity; ++i) {
        c->locals[i] = function->parameters[i];
        c->locals[i].slot = 16 + 8 * (function->arity - 1 - i);
    }
    emit(c, "F");
    number(c, index);
    /* Reserve the bounded frame with a forward trampoline. Only actual slots
       are allocated, and calls read their arguments above rbp. */
    emit(c, ":\npush rbp\nmov rbp, rsp\njmp S");
    number(c, index);
    emit(c, "\nB");
    number(c, index);
    emit(c, ":\n");
    const int returns = block(c, 0);
    if (function->result != VOID && !returns)
        diagnose(c, SEMANTIC, function->name, "funcao com retorno pode terminar sem valor");
    if (function->result == VOID)
        emit(c, "mov rsp, rbp\npop rbp\nret\n");
    emit(c, "S");
    number(c, index);
    emit(c, ":\n");
    if (c->next_slot)
        immediate(c, "sub rsp, ", c->next_slot * 8);
    immediate(c, "jmp B", index);
}

static void library(struct compiler *c)
{
    if (c->helpers & H_PRINT_INT) {
        emit(c, "k_print_int:\nsub rsp, 16\nmov rdi, rsp\nadd rdi, 16\n"
            "xor esi, esi\nmov ebx, eax\ntest eax, eax\njge k_decimal\n"
            "xor ecx, ecx\nsub ecx, eax\nmov eax, ecx\n"
            "k_decimal:\nmov ecx, 10\n"
            "k_digit:\nxor edx, edx\ndiv ecx\nadd edx, 48\ndec rdi\n"
            /* mov byte [rdi], dl */
            "db 136,23\ninc esi\ntest eax, eax\njnz k_digit\n"
            "test ebx, ebx\njge k_decimal_write\ndec rdi\nmov edx, 45\ndb 136,23\ninc esi\n"
            "k_decimal_write:\n");
        syscall(c, 1);
        emit(c, "add rsp, 16\nret\n");
    }
    if (c->helpers & H_PRINT_BOOL) {
        emit(c, ".section .data\nk_true: db 116,114,117,101\nk_false: db 102,97,108,115,101\n"
            ".section .text\nk_print_bool:\ntest eax, eax\njz k_bool_false\n"
            "lea rdi, [rip + k_true]\nmov esi, 4\njmp k_bool_write\n"
            "k_bool_false:\nlea rdi, [rip + k_false]\nmov esi, 5\nk_bool_write:\n");
        syscall(c, 1);
        emit(c, "ret\n");
    }
    if (c->helpers & H_DIV_ZERO) {
        emit(c, ".section .data\nk_div_msg: db \"Kust R001: divisao por zero\",10\n"
            ".section .text\nk_div_zero:\nlea rdi, [rip + k_div_msg]\nmov esi, 28\n");
        syscall(c, 1);
        emit(c, "mov edi, 1\n");
        syscall(c, 2);
    }
    if (c->helpers & H_READ_BYTE) {
        emit(c, "k_read_byte:\nmov edi, eax\nxor eax, eax\npush rax\nmov rsi, rsp\nmov edx, 1\n");
        syscall(c, APP_SYS_FILE_READ);
        emit(c, "pop rcx\ntest eax, eax\njl k_read_done\njz k_read_eof\n"
            "mov eax, ecx\nret\nk_read_eof:\nmov eax, 4294967285\nk_read_done:\nret\n");
    }
    if (c->helpers & H_MEMORY) {
        emit(c, ".section .bss\nk_buffer: resb 16\n.section .text\n"
            "k_mem_alloc:\nlea rbx, [rip + k_buffer]\nmov rcx, [rbx]\ntest rcx, rcx\njnz k_mem_bad\n"
            "test eax, eax\njle k_mem_bad\ncmp eax, 4096\nja k_mem_bad\nmov edi, eax\n");
        syscall(c, APP_SYS_ALLOC);
        emit(c, "test rax, rax\njl k_mem_ret\nmov [rbx], rax\nmov [rbx + 8], rdi\nxor eax, eax\nret\n"
            "k_mem_free:\nlea rbx, [rip + k_buffer]\nmov rdi, [rbx]\ntest rdi, rdi\njz k_mem_bad\n");
        syscall(c, APP_SYS_FREE);
        emit(c, "test eax, eax\njnz k_mem_ret\nmov [rbx], rax\nmov [rbx + 8], rax\nret\n"
            "k_mem_read:\nlea rbx, [rip + k_buffer]\nmov edx, [rbx + 8]\n"
            "cmp eax, edx\njae k_mem_bad\nmov rdi, [rbx]\nadd rdi, rax\n"
            "movzx eax, byte [rdi]\nret\n"
            "k_mem_write:\ncmp eax, 255\nja k_mem_bad\nlea rbx, [rip + k_buffer]\n"
            "mov edx, [rbx + 8]\ncmp ecx, edx\njae k_mem_bad\nmov rdi, [rbx]\nadd rdi, rcx\n"
            /* mov byte [rdi], al */
            "db 136,7\nxor eax, eax\nret\n"
            "k_mem_bad:\nmov eax, 4294967295\nk_mem_ret:\nret\n");
    }
}

unsigned int kust_compile(const unsigned char *source, unsigned int size, char *output,
    unsigned int capacity, struct kust_error *error)
{
    *error = (struct kust_error){0};
    struct compiler *c = &workspace;
    /* The tables are initialized as entries are published. */
    c->source = source;
    c->size = size;
    c->used = c->labels = c->helpers = c->depth = 0;
    c->local_count = c->global_count = c->function_count = 0;
    c->evaluating = c->skip_value = 0;
    c->output = output;
    c->capacity = capacity < KUST_ASM_LIMIT ? capacity : KUST_ASM_LIMIT;
    c->error = error;
    c->lexer = (struct lexer){.line = 1, .column = 1};
    if (size > KUST_SOURCE_LIMIT)
        diagnose(c, LIMIT, location(c, END), "fonte excede 32 KiB");
    if (size >= 3 && source[0] == 0xef && source[1] == 0xbb && source[2] == 0xbf)
        c->lexer.at = 3;
    next_token(c);
    const int main = collect(c);
    if (c->error->code)
        return 0;
    emit(c, ".section .data\nk_title:\n");
    literal_bytes(c, c->title);
    emit(c, "db 0\n.section .text\nentry start\nstart:\n");
    immediate(c, "call F", main);
    emit(c, "xor edi, edi\n");
    syscall(c, 2);
    for (unsigned int i = 0; i < c->function_count && !error->code; ++i)
        function_body(c, i);
    library(c);
    return error->code ? 0 : c->used;
}
