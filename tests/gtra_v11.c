/* Host test. Only the loader, page bookkeeping and assembler run here. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../kernel/apps/app.c"
#include "../kernel/apps/assembler.h"

static unsigned char file_bytes[APP_FILE_MAX + 1];
static unsigned int allocated, left, fail_at, assembly_calls;
unsigned long long app_tss_descriptor[2];

void *page_alloc(void)
{
    if (!left)
        return 0;
    --left;
    void *page = malloc(PAGE_SIZE);
    assert(page);
    memset(page, 0xa5, PAGE_SIZE);
    ++allocated;
    return page;
}

int page_free(void *page)
{
    assert(page && allocated);
    free(page);
    --allocated;
    return 1;
}

enum fat16_read_result fat16_read(const struct fat16_volume *volume, const struct fat16_file *file,
    int (*consume)(void *, const unsigned char *, unsigned int), void *context)
{
    (void)volume;
    for (unsigned int at = 0; at < file->size;) {
        if (fail_at && at >= fail_at)
            return FAT16_READ_ERROR;
        const unsigned int remaining = file->size - at;
        const unsigned int count = remaining < 19 ? remaining : 19;
        if (!consume(context, file_bytes + at, count))
            return FAT16_READ_STOPPED;
        at += count;
    }
    return FAT16_READ_OK;
}

unsigned int timer_ticks(void) { return 123; }
void app_enter(const struct app_frame *frame) { (void)frame; abort(); }
_Noreturn void app_leave(void) { abort(); }

void app_files_begin(unsigned int slot, const struct fat16_volume *volume, const struct fat16_path *dir)
{
    (void)slot; (void)volume; (void)dir;
}

unsigned int app_files_pages(unsigned int slot) { (void)slot; return 0; }
int app_files_finish(unsigned int slot, int commit) { (void)slot; (void)commit; return 0; }

int app_file_open(unsigned int slot, const unsigned char *path, unsigned int mode)
{
    (void)slot; (void)path; (void)mode;
    abort();
}

int app_file_read(unsigned int slot, unsigned int handle, unsigned char *data, unsigned int size)
{
    (void)slot; (void)handle; (void)data; (void)size;
    abort();
}

int app_file_write(unsigned int slot, unsigned int handle, const unsigned char *data, unsigned int size)
{
    (void)slot; (void)handle; (void)data; (void)size;
    abort();
}

int app_file_close(unsigned int slot, unsigned int handle, int commit)
{
    (void)slot; (void)handle; (void)commit;
    abort();
}

int app_file_assemble(unsigned int slot, const unsigned char *source, const unsigned char *target,
    unsigned int version, struct asm_diagnostic *diagnostic)
{
    assert(slot == 0 && version == APP_V11_VERSION);
    assert(!strcmp((const char *)source, "SOURCE.ASM"));
    assert(!strcmp((const char *)target, "OUTPUT.APP"));
    ++assembly_calls;
    diagnostic->line = 13;
    return APP_ERR_SYNTAX;
}

static void header_validation(void)
{
    const struct app_info legacy = { APP_FORMAT_VERSION, APP_HEADER_SIZE, 4096, 0, 0, 10 };
    struct app_info read;
    app_make_header(file_bytes, &legacy);
    assert(app_header_size(file_bytes, 7) == 0);
    assert(app_validate_header(file_bytes, 32, 4128, &read));
    assert(read.version == 1 && read.entry == 10 && read.code_size == 4096);
    assert(!app_validate_header(file_bytes, 32, 4129, &read));
    const struct app_info extended = { APP_V11_VERSION, APP_V11_HEADER_SIZE, 4097, 8192, 53247, 4095 };
    app_make_header(file_bytes, &extended);
    const unsigned int size = 64 + 4097 + 8192;
    assert(app_validate_header(file_bytes, 64, size, &read));
    assert(read.code_size + read.data_size + read.bss_size == 65536);
    assert(!app_validate_header(file_bytes, 63, size, &read));
    assert(!app_validate_header(file_bytes, 64, size - 1, &read));
    assert(!app_validate_header(file_bytes, 64, size + 1, &read));
    const unsigned int fields[] = { 0, 6, 8, 12, 16, 20, 27, 31, 32, 63 };
    for (unsigned int i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i) {
        file_bytes[fields[i]] ^= 0x80;
        assert(!app_validate_header(file_bytes, 64, size, &read));
        file_bytes[fields[i]] ^= 0x80;
    }
    file_bytes[5] = 2;
    assert(app_header_size(file_bytes, 8) == -2);
}

static void assembly_arguments(struct process *process)
{
    unsigned char source[] = "SOURCE.ASM", target[] = "OUTPUT.APP";
    const unsigned long long from = APP_V11_DATA_BASE + PAGE_SIZE - 3;
    const unsigned long long to = APP_V11_STACK_BASE + PAGE_SIZE - 4;
    copy_user(process, from, source, sizeof(source), 1);
    copy_user(process, to, target, sizeof(target), 1);
    struct app_frame *frame = &process->frame;
    frame->rax = APP_SYS_ASSEMBLE;
    frame->rdi = from;
    frame->rsi = to;
    frame->rdx = APP_V11_VERSION;
    service_file(process);
    assert((long long)frame->rax == APP_ERR_SYNTAX && frame->rdx == 13 && assembly_calls == 1);

    const unsigned long long invalid[] = {0, APP_V11_STACK_BASE - 1, ~0ull};
    for (unsigned int i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        frame->rax = APP_SYS_ASSEMBLE;
        frame->rsi = invalid[i];
        frame->rdx = APP_V11_VERSION;
        service_file(process);
        assert((long long)frame->rax == APP_ERR_INVALID && frame->rdx == 0 && assembly_calls == 1);
    }
    frame->rax = APP_SYS_ASSEMBLE;
    frame->rsi = to;
    frame->rdx = 0x100000101ull;
    service_file(process);
    assert((long long)frame->rax == APP_ERR_INVALID && frame->rdx == 0 && assembly_calls == 1);
}

static void loading(void)
{
    const struct fat16_volume volume = {0};
    const struct app_launch launch = { .args = (const unsigned char *)"T.APP", .argc = 1, .args_size = 6 };
    const struct app_info wide = { APP_V11_VERSION, APP_V11_HEADER_SIZE, 4097, 8192, 53247, 4095 };
    struct fat16_file file = { .size = wide.header_size + wide.code_size + wide.data_size };
    app_make_header(file_bytes, &wide);
    for (unsigned int i = wide.header_size; i < file.size; ++i)
        file_bytes[i] = i % 251;

    left = 100;
    assert(app_start(0, &volume, &file, &launch) == APP_NO_NX && !allocated);
    stack_page_flags = PAGE_NX | PRESENT | WRITABLE | USER;
    --file.size;
    assert(app_start(0, &volume, &file, &launch) == APP_INVALID && !allocated);
    ++file.size;
    file_bytes[5] = 2;
    assert(app_start(0, &volume, &file, &launch) == APP_BAD_VERSION && !allocated);
    file_bytes[5] = 1;
    ++file_bytes[16];
    assert(app_start(0, &volume, &file, &launch) == APP_INVALID && !allocated);
    --file_bytes[16];
    for (unsigned int failure = 0; failure < 21; ++failure) {
        left = failure;
        assert(app_start(0, &volume, &file, &launch) == APP_NO_MEMORY);
        assert(!allocated && processes[0].state == UNUSED);
    }
    left = 100;
    fail_at = 3000;
    assert(app_start(0, &volume, &file, &launch) == APP_READ_ERROR && !allocated);
    fail_at = 0;
    const struct app_launch bad_args = {0};
    assert(app_start(0, &volume, &file, &bad_args) == APP_BAD_ARGS && !allocated);
    assert(app_start(0, &volume, &file, &launch) == APP_STARTED && allocated == 21);
    assert(app_task_info(0).memory_bytes == 21 * PAGE_SIZE);

    const struct process *p = &processes[0];
    unsigned char cross[2];
    assert(user_range(p, APP_CODE_BASE + 4095, 2, 0));
    assert(!user_range(p, APP_CODE_BASE + 4095, 2, 1));
    assert(!user_range(p, APP_CODE_BASE + 4095, 3, 0));
    copy_user(p, APP_CODE_BASE + 4095, cross, 2, 0);
    assert(!memcmp(cross, file_bytes + 64 + 4095, 2));
    assert(user_range(p, APP_V11_DATA_BASE + 4095, 2, 1));
    copy_user(p, APP_V11_DATA_BASE + 4095, cross, 2, 0);
    assert(!memcmp(cross, file_bytes + 64 + 4097 + 4095, 2));
    for (unsigned int i = 8192; i < 16 * PAGE_SIZE - PAGE_SIZE; ++i) {
        const unsigned long long address = APP_V11_DATA_BASE + i;
        assert(user_page(p, address)[i % PAGE_SIZE] == 0);
    }
    assert((p->frame.rsp & 15) == 0 && p->frame.rsp >= APP_V11_STACK_BASE);
    assert(!user_range(p, APP_V11_STACK_BASE - 1, 2, 1));
    assembly_arguments(&processes[0]);
    app_cancel(0);
    assert(!allocated && app_task_info(0).memory_bytes == 0);
    app_release(0);

    const struct app_info legacy = { APP_FORMAT_VERSION, APP_HEADER_SIZE, 4096, 0, 0, 0 };
    app_make_header(file_bytes, &legacy);
    file.size = 4128;
    assert(app_start(0, &volume, &file, &launch) == APP_STARTED && allocated == 2);
    assert(processes[0].frame.rsp >= APP_V1_STACK_BASE && processes[0].frame.rsp < APP_V1_STACK_BASE + PAGE_SIZE);
    assert(heap_base(&processes[0]) == APP_V1_HEAP_BASE);
    app_release(0);
    assert(!allocated);
}

struct asm_fixture {
    const char *source;
    unsigned char out[APP_V11_IMAGE_LIMIT];
};

static unsigned char read_source(void *context, unsigned int offset)
{
    return ((const struct asm_fixture *)context)->source[offset];
}

static int emit_byte(void *context, unsigned int offset, unsigned char value)
{
    struct asm_fixture *fixture = context;
    assert(offset < sizeof(fixture->out));
    fixture->out[offset] = value;
    return 1;
}

static void assembling(void)
{
    static struct asm_fixture fixture;
    static char source[ASM_V11_SOURCE_LIMIT + 1];
    const struct asm_io io = { &fixture, read_source, emit_byte };
    struct app_info info;
    struct asm_diagnostic diagnostic;
    left = 100;
    fixture.source = "entry begin\nbegin: mov eax, 2\nmov edi, 0\nint 0x80\n";
    assert(!app_assemble(&io, strlen(fixture.source), APP_FORMAT_VERSION, &info, &diagnostic));
    assert(info.code_size == 12 && info.entry == 0 && !allocated);
    assert(!memcmp(fixture.out, "\xb8\x02\0\0\0\xbf\0\0\0\0\xcd\x80", 12));

    fixture.source = ".section .bss\nzero: resb 256\n.section .data\nmsg: db 22, 23\n"
        ".section .text\nentry start\nstart: lea rdi, [rip + zero]\nret\n";
    assert(!app_assemble(&io, strlen(fixture.source), APP_V11_VERSION, &info, &diagnostic));
    assert(info.code_size == 8 && info.data_size == 2 && info.bss_size == 256 && !allocated);
    assert(fixture.out[0] == 0x48 && fixture.out[1] == 0x8d && fixture.out[2] == 0x3d);
    assert(fixture.out[3] == 0xfb && fixture.out[4] == 0x0f && fixture.out[5] == 1 && fixture.out[6] == 0);
    assert(fixture.out[8] == 22 && fixture.out[9] == 23);

    fixture.source = source;
    unsigned int length = 0;
    for (unsigned int i = 0; i < ASM_V11_LABEL_LIMIT; ++i)
        length += (unsigned int)snprintf(source + length, sizeof(source) - length, "label_%u: nop\n", i);
    assert(length > ASM_V1_SOURCE_LIMIT);
    assert(!app_assemble(&io, length, APP_V11_VERSION, &info, &diagnostic) && !allocated);
    length += (unsigned int)snprintf(source + length, sizeof(source) - length, "extra: ret\n");
    assert(app_assemble(&io, length, APP_V11_VERSION, &info, &diagnostic) && !allocated);

    fixture.source = ".section .text\nentry data\nret\n.section .data\ndata: db 1\n";
    assert(app_assemble(&io, strlen(fixture.source), APP_V11_VERSION, &info, &diagnostic) && !allocated);
    fixture.source = "ret\n.section .bss\nresb 65536\n";
    assert(app_assemble(&io, strlen(fixture.source), APP_V11_VERSION, &info, &diagnostic) && !allocated);
    fixture.source = "ret\n.section .bss\nresb 65535\n";
    assert(!app_assemble(&io, strlen(fixture.source), APP_V11_VERSION, &info, &diagnostic) && !allocated);
    assert(info.code_size == 1 && info.bss_size == 65535);
    memcpy(source, "ret\n", 4);
    memset(source + 4, ' ', ASM_V11_SOURCE_LIMIT - 4);
    fixture.source = source;
    assert(!app_assemble(&io, ASM_V11_SOURCE_LIMIT, APP_V11_VERSION, &info, &diagnostic) && !allocated);
    fixture.source = "ret\n";
    assert(app_assemble(&io, ASM_V11_SOURCE_LIMIT + 1, APP_V11_VERSION, &info, &diagnostic));
}

int main(void)
{
    header_validation();
    loading();
    assembling();
    puts("GTRA v1/v1.1 header, multi-page load, rollback and assembler passed");
    return 0;
}
