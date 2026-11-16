#ifndef GPT_R_FAT16_H
#define GPT_R_FAT16_H

#include "../drivers/ata.h"

struct fat16_volume {
    const struct ata_disk* disk;
    unsigned int fat_start, fat_sectors, fat_copies, root_start, data_start;
    unsigned int clusters, root_entries, sectors_per_cluster;
};

enum { FAT16_LOWER_BASE = 0x08, FAT16_LOWER_EXTENSION = 0x10 };

struct fat16_file {
    char name[13];
    unsigned int first_cluster, size, modified_date;
    int is_directory;
};

struct fat16_dir {
    unsigned int index, first_cluster, cluster, cluster_sector, links;
    unsigned int lba, cycle_anchor, cycle_period, cycle_span;
    int ended;
    unsigned char sector[ATA_SECTOR_SIZE];
};

int fat16_mount(struct fat16_volume* volume, const struct ata_disk* disk);
/* Returns 1 for an entry, 0 at end of directory and -1 on read failure. */
int fat16_next(const struct fat16_volume* volume, struct fat16_dir* dir,
    struct fat16_file* file);
/* Lists files and folders for a cursor with first_cluster set. */
int fat16_next_at(const struct fat16_volume *volume, struct fat16_dir *dir,
    struct fat16_file *file);
int fat16_lookup_at(const struct fat16_volume *volume, unsigned int directory,
    const unsigned char *name, struct fat16_file *file);
enum fat16_scan_result {
    FAT16_SCAN_ERROR = -1, FAT16_SCAN_END, FAT16_SCAN_ENTRY, FAT16_SCAN_SKIP
};
/* Read one slot, so callers can cap work per desktop iteration. */
enum fat16_scan_result fat16_scan(const struct fat16_volume *volume,
    struct fat16_dir *dir, struct fat16_file *file);
int fat16_space(const struct fat16_volume *volume, unsigned int *total,
    unsigned int *free);
enum fat16_read_result { FAT16_READ_ERROR, FAT16_READ_OK, FAT16_READ_STOPPED };
/* The consumer returns zero to stop before reading any more sectors. */
enum fat16_read_result fat16_read(const struct fat16_volume* volume, const struct fat16_file* file,
    int (*consume)(void*, const unsigned char*, unsigned int),
    void* context);

enum fat16_edit_result {
    FAT16_EDIT_OK, FAT16_EDIT_NAME, FAT16_EDIT_SIZE, FAT16_EDIT_MISSING,
    FAT16_EDIT_PROTECTED, FAT16_EDIT_CASE, FAT16_EDIT_TEXT_EXTENSION, FAT16_EDIT_FULL, FAT16_EDIT_IO, FAT16_EDIT_CLEANUP, FAT16_EDIT_EXISTS
};

enum fat16_edit_result fat16_write(const struct fat16_volume* volume,
    const unsigned char* name, unsigned int name_size,
    const unsigned char* text, unsigned int length);
enum fat16_edit_result fat16_write_at(const struct fat16_volume *volume,
    unsigned int directory, const unsigned char *name, unsigned int name_size,
    const unsigned char *text, unsigned int length);
enum fat16_edit_result fat16_mkdir(const struct fat16_volume *volume,
    unsigned int directory, const unsigned char *name, unsigned int name_size);
unsigned int fat16_mutations(void);
enum fat16_edit_result fat16_remove(const struct fat16_volume* volume,
    const unsigned char* name, unsigned int name_size);

#endif
