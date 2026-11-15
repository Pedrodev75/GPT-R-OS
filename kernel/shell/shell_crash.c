#include "shell_internal.h"
#include "../memory/pages.h"
#include "../memory/heap.h"

static void exhaust_pages(unsigned int *row)
{
    const unsigned int initial = pages_free_count();
    void **head = 0;
    void **page;
    unsigned int count = 0;
    /* Chain the test pages themselves so only those pages are freed. */
    while ((page = page_alloc())) {
        *page = head;
        head = page;
        ++count;
    }
    shell_print_number(row, "Paginas alocadas ate esgotar: ", count, "");
    while (head) {
        page = head;
        head = *page;
        if (!page_free(page)) {
            shell_print_line(row, "crash alloc: falha ao liberar pagina");
            return;
        }
    }
    if (count == initial && pages_free_count() == initial)
        shell_print_line(row, "Alocador retornou NULL. Contagem restaurada.");
    else
        shell_print_line(row, "crash alloc: contagem inesperada");
}

void shell_crash(unsigned int *row, const unsigned char *type)
{
    while (*type == ' ')
        ++type;
    if (shell_equals(type, "ud")) {
        __asm__ volatile ("ud2");
    } else if (shell_equals(type, "div")) {
        __asm__ volatile ("xor %%ecx, %%ecx; xor %%edx, %%edx; "
                          "mov $1, %%eax; div %%ecx"
                          : : : "rax", "rcx", "rdx", "cc");
    } else if (shell_equals(type, "pf")) {
        __asm__ volatile ("movb (%0), %%al" : : "r"(0x40000000ull) : "rax", "memory");
    } else if (shell_equals(type, "gp")) {
        /* Noncanonical in both 48-bit and 57-bit address modes. */
        __asm__ volatile ("movb (%0), %%al" : : "r"(0x8000000000000000ull)
                          : "rax", "memory");
    } else if (shell_equals(type, "int3")) {
        __asm__ volatile ("int3");
    } else if (shell_equals(type, "alloc")) {
        exhaust_pages(row);
    } else if (shell_equals(type, "heap")) {
        shell_print_line(row, heap_self_test() ? "crash heap: PASS" : "crash heap: FAIL");
    } else if (shell_equals(type, "hang")) {
        __asm__ volatile ("cli; 1: pause; jmp 1b" : : : "memory");
        __builtin_unreachable();
    } else if (shell_equals(type, "triple")) {
        const unsigned char empty_idtr[10] = {0};
        __asm__ volatile ("cli; lidt %0; ud2" : : "m"(empty_idtr) : "memory");
        __builtin_unreachable();
    } else {
        shell_print_line(row, "Uso: crash <ud|div|pf|gp|int3|alloc|heap|hang|triple>");
    }
}
