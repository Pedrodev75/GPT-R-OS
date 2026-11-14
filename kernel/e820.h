#ifndef GPT_R_E820_H
#define GPT_R_E820_H

enum { E820_MAX_ENTRIES = 128, E820_COMPLETE = 1, E820_USABLE = 1 };
#define E820_MAP ((const volatile struct e820_map *)0x5000)

/* physical 0x5000, populated by collect_e820 in boot.S */
struct e820_entry {
    unsigned long long base, length;
    unsigned int type, attributes;
} __attribute__((packed));

struct e820_map {
    unsigned short count, status;
    struct e820_entry entries[E820_MAX_ENTRIES];
} __attribute__((packed));

_Static_assert(sizeof(struct e820_entry) == 24, "E820 entry size");

#endif
