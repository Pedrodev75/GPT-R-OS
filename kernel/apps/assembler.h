#ifndef GPT_R_ASSEMBLER_H
#define GPT_R_ASSEMBLER_H

/* Return an error message, or zero on success. Output holds APP_IMAGE_LIMIT bytes. */
const char *app_assemble(const unsigned char *source, unsigned int source_size,
    unsigned char *code, unsigned int *code_size, unsigned int *entry,
    unsigned int *error_line);

#endif
