#include "app_files.h"
#include "app.h"
#include "assembler.h"
#include "../memory/pages.h"

enum { BUFFER_PAGES = APP_FILE_LIMIT / PAGE_SIZE, HANDLE_BASE = 3 };

struct handle {
    struct fat16_location entry;
    struct fat16_file original;
    unsigned int offset, seen;
    unsigned char *pages[BUFFER_PAGES];
    int used, output, existed;
};

struct file_table {
    struct ata_disk disk;
    struct fat16_volume volume;
    struct fat16_path cwd;
    struct handle handles[APP_HANDLE_LIMIT];
};

static struct file_table tables[APP_PROCESS_SLOTS];

void app_files_begin(unsigned int slot, const struct fat16_volume *volume,
    const struct fat16_path *directory)
{
    struct file_table *table = &tables[slot];
    table->disk = *volume->disk;
    table->volume = *volume;
    table->volume.disk = &table->disk;
    table->cwd = directory ? *directory : (struct fat16_path){0};
    for (unsigned int i = 0; i < APP_HANDLE_LIMIT; ++i)
        table->handles[i] = (struct handle){0};
}

static unsigned int name_length(const unsigned char *name)
{
    unsigned int length = 0;
    while (name[length])
        ++length;
    return length;
}

static unsigned char upper(unsigned char ch)
{
    return ch >= 'a' && ch <= 'z' ? ch - ('a' - 'A') : ch;
}

static int equal_alias(const unsigned char *a, const unsigned char *b)
{
    while (*a && upper(*a) == upper(*b)) {
        ++a;
        ++b;
    }
    return *a == *b;
}

int app_file_in_use(const struct fat16_volume *volume, const struct fat16_location *entry, int writers_only)
{
    for (unsigned int slot = 0; slot < APP_PROCESS_SLOTS; ++slot) {
        if (volume->fat_start != tables[slot].volume.fat_start)
            continue;
        for (unsigned int i = 0; i < APP_HANDLE_LIMIT; ++i) {
            const struct handle *other = &tables[slot].handles[i];
            if (other->used && (!writers_only || other->output) && other->entry.directory == entry->directory &&
                equal_alias(other->entry.name, entry->name))
                return 1;
        }
    }
    return 0;
}

static int storage_error(enum fat16_edit_result status)
{
    switch (status) {
    case FAT16_EDIT_OK: case FAT16_EDIT_CLEANUP:
        return 0;
    case FAT16_EDIT_FULL: case FAT16_EDIT_SIZE:
        return APP_ERR_LIMIT;
    case FAT16_EDIT_IO: case FAT16_EDIT_UNCERTAIN:
        return APP_ERR_IO;
    case FAT16_EDIT_NAME: case FAT16_EDIT_TEXT_EXTENSION:
        return APP_ERR_INVALID;
    default:
        return APP_ERR_ACCESS;
    }
}

int app_file_open(unsigned int slot, const unsigned char *path, unsigned int mode)
{
    struct file_table *table = &tables[slot];
    if (!*path || mode > APP_FILE_WRITE_MODE)
        return APP_ERR_INVALID;
    unsigned int index = 0;
    while (index < APP_HANDLE_LIMIT && table->handles[index].used)
        ++index;
    if (index == APP_HANDLE_LIMIT)
        return APP_ERR_LIMIT;

    struct handle next = {0};
    const int parent = fat16_resolve_parent(&table->volume, &table->cwd, path, &next.entry);
    if (parent < 0)
        return APP_ERR_IO;
    if (!parent)
        return APP_ERR_MISSING;
    const int found = fat16_find_at(&table->volume, next.entry.directory, next.entry.name, &next.original);
    if (found < 0)
        return APP_ERR_IO;
    if (found && next.original.is_directory)
        return APP_ERR_ACCESS;
    next.output = mode == APP_FILE_WRITE_MODE;
    if (next.output) {
        if (app_file_in_use(&table->volume, &next.entry, 1))
            return APP_ERR_ACCESS;
        const int error = storage_error(fat16_check_write(&table->volume,
            next.entry.directory, next.entry.name, name_length(next.entry.name)));
        if (error)
            return error;
    } else if (!found) {
        return APP_ERR_MISSING;
    }

    next.used = 1;
    next.existed = found;
    next.seen = fat16_mutations();
    table->handles[index] = next;
    return HANDLE_BASE + index;
}

int app_file_assemble(unsigned int slot, const unsigned char *source, const unsigned char *target,
    unsigned int version, struct asm_diagnostic *diagnostic)
{
    const struct file_table *table = &tables[slot];
    return app_assemble_at(&table->volume, &table->cwd, source, target, version, diagnostic);
}

int app_file_assemble_buffer(unsigned int slot, const struct asm_io *source, unsigned int size,
    const unsigned char *target, unsigned int version, struct asm_diagnostic *diagnostic)
{
    const struct file_table *table = &tables[slot];
    return app_assemble_buffer_at(&table->volume, &table->cwd, source, size,
        target, version, diagnostic);
}

static struct handle *find_handle(struct file_table *table, unsigned int number)
{
    if (number < HANDLE_BASE || number - HANDLE_BASE >= APP_HANDLE_LIMIT)
        return 0;
    struct handle *handle = &table->handles[number - HANDLE_BASE];
    return handle->used ? handle : 0;
}

static int still_current(const struct file_table *table, struct handle *handle)
{
    if (fat16_file_changed(handle->entry.directory, handle->entry.name, handle->seen))
        return APP_ERR_CHANGED;
    struct fat16_file latest;
    const int found = fat16_find_at(&table->volume, handle->entry.directory,
        handle->entry.name, &latest);
    if (found < 0)
        return APP_ERR_IO;
    if (found != handle->existed || (found &&
        (latest.is_directory || latest.first_cluster != handle->original.first_cluster ||
         latest.size != handle->original.size || latest.modified_date != handle->original.modified_date)))
        return APP_ERR_CHANGED;
    handle->seen = fat16_mutations();
    return 0;
}

int app_file_read(unsigned int slot, unsigned int number, unsigned char *data, unsigned int size)
{
    struct file_table *table = &tables[slot];
    struct handle *handle = find_handle(table, number);
    if (!handle)
        return APP_ERR_INVALID;
    if (handle->output)
        return APP_ERR_ACCESS;
    const int status = still_current(table, handle);
    if (status)
        return status;
    const unsigned int remaining = handle->original.size - handle->offset;
    const unsigned int count = size < remaining ? size : remaining;
    if (!fat16_read_range(&table->volume, &handle->original, handle->offset, data, count))
        return APP_ERR_IO;
    handle->offset += count;
    return count;
}

int app_file_write(unsigned int slot, unsigned int number, const unsigned char *data, unsigned int size)
{
    struct handle *handle = find_handle(&tables[slot], number);
    if (!handle)
        return APP_ERR_INVALID;
    if (!handle->output)
        return APP_ERR_ACCESS;
    if (!size)
        return 0;
    if (size > APP_FILE_LIMIT - handle->offset)
        return APP_ERR_LIMIT;

    const unsigned int first = (handle->offset + PAGE_SIZE - 1) / PAGE_SIZE;
    const unsigned int end = (handle->offset + size + PAGE_SIZE - 1) / PAGE_SIZE;
    unsigned int page = first;
    for (; page < end; ++page) {
        handle->pages[page] = page_alloc();
        if (!handle->pages[page]) {
            for (unsigned int i = first; i < page; ++i) {
                page_free(handle->pages[i]);
                handle->pages[i] = 0;
            }
            return APP_ERR_MEMORY;
        }
    }
    for (unsigned int i = 0; i < size; ++i) {
        const unsigned int at = handle->offset + i;
        handle->pages[at / PAGE_SIZE][at % PAGE_SIZE] = data[i];
    }
    handle->offset += size;
    return size;
}

struct output_reader { const struct handle *handle; unsigned int position; };

static int output_sector(void *context, unsigned char sector[512], unsigned int size)
{
    struct output_reader *read = context;
    for (unsigned int i = 0; i < size; ++i) {
        const unsigned int at = read->position++;
        sector[i] = read->handle->pages[at / PAGE_SIZE][at % PAGE_SIZE];
    }
    return 1;
}

int app_file_close(unsigned int slot, unsigned int number, int commit)
{
    struct file_table *table = &tables[slot];
    struct handle *handle = find_handle(table, number);
    if (!handle)
        return APP_ERR_INVALID;
    int result = 0;
    if (commit && handle->output) {
        result = still_current(table, handle);
        if (!result) {
            struct output_reader reader = {handle, 0};
            const struct fat16_location *entry = &handle->entry;
            result = storage_error(fat16_write_stream_at(&table->volume, entry->directory,
                entry->name, name_length(entry->name), handle->offset, output_sector, &reader));
        }
    }
    for (unsigned int i = 0; i < BUFFER_PAGES; ++i)
        if (handle->pages[i])
            page_free(handle->pages[i]);
    *handle = (struct handle){0};
    return result;
}

int app_files_finish(unsigned int slot, int commit)
{
    int result = 0;
    for (unsigned int i = 0; i < APP_HANDLE_LIMIT; ++i) {
        if (tables[slot].handles[i].used) {
            const int status = app_file_close(slot, HANDLE_BASE + i, commit);
            if (status && !result)
                result = status;
        }
    }
    return result;
}

unsigned int app_files_pages(unsigned int slot)
{
    unsigned int count = 0;
    for (unsigned int i = 0; i < APP_HANDLE_LIMIT; ++i)
        for (unsigned int page = 0; page < BUFFER_PAGES; ++page)
            count += tables[slot].handles[i].pages[page] != 0;
    return count;
}
