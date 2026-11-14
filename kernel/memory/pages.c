#include "pages.h"

enum {
    FIRST_ADDRESS = 0x100000,
    END_ADDRESS = 0x200000,
    PAGE_COUNT = (END_ADDRESS - FIRST_ADDRESS) / PAGE_SIZE
};

enum {
    RESERVED, FREE = 1, ALLOCATED = 2
};

static unsigned char pages[PAGE_COUNT];
static unsigned int free_count;

static int page_is_usable(const volatile struct e820_map *map,
                          unsigned long long address)
{
    const unsigned long long end = address + PAGE_SIZE;
    int covered = 0;
    for (unsigned int i = 0; i < map->count; ++i) {
        const volatile struct e820_entry *entry = &map->entries[i];
        const unsigned long long base = entry->base;
        const unsigned long long length = entry->length;
        if (!length || base >= end)
            continue;
        if (entry->type != E820_USABLE) {
            if (base >= address || length > address - base)
                return 0;
        } else if (base <= address && length >= end - base) {
            covered = 1;
        }
    }
    return covered;
}

void pages_init(const volatile struct e820_map *map)
{
    const int complete = map->status == E820_COMPLETE &&
                         map->count <= E820_MAX_ENTRIES;
    free_count = 0;
    for (unsigned int i = 0; i < PAGE_COUNT; ++i) {
        const unsigned long long address = FIRST_ADDRESS + i * PAGE_SIZE;
        pages[i] = complete && page_is_usable(map, address) ? FREE : RESERVED;
        free_count += pages[i] == FREE;
    }
}

void *page_alloc(void)
{
    for (unsigned int i = 0; i < PAGE_COUNT; ++i) {
        if (pages[i] == FREE) {
            pages[i] = ALLOCATED;
            --free_count;
            return (void *)(unsigned long long)(FIRST_ADDRESS + i * PAGE_SIZE);
        }
    }
    return 0;
}

int page_free(void *page)
{
    const unsigned long long address = (unsigned long long)page;
    if (address < FIRST_ADDRESS || address >= END_ADDRESS ||
        address % PAGE_SIZE)
        return 0;
    const unsigned int index = (address - FIRST_ADDRESS) / PAGE_SIZE;
    if (pages[index] != ALLOCATED)
        return 0;
    pages[index] = FREE;
    ++free_count;
    return 1;
}

unsigned int pages_free_count(void)
{
    return free_count;
}
