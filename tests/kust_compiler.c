#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "apps/kust/compiler.h"
#include "kernel/apps/assembler.h"
#include "kernel/memory/pages.h"

static char assembly[KUST_ASM_LIMIT];
static unsigned char image[APP_V11_IMAGE_LIMIT], large[KUST_SOURCE_LIMIT + 1];
static unsigned int pages;

void *page_alloc(void)
{
    void *page = malloc(PAGE_SIZE);
    if (page)
        ++pages;
    return page;
}

int page_free(void *page)
{
    assert(pages && page);
    --pages;
    free(page);
    return 1;
}

static unsigned char source_byte(void *context, unsigned int at)
{
    return ((const unsigned char *)context)[at];
}

static int payload_byte(void *context, unsigned int at, unsigned char byte)
{
    (void)context;
    assert(at < sizeof(image));
    image[at] = byte;
    return 1;
}

static void accepts(const unsigned char *source, unsigned int length)
{
    struct kust_error error;
    const unsigned int result = kust_compile(source, length, assembly, sizeof(assembly), &error);
    if (!result)
        fprintf(stderr, "Kust E%03u, %u:%u: %s\n", error.code, error.line, error.column, error.message);
    assert(result);
    const struct asm_io io = {assembly, source_byte, payload_byte};
    struct asm_diagnostic diagnostic;
    struct app_info info;
    const int status = app_assemble(&io, result, APP_V11_VERSION, &info, &diagnostic);
    if (status)
        fprintf(stderr, "Assembly at line %u: %s\n", diagnostic.line, diagnostic.message);
    assert(!status && info.version == APP_V11_VERSION && info.entry < info.code_size && !pages);
    unsigned char header[64];
    app_make_header(header, &info);
    assert(app_validate_header(header, sizeof(header), 64 + info.code_size + info.data_size, &info));
}

static void rejects(const char *source, unsigned int code)
{
    struct kust_error error;
    assert(!kust_compile((const unsigned char *)source, strlen(source), assembly, sizeof(assembly), &error));
    if (error.code != code)
        fprintf(stderr, "Expected E%03u, got E%03u at %u:%u: %s\n",
            code, error.code, error.line, error.column, error.message);
    assert(error.code == code && error.line && error.column);
}

#define MAIN "app \"The test\"\nfunc main() {\n"

int main(int argc, char **argv)
{
    assert(argc > 1);
    for (int i = 1; i < argc; ++i) {
        FILE *file = fopen(argv[i], "rb");
        assert(file);
        const unsigned int n = fread(large, 1, sizeof(large), file);
        assert(!ferror(file) && n < sizeof(large));
        fclose(file);
        accepts(large, n);
        accepts(large, n);
    }
    static const unsigned char normal[] = "\xef\xbb\xbf"
        "app \"x\"\r\n/* \n comment */\r\n"
        "const FLAG: bool = true || (1 / 0 > 2)\n"
        "func f(a: i32, b: bool) -> i32 { if b { return -a } else { return a } }\n"
        "func main() {\n"
        " term.print(\"a\\t\\r\\n\\\"\\\\\")\n"
        " term.print(\"\")\n"
        " let result: i32 = f(-2147483648, FLAG)\n"
        " term.println(result >= 0 == false)\n"
        "}\n";
    accepts(normal, sizeof(normal) - 1);
    memset(large, ' ', KUST_SOURCE_LIMIT);
    memcpy(large + KUST_SOURCE_LIMIT - sizeof(normal) + 1, normal + 3, sizeof(normal) - 4);
    accepts(large, KUST_SOURCE_LIMIT);
    memset(large, ' ', sizeof(large));
    large[KUST_SOURCE_LIMIT] = 0;

    rejects(MAIN "let x: i32 = y }", 1);
    rejects(MAIN "mut x: bool = 1 }", 2);
    rejects(MAIN "if 1 {} }", 2);
    rejects(MAIN "term.println(1 && true) }", 2);
    rejects(MAIN "let x: i32 = 1; x = 2 }", 3);
    rejects(MAIN "const x: i32 = 1; x -= 2 }", 3);
    rejects(MAIN "term.println(\"bad\\q\") }", 4);
    rejects(MAIN "term.println(\"\xc0\x80\") }", 4);
    rejects(MAIN "/* unterminated", 4);
    rejects(MAIN "term.println(1 }", 5);
    rejects(MAIN "let abcdefghijklmnopqrstuvwxyz123456: i32 = 1 }", 6);
    rejects(MAIN "term.println((((((((((((((((((((((((((((((1)))))))))))))))))))))))))))))) }", 6);
    rejects(MAIN "let x: i32 = 1; const x: i32 = 2 }", 7);
    rejects("app \"x\" func main(x: i32) {}", 8);
    rejects(MAIN "const x: i32 = 1 / 0 }", 8);
    rejects(MAIN "term.println(2147483648) }", 8);
    rejects(MAIN "term.println(-2147483649) }", 8);
    rejects(MAIN "f() } func f(x: i32) {}", 8);
    rejects(MAIN "f(false) } func f(x: i32) {}", 2);
    rejects(MAIN "} func f() -> i32 { if true { return 0 } }", 8);
    rejects(MAIN "if true { let x: i32 = 1 } term.println(x) }", 1);

    struct kust_error error;
    assert(!kust_compile(large, sizeof(large), assembly, sizeof(assembly), &error) && error.code == 6);
    memcpy(large, MAIN, sizeof(MAIN) - 1);
    memset(large + sizeof(MAIN) - 1, ';', KUST_TOKEN_LIMIT);
    large[sizeof(MAIN) - 1 + KUST_TOKEN_LIMIT] = 0;
    rejects((const char *)large, 6);
    assert(!kust_compile(normal, sizeof(normal) - 1, assembly, 32, &error) && error.code == 9);
    accepts(normal, sizeof(normal) - 1);
    puts("Kust language, resource limits, diagnostics and GTRA v1.1 assembly passed");
}
