#ifndef GPT_R_APP_H
#define GPT_R_APP_H

#include "../fs/fat16.h"

enum app_status {
    APP_EXITED, APP_FAULTED, APP_INVALID, APP_READ_ERROR, APP_NO_MEMORY
};

struct app_result {
    enum app_status status;
    unsigned long long exit_code, vector, rip, fault_address;
};

void app_init(void);
struct app_result app_run(const struct fat16_volume *volume,
    const struct fat16_file *file,
    void (*output)(void *, const unsigned char *, unsigned int), void *context);
void app_handle_exception(unsigned long long cs, unsigned long long vector,
    unsigned long long rip, unsigned long long fault_address);

#endif
