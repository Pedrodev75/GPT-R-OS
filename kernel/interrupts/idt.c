#include "../apps/app.h"
#include "../drivers/console.h"
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

enum { EXCEPTION_COUNT = 32, KEYBOARD_VECTOR = 0x21, SYSCALL_VECTOR = 0x80, KERNEL_CODE_SELECTOR = 0x08 };
extern void (* const exception_entries[EXCEPTION_COUNT])(void);
extern void timer_irq_entry(void);
extern void keyboard_irq_entry(void);
extern void app_syscall_entry(void);
static struct idt_gate idt[SYSCALL_VECTOR + 1] __attribute__((aligned(16)));

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
    for (unsigned int i = KEYBOARD_VECTOR + 1; i < SYSCALL_VECTOR; ++i)
        idt[i] = (struct idt_gate){0};
    set_gate(SYSCALL_VECTOR, app_syscall_entry);
    idt[SYSCALL_VECTOR].attributes = 0xee;

    const struct idt_pointer pointer = { sizeof(idt) - 1, (unsigned long long)idt };
    __asm__ volatile ("lidt %0" : : "m"(pointer) : "memory");
}

/* Matches the pushes in exceptions.S and the 64-bit CPU frame. */
struct exception_frame {
    unsigned long long gpr[15];
    unsigned long long vector, error, rip, cs, rflags, rsp, ss;
};

static const char* const exception_names[EXCEPTION_COUNT] = {
    [0] = "#DE - Erro de divisao",
    [1] = "#DB - Depuracao",
    [2] = "NMI - Interrupcao nao mascaravel",
    [3] = "#BP - Ponto de parada",
    [4] = "#OF - Overflow",
    [5] = "#BR - Limite excedido",
    [6] = "#UD - Instrucao invalida",
    [7] = "#NM - Unidade de ponto flutuante indisponivel",
    [8] = "#DF - Falha dupla",
    [9] = "Segmento do coprocessador excedido",
    [10] = "#TS - TSS invalido",
    [11] = "#NP - Segmento ausente",
    [12] = "#SS - Falha de pilha",
    [13] = "#GP - Falha de protecao geral",
    [14] = "#PF - Falha de pagina",
    [16] = "#MF - Excecao x87",
    [17] = "#AC - Verificacao de alinhamento",
    [18] = "#MC - Erro de hardware",
    [19] = "#XM - Excecao SIMD",
    [20] = "#VE - Excecao de virtualizacao",
    [21] = "#CP - Falha de protecao de controle",
    [28] = "#HV - Injetador do hipervisor",
    [29] = "#VC - Comunicacao com o hipervisor",
    [30] = "#SX - Excecao de seguranca"
};

static unsigned int write_text(unsigned int cell,
    const char* text)
{
    while (*text)
        console_put(cell++, 0x4f00 | (unsigned char)*text++);
    return cell;
}

static void write_hex(unsigned int cell, const char* label,
    unsigned long long value, const char* separator)
{
    static const char hex[] = "0123456789ABCDEF";
    cell = write_text(cell, label);
    cell = write_text(cell, separator);
    for (int shift = 60; shift >= 0; shift -= 4)
        console_put(cell++, 0x4f00 | hex[(value >> shift) & 15]);
}

_Noreturn void exception_panic(const struct exception_frame* frame)
{
    static const char* const labels[] = {
        "RAX", "RBX", "RCX", "RDX", "RSI", "RDI", "RBP", "R8",
        "R9", "R10", "R11", "R12", "R13", "R14", "R15"
    };
    unsigned long long cr2 = 0;
    if (frame->vector == 14)
        __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));
    app_handle_exception(frame->cs, frame->vector, frame->rip, cr2);

#ifdef GPT_R_GRAPHICS
    vga_graphics_restore_console();
    vga_graphics_cursor(CONSOLE_WIDTH * CONSOLE_HEIGHT);
#else
    __asm__ volatile ("outb %%al, %%dx" : : "a"(0x0a), "d"(0x3d4));
    __asm__ volatile ("outb %%al, %%dx" : : "a"(0x20), "d"(0x3d5));
#endif
    for (unsigned int i = 0; i < CONSOLE_WIDTH * CONSOLE_HEIGHT; ++i)
        console_put(i, 0x4f20);
    write_text(0, "GPT-R OS - CPU EXCEPTION");
    const char* name = exception_names[frame->vector];
    write_text(CONSOLE_WIDTH, name ? name : "Vetor reservado");

    write_hex(3 * CONSOLE_WIDTH, "VECTOR", frame->vector, " 0x");
    write_hex(4 * CONSOLE_WIDTH, "ERROR", frame->error, " 0x");
    write_hex(5 * CONSOLE_WIDTH, "RIP", frame->rip, " 0x");
    write_hex(6 * CONSOLE_WIDTH, "RFLAGS", frame->rflags, " 0x");
    write_hex(7 * CONSOLE_WIDTH, "RSP", frame->rsp, " 0x");
    if (frame->vector == 14)
        write_hex(8 * CONSOLE_WIDTH, "CR2", cr2, " 0x");

    for (unsigned int i = 0; i < 15; ++i)
        write_hex((10 + i / 2) * CONSOLE_WIDTH + (i % 2) * (CONSOLE_WIDTH / 2),
            labels[i], frame->gpr[i], " ");
    for (;;)
        __asm__ volatile ("cli; hlt");
}
