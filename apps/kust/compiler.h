#ifndef KUST_COMPILER_H
#define KUST_COMPILER_H

enum {
    KUST_SOURCE_LIMIT = 32768, KUST_ASM_LIMIT = 65536, KUST_TOKEN_LIMIT = 16384,
    KUST_NAME_LIMIT = 31, KUST_STRING_LIMIT = 512, KUST_FUNCTION_LIMIT = 64,
    KUST_PARAMETER_LIMIT = 8, KUST_LOCAL_LIMIT = 64, KUST_GLOBAL_LIMIT = 64,
    KUST_DEPTH_LIMIT = 24, KUST_LABEL_LIMIT = 1024
};

struct kust_error {
    unsigned int code, line, column;
    const char *message;
    const char *expected, *received;
};

/* Generates Assembly in output and returns its byte count, or zero on error.
   The compiler has one workspace per process. Output need not be NUL terminated. */
unsigned int kust_compile(const unsigned char *source, unsigned int size, char *output,
    unsigned int capacity, struct kust_error *error);

#endif
