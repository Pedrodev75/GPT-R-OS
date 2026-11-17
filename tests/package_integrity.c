#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../kernel/packages/package.h"

#define MAIN_FILE "VELHA.APP"
#define DATA_FILE "data.txt"
#define RECEIPT "PKG.DAT"

struct member {
    char name[13];
    unsigned int size;
    unsigned char data[4200];
};

struct storage {
    struct member files[5];
    const char *fail_write;
    unsigned int available, disk_writes;
    int exists, running, fail_remove;
};

static unsigned char archive[PKG_SIZE_LIMIT];

static int read_archive(void *context, unsigned int offset, unsigned char *out, unsigned int size)
{
    memcpy(out, (const unsigned char *)context + offset, size);
    return 1;
}

static enum pkg_status folder(void *context, const char *id, int create, unsigned int *directory)
{
    struct storage *disk = context;
    assert(!strcmp(id, "VELHA"));
    if (create) {
        assert(!disk->exists);
        disk->exists = 1;
        ++disk->disk_writes;
    }
    *directory = 5;
    return disk->exists ? PKG_OK : PKG_MISSING;
}

static enum pkg_status lookup(void *context, unsigned int directory, const char *name,
    struct pkg_file *out)
{
    struct storage *disk = context;
    assert(directory == 5);
    for (unsigned int i = 0; i < sizeof(disk->files) / sizeof(disk->files[0]); ++i) {
        if (!disk->files[i].name[0] || !pkg_alias_equal(name, disk->files[i].name))
            continue;
        if (strcmp(name, disk->files[i].name))
            return PKG_CONFLICT;
        *out = (struct pkg_file){ disk->files[i].size, i };
        return PKG_OK;
    }
    return PKG_MISSING;
}

static int read_member(void *context, const struct pkg_file *file, unsigned int offset,
    unsigned char *out, unsigned int size)
{
    const struct storage *disk = context;
    const struct member *member = &disk->files[file->location];
    if (offset > member->size || size > member->size - offset)
        return 0;
    memcpy(out, member->data + offset, size);
    return 1;
}

static enum pkg_status write_member(void *context, unsigned int directory, const char *name,
    const struct pkg_source *source, int exclusive)
{
    struct storage *disk = context;
    if (disk->fail_write && !strcmp(disk->fail_write, name))
        return PKG_IO;
    struct pkg_file file;
    const enum pkg_status status = lookup(disk, directory, name, &file);
    if (status == PKG_CONFLICT || (status == PKG_OK && exclusive))
        return PKG_CONFLICT;
    struct member *member = status == PKG_OK ? &disk->files[file.location] : 0;
    for (unsigned int i = 0; !member && i < sizeof(disk->files) / sizeof(disk->files[0]); ++i)
        if (!disk->files[i].name[0])
            member = &disk->files[i];
    if (!member || source->size > sizeof(member->data))
        return PKG_FULL;
    if (!source->read(source->context, 0, member->data, source->size))
        return PKG_IO;
    strcpy(member->name, name);
    member->size = source->size;
    ++disk->disk_writes;
    return PKG_OK;
}

static enum pkg_status remove_member(void *context, unsigned int directory, const char *name)
{
    struct storage *disk = context;
    struct pkg_file file;
    if (disk->fail_remove)
        return PKG_IO;
    const enum pkg_status status = lookup(disk, directory, name, &file);
    if (status == PKG_OK)
        disk->files[file.location].name[0] = 0;
    return status;
}

static int space(void *context, unsigned int *available, unsigned int *block)
{
    *available = ((struct storage *)context)->available;
    *block = 512;
    return 1;
}

static int running(void *context, unsigned int directory)
{
    assert(directory == 5);
    return ((struct storage *)context)->running;
}

static void record_is(const struct pkg_storage *disk, enum pkg_state expected)
{
    struct pkg_manifest manifest;
    enum pkg_state state;
    assert(pkg_load(disk, 5, "VELHA", &manifest, &state) == PKG_OK);
    assert(state == expected);
}

static void invalid_archives(struct pkg_source source)
{
    struct pkg_manifest manifest;
    assert(pkg_validate(&source, &manifest) == PKG_OK);
    assert(manifest.count == 2 && !strcmp(manifest.files[0].name, MAIN_FILE));
    const unsigned int table = manifest.metadata_size - 2 * PKG_ENTRY_SIZE;
    const unsigned int original_size = source.size;
    source.size = 15;
    assert(pkg_validate(&source, &manifest) == PKG_FORMAT);
    source.size = original_size - 1;
    assert(pkg_validate(&source, &manifest) == PKG_FORMAT);
    source.size = original_size;
    archive[4] = 2;
    assert(pkg_validate(&source, &manifest) == PKG_VERSION);
    archive[4] = 1;
    archive[12] = PKG_FILES + 1;
    assert(pkg_validate(&source, &manifest) == PKG_FORMAT);
    archive[12] = 2;
    const unsigned char initial = archive[table];
    archive[table] = '/';
    assert(pkg_validate(&source, &manifest) == PKG_FORMAT);
    archive[table] = initial;
    unsigned char saved[12];
    memcpy(saved, archive + table + PKG_ENTRY_SIZE, sizeof(saved));
    memcpy(archive + table + PKG_ENTRY_SIZE, archive + table, sizeof(saved));
    assert(pkg_validate(&source, &manifest) == PKG_FORMAT);
    memcpy(archive + table + PKG_ENTRY_SIZE, saved, sizeof(saved));
    archive[source.size - 1] ^= 1;
    assert(pkg_validate(&source, &manifest) == PKG_CRC);
    archive[source.size - 1] ^= 1;
    const unsigned char extension = archive[table + 6];
    archive[table + 6] = 'X';
    assert(pkg_validate(&source, &manifest) == PKG_APP);
    archive[table + 6] = extension;
    assert(pkg_validate(&source, &manifest) == PKG_OK);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    FILE *file = fopen(argv[1], "rb");
    assert(file);
    const unsigned int size = (unsigned int)fread(archive, 1, sizeof(archive), file);
    assert(!ferror(file) && fgetc(file) == EOF);
    fclose(file);
    const struct pkg_source source = { archive, size, read_archive };
    invalid_archives(source);

    struct storage volume = {0};
    const struct pkg_storage disk = { &volume, folder, lookup, read_member,
        write_member, remove_member, space, running };
    assert(pkg_install(&disk, &source) == PKG_FULL);
    assert(!volume.exists && !volume.disk_writes);
    volume.available = 128;
    volume.exists = 1;
    assert(pkg_install(&disk, &source) == PKG_CONFLICT);
    assert(!volume.disk_writes);
    volume.exists = 0;
    volume.fail_write = DATA_FILE;
    struct pkg_file meta;
    assert(pkg_install(&disk, &source) == PKG_IO);
    record_is(&disk, PKG_INACTIVE);
    assert(lookup(&volume, 5, MAIN_FILE, &meta) == PKG_MISSING);
    volume.fail_remove = 1;
    assert(pkg_install(&disk, &source) == PKG_PENDING);
    record_is(&disk, PKG_INSTALLING);
    assert(pkg_install(&disk, &source) == PKG_PENDING);
    assert(lookup(&volume, 5, MAIN_FILE, &meta) == PKG_OK);
    unsigned int preserved;
    volume.fail_remove = 0;
    assert(pkg_remove(&disk, 5, "VELHA", &preserved) == PKG_OK && !preserved);
    volume.fail_write = 0;
    assert(pkg_install(&disk, &source) == PKG_OK);
    record_is(&disk, PKG_INSTALLED);
    assert(pkg_install(&disk, &source) == PKG_DUPLICATE);
    volume.running = 1;
    assert(pkg_remove(&disk, 5, "VELHA", &preserved) == PKG_RUNNING);
    volume.running = 0;
    assert(lookup(&volume, 5, DATA_FILE, &meta) == PKG_OK);
    volume.files[meta.location].data[0] ^= 1;
    const unsigned char changed = volume.files[meta.location].data[0];
    const struct pkg_source unknown = { "documento", 9, read_archive };
    assert(write_member(&volume, 5, "user.txt", &unknown, 1) == PKG_OK);
    assert(pkg_remove(&disk, 5, "VELHA", &preserved) == PKG_OK && preserved == 1);
    record_is(&disk, PKG_INACTIVE);
    assert(lookup(&volume, 5, MAIN_FILE, &meta) == PKG_MISSING);
    assert(lookup(&volume, 5, DATA_FILE, &meta) == PKG_OK);
    assert(volume.files[meta.location].data[0] == changed);
    assert(lookup(&volume, 5, "user.txt", &meta) == PKG_OK);
    assert(pkg_install(&disk, &source) == PKG_CONFLICT);
    assert(remove_member(&volume, 5, DATA_FILE) == PKG_OK);
    assert(pkg_install(&disk, &source) == PKG_OK);
    record_is(&disk, PKG_INSTALLED);
    puts("GTRP v1 validation, lifecycle and recovery passed");
}
