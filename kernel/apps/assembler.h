#ifndef GPT_R_ASSEMBLER_H
#define GPT_R_ASSEMBLER_H

#include "app.h"

enum {
    ASM_V1_SOURCE_LIMIT = 4096, ASM_V11_SOURCE_LIMIT = 65536,
    ASM_V1_LABEL_LIMIT = 64, ASM_V11_LABEL_LIMIT = 1024
};

struct asm_io {
    void *context;
    unsigned char (*read)(void *, unsigned int);
    int (*write)(void *, unsigned int, unsigned char);
};

/* Writes the packed payload without its header. Return zero on success. */
const char *app_assemble(const struct asm_io *io, unsigned int source_size,
    unsigned int version, struct app_info *info, unsigned int *error_line);

#endif
