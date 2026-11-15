#ifndef GPT_R_PAGES_H
#define GPT_R_PAGES_H

#include "e820.h"

enum { PAGE_SIZE = 4096 };

void pages_init(const volatile struct e820_map* map);
/* Single kernel context. IRQ handlers do not call the allocator. */
void* page_alloc(void);
/* Returns 1 on success or 0 for an invalid or already freed page. */
int page_free(void* page);
unsigned int pages_free_count(void);
unsigned int pages_managed_count(void);
int pages_contains_range(unsigned long long address, unsigned int length);

#endif
