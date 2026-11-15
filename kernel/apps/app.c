#include "app.h"
#include "../memory/pages.h"

enum {
    HEADER_SIZE = 32,
    USER_IMAGE = 0x80000000u,
    USER_STACK = USER_IMAGE + 2 * PAGE_SIZE,
    USER_PDPT_SLOT = USER_IMAGE >> 30,
    PRESENT = 1, WRITABLE = 2, USER = 4
};

struct task_state {
    unsigned int reserved0;
    unsigned long long rsp[3];
    unsigned long long reserved1;
    unsigned long long ist[7];
    unsigned long long reserved2;
    unsigned short reserved3, io_map;
} __attribute__((packed));

_Static_assert(sizeof(struct task_state) == 104, "Invalid x86-64 TSS size");

extern unsigned long long app_tss_descriptor[2];
extern void app_enter(unsigned long long entry, unsigned long long stack_top);
extern _Noreturn void app_leave(void);

static struct task_state tss;
static unsigned char interrupt_stack[2 * PAGE_SIZE] __attribute__((aligned(16)));
static unsigned long long directory[512] __attribute__((aligned(PAGE_SIZE)));
static unsigned long long table[512] __attribute__((aligned(PAGE_SIZE)));
static unsigned long long stack_page_flags;

static struct {
    int active;
    unsigned int image_size;
    struct app_result result;
    void (*output)(void *, const unsigned char *, unsigned int);
    void *context;
} running;

static unsigned long long enable_nx(void)
{
    unsigned int eax, edx;
    __asm__ volatile ("cpuid" : "=a"(eax) : "a"(0x80000000u) : "rbx", "rcx", "rdx");
    if (eax < 0x80000001u)
        return 0;
    __asm__ volatile ("cpuid" : "=d"(edx) : "a"(0x80000001u) : "rbx", "rcx");
    if (!(edx & (1u << 20)))
        return 0;
    __asm__ volatile ("rdmsr" : "=a"(eax), "=d"(edx) : "c"(0xc0000080u));
    __asm__ volatile ("wrmsr" : : "a"(eax | (1u << 11)), "d"(edx), "c"(0xc0000080u) : "memory");
    return 1ull << 63;
}

void app_init(void)
{
    tss = (struct task_state){
        .rsp = { (unsigned long long)(interrupt_stack + sizeof(interrupt_stack)) },
        .io_map = sizeof(tss)
    };
    const unsigned long long base = (unsigned long long)&tss;
    app_tss_descriptor[0] = 0x0000890000000000ull | (sizeof(tss) - 1) |
        ((base & 0x00ffffffull) << 16) | ((base & 0xff000000ull) << 32);
    app_tss_descriptor[1] = base >> 32;
    __asm__ volatile ("ltr %w0" : : "r"((unsigned short)0x28) : "memory");
    running.active = 0;
    stack_page_flags = PRESENT | WRITABLE | USER | enable_nx();
}

struct image_reader {
    unsigned char header[HEADER_SIZE];
    unsigned char *image;
    unsigned int size;
};

static int collect_image(void *context, const unsigned char *data, unsigned int size)
{
    struct image_reader *reader = context;
    for (unsigned int i = 0; i < size; ++i) {
        const unsigned int at = reader->size++;
        if (at < HEADER_SIZE)
            reader->header[at] = data[i];
        else if (at < HEADER_SIZE + PAGE_SIZE)
            reader->image[at - HEADER_SIZE] = data[i];
    }
    return 1;
}

static unsigned int read_u32(const unsigned char *p)
{
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned int)p[3] << 24);
}

static int valid_image(const struct image_reader *reader, unsigned int file_size,
                       unsigned int *entry)
{
    const unsigned char *h = reader->header;
    if (reader->size != file_size || read_u32(h) != 0x41525447u ||
        read_u32(h + 4) != ((HEADER_SIZE << 16) | 1) ||
        read_u32(h + 8) != file_size - HEADER_SIZE)
        return 0;
    *entry = read_u32(h + 12);
    if (*entry >= file_size - HEADER_SIZE)
        return 0;
    for (unsigned int i = 16; i < HEADER_SIZE; ++i)
        if (h[i])
            return 0;
    return 1;
}

static void flush_tlb(void)
{
    unsigned long long root;
    __asm__ volatile ("mov %%cr3, %0; mov %0, %%cr3" : "=&r"(root) : : "memory");
}

static struct app_result execute_image(void *image, void *stack, unsigned int size,
    unsigned int entry, void (*output)(void *, const unsigned char *, unsigned int),
    void *context)
{
    volatile unsigned long long *const pml4 = (void *)0x1000;
    volatile unsigned long long *const pdpt = (void *)0x2000;
    unsigned long long flags;
    __asm__ volatile ("pushfq; pop %0; cli" : "=r"(flags) : : "memory");

    for (unsigned int i = 0; i < 512; ++i) {
        directory[i] = 0;
        table[i] = 0;
    }
    directory[0] = (unsigned long long)table | PRESENT | WRITABLE | USER;
    table[0] = (unsigned long long)image | PRESENT | USER;
    table[2] = (unsigned long long)stack | stack_page_flags;
    /* Only the app PDPT branch receives user permissions. Leave the guard PTE absent. */
    pml4[0] |= USER;
    pdpt[USER_PDPT_SLOT] = (unsigned long long)directory | PRESENT | WRITABLE | USER;
    flush_tlb();

    running.image_size = size;
    running.output = output;
    running.context = context;
    running.active = 1;
    app_enter(USER_IMAGE + entry, USER_STACK + PAGE_SIZE);

    running.active = 0;
    pdpt[USER_PDPT_SLOT] = 0;
    pml4[0] &= ~(unsigned long long)USER;
    flush_tlb();
    page_free(stack);
    page_free(image);
    const struct app_result result = running.result;
    __asm__ volatile ("push %0; popfq" : : "r"(flags) : "cc", "memory");
    return result;
}

struct app_result app_run(const struct fat16_volume *volume,
    const struct fat16_file *file,
    void (*output)(void *, const unsigned char *, unsigned int), void *context)
{
    if (file->size <= HEADER_SIZE || file->size > HEADER_SIZE + PAGE_SIZE)
        return (struct app_result){ .status = APP_INVALID };
    unsigned char *image = page_alloc();
    if (!image)
        return (struct app_result){ .status = APP_NO_MEMORY };

    for (unsigned int i = 0; i < PAGE_SIZE; ++i)
        image[i] = 0;
    struct image_reader reader = { .image = image };
    const int read_ok = fat16_read(volume, file, collect_image, &reader) == FAT16_READ_OK;
    unsigned int entry;
    enum app_status failure = APP_READ_ERROR;
    if (read_ok) {
        failure = APP_INVALID;
        if (valid_image(&reader, file->size, &entry)) {
            unsigned char *stack = page_alloc();
            if (stack) {
                for (unsigned int i = 0; i < PAGE_SIZE; ++i)
                    stack[i] = 0;
                return execute_image(image, stack, file->size - HEADER_SIZE,
                    entry, output, context);
            }
            failure = APP_NO_MEMORY;
        }
    }
    page_free(image);
    return (struct app_result){ .status = failure };
}

static int contains(unsigned long long address, unsigned long long length,
                    unsigned long long start, unsigned int size)
{
    return address >= start && length <= size && address - start <= size - length;
}

long long app_syscall(unsigned long long number, unsigned long long argument,
    unsigned long long length, unsigned long long cs)
{
    if (!running.active || (cs & 3) != 3)
        return -1;
    if (number == 2) {
        running.result = (struct app_result){ .status = APP_EXITED, .exit_code = argument };
        app_leave();
    }
    if (number != 1)
        return -1;
    if (length == 0)
        return 0;
    if (!contains(argument, length, USER_IMAGE, running.image_size) &&
        !contains(argument, length, USER_STACK, PAGE_SIZE))
        return -1;
    running.output(running.context, (const unsigned char *)argument, (unsigned int)length);
    return (long long)length;
}

void app_handle_exception(unsigned long long cs, unsigned long long vector,
    unsigned long long rip, unsigned long long fault_address)
{
    /* CPU-wide failures retain the kernel's diagnostic path. */
    if (running.active && (cs & 3) == 3 && vector != 2 && vector != 8 && vector != 18) {
        running.result = (struct app_result){ .status = APP_FAULTED,
            .vector = vector, .rip = rip, .fault_address = fault_address };
        app_leave();
    }
}
