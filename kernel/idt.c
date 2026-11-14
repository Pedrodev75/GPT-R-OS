#include "idt.h"

struct idt_gate {
    unsigned short offset_low;
    unsigned short selector;
    unsigned char ist;
    unsigned char attributes;
    unsigned short offset_mid;
    unsigned int offset_high;
    unsigned int reserved;
} __attribute__((packed));

struct idt_pointer {
    unsigned short limit;
    unsigned long long base;
} __attribute__((packed));

_Static_assert(sizeof(struct idt_gate) == 16, "Invalid IDT gate size");
_Static_assert(sizeof(struct idt_pointer) == 10, "Invalid IDTR operand size");

enum { EXCEPTION_COUNT = 32, KEYBOARD_VECTOR = 0x21, KERNEL_CODE_SELECTOR = 0x08 };
extern void (* const exception_entries[EXCEPTION_COUNT])(void);
extern void timer_irq_entry(void);
extern void keyboard_irq_entry(void);
static struct idt_gate idt[KEYBOARD_VECTOR + 1] __attribute__((aligned(16)));

static void set_gate(unsigned int vector, void (*entry)(void))
{
    const unsigned long long address = (unsigned long long)entry;
    idt[vector] = (struct idt_gate){
        .offset_low = address,
        .selector = KERNEL_CODE_SELECTOR,
        .attributes = 0x8e,
        .offset_mid = address >> 16,
        .offset_high = address >> 32
    };
}

void idt_init(void)
{
    for (unsigned int i = 0; i < EXCEPTION_COUNT; ++i)
        set_gate(i, exception_entries[i]);
    set_gate(0x20, timer_irq_entry);
    set_gate(KEYBOARD_VECTOR, keyboard_irq_entry);

    const struct idt_pointer pointer = { sizeof(idt) - 1, (unsigned long long)idt };
    __asm__ volatile ("lidt %0" : : "m"(pointer) : "memory");
}

static void write_line(unsigned int row, const char* label,
    unsigned long long value)
{
    static const char hex[] = "0123456789ABCDEF";
    volatile unsigned short* cell =
        (volatile unsigned short*)0xb8000 + row * 80;

    while (*label)
        *cell++ = 0x4f00 | (unsigned char)*label++;
    for (int shift = 60; shift >= 0; shift -= 4)
        *cell++ = 0x4f00 | hex[(value >> shift) & 15];
}

_Noreturn void exception_panic(unsigned long long vector,
    unsigned long long error,
    unsigned long long rip)
{
    volatile unsigned short* const vga = (volatile unsigned short*)0xb8000;
    const char title[] = "GPT-R OS - CPU EXCEPTION";

    for (unsigned int i = 0; i < 80 * 25; ++i)
        vga[i] = 0x4f20;
    for (unsigned int i = 0; title[i]; ++i)
        vga[i] = 0x4f00 | title[i];

    write_line(2, "VECTOR 0x", vector);
    write_line(3, "ERROR  0x", error);
    write_line(4, "RIP    0x", rip);
    for (;;)
        __asm__ volatile ("cli; hlt");
}
