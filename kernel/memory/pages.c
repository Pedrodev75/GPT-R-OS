#include "pages.h"

enum {
    FIRST_ADDRESS = 0x100000,
    LARGE_PAGE_SIZE = 0x200000,
    END_ADDRESS = 0x40000000,
    DIRECTORY_ENTRIES = END_ADDRESS / LARGE_PAGE_SIZE,
    PRESENT_WRITABLE_LARGE = 0x83,
    PAGE_COUNT = (END_ADDRESS - FIRST_ADDRESS) / PAGE_SIZE
};

enum {
    RESERVED, FREE = 1, ALLOCATED = 2
};

static unsigned char pages[PAGE_COUNT];
static unsigned int free_count;

static int range_is_usable(const volatile struct e820_map* map,
    unsigned long long address, unsigned int size)
{
    const unsigned long long end = address + size;
    int covered = 0;
    for (unsigned int i = 0; i < map->count; ++i) {
        const volatile struct e820_entry* entry = &map->entries[i];
        const unsigned long long base = entry->base;
        const unsigned long long length = entry->length;
        if (!length || base >= end)
            continue;
        if (entry->type != E820_USABLE) {
            if (base >= address || length > address - base)
                return 0;
        }
        else if (base <= address && length >= end - base) {
            covered = 1;
        }
    }
    return covered;
}

void pages_init(const volatile struct e820_map* map)
{
    const int complete = map->status == E820_COMPLETE &&
        map->count <= E820_MAX_ENTRIES;
    /* boot.S installs this directory and maps the first 2 MiB. */
    volatile unsigned long long* const directory = (volatile unsigned long long*)0x3000;
    free_count = 0;
    for (unsigned int slot = 1; slot < DIRECTORY_ENTRIES; ++slot) {
        const unsigned int base = slot * LARGE_PAGE_SIZE;
        const int usable = complete && range_is_usable(map, base, LARGE_PAGE_SIZE);
        directory[slot] = usable ? base | PRESENT_WRITABLE_LARGE : 0;

        const unsigned int first = (base - FIRST_ADDRESS) / PAGE_SIZE;
        for (unsigned int i = first; i < first + LARGE_PAGE_SIZE / PAGE_SIZE; ++i)
            pages[i] = usable ? FREE : RESERVED;
        if (usable)
            free_count += LARGE_PAGE_SIZE / PAGE_SIZE;
    }

    /* Keep the bootloader, stack, E820 handoff and paging tables below 1 MiB. */
    for (unsigned int i = 0; i < FIRST_ADDRESS / PAGE_SIZE; ++i) {
        const unsigned int address = FIRST_ADDRESS + i * PAGE_SIZE;
        pages[i] = complete && range_is_usable(map, address, PAGE_SIZE) ? FREE : RESERVED;
        free_count += pages[i] == FREE;
    }
    unsigned long long cr3;
    __asm__ volatile ("mov %%cr3, %0; mov %0, %%cr3" : "=&r"(cr3) : : "memory");
}

void* page_alloc(void)
{
    for (unsigned int i = PAGE_COUNT; i-- > 0;) {
        if (pages[i] == FREE) {
            pages[i] = ALLOCATED;
            --free_count;
            return (void*)(unsigned long long)(FIRST_ADDRESS + i * PAGE_SIZE);
        }
    }
    return 0;
}

int page_free(void* page)
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
