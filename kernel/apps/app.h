#ifndef GPT_R_APP_H
#define GPT_R_APP_H

#include "../fs/fat16.h"

enum app_status {
    APP_EXITED, APP_FAULTED, APP_INVALID, APP_READ_ERROR, APP_NO_MEMORY, APP_BAD_ARGS, APP_INTERRUPTED
};

struct app_result {
    enum app_status status;
    unsigned long long exit_code, vector, rip, fault_address;
};

enum { APP_HEADER_SIZE = 32, APP_IMAGE_LIMIT = 4096, APP_FORMAT_VERSION = 1 };
struct app_info {
    unsigned int image_size, entry;
};

/* Returns 1 for a valid app, 0 for bad format and -1 on I/O error. */
int app_inspect(const struct fat16_volume *volume, const struct fat16_file *file,
    struct app_info *info);
void app_make_header(unsigned char *header, unsigned int size, unsigned int entry);

enum { APP_ARG_LIMIT = 40, APP_ARG_BYTES = 256 };

struct app_launch {
    const unsigned char *args;
    unsigned int argc, args_size;
    void (*output)(void *, const unsigned char *, unsigned int);
    unsigned int (*input)(void *, unsigned char *, unsigned int);
    void *context;
};

void app_init(void);
void app_interrupt(unsigned long long cs);
int app_interrupt_pending(void);
struct app_result app_run(const struct fat16_volume *volume,
    const struct fat16_file *file, const struct app_launch *launch);
void app_handle_exception(unsigned long long cs, unsigned long long vector,
    unsigned long long rip, unsigned long long fault_address);

#endif
