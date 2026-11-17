#include "fat_storage.h"
#include "../apps/app.h"

static unsigned int length(const char *text)
{
    unsigned int size = 0;
    while (text[size])
        ++size;
    return size;
}

static int exact(const char *a, const char *b)
{
    while (*a && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b;
}

static enum pkg_status edit_status(enum fat16_edit_result status)
{
    switch (status) {
    case FAT16_EDIT_OK: case FAT16_EDIT_CLEANUP: return PKG_OK;
    case FAT16_EDIT_FULL: case FAT16_EDIT_SIZE: return PKG_FULL;
    case FAT16_EDIT_IO: case FAT16_EDIT_PARTIAL: return PKG_IO;
    default: return PKG_CONFLICT;
    }
}

static enum pkg_status find(struct pkg_fat *fat, unsigned int directory, const char *name,
    struct fat16_file *file)
{
    struct fat16_dir scan = { .first_cluster = directory };
    int next;
    while ((next = fat16_next_at(&fat->volume, &scan, file)) > 0)
        if (pkg_alias_equal(name, file->name))
            return exact(name, file->name) ? PKG_OK : PKG_CONFLICT;
    return next < 0 ? PKG_IO : PKG_MISSING;
}

enum pkg_status pkg_fat_open(struct pkg_fat *fat)
{
    struct fat16_file apps;
    if (!ata_identify(&fat->disk) || !fat16_mount(&fat->volume, &fat->disk))
        return PKG_IO;
    const enum pkg_status status = find(fat, 0, "APPS", &apps);
    if (status != PKG_OK)
        return status;
    if (!apps.is_directory || !apps.first_cluster)
        return PKG_CONFLICT;
    fat->apps = apps.first_cluster;
    return PKG_OK;
}

static enum pkg_status folder(void *context, const char *id, int create, unsigned int *directory)
{
    struct pkg_fat *fat = context;
    struct fat16_file file;
    if (!pkg_valid_id(id))
        return PKG_FORMAT;
    enum pkg_status status = find(fat, fat->apps, id, &file);
    if (create) {
        if (status != PKG_MISSING)
            return status == PKG_IO ? status : PKG_CONFLICT;
        status = edit_status(fat16_mkdir(&fat->volume, fat->apps, (const unsigned char *)id, length(id)));
        if (status == PKG_OK)
            status = find(fat, fat->apps, id, &file);
    }
    if (status != PKG_OK)
        return status;
    if (!file.is_directory || !file.first_cluster)
        return PKG_CONFLICT;
    *directory = file.first_cluster;
    return PKG_OK;
}

static enum pkg_status lookup(void *context, unsigned int directory, const char *name,
    struct pkg_file *file)
{
    struct pkg_fat *fat = context;
    struct fat16_file found;
    const enum pkg_status status = find(fat, directory, name, &found);
    if (status != PKG_OK)
        return status;
    if (found.is_directory)
        return PKG_CONFLICT;
    fat->cursor = (struct fat16_read_cursor){0};
    *file = (struct pkg_file){ found.size, found.first_cluster };
    return PKG_OK;
}

static int read_file(void *context, const struct pkg_file *file, unsigned int offset,
    unsigned char *out, unsigned int size)
{
    struct pkg_fat *fat = context;
    const struct fat16_file entry = { .size = file->size, .first_cluster = file->location };
    return fat16_read_range_cursor(&fat->volume, &entry, &fat->cursor, offset, out, size);
}

struct sector_reader { const struct pkg_source *source; unsigned int offset; };

static int next_sector(void *context, unsigned char sector[512], unsigned int size)
{
    struct sector_reader *reader = context;
    if (!reader->source->read(reader->source->context, reader->offset, sector, size))
        return 0;
    reader->offset += size;
    return 1;
}

static enum pkg_status write_file(void *context, unsigned int directory, const char *name,
    const struct pkg_source *source, int exclusive)
{
    struct pkg_fat *fat = context;
    struct sector_reader reader = { .source = source };
    const unsigned char *path = (const unsigned char *)name;
    return edit_status(exclusive ?
        fat16_write_new_stream_at(&fat->volume, directory, path, length(name), source->size, next_sector, &reader) :
        fat16_write_stream_at(&fat->volume, directory, path, length(name), source->size, next_sector, &reader));
}

static enum pkg_status remove_file(void *context, unsigned int directory, const char *name)
{
    struct pkg_fat *fat = context;
    return edit_status(fat16_remove_at(&fat->volume, directory, (const unsigned char *)name, length(name)));
}

static int space(void *context, unsigned int *available, unsigned int *block)
{
    struct pkg_fat *fat = context;
    unsigned int total;
    *block = fat->volume.sectors_per_cluster * ATA_SECTOR_SIZE;
    return fat16_space(&fat->volume, &total, available);
}

static int running(void *context, unsigned int directory)
{
    struct pkg_fat *fat = context;
    return app_directory_running(&fat->volume, directory);
}

struct pkg_storage pkg_fat_storage(struct pkg_fat *fat)
{
    return (struct pkg_storage){ fat, folder, lookup, read_file, write_file, remove_file, space, running };
}

static int source_read(void *context, unsigned int offset, unsigned char *out, unsigned int size)
{
    struct pkg_fat_source *source = context;
    return fat16_read_range_cursor(source->volume, &source->file, &source->cursor, offset, out, size);
}

struct pkg_source pkg_fat_reader(struct pkg_fat_source *source)
{
    return (struct pkg_source){ source, source->file.size, source_read };
}
