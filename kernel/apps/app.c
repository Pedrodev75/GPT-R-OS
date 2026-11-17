#include "app.h"
#include "app_files.h"
#include "../desktop/app_canvas.h"
#include "../memory/pages.h"
#include "../drivers/timer.h"

enum {
    APP_HEAP_PAGES = 64, APP_V11_STACK_PAGES = 4,
    APP_SEGMENT_PAGES = APP_V11_IMAGE_LIMIT / PAGE_SIZE,
    USER_WINDOW_PAGES = (APP_V11_HEAP_BASE - APP_CODE_BASE) / PAGE_SIZE + APP_HEAP_PAGES,
    USER_PDPT_SLOT = APP_CODE_BASE >> 30,
    PRESENT = 1, WRITABLE = 2, USER = 4
};

#define APP_V1_STACK_BASE (APP_CODE_BASE + 2 * PAGE_SIZE)
#define APP_V1_HEAP_BASE (APP_CODE_BASE + 4 * PAGE_SIZE)
#define PAGE_NX (1ull << 63)

_Static_assert(USER_WINDOW_PAGES < 512, "User layout exceeds the page table");

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

struct image_pages {
    unsigned char *code[APP_SEGMENT_PAGES], *data[APP_SEGMENT_PAGES], *stack[APP_V11_STACK_PAGES];
    unsigned int code_count, data_count, stack_count;
};

struct process {
    struct app_frame frame;
    unsigned char fpu[512] __attribute__((aligned(16)));
    enum process_state state;
    unsigned int pid;
    unsigned int started_at, stopped_at, cpu_ticks;
    unsigned int executable_directory, executable_volume;
    struct app_info info;
    struct image_pages image;
    unsigned char *heap_pages[APP_HEAP_PAGES];
    unsigned char heap_runs[APP_HEAP_PAGES];
    unsigned int read_capacity;
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
    return PAGE_NX;
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

static unsigned long long stack_base(const struct app_info *info)
{
    return info->version == APP_V11_VERSION ? APP_V11_STACK_BASE : APP_V1_STACK_BASE;
}

static unsigned long long heap_base(const struct process *process)
{
    return process->info.version == APP_V11_VERSION ? APP_V11_HEAP_BASE : APP_V1_HEAP_BASE;
}

static int allocate_pages(unsigned char **pages, unsigned int *count, unsigned int bytes)
{
    const unsigned int needed = (bytes + PAGE_SIZE - 1) / PAGE_SIZE;
    while (*count < needed) {
        unsigned char *page = page_alloc();
        if (!page)
            return 0;
        for (unsigned int i = 0; i < PAGE_SIZE; ++i)
            page[i] = 0;
        pages[(*count)++] = page;
    }
    return 1;
}

static void release_pages(unsigned char **pages, unsigned int *count)
{
    while (*count) {
        const unsigned int index = --*count;
        page_free(pages[index]);
        pages[index] = 0;
    }
}

static void free_image(struct image_pages *image)
{
    release_pages(image->stack, &image->stack_count);
    release_pages(image->data, &image->data_count);
    release_pages(image->code, &image->code_count);
}

struct image_reader {
    unsigned char header[APP_V11_HEADER_SIZE];
    unsigned int position, file_size;
    struct app_info info;
    struct image_pages *image;
    enum app_status status;
};

static void copy_image_bytes(struct image_reader *reader, const unsigned char *in, unsigned int size)
{
    unsigned int at = reader->position - reader->info.header_size;
    while (size) {
        const int code = at < reader->info.code_size;
        const unsigned int offset = code ? at : at - reader->info.code_size;
        unsigned char **pages = code ? reader->image->code : reader->image->data;
        const unsigned int remaining = (code ? reader->info.code_size : reader->info.data_size) - offset;
        const unsigned int room = PAGE_SIZE - offset % PAGE_SIZE;
        unsigned int count = size < room ? size : room;
        if (count > remaining)
            count = remaining;
        unsigned char *out = pages[offset / PAGE_SIZE] + offset % PAGE_SIZE;
        for (unsigned int i = 0; i < count; ++i)
            out[i] = in[i];
        in += count;
        at += count;
        size -= count;
    }
}

static int collect_image(void *context, const unsigned char *data, unsigned int size)
{
    struct image_reader *reader = context;
    if (size > reader->file_size - reader->position) {
        reader->status = APP_INVALID;
        return 0;
    }
    while (size && reader->status != APP_STARTED) {
        reader->header[reader->position++] = *data++;
        --size;
        if (reader->position < APP_HEADER_PREFIX)
            continue;
        const int header_size = app_header_size(reader->header, reader->position);
        if (header_size <= 0 || (unsigned int)header_size >= reader->file_size) {
            reader->status = header_size == -2 ? APP_BAD_VERSION : APP_INVALID;
            return 0;
        }
        if (reader->position < (unsigned int)header_size)
            continue;
        if (!app_validate_header(reader->header, reader->position, reader->file_size, &reader->info))
            return 0;
        if (reader->image) {
            const struct app_info *info = &reader->info;
            struct image_pages *image = reader->image;
            if (info->version == APP_V11_VERSION && !(stack_page_flags & PAGE_NX)) {
                reader->status = APP_NO_NX;
                return 0;
            }
            if (!allocate_pages(image->code, &image->code_count, info->code_size) ||
                !allocate_pages(image->data, &image->data_count, info->data_size + info->bss_size)) {
                reader->status = APP_NO_MEMORY;
                return 0;
            }
        }
        reader->status = APP_STARTED;
    }
    if (size && reader->image)
        copy_image_bytes(reader, data, size);
    reader->position += size;
    return 1;
}

static enum app_status read_image(const struct fat16_volume *volume, const struct fat16_file *file,
    struct image_pages *image, struct app_info *info)
{
    if (file->size <= APP_HEADER_SIZE || file->size > APP_FILE_MAX)
        return APP_INVALID;
    struct image_reader reader = { .image = image, .file_size = file->size, .status = APP_INVALID };
    const enum fat16_read_result status = fat16_read(volume, file, collect_image, &reader);
    if (status == FAT16_READ_ERROR)
        return APP_READ_ERROR;
    if (reader.status != APP_STARTED)
        return reader.status;
    if (status != FAT16_READ_OK || reader.position != file->size)
        return APP_INVALID;
    *info = reader.info;
    return APP_STARTED;
}

int app_inspect(const struct fat16_volume *volume, const struct fat16_file *file,
    struct app_info *info)
{
    const enum app_status result = read_image(volume, file, 0, info);
    return result == APP_STARTED ? 1 : result == APP_READ_ERROR ? -1 :
        result == APP_BAD_VERSION ? -2 : 0;
}

static void flush_tlb(void)
{
    unsigned long long root;
    __asm__ volatile ("mov %%cr3, %0; mov %0, %%cr3" : "=&r"(root) : : "memory");
}

static unsigned long long build_user_stack(unsigned char *stack,
    unsigned long long last_page, const struct app_launch *launch)
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
        frame[i + 1] = last_page + strings_at + copied;
        unsigned char ch;
        do {
            if (copied == launch->args_size)
                return 0;
            ch = launch->args[copied];
            stack[strings_at + copied++] = ch;
        } while (ch);
    }
    frame[launch->argc + 1] = 0;
    return copied == launch->args_size ? last_page + frame_at : 0;
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
    free_image(&process->image);
}

static void map_pages(unsigned long long base, unsigned char *const *pages,
    unsigned int count, unsigned long long flags)
{
    const unsigned int first = (base - APP_CODE_BASE) / PAGE_SIZE;
    for (unsigned int i = 0; i < count; ++i)
        table[first + i] = pages[i] ? (unsigned long long)pages[i] | flags : 0;
}

static void dispatch(struct process *process)
{
    volatile unsigned long long *const pml4 = (void *)0x1000;
    volatile unsigned long long *const pdpt = (void *)0x2000;
    unsigned long long flags;
    __asm__ volatile ("pushfq; pop %0; cli" : "=r"(flags) : : "memory");

    /* Only this process's leaf mappings are user-accessible. */
    for (unsigned int i = 0; i < USER_WINDOW_PAGES; ++i)
        table[i] = 0;
    const struct image_pages *image = &process->image;
    map_pages(APP_CODE_BASE, image->code, image->code_count, PRESENT | USER);
    map_pages(APP_V11_DATA_BASE, image->data, image->data_count, stack_page_flags);
    map_pages(stack_base(&process->info), image->stack, image->stack_count, stack_page_flags);
    map_pages(heap_base(process), process->heap_pages, APP_HEAP_PAGES, stack_page_flags);
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
    unsigned int pages = process->image.code_count + process->image.data_count + process->image.stack_count;
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
    struct image_pages image = {0};
    struct app_info info;
    enum app_status result = read_image(volume, file, &image, &info);
    if (result != APP_STARTED)
        goto fail;
    const unsigned int stack_pages = info.version == APP_V11_VERSION ? APP_V11_STACK_PAGES : 1;
    if (!allocate_pages(image.stack, &image.stack_count, stack_pages * PAGE_SIZE)) {
        result = APP_NO_MEMORY;
        goto fail;
    }
    const unsigned long long last_page = stack_base(&info) + (stack_pages - 1) * PAGE_SIZE;
    const unsigned long long rsp = build_user_stack(image.stack[stack_pages - 1], last_page, launch);
    if (!rsp) {
        result = APP_BAD_ARGS;
        goto fail;
    }
    *process = (struct process){
        .frame = { .rip = APP_CODE_BASE + info.entry, .cs = 0x23,
            .flags = 0x202, .rsp = rsp, .ss = 0x1b },
        .state = READY, .pid = allocate_pid(), .image = image, .info = info,
        .started_at = timer_ticks(), .executable_directory = launch->executable_directory,
        .executable_volume = volume->fat_start, .headless = slot >= APP_PROCESS_LIMIT,
        .output = launch->output, .context = launch->context
    };
    app_files_begin(slot, volume, launch->directory);
    /* FXSAVE layout: x87 control word 0x037f, MXCSR 0x1f80. */
    process->fpu[0] = 0x7f;
    process->fpu[1] = 0x03;
    process->fpu[24] = 0x80;
    process->fpu[25] = 0x1f;
    return APP_STARTED;

fail:
    free_image(&image);
    return result;
}

int app_ready(void)
{
    for (unsigned int i = 0; i < APP_PROCESS_SLOTS; ++i)
        if (processes[i].state == READY)
            return 1;
    return 0;
}

int app_directory_running(const struct fat16_volume *volume, unsigned int directory)
{
    for (unsigned int i = 0; i < APP_PROCESS_SLOTS; ++i) {
        const struct process *process = &processes[i];
        if (process->state != UNUSED && process->state != FINISHED &&
            process->executable_volume == volume->fat_start && process->executable_directory == directory)
            return 1;
    }
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
    const struct app_info *info = &process->info;
    const unsigned int data_size = info->data_size + info->bss_size;
    if (contains(address, length, stack_base(info), process->image.stack_count * PAGE_SIZE) ||
        (!writable && contains(address, length, APP_CODE_BASE, info->code_size)) ||
        (data_size && contains(address, length, APP_V11_DATA_BASE, data_size)))
        return 1;
    const unsigned long long heap = heap_base(process);
    if (!contains(address, length, heap, APP_HEAP_PAGES * PAGE_SIZE) ||
        address == heap + APP_HEAP_PAGES * PAGE_SIZE)
        return 0;
    unsigned int start = (address - heap) / PAGE_SIZE;
    while (start && !process->heap_runs[start])
        --start;
    return process->heap_runs[start] &&
        contains(address, length, heap + start * PAGE_SIZE, process->heap_runs[start] * PAGE_SIZE);
}

static unsigned char *user_page(const struct process *process, unsigned long long address)
{
    const unsigned long long stack = stack_base(&process->info);
    const unsigned long long heap = heap_base(process);
    if (address >= heap)
        return process->heap_pages[(address - heap) / PAGE_SIZE];
    if (address >= stack)
        return process->image.stack[(address - stack) / PAGE_SIZE];
    if (address >= APP_V11_DATA_BASE)
        return process->image.data[(address - APP_V11_DATA_BASE) / PAGE_SIZE];
    return process->image.code[(address - APP_CODE_BASE) / PAGE_SIZE];
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
    const unsigned long long start = heap_base(process) + first * PAGE_SIZE;
    map_pages(start, process->heap_pages + first, count, stack_page_flags);
    flush_tlb();
    return start;
}

static long long free_user(struct process *process, unsigned long long address)
{
    const unsigned long long heap = heap_base(process);
    if (address < heap || address >= heap + APP_HEAP_PAGES * PAGE_SIZE ||
        address % PAGE_SIZE)
        return APP_ERR_INVALID;
    const unsigned int first = (address - heap) / PAGE_SIZE;
    const unsigned int count = process->heap_runs[first];
    if (!count)
        return APP_ERR_INVALID;
    process->heap_runs[first] = 0;
    for (unsigned int i = first; i < first + count; ++i)
        table[(heap - APP_CODE_BASE) / PAGE_SIZE + i] = 0;
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
