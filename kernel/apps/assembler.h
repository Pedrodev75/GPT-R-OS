#ifndef GPT_R_ASSEMBLER_H
#define GPT_R_ASSEMBLER_H

#include "app.h"
#include "app_abi.h"

enum {
    ASM_V1_SOURCE_LIMIT = 4096, ASM_V11_SOURCE_LIMIT = 65536,
    ASM_V1_LABEL_LIMIT = 64, ASM_V11_LABEL_LIMIT = 1024
};

struct asm_io {
    void *context;
    unsigned char (*read)(void *, unsigned int);
    int (*write)(void *, unsigned int, unsigned char);
};

struct asm_diagnostic {
    const char *message;
    unsigned int line;
};

/* Writes the packed payload without its header. Return zero or an APP_ERR code. */
int app_assemble(const struct asm_io *io, unsigned int source_size,
    unsigned int version, struct app_info *info, struct asm_diagnostic *diagnostic);
int app_assemble_at(const struct fat16_volume *volume, const struct fat16_path *cwd,
    const unsigned char *source, const unsigned char *target, unsigned int version,
    struct asm_diagnostic *diagnostic);

#endif
