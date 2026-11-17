#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kernel/apps/app_abi.h"
#include "kernel/apps/app_files.h"
#include "kernel/fs/fat16.h"
#include "kernel/memory/pages.h"

enum { FAT_SECTORS = 17, ROOT = 1 + 2 * FAT_SECTORS, DATA = ROOT + 2, CLUSTERS = 4096 };
static unsigned char medium[DATA + CLUSTERS][ATA_SECTOR_SIZE];
static struct ata_disk disk = {.sectors = DATA + CLUSTERS};
static struct fat16_volume volume = {&disk, 1, FAT_SECTORS, 2, ROOT, DATA, CLUSTERS, 32, 1};
static unsigned int fail_lba, outstanding;
static int fail, publish_before_failure;

#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expr); exit(1); } } while (0)

int ata_read_sector(const struct ata_disk *device, unsigned int lba, unsigned char data[512])
{
    if (lba >= device->sectors)
        return 0;
    memcpy(data, medium[lba], 512);
    return 1;
}
int ata_write_sector(const struct ata_disk *device, unsigned int lba, const unsigned char data[512])
{
    if (lba >= device->sectors)
        return 0;
    if (fail && lba == fail_lba) {
        fail = 0;
        if (publish_before_failure)
            memcpy(medium[lba], data, 512);
        return 0;
    }
    memcpy(medium[lba], data, 512);
    return 1;
}
void *page_alloc(void)
{
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
    puts("FAT16 and APP checks completed.");
    return 0;
}
