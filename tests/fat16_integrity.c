#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kernel/apps/app_abi.h"
#include "kernel/apps/app_files.h"
#include "kernel/apps/assembler.h"
#include "kernel/fs/fat16.h"
#include "kernel/memory/pages.h"

enum { FAT_SECTORS = 17, ROOT = 1 + 2 * FAT_SECTORS, DATA = ROOT + 2, CLUSTERS = 4096 };
static unsigned char medium[DATA + CLUSTERS][ATA_SECTOR_SIZE];
static struct ata_disk disk = {.sectors = DATA + CLUSTERS};
static struct fat16_volume volume = {&disk, 1, FAT_SECTORS, 2, ROOT, DATA, CLUSTERS, 32, 1};
static unsigned int fail_lba, read_fail_lba, outstanding;
static int fail, skip_failure, read_fail, publish_before_failure, page_budget = -1;

#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expr); exit(1); } } while (0)

int ata_read_sector(const struct ata_disk *device, unsigned int lba, unsigned char data[512])
{
    if (lba >= device->sectors)
        return 0;
    if (read_fail && lba == read_fail_lba) {
        read_fail = 0;
        return 0;
    }
    memcpy(data, medium[lba], 512);
    return 1;
}
int ata_write_sector(const struct ata_disk *device, unsigned int lba, const unsigned char data[512])
{
    if (lba >= device->sectors)
        return 0;
    if (fail && lba == fail_lba && !skip_failure) {
        if (publish_before_failure)
            memcpy(medium[lba], data, 512);
        publish_before_failure = 0;
        --fail;
        return 0;
    }
    if (fail && lba == fail_lba)
        --skip_failure;
    memcpy(medium[lba], data, 512);
    return 1;
}
void *page_alloc(void)
{
    if (!page_budget)
        return NULL;
    if (page_budget > 0)
        --page_budget;
    void *page = malloc(PAGE_SIZE);
    outstanding += page != NULL;
    return page;
}
int page_free(void *page)
{
    CHECK(page && outstanding);
    --outstanding;
    free(page);
    return 1;
}

static enum fat16_edit_result put(const char *name, const char *data)
{
    return fat16_write_at(&volume, 0, (const unsigned char *)name, (unsigned int)strlen(name),
        (const unsigned char *)data, (unsigned int)strlen(data));
}

static struct fat16_file find(const char *name)
{
    struct fat16_file f;
    CHECK(fat16_lookup_at(&volume, 0, (const unsigned char *)name, &f) == 1);
    return f;
}

static int content_is(const char *name, const char *expected)
{
    const struct fat16_file f = find(name);
    unsigned char bytes[40];
    return f.size == strlen(expected) && f.size <= sizeof(bytes) &&
        fat16_read_range(&volume, &f, 0, bytes, f.size) &&
        !memcmp(bytes, expected, f.size);
}

static void check_mirrors(void)
{
    CHECK(!memcmp(medium[1], medium[1 + FAT_SECTORS], FAT_SECTORS * 512));
}

static unsigned int next_free(unsigned int from)
{
    for (unsigned int cluster = from; cluster < CLUSTERS + 2; ++cluster) {
        const unsigned int lba = 1 + cluster / 256, at = cluster % 256 * 2;
        if (!(medium[lba][at] || medium[lba][at + 1]))
            return cluster;
    }
    CHECK(0);
    return 0;
}

static void check_app(const char *name, unsigned int version)
{
    const struct fat16_file file = find(name);
    struct app_info info;
    unsigned char header[APP_V11_HEADER_SIZE];
    const unsigned int size = version == APP_V11_VERSION ? sizeof(header) : APP_HEADER_SIZE;
    CHECK(file.size > size && fat16_read_range(&volume, &file, 0, header, size));
    CHECK(app_validate_header(header, size, file.size, &info) && info.version == version);
}

static int assemble(const char *source, const char *target, unsigned int version,
    struct asm_diagnostic *diag)
{
    return app_file_assemble(0, (const unsigned char *)source, (const unsigned char *)target, version, diag);
}

static void assembly_service(void)
{
    memset(medium, 0, sizeof(medium));
    static unsigned char snapshot[sizeof(medium)];
    struct asm_diagnostic diag;
    CHECK(fat16_mkdir(&volume, 0, (const unsigned char *)"WORK", 4) == FAT16_EDIT_OK);
    struct fat16_path cwd;
    const struct fat16_path root = {0};
    CHECK(fat16_resolve_dir(&volume, &root, (const unsigned char *)"/WORK", &cwd) == 1);
    app_files_begin(0, &volume, &cwd);
    app_files_begin(1, &volume, &root);
    CHECK(put("GOOD.ASM", "entry main\nmain: ret\n") == FAT16_EDIT_OK);
    CHECK(assemble("/GOOD.ASM", "LEGACY.APP", APP_FORMAT_VERSION, &diag) == 0 && !diag.line);
    struct fat16_file file;
    struct fat16_location where;
    CHECK(fat16_resolve_file(&volume, &cwd, (const unsigned char *)"LEGACY.APP", &where, &file) == 1);
    CHECK(file.size == 33 && where.directory == fat16_path_cluster(&cwd));
    CHECK(assemble("/GOOD.ASM", "/KEEP.APP", APP_V11_VERSION, &diag) == 0);
    check_app("KEEP.APP", APP_V11_VERSION);
    CHECK(put("WIDE.ASM", "nop\nret\n") == FAT16_EDIT_OK);
    CHECK(put("BAD.ASM", "nop\nunknown\n") == FAT16_EDIT_OK);
    CHECK(put("LIMIT.ASM", "ret\n.section .bss\nresb 65536\n") == FAT16_EDIT_OK);
    CHECK(assemble("/BAD.ASM", "/KEEP.APP", APP_V11_VERSION, &diag) == APP_ERR_SYNTAX && diag.line == 2);
    CHECK(assemble("/LIMIT.ASM", "/KEEP.APP", APP_V11_VERSION, &diag) == APP_ERR_LIMIT && diag.line == 3);
    CHECK(assemble("/ABSENT.ASM", "/KEEP.APP", APP_V11_VERSION, &diag) == APP_ERR_MISSING && !diag.line);
    CHECK(assemble("/GOOD.ASM", "/KEEP.APP", 2, &diag) == APP_ERR_INVALID);

    const int reading = app_file_open(1, (const unsigned char *)"KEEP.APP", APP_FILE_READ_MODE);
    CHECK(reading >= 0);
    CHECK(assemble("/GOOD.ASM", "/KEEP.APP", APP_V11_VERSION, &diag) == APP_ERR_ACCESS);
    CHECK(app_file_close(1, reading, 0) == 0);
    const int writing = app_file_open(1, (const unsigned char *)"GOOD.ASM", APP_FILE_WRITE_MODE);
    CHECK(writing >= 0);
    CHECK(assemble("/GOOD.ASM", "/KEEP.APP", APP_V11_VERSION, &diag) == APP_ERR_ACCESS);
    CHECK(app_file_close(1, writing, 0) == 0);

    const struct fat16_file original = find("KEEP.APP");
    for (unsigned int slot = 0; slot < volume.root_entries; ++slot) {
        unsigned char *entry = medium[ROOT + slot / 16] + slot % 16 * 32;
        if (!memcmp(entry, "KEEP    APP", 11)) {
            entry[11] |= 1;
            CHECK(assemble("/GOOD.ASM", "/KEEP.APP", APP_V11_VERSION, &diag) == APP_ERR_ACCESS);
            entry[11] &= ~1u;
            break;
        }
    }
    unsigned char original_bytes[128], current[128];
    CHECK(original.size <= sizeof(original_bytes));
    CHECK(fat16_read_range(&volume, &original, 0, original_bytes, original.size));
    memcpy(snapshot, medium, sizeof(medium));
    const unsigned int free_cluster = next_free(2);
    const unsigned int sites[] = {DATA + free_cluster - 2, 1, 1 + FAT_SECTORS, ROOT};
    for (unsigned int i = 0; i < sizeof(sites) / sizeof(sites[0]); ++i) {
        for (int published = 0; published <= 1; ++published) {
            fail_lba = sites[i];
            fail = 1;
            publish_before_failure = published;
            CHECK(assemble("/WIDE.ASM", "/KEEP.APP", APP_V11_VERSION, &diag) == APP_ERR_WRITE);
            const struct fat16_file latest = find("KEEP.APP");
            CHECK(latest.first_cluster == original.first_cluster && latest.size == original.size);
            CHECK(fat16_read_range(&volume, &latest, 0, current, latest.size));
            CHECK(!memcmp(current, original_bytes, latest.size));
            CHECK(!memcmp(snapshot + 512, medium[1], 2 * FAT_SECTORS * 512));
            check_mirrors();
            CHECK(!outstanding);
        }
    }
    for (int budget = 0; budget < 3; ++budget) {
        page_budget = budget;
        CHECK(assemble("/GOOD.ASM", "/KEEP.APP", APP_V11_VERSION, &diag) == APP_ERR_MEMORY);
        CHECK(!outstanding);
    }
    page_budget = -1;
    read_fail_lba = DATA + find("WIDE.ASM").first_cluster - 2;
    read_fail = 1;
    CHECK(assemble("/WIDE.ASM", "/KEEP.APP", APP_V11_VERSION, &diag) == APP_ERR_IO);
    CHECK(!outstanding && find("KEEP.APP").first_cluster == original.first_cluster);

    for (unsigned int cluster = 2; cluster < CLUSTERS + 2; ++cluster) {
        const unsigned int lba = 1 + cluster / 256, at = cluster % 256 * 2;
        if (!(medium[lba][at] || medium[lba][at + 1])) {
            medium[lba][at] = medium[lba][at + 1] = 0xff;
            medium[lba + FAT_SECTORS][at] = medium[lba + FAT_SECTORS][at + 1] = 0xff;
        }
    }
    CHECK(assemble("/WIDE.ASM", "/KEEP.APP", APP_V11_VERSION, &diag) == APP_ERR_LIMIT);
    CHECK(find("KEEP.APP").first_cluster == original.first_cluster && !outstanding);
    check_mirrors();
    memcpy(medium, snapshot, sizeof(medium));

    fail_lba = ROOT;
    fail = 2;
    publish_before_failure = 1;
    CHECK(assemble("/WIDE.ASM", "/KEEP.APP", APP_V11_VERSION, &diag) == APP_ERR_UNCERTAIN);
    check_app("KEEP.APP", APP_V11_VERSION);
    check_mirrors();
    CHECK(!outstanding);
    memcpy(medium, snapshot, sizeof(medium));
    for (unsigned int i = 0; i < 3; ++i) {
        CHECK(assemble("/WIDE.ASM", "/KEEP.APP", APP_V11_VERSION, &diag) == 0);
        check_mirrors();
        CHECK(!outstanding);
    }
    check_app("KEEP.APP", APP_V11_VERSION);

    const unsigned int work = fat16_path_cluster(&cwd);
    for (unsigned int i = 0; i < 13; ++i) {
        unsigned char name[] = "F00.APP";
        name[1] += i / 10;
        name[2] += i % 10;
        CHECK(fat16_create(&volume, work, name, sizeof(name) - 1) == FAT16_EDIT_OK);
    }
    memcpy(snapshot, medium, sizeof(medium));
    const unsigned int next = next_free(next_free(2) + 1);
    fail_lba = DATA + next - 2;
    fail = 1;
    skip_failure = 1; /* Let prepare_slot initialize the new sector. */
    publish_before_failure = 1;
    CHECK(assemble("/GOOD.ASM", "GROW.APP", APP_V11_VERSION, &diag) == APP_ERR_WRITE);
    CHECK(fat16_resolve_file(&volume, &cwd, (const unsigned char *)"GROW.APP", &where, &file) == 0);
    CHECK(!memcmp(snapshot + 512, medium[1], 2 * FAT_SECTORS * 512));
    CHECK(!outstanding && !fail);
    CHECK(assemble("/GOOD.ASM", "GROW.APP", APP_V11_VERSION, &diag) == 0);
    CHECK(fat16_resolve_file(&volume, &cwd, (const unsigned char *)"GROW.APP", &where, &file) == 1);
    check_mirrors();
    CHECK(!outstanding);

    /* 10,919 near jumps use six bytes each, but only five bytes per source line. */
    static char max_source[56000];
    memcpy(max_source, "jmp x\n", 6);
    unsigned int length = 6;
    for (unsigned int i = 0; i < 10919; ++i) {
        memcpy(max_source + length, "jz x\n", 5);
        length += 5;
    }
    const char suffix[] = "x: mov eax, 2\nxor edi, edi\nint 0x80\ndb 0,0,0,0,0,0,0,0\n";
    memcpy(max_source + length, suffix, sizeof(suffix));
    CHECK(put("MAX.ASM", max_source) == FAT16_EDIT_OK);
    CHECK(assemble("/MAX.ASM", "/MAX.APP", APP_V11_VERSION, &diag) == 0);
    CHECK(find("MAX.APP").size == APP_FILE_MAX);
    check_app("MAX.APP", APP_V11_VERSION);
    check_mirrors();
    CHECK(!outstanding);
    puts("Assembly service checked success, open-file protection, syntax, limits and I/O rollback");
}

int main(void)
{
    for (int published = 0; published <= 1; ++published) {
        memset(medium, 0, sizeof(medium));
        CHECK(put("keep.txt", "safe") == FAT16_EDIT_OK);
        fail_lba = 1;
        publish_before_failure = published;
        fail = 1;
        CHECK(put("keep.txt", "lost") == FAT16_EDIT_IO);
        CHECK(content_is("keep.txt", "safe"));
        const int equal = memcmp(medium[1], medium[1 + FAT_SECTORS], FAT_SECTORS * 512) == 0;
        printf("FAT mirrors equal after injected write failure (persisted=%d): %d\n", published, equal);
        CHECK(equal);
        CHECK(put("retry.txt", "next") == FAT16_EDIT_OK);
        CHECK(content_is("retry.txt", "next"));
    }
    memset(medium, 0, sizeof(medium));
    CHECK(put("race.txt", "base") == FAT16_EDIT_OK);
    const struct fat16_file first = find("race.txt");
    app_files_begin(0, &volume, 0);
    const int handle = app_file_open(0, (const unsigned char *)"race.txt", APP_FILE_WRITE_MODE);
    CHECK(handle >= 0);
    CHECK(app_file_write(0, handle, (const unsigned char *)"save", 4) == 4);
    CHECK(put("race.txt", "next") == FAT16_EDIT_OK);
    CHECK(put("race.txt", "last") == FAT16_EDIT_OK);
    const struct fat16_file last = find("race.txt");
    CHECK(first.size == last.size && first.first_cluster == last.first_cluster &&
        first.modified_date == last.modified_date);
    const int outcome = app_file_close(0, handle, 1);
    printf("Close after two concurrent writes with repeated metadata: %d\n", outcome);
    CHECK(outcome == APP_ERR_CHANGED);
    CHECK(content_is("race.txt", "last"));

    const int kept = app_file_open(0, (const unsigned char *)"race.txt", APP_FILE_WRITE_MODE);
    CHECK(kept >= 0);
    CHECK(app_file_write(0, kept, (const unsigned char *)"keep", 4) == 4);
    CHECK(put("other.txt", "data") == FAT16_EDIT_OK);
    CHECK(app_file_close(0, kept, 1) == 0);
    CHECK(content_is("race.txt", "keep"));
    const unsigned int before = fat16_mutations();
    CHECK(fat16_transfer(&volume, 0, (const unsigned char *)"race.txt", 0,
        (const unsigned char *)"moved.txt", 1) == FAT16_EDIT_OK);
    CHECK(fat16_file_changed(0, (const unsigned char *)"race.txt", before));
    CHECK(fat16_file_changed(0, (const unsigned char *)"MOVED.TXT", before));
    CHECK(!fat16_file_changed(0, (const unsigned char *)"other.txt", before));
    CHECK(content_is("moved.txt", "keep"));
    const int reader = app_file_open(0, (const unsigned char *)"moved.txt", APP_FILE_READ_MODE);
    unsigned char data[4];
    CHECK(reader >= 0 && app_file_read(0, reader, data, 2) == 2);
    CHECK(put("moved.txt", "news") == FAT16_EDIT_OK);
    CHECK(app_file_read(0, reader, data, 2) == APP_ERR_CHANGED);
    CHECK(app_file_close(0, reader, 1) == 0);
    CHECK(outstanding == 0);
    assembly_service();
    puts("FAT16 and APP checks completed.");
    return 0;
}
