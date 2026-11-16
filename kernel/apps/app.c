#include "app.h"
#include "../memory/pages.h"
#include "../drivers/keyboard.h"

enum {
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
/* Register order shared with app_entry.S. */
struct app_frame {
    unsigned long long rax, rbx, rcx, rdx, rsi, rdi, rbp;
    unsigned long long r8, r9, r10, r11, r12, r13, r14, r15;
    unsigned long long rip, cs, flags, rsp, ss;
};

_Static_assert(sizeof(struct app_frame) == 160, "Invalid app context size");
_Static_assert(__builtin_offsetof(struct app_frame, cs) == 128, "Invalid app frame");

extern void app_enter(const struct app_frame *frame);
extern _Noreturn void app_leave(void);

static struct task_state tss;
static unsigned char interrupt_stack[2 * PAGE_SIZE] __attribute__((aligned(16)));
static unsigned long long directory[512] __attribute__((aligned(PAGE_SIZE)));
static unsigned long long table[512] __attribute__((aligned(PAGE_SIZE)));
static unsigned long long stack_page_flags;

enum process_state { UNUSED, READY, WAITING, FINISHED };

struct process {
    struct app_frame frame;
    unsigned char fpu[512] __attribute__((aligned(16)));
    enum process_state state;
    unsigned char *image, *stack;
    unsigned int image_size, read_offset, read_capacity;
    void (*output)(void *, const unsigned char *, unsigned int);
    void *context;
    struct app_result result;
};

static struct process processes[APP_PROCESS_LIMIT];
static struct process *running;
static unsigned int next_slot;

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
    running = 0;
    next_slot = 0;
    for (unsigned int i = 0; i < APP_PROCESS_LIMIT; ++i)
        processes[i].state = UNUSED;
    for (unsigned int i = 0; i < 512; ++i)
        directory[i] = table[i] = 0;
    directory[0] = (unsigned long long)table | PRESENT | WRITABLE | USER;
    stack_page_flags = PRESENT | WRITABLE | USER | enable_nx();

    /* All supported x86-64 CPUs have FXSAVE and SSE2. Kernel code uses only GPRs. */
    unsigned long long cr0, cr4;
    __asm__ volatile ("mov %%cr0, %0; mov %%cr4, %1" : "=r"(cr0), "=r"(cr4));
    cr0 = (cr0 & ~0x0cull) | 0x22;
    cr4 |= (1ull << 9) | (1ull << 10);
    __asm__ volatile ("mov %0, %%cr0; mov %1, %%cr4" : : "r"(cr0), "r"(cr4) : "memory");
}

struct image_reader {
    unsigned char header[APP_HEADER_SIZE];
    unsigned char *image;
    unsigned int size;
};

static int collect_image(void *context, const unsigned char *data, unsigned int size)
{
    struct image_reader *reader = context;
    for (unsigned int i = 0; i < size; ++i) {
        const unsigned int at = reader->size++;
        if (at < APP_HEADER_SIZE)
            reader->header[at] = data[i];
        else if (reader->image && at < APP_HEADER_SIZE + APP_IMAGE_LIMIT)
            reader->image[at - APP_HEADER_SIZE] = data[i];
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
        read_u32(h + 4) != ((APP_HEADER_SIZE << 16) | APP_FORMAT_VERSION) ||
        read_u32(h + 8) != file_size - APP_HEADER_SIZE)
        return 0;
    *entry = read_u32(h + 12);
    if (*entry >= file_size - APP_HEADER_SIZE)
        return 0;
    for (unsigned int i = 16; i < APP_HEADER_SIZE; ++i)
        if (h[i])
            return 0;
    return 1;
}

static int read_image(const struct fat16_volume *volume, const struct fat16_file *file,
    unsigned char *image, struct app_info *info)
{
    if (file->size <= APP_HEADER_SIZE || file->size > APP_HEADER_SIZE + APP_IMAGE_LIMIT)
        return 0;
    struct image_reader reader = { .image = image };
    if (fat16_read(volume, file, collect_image, &reader) != FAT16_READ_OK)
        return -1;
    if (!valid_image(&reader, file->size, &info->entry))
        return 0;
    info->image_size = file->size - APP_HEADER_SIZE;
    return 1;
}

int app_inspect(const struct fat16_volume *volume, const struct fat16_file *file,
    struct app_info *info)
{
    return read_image(volume, file, 0, info);
}

void app_make_header(unsigned char *header, unsigned int size, unsigned int entry)
{
    for (unsigned int i = 0; i < APP_HEADER_SIZE; ++i)
        header[i] = 0;
    header[0] = 'G';
    header[1] = 'T';
    header[2] = 'R';
    header[3] = 'A';
    header[4] = APP_FORMAT_VERSION;
    header[6] = APP_HEADER_SIZE;
    for (unsigned int i = 0; i < 4; ++i) {
        header[8 + i] = size >> (i * 8);
        header[12 + i] = entry >> (i * 8);
    }
}

static void flush_tlb(void)
{
    unsigned long long root;
    __asm__ volatile ("mov %%cr3, %0; mov %0, %%cr3" : "=&r"(root) : : "memory");
}

static unsigned long long build_user_stack(unsigned char *stack,
    const struct app_launch *launch)
{
    if (!launch->argc || launch->argc > APP_ARG_LIMIT ||
        !launch->args_size || launch->args_size > APP_ARG_BYTES)
        return 0;

    const unsigned int strings_at = PAGE_SIZE - launch->args_size;
    const unsigned int frame_at =
        (strings_at - (launch->argc + 2) * sizeof(unsigned long long)) & ~15u;
    unsigned long long *frame = (void *)(stack + frame_at);
    unsigned int copied = 0;
    frame[0] = launch->argc;
    for (unsigned int i = 0; i < launch->argc; ++i) {
        frame[i + 1] = USER_STACK + strings_at + copied;
        unsigned char ch;
        do {
            if (copied == launch->args_size)
                return 0;
            ch = launch->args[copied];
            stack[strings_at + copied++] = ch;
        } while (ch);
    }
    frame[launch->argc + 1] = 0;
    return copied == launch->args_size ? USER_STACK + frame_at : 0;
}

static void release_pages(struct process *process)
{
    if (process->image) {
        page_free(process->stack);
        page_free(process->image);
        process->stack = process->image = 0;
    }
}

static void dispatch(struct process *process)
{
    volatile unsigned long long *const pml4 = (void *)0x1000;
    volatile unsigned long long *const pdpt = (void *)0x2000;
    unsigned long long flags;
    __asm__ volatile ("pushfq; pop %0; cli" : "=r"(flags) : : "memory");

    /* Only this process's two leaf mappings are user-accessible. */
    table[0] = (unsigned long long)process->image | PRESENT | USER;
    table[2] = (unsigned long long)process->stack | stack_page_flags;
    pml4[0] |= USER;
    pdpt[USER_PDPT_SLOT] = (unsigned long long)directory | PRESENT | WRITABLE | USER;
    flush_tlb();

    running = process;
    __asm__ volatile ("fxrstor64 %0" : : "m"(process->fpu) : "memory");
    app_enter(&process->frame);
    __asm__ volatile ("fxsave64 %0" : "=m"(process->fpu) : : "memory");
    running = 0;

    pdpt[USER_PDPT_SLOT] = 0;
    pml4[0] &= ~(unsigned long long)USER;
    flush_tlb();
    __asm__ volatile ("push %0; popfq" : : "r"(flags) : "cc", "memory");
    if (process->state == FINISHED)
        release_pages(process);
}

enum app_status app_start(unsigned int slot, const struct fat16_volume *volume,
    const struct fat16_file *file, const struct app_launch *launch)
{
    struct process *process = &processes[slot];
    if (process->state != UNUSED)
        return APP_LIMIT;
    if (file->size <= APP_HEADER_SIZE || file->size > APP_HEADER_SIZE + APP_IMAGE_LIMIT)
        return APP_INVALID;
    unsigned char *image = page_alloc();
    if (!image)
        return APP_NO_MEMORY;

    for (unsigned int i = 0; i < PAGE_SIZE; ++i)
        image[i] = 0;
    struct app_info info;
    const int valid = read_image(volume, file, image, &info);
    enum app_status failure = valid < 0 ? APP_READ_ERROR : APP_INVALID;
    if (valid > 0) {
        unsigned char *stack = page_alloc();
        if (stack) {
            for (unsigned int i = 0; i < PAGE_SIZE; ++i)
                stack[i] = 0;
            const unsigned long long rsp = build_user_stack(stack, launch);
            if (rsp) {
                *process = (struct process){
                    .frame = { .rip = USER_IMAGE + info.entry, .cs = 0x23,
                        .flags = 0x202, .rsp = rsp, .ss = 0x1b },
                    .state = READY, .image = image, .stack = stack, .image_size = info.image_size,
                    .output = launch->output, .context = launch->context
                };
                /* FXSAVE layout: x87 control word 0x037f, MXCSR 0x1f80. */
                process->fpu[0] = 0x7f;
                process->fpu[1] = 0x03;
                process->fpu[24] = 0x80;
                process->fpu[25] = 0x1f;
                return APP_STARTED;
            }
            page_free(stack);
            failure = APP_BAD_ARGS;
        } else {
            failure = APP_NO_MEMORY;
        }
    }
    page_free(image);
    return failure;
}

int app_ready(void)
{
    for (unsigned int i = 0; i < APP_PROCESS_LIMIT; ++i)
        if (processes[i].state == READY)
            return 1;
    return 0;
}

void app_schedule(void)
{
    for (unsigned int i = 0; i < APP_PROCESS_LIMIT; ++i) {
        const unsigned int slot = (next_slot + i) % APP_PROCESS_LIMIT;
        if (processes[slot].state == READY) {
            next_slot = (slot + 1) % APP_PROCESS_LIMIT;
            dispatch(&processes[slot]);
            return;
        }
    }
}

static int contains(unsigned long long address, unsigned long long length,
                    unsigned long long start, unsigned int size)
{
    return address >= start && length <= size && address - start <= size - length;
}

static _Noreturn void finish(struct app_result result)
{
    running->result = result;
    running->state = FINISHED;
    app_leave();
}

void app_preempt(const struct app_frame *frame)
{
    if (running && (frame->cs & 3) == 3) {
        running->frame = *frame;
        app_leave();
    }
}

void app_syscall(struct app_frame *frame)
{
    const unsigned long long number = frame->rax;
    const unsigned long long argument = frame->rdi;
    const unsigned long long length = frame->rsi;
    frame->rax = -1;
    if (!running || (frame->cs & 3) != 3)
        return;
    if (number == 2)
        finish((struct app_result){ .status = APP_EXITED, .exit_code = argument });
    if (number != 1 && number != 3)
        return;
    if (!length) {
        frame->rax = 0;
        return;
    }

    const int in_stack = contains(argument, length, USER_STACK, PAGE_SIZE);
    if (number == 3) {
        if (!in_stack)
            return;
        running->read_offset = argument - USER_STACK;
        running->read_capacity = length;
        running->frame = *frame;
        running->state = WAITING;
        app_leave();
    }
    if (!in_stack && !contains(argument, length, USER_IMAGE, running->image_size))
        return;

    running->output(running->context, (const unsigned char *)argument, length);
    frame->rax = length;
}

unsigned int app_read_capacity(unsigned int slot)
{
    const struct process *process = &processes[slot];
    return process->state == WAITING ? process->read_capacity : 0;
}

void app_complete_read(unsigned int slot, const unsigned char *bytes, unsigned int count)
{
    struct process *process = &processes[slot];
    if (process->state != WAITING || count >= process->read_capacity)
        return;
    unsigned char *buffer = process->stack + process->read_offset;
    for (unsigned int i = 0; i < count; ++i)
        buffer[i] = bytes[i];
    buffer[count] = 0;
    process->frame.rax = count;
    process->state = READY;
}

void app_cancel(unsigned int slot)
{
    struct process *process = &processes[slot];
    if (process->state == READY || process->state == WAITING) {
        process->result = (struct app_result){ .status = APP_INTERRUPTED };
        process->state = FINISHED;
        release_pages(process);
    }
}

int app_take_result(unsigned int slot, struct app_result *result)
{
    struct process *process = &processes[slot];
    if (process->state != FINISHED)
        return 0;
    *result = process->result;
    process->state = UNUSED;
    return 1;
}

void app_release(unsigned int slot)
{
    app_cancel(slot);
    processes[slot].state = UNUSED;
}

#ifndef GPT_R_GRAPHICS
struct app_result app_run(const struct fat16_volume *volume,
    const struct fat16_file *file, const struct app_launch *launch)
{
    const enum app_status status = app_start(0, volume, file, launch);
    if (status != APP_STARTED)
        return (struct app_result){ .status = status };
    struct process *process = &processes[0];
    struct app_result result;
    while (!app_take_result(0, &result)) {
        if (keyboard_take_interrupt()) {
            app_cancel(0);
        } else if (process->state == WAITING) {
            unsigned char *buffer = process->stack + process->read_offset;
            const unsigned int count = launch->input(launch->context, buffer,
                process->read_capacity);
            if (count >= process->read_capacity)
                app_cancel(0);
            else
                app_complete_read(0, buffer, count);
        } else {
            app_schedule();
        }
    }
    return result;
}
#endif

void app_handle_exception(unsigned long long cs, unsigned long long vector,
    unsigned long long rip, unsigned long long fault_address)
{
    /* CPU-wide failures retain the kernel's diagnostic path. */
    if (running && (cs & 3) == 3 && vector != 2 && vector != 8 && vector != 18)
        finish((struct app_result){ .status = APP_FAULTED,
            .vector = vector, .rip = rip, .fault_address = fault_address });
}
