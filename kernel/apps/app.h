#ifndef GPT_R_APP_H
#define GPT_R_APP_H

#include "../fs/fat16_path.h"

enum app_status {
    APP_EXITED, APP_FAULTED, APP_INVALID, APP_READ_ERROR, APP_NO_MEMORY, APP_BAD_ARGS, APP_INTERRUPTED,
    APP_STARTED, APP_LIMIT, APP_SAVE_ERROR, APP_OUTPUT_ERROR, APP_NO_NX, APP_BAD_VERSION
};

struct app_result {
    enum app_status status;
    unsigned long long exit_code, vector, rip, fault_address;
};

enum {
    APP_HEADER_PREFIX = 8, APP_HEADER_SIZE = 32, APP_IMAGE_LIMIT = 4096, APP_FORMAT_VERSION = 1,
    APP_V11_HEADER_SIZE = 64, APP_V11_IMAGE_LIMIT = 65536, APP_V11_VERSION = 0x0101,
    APP_FILE_MAX = APP_V11_HEADER_SIZE + APP_V11_IMAGE_LIMIT
};
#define APP_CODE_BASE 0x80000000ull
#define APP_V11_DATA_BASE 0x80011000ull
#define APP_V11_STACK_BASE 0x80022000ull
#define APP_V11_HEAP_BASE 0x80027000ull
struct app_info {
    unsigned int version, header_size, code_size, data_size, bss_size, entry;
};

/* Returns 1 for a valid app, 0 for bad format, -1 on I/O error and -2 for an unknown version. */
int app_inspect(const struct fat16_volume *volume, const struct fat16_file *file,
    struct app_info *info);
/* Returns a known header size, zero for invalid or incomplete input, or -2 for an unknown version. */
int app_header_size(const unsigned char *header, unsigned int available);
int app_validate_header(const unsigned char *header, unsigned int available,
    unsigned int file_size, struct app_info *info);
void app_make_header(unsigned char *header, const struct app_info *info);

enum { APP_ARG_LIMIT = 40, APP_ARG_BYTES = 256 };

struct app_launch {
    const unsigned char *args;
    unsigned int argc, args_size;
    int (*output)(void *, const unsigned char *, unsigned int);
    void *context;
    const struct fat16_path *directory;
    unsigned int executable_directory;
};

void app_init(void);
enum {
    APP_PROCESS_LIMIT = 4,
    APP_JOB_LIMIT = 8,
    APP_PROCESS_SLOTS = APP_PROCESS_LIMIT + APP_JOB_LIMIT
};

enum app_status app_start(unsigned int slot, const struct fat16_volume *volume,
    const struct fat16_file *file, const struct app_launch *launch);
enum app_task_state { APP_TASK_RUNNING, APP_TASK_WAITING, APP_TASK_FINISHED };
struct app_task_info {
    unsigned int pid, cpu_ticks, elapsed_ticks, memory_bytes;
    enum app_task_state state;
};

struct app_task_info app_task_info(unsigned int slot);
int app_ready(void);
int app_directory_running(const struct fat16_volume *volume, unsigned int directory);
void app_schedule(void);
unsigned int app_read_capacity(unsigned int slot);
void app_complete_read(unsigned int slot, const unsigned char *bytes, unsigned int count);
unsigned int app_drain_output(unsigned int slot, unsigned char *bytes, unsigned int capacity);
void app_cancel(unsigned int slot);
#ifdef GPT_R_GRAPHICS
void app_deliver_click(unsigned int slot, unsigned int x, unsigned int y);
#endif
int app_take_result(unsigned int slot, struct app_result *result);
void app_release(unsigned int slot);
void app_handle_exception(unsigned long long cs, unsigned long long vector,
    unsigned long long rip, unsigned long long fault_address);

#endif
