#ifndef GPT_R_APP_FILES_H
#define GPT_R_APP_FILES_H

#include "app_abi.h"

struct fat16_volume;
struct fat16_path;
struct fat16_location;
struct asm_diagnostic;
struct asm_io;

/* The filesystem adapter binds a launch volume and directory to this table. */
void app_files_begin(unsigned int slot, const struct fat16_volume *volume,
    const struct fat16_path *directory);
int app_file_open(unsigned int slot, const unsigned char *path, unsigned int mode);
int app_file_read(unsigned int slot, unsigned int handle, unsigned char *data, unsigned int size);
int app_file_write(unsigned int slot, unsigned int handle, const unsigned char *data, unsigned int size);
int app_file_close(unsigned int slot, unsigned int handle, int commit);
int app_files_finish(unsigned int slot, int commit);
unsigned int app_files_pages(unsigned int slot);
int app_file_in_use(const struct fat16_volume *volume, const struct fat16_location *entry, int writers_only);
int app_file_assemble(unsigned int slot, const unsigned char *source, const unsigned char *target,
    unsigned int version, struct asm_diagnostic *diagnostic);
int app_file_assemble_buffer(unsigned int slot, const struct asm_io *source, unsigned int size,
    const unsigned char *target, unsigned int version, struct asm_diagnostic *diagnostic);

#endif
