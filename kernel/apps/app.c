#include "app.h"
#include "app_files.h"
#include "../desktop/app_canvas.h"
#include "../memory/pages.h"
#include "../drivers/timer.h"

enum {
    USER_IMAGE = 0x80000000u,
    USER_STACK = USER_IMAGE + 2 * PAGE_SIZE,
    USER_HEAP_SLOT = 4, USER_HEAP = USER_IMAGE + USER_HEAP_SLOT * PAGE_SIZE,
    APP_HEAP_PAGES = 64,
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

enum process_state { UNUSED, READY, WAITING, WAITING_OUTPUT, WAITING_CLICK, WAITING_IO, FINISHED };

struct process {
    struct app_frame frame;
    unsigned char fpu[512] __attribute__((aligned(16)));
    enum process_state state;
    unsigned int pid;
    unsigned int started_at, stopped_at, cpu_ticks;
    unsigned char *image, *stack;
    unsigned char *heap_pages[APP_HEAP_PAGES];
    unsigned char heap_runs[APP_HEAP_PAGES];
    unsigned int image_size, read_capacity;
    unsigned int output_offset;
    unsigned long long read_address;
    int (*output)(void *, const unsigned char *, unsigned int);
    int headless;
    void *context;
    struct app_result result;
};

static struct process processes[APP_PROCESS_SLOTS];
static struct process *running;
static unsigned int next_slot, next_pid;
static unsigned char io_buffer[APP_IO_LIMIT];
static void service_file(struct process *process);

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
    next_pid = 1;
    for (unsigned int i = 0; i < APP_PROCESS_SLOTS; ++i) {
        processes[i].state = UNUSED;
        processes[i].pid = 0;
    }
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

static void release_resources(struct process *process)
{
    process->stopped_at = timer_ticks();
    const int saved = app_files_finish(process - processes, process->result.status == APP_EXITED);
    if (saved && process->result.status == APP_EXITED)
        process->result.status = APP_SAVE_ERROR;
    for (unsigned int i = 0; i < APP_HEAP_PAGES; ++i) {
        if (process->heap_pages[i])
            page_free(process->heap_pages[i]);
        process->heap_pages[i] = 0;
        process->heap_runs[i] = 0;
    }
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

    /* Only this process's leaf mappings are user-accessible. */
    table[0] = (unsigned long long)process->image | PRESENT | USER;
    table[2] = (unsigned long long)process->stack | stack_page_flags;
    for (unsigned int i = 0; i < APP_HEAP_PAGES; ++i) {
        const unsigned long long page = (unsigned long long)process->heap_pages[i];
        table[USER_HEAP_SLOT + i] = page ? page | stack_page_flags : 0;
    }
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
    if (process->state == WAITING_IO)
        service_file(process);
    else if (process->state == FINISHED)
        release_resources(process);
}

static unsigned int allocate_pid(void)
{
    for (;;) {
        unsigned int pid = next_pid++;
        if (!pid)
            continue;
        unsigned int i = 0;
        while (i < APP_PROCESS_SLOTS && processes[i].pid != pid)
            ++i;
        if (i == APP_PROCESS_SLOTS)
            return pid;
    }
}

struct app_task_info app_task_info(unsigned int slot)
{
    const struct process *process = &processes[slot];
    const enum app_task_state state = process->state == READY ? APP_TASK_RUNNING :
        (process->state == WAITING || process->state == WAITING_OUTPUT || process->state == WAITING_CLICK ||
         process->state == WAITING_IO) ? APP_TASK_WAITING : APP_TASK_FINISHED;
    if (!process->pid)
        return (struct app_task_info){0};
    unsigned int pages = process->image ? 2 : 0;
    if (pages) {
        for (unsigned int i = 0; i < APP_HEAP_PAGES; ++i)
            pages += process->heap_pages[i] != 0;
        pages += app_files_pages(slot);
    }
    const unsigned int end = state == APP_TASK_FINISHED ? process->stopped_at : timer_ticks();
    return (struct app_task_info){
        .pid = process->pid, .state = state, .cpu_ticks = process->cpu_ticks,
        .elapsed_ticks = end - process->started_at, .memory_bytes = pages * PAGE_SIZE
    };
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
                    .state = READY, .pid = allocate_pid(), .image = image, .stack = stack, .image_size = info.image_size,
                    .started_at = timer_ticks(),
                    .headless = slot >= APP_PROCESS_LIMIT,
                    .output = launch->output, .context = launch->context
                };
                app_files_begin(slot, volume, launch->directory);
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
    for (unsigned int i = 0; i < APP_PROCESS_SLOTS; ++i)
        if (processes[i].state == READY)
            return 1;
    return 0;
}

void app_schedule(void)
{
    for (unsigned int i = 0; i < APP_PROCESS_SLOTS; ++i) {
        const unsigned int slot = (next_slot + i) % APP_PROCESS_SLOTS;
        if (processes[slot].state == READY) {
            next_slot = (slot + 1) % APP_PROCESS_SLOTS;
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

static int user_range(const struct process *process, unsigned long long address,
    unsigned long long length, int writable)
{
    if (contains(address, length, USER_STACK, PAGE_SIZE) ||
        (!writable && contains(address, length, USER_IMAGE, process->image_size)))
        return 1;
    if (!contains(address, length, USER_HEAP, APP_HEAP_PAGES * PAGE_SIZE) ||
        address == USER_HEAP + APP_HEAP_PAGES * PAGE_SIZE)
        return 0;
    unsigned int start = (address - USER_HEAP) / PAGE_SIZE;
    while (start && !process->heap_runs[start])
        --start;
    return process->heap_runs[start] &&
        contains(address, length, USER_HEAP + start * PAGE_SIZE, process->heap_runs[start] * PAGE_SIZE);
}

static unsigned char *user_page(const struct process *process, unsigned long long address)
{
    const unsigned int page = (address - USER_IMAGE) / PAGE_SIZE;
    if (page == 0)
        return process->image;
    if (page == 2)
        return process->stack;
    return process->heap_pages[page - USER_HEAP_SLOT];
}

/* Disk operations run with the user page tables detached. */
static void copy_user(const struct process *process, unsigned long long address,
    unsigned char *data, unsigned int size, int to_user)
{
    while (size) {
        const unsigned int byte = address % PAGE_SIZE;
        unsigned char *page = user_page(process, address);
        const unsigned int room = PAGE_SIZE - byte;
        const unsigned int count = size < room ? size : room;
        for (unsigned int i = 0; i < count; ++i) {
            if (to_user)
                page[byte + i] = data[i];
            else
                data[i] = page[byte + i];
        }
        address += count;
        data += count;
        size -= count;
    }
}

static int read_path(const struct process *process, unsigned long long address,
    unsigned char path[FAT16_PATH_CAPACITY])
{
    for (unsigned int i = 0; i < FAT16_PATH_CAPACITY; ++i, ++address) {
        if (!user_range(process, address, 1, 0))
            return 0;
        path[i] = user_page(process, address)[address % PAGE_SIZE];
        if (!path[i])
            return i != 0;
    }
    return 0;
}

static void service_file(struct process *process)
{
    const unsigned int slot = process - processes;
    struct app_frame *frame = &process->frame;
    const unsigned long long number = frame->rax, handle = frame->rdi;
    long long result = APP_ERR_INVALID;
    if (number == APP_SYS_OPEN) {
        unsigned char path[FAT16_PATH_CAPACITY];
        if (frame->rsi <= APP_FILE_WRITE_MODE && read_path(process, handle, path))
            result = app_file_open(slot, path, frame->rsi);
    } else if (handle <= 0xffffffffu) {
        const unsigned long long address = frame->rsi, length = frame->rdx;
        if (number == APP_SYS_CLOSE) {
            result = app_file_close(slot, handle, 1);
        } else if (length <= sizeof(io_buffer) &&
            (!length || user_range(process, address, length, number == APP_SYS_FILE_READ))) {
            if (number == APP_SYS_FILE_READ) {
                result = app_file_read(slot, handle, io_buffer, length);
                if (result > 0)
                    copy_user(process, address, io_buffer, result, 1);
            } else {
                copy_user(process, address, io_buffer, length, 0);
                result = app_file_write(slot, handle, io_buffer, length);
            }
        }
    }
    frame->rax = result;
    process->state = READY;
}

static long long allocate_user(struct process *process, unsigned long long bytes)
{
    if (!bytes || bytes > APP_HEAP_PAGES * PAGE_SIZE)
        return APP_ERR_INVALID;
    const unsigned int count = (bytes + PAGE_SIZE - 1) / PAGE_SIZE;
    unsigned int first = 0, run = 0;
    for (unsigned int i = 0; i < APP_HEAP_PAGES && run < count; ++i) {
        if (process->heap_pages[i]) {
            run = 0;
        } else if (run++ == 0) {
            first = i;
        }
    }
    if (run < count)
        return APP_ERR_MEMORY;

    unsigned int i = 0;
    for (; i < count; ++i) {
        unsigned char *page = page_alloc();
        if (!page)
            break;
        for (unsigned int b = 0; b < PAGE_SIZE; ++b)
            page[b] = 0;
        process->heap_pages[first + i] = page;
    }
    if (i != count) {
        while (i) {
            --i;
            page_free(process->heap_pages[first + i]);
            process->heap_pages[first + i] = 0;
        }
        return APP_ERR_MEMORY;
    }
    process->heap_runs[first] = count;
    for (i = 0; i < count; ++i)
        table[USER_HEAP_SLOT + first + i] =
            (unsigned long long)process->heap_pages[first + i] | stack_page_flags;
    flush_tlb();
    return USER_HEAP + first * PAGE_SIZE;
}

static long long free_user(struct process *process, unsigned long long address)
{
    if (address < USER_HEAP || address >= USER_HEAP + APP_HEAP_PAGES * PAGE_SIZE ||
        address % PAGE_SIZE)
        return APP_ERR_INVALID;
    const unsigned int first = (address - USER_HEAP) / PAGE_SIZE;
    const unsigned int count = process->heap_runs[first];
    if (!count)
        return APP_ERR_INVALID;
    process->heap_runs[first] = 0;
    for (unsigned int i = first; i < first + count; ++i)
        table[USER_HEAP_SLOT + i] = 0;
    flush_tlb();
    for (unsigned int i = first; i < first + count; ++i) {
        page_free(process->heap_pages[i]);
        process->heap_pages[i] = 0;
    }
    return 0;
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
        ++running->cpu_ticks;
        running->frame = *frame;
        app_leave();
    }
}

#ifdef GPT_R_GRAPHICS
static void app_gui_syscall(struct app_frame *frame, unsigned long long number)
{
    if (running->headless)
        return;
    const unsigned int slot = running - processes;
    switch (number) {
    case APP_GUI_BEGIN:
        frame->rax = app_canvas_begin(slot, frame->rdi);
        break;
    case APP_GUI_RECT:
        frame->rax = app_canvas_rect(slot, frame->rdi, frame->rsi, frame->rdx, frame->rcx, frame->r8);
        break;
    case APP_GUI_TEXT: {
        const unsigned long long address = frame->rdx, size = frame->rcx;
        if (user_range(running, address, size, 0))
            frame->rax = app_canvas_text(slot, frame->rdi, frame->rsi,
                (const unsigned char *)address, size, frame->r8);
        break;
    }
    case APP_GUI_PRESENT:
        frame->rax = app_canvas_present(slot);
        break;
    case APP_GUI_WAIT:
        if (app_canvas_visible(slot)) {
            const unsigned long long click = app_canvas_poll(slot);
            if (click) {
                frame->rax = click;
                break;
            }
            running->frame = *frame;
            running->state = WAITING_CLICK;
            app_leave();
        }
        break;
    }
}

void app_deliver_click(unsigned int slot, unsigned int x, unsigned int y)
{
    struct process *process = &processes[slot];
    app_canvas_click(slot, x, y);
    if (process->state == WAITING_CLICK) {
        const unsigned long long click = app_canvas_poll(slot);
        if (click) {
            process->frame.rax = click;
            process->state = READY;
        }
    }
}
#endif

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
    if (number == APP_SYS_ALLOC) {
        frame->rax = allocate_user(running, argument);
        return;
    }
    if (number == APP_SYS_FREE) {
        frame->rax = free_user(running, argument);
        return;
    }
    if (number >= APP_SYS_OPEN && number <= APP_SYS_CLOSE) {
        running->frame = *frame;
        running->frame.rax = number;
        running->state = WAITING_IO;
        app_leave();
    }
    if (number != 1 && number != 3) {
#ifdef GPT_R_GRAPHICS
        app_gui_syscall(frame, number);
#endif
        return;
    }
    if (!length) {
        frame->rax = 0;
        return;
    }

    if (number == 3) {
        if (length > APP_IO_LIMIT || !user_range(running, argument, length, 1))
            return;
        running->read_address = argument;
        running->read_capacity = length;
        running->frame = *frame;
        running->state = WAITING;
        app_leave();
    }
    if (!user_range(running, argument, length, 0))
        return;

    if (running->headless) {
        running->frame = *frame;
        running->output_offset = 0;
        running->state = WAITING_OUTPUT;
        app_leave();
    }
    if (!running->output(running->context, (const unsigned char *)argument, length))
        finish((struct app_result){ .status = APP_OUTPUT_ERROR });
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
    copy_user(process, process->read_address, (unsigned char *)bytes, count, 1);
    unsigned char zero = 0;
    copy_user(process, process->read_address + count, &zero, 1, 1);
    process->frame.rax = count;
    process->state = READY;
}

unsigned int app_drain_output(unsigned int slot, unsigned char *bytes, unsigned int capacity)
{
    struct process *process = &processes[slot];
    if (process->state != WAITING_OUTPUT || !capacity)
        return 0;
    const unsigned int remaining = process->frame.rsi - process->output_offset;
    const unsigned int count = remaining < capacity ? remaining : capacity;
    copy_user(process, process->frame.rdi + process->output_offset, bytes, count, 0);
    process->output_offset += count;
    if (count == remaining) {
        process->frame.rax = process->frame.rsi;
        process->state = READY;
    }
    return count;
}

void app_cancel(unsigned int slot)
{
    struct process *process = &processes[slot];
    if (process->state == READY || process->state == WAITING || process->state == WAITING_OUTPUT ||
        process->state == WAITING_CLICK) {
        process->result = (struct app_result){ .status = APP_INTERRUPTED };
        process->state = FINISHED;
        release_resources(process);
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
    processes[slot].pid = 0;
}

void app_handle_exception(unsigned long long cs, unsigned long long vector,
    unsigned long long rip, unsigned long long fault_address)
{
    /* CPU-wide failures retain the kernel's diagnostic path. */
    if (running && (cs & 3) == 3 && vector != 2 && vector != 8 && vector != 18)
        finish((struct app_result){ .status = APP_FAULTED,
            .vector = vector, .rip = rip, .fault_address = fault_address });
}
