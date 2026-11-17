#include "compiler.h"
#include "../../kernel/apps/app.h"
#include "../../kernel/apps/app_abi.h"

static char io_buffer[APP_IO_LIMIT];

static long system_call(long number, unsigned long a, unsigned long b, unsigned long *d, unsigned long c)
{
    unsigned long data = *d;
    __asm__ volatile("int $0x80" : "+a"(number), "+d"(data) : "D"(a), "S"(b), "c"(c) : "memory", "cc");
    *d = data;
    return number;
}

static long invoke(long number, unsigned long a, unsigned long b, unsigned long data)
{
    return system_call(number, a, b, &data, 0);
}

static unsigned int length(const char *s)
{
    unsigned int size = 0;
    while (s[size])
        ++size;
    return size;
}

static char lower(char c) { return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c; }

static int equal(const char *a, const char *b)
{
    while (*a && lower(*a) == lower(*b)) {
        ++a;
        ++b;
    }
    return lower(*a) == lower(*b);
}

static void write_term(const char *text, unsigned int size)
{
    invoke(1, (unsigned long)text, size, 0);
}

static void print(const char *text)
{
    write_term(text, length(text));
}

static void decimal(unsigned int number)
{
    char bytes[10];
    unsigned int i = sizeof(bytes);
    do {
        bytes[--i] = '0' + number % 10;
        number /= 10;
    } while (number);
    write_term(bytes + i, sizeof(bytes) - i);
}

static void report(const char *file, struct kust_error error)
{
    print("Kust E0");
    if (error.code < 10)
        print("0");
    decimal(error.code);
    print(": ");
    print(error.message);
    if (file) {
        print("\nArquivo: ");
        print(file);
    }
    if (error.line) {
        print("\nLinha: ");
        decimal(error.line);
        print(", coluna: ");
        decimal(error.column);
    }
    if (error.expected) {
        print("\nEsperado: ");
        print(error.expected);
        print("\nRecebido: ");
        print(error.received);
    }
    print("\n");
}

static int error_message(unsigned int code, const char *file, const char *message)
{
    report(file, (struct kust_error){.code = code, .message = message});
    return 1;
}

static const char *service_error(long status)
{
    switch (status) {
    case APP_ERR_INVALID: return "caminho, nome 8.3 ou operacao invalida";
    case APP_ERR_MISSING: return "arquivo ou diretorio nao encontrado";
    case APP_ERR_ACCESS: return "destino protegido, em uso ou com outra capitalizacao";
    case APP_ERR_IO: return "falha de leitura";
    case APP_ERR_MEMORY: return "memoria insuficiente";
    case APP_ERR_LIMIT: return "limite de tamanho, disco ou diretorio";
    case APP_ERR_CHANGED: return "arquivo modificado durante a leitura";
    case APP_ERR_SYNTAX: return "assembler rejeitou o codigo gerado";
    case APP_ERR_WRITE: return "falha de escrita no disco";
    case APP_ERR_UNCERTAIN: return "falha de disco com estado do destino incerto";
    default: return "falha no servico do sistema";
    }
}

static const char *basename(const char *path)
{
    const char *name = path;
    for (; *path; ++path)
        if (*path == '/')
            name = path + 1;
    return name;
}

static int valid_path(const char *path, const char *extension)
{
    const unsigned int size = length(path);
    if (!size || size >= FAT16_PATH_CAPACITY)
        return 0;
    const char *name = basename(path);
    const unsigned int name_size = length(name);
    if (name_size < 5 || name_size > 12 || !equal(name + name_size - 4, extension))
        return 0;
    for (unsigned int i = 0; i < size; ++i)
        if ((unsigned char)path[i] <= ' ' || path[i] == '\\' || path[i] == ':')
            return 0;
    for (unsigned int i = 0; i < name_size - 4; ++i)
        if (name[i] == '.')
            return 0;
    return 1;
}

static long load_source(const char *file, unsigned char *source)
{
    const long handle = invoke(APP_SYS_OPEN, (unsigned long)file, APP_FILE_READ_MODE, 0);
    if (handle < 0)
        return handle;
    long result = 0;
    unsigned int size = 0;
    for (;;) {
        const unsigned int left = KUST_SOURCE_LIMIT - size;
        const unsigned int count = left < APP_IO_LIMIT ? left : APP_IO_LIMIT;
        const long n = invoke(APP_SYS_FILE_READ, handle,
            (unsigned long)(count ? source + size : (unsigned char *)io_buffer), count ? count : 1);
        if (n <= 0) {
            result = n < 0 ? n : (long)size;
            break;
        }
        if (!count) {
            result = APP_ERR_LIMIT;
            break;
        }
        size += n;
    }
    const long status = invoke(APP_SYS_CLOSE, handle, 0, 0);
    return result >= 0 && status < 0 ? status : result;
}

static void help(void)
{
    print("Kust 0.1\n"
        "kust build ARQUIVO.KST [-o SAIDA.APP]\n"
        "kust check ARQUIVO.KST\n"
        "kust docs [CAPITULO [PAG]]\n"
        "kust version\n"
        "kust help\n");
}

static void docs_index(void)
{
    print("MANUAL KUST 0.1\n"
        "kust docs CAPITULO [PAG]\n"
        "START    SYNTAX   TYPES\n"
        "FLOW     FUNCS    API\n"
        "ERRORS   LIMITS   CLI\n"
        "README   (indice completo)\n"
        "kust docs --all CAPITULO\n"
        "Arquivos: /SYS64/DOCS/KUST\n");
}

static unsigned int page_number(const char *text)
{
    unsigned int value = 0;
    for (; *text; ++text) {
        if (*text < '0' || *text > '9' || value >= 100)
            return 0;
        value = value * 10 + (*text - '0');
    }
    return value;
}

static int docs_page(const char *chapter, unsigned int page, unsigned int size)
{
    /* The graphical APP console has 28 columns and 12 rows, including
       the command's exit status. Keep one blank row around it. */
    enum { WIDTH = 27, ROWS = 8 };
    unsigned int lines = 0;
    int heading = 0;
    for (unsigned int start = 0; start < size;) {
        unsigned int end = start, space = start;
        while (end < size && end - start < WIDTH && io_buffer[end] != '\n' && io_buffer[end] != '\r') {
            if (io_buffer[end] == ' ')
                space = end;
            ++end;
        }
        if (end - start == WIDTH && end < size && io_buffer[end] > ' ' && space > start)
            end = space;
        if (lines / ROWS + 1 == page) {
            if (!heading) {
                print(chapter);
                print("  pag ");
                decimal(page);
                print("\n");
                heading = 1;
            }
            write_term(io_buffer + start, end - start);
            print("\n");
        }
        ++lines;
        if (end < size && io_buffer[end] == ' ' && end > start)
            ++end;
        if (end < size && io_buffer[end] == '\r')
            ++end;
        if (end < size && io_buffer[end] == '\n')
            ++end;
        start = end;
    }
    if (!heading)
        return error_message(10, chapter, "pagina inexistente");
    if (lines > page * ROWS) {
        print("Prox: ");
        print(chapter);
        print(" ");
        decimal(page + 1);
        print("\n");
    }
    return 0;
}

static int docs(const char *chapter, unsigned int page, int all)
{
    static const char *const chapters[] = {
        "README", "START", "SYNTAX", "TYPES", "FLOW", "FUNCS", "API", "ERRORS", "LIMITS", "CLI"
    };
    char path[sizeof("/SYS64/DOCS/KUST/12345678.TXT")] = "/SYS64/DOCS/KUST/";
    unsigned int selected = 0;
    if (chapter) {
        while (selected < sizeof(chapters) / sizeof(chapters[0]) && !equal(chapter, chapters[selected]))
            ++selected;
        if (selected == sizeof(chapters) / sizeof(chapters[0]))
            return error_message(10, chapter, "capitulo desconhecido. Consulte kust docs");
    }
    unsigned int at = length(path);
    for (const char *p = chapters[selected]; *p; ++p)
        path[at++] = *p;
    for (const char *p = ".TXT"; *p; ++p)
        path[at++] = *p;
    path[at] = 0;
    const long handle = invoke(APP_SYS_OPEN, (unsigned long)path, APP_FILE_READ_MODE, 0);
    if (handle < 0)
        return error_message(11, path, service_error(handle));
    long read = invoke(APP_SYS_FILE_READ, handle, (unsigned long)io_buffer, sizeof(io_buffer));
    if (all) {
        while (read > 0) {
            write_term(io_buffer, read);
            read = invoke(APP_SYS_FILE_READ, handle, (unsigned long)io_buffer, sizeof(io_buffer));
        }
    }
    const long status = invoke(APP_SYS_CLOSE, handle, 0, 0);
    if (read < 0 || status < 0)
        return error_message(11, path, service_error(read < 0 ? read : status));
    return all ? 0 : docs_page(chapters[selected], page, read);
}

static int compile_file(const char *file, const char *target, int build)
{
    char default_name[13];
    if (!valid_path(file, ".KST"))
        return error_message(10, file, "use um caminho FAT16 com nome 8.3 e extensao .KST");
    if (build && !target) {
        const char *name = basename(file);
        const unsigned int stem = length(name) - 4;
        for (unsigned int i = 0; i < stem; ++i)
            default_name[i] = name[i];
        for (unsigned int i = 0; i <= 4; ++i)
            default_name[stem + i] = ".APP"[i];
        target = default_name;
    }
    if (build && !valid_path(target, ".APP"))
        return error_message(10, target, "use um caminho FAT16 com nome 8.3 e extensao .APP");

    const long memory = invoke(APP_SYS_ALLOC, KUST_SOURCE_LIMIT + KUST_ASM_LIMIT, 0, 0);
    if (memory < 0)
        return error_message(11, file, service_error(memory));
    unsigned char *source = (unsigned char *)memory;
    char *assembly = (char *)source + KUST_SOURCE_LIMIT;
    const long size = load_source(file, source);
    int result = 1;
    if (size < 0) {
        error_message(11, file, size == APP_ERR_LIMIT ? "fonte excede 32 KiB" : service_error(size));
    } else {
        struct kust_error error;
        const unsigned int generated = kust_compile(source, size, assembly, KUST_ASM_LIMIT, &error);
        if (!generated) {
            report(file, error);
        } else if (!build) {
            print("Kust: fonte valido\n");
            result = 0;
        } else {
            unsigned long data = APP_ASM_BUFFER | APP_V11_VERSION;
            const long status = system_call(APP_SYS_ASSEMBLE, (unsigned long)assembly,
                (unsigned long)target, &data, generated);
            if (status < 0) {
                error_message(12, target, service_error(status));
                if (data) {
                    print("Linha do Assembly gerado: ");
                    decimal(data);
                    print("\n");
                }
            } else {
                print("Kust: gerado ");
                print(target);
                print("\n");
                result = 0;
            }
        }
    }
    invoke(APP_SYS_FREE, memory, 0, 0);
    return result;
}

int kust_main(unsigned int argc, const char *const *argv)
{
    if (argc <= 1 || (argc == 2 && equal(argv[1], "help"))) {
        help();
        return 0;
    }
    if (argc == 2 && equal(argv[1], "version")) {
        print("Kust 0.1 | compilador 0.1 | GTRA v1.1\n");
        return 0;
    }
    if (argc <= 4 && equal(argv[1], "docs")) {
        if (argc == 2) {
            docs_index();
            return 0;
        }
        if (equal(argv[2], "--all"))
            return docs(argc == 4 ? argv[3] : 0, 1, 1);
        const unsigned int page = argc == 4 ? page_number(argv[3]) : 1;
        if (!page)
            return error_message(10, argv[3], "numero de pagina invalido");
        return docs(argv[2], page, 0);
    }
    if (argc == 3 && equal(argv[1], "check"))
        return compile_file(argv[2], 0, 0);
    if (equal(argv[1], "build") && (argc == 3 || (argc == 5 && equal(argv[3], "-o"))))
        return compile_file(argv[2], argc == 5 ? argv[4] : 0, 1);
    return error_message(10, 0, "argumentos invalidos. Consulte kust help");
}
