#ifndef GPT_R_FAT16_H
#define GPT_R_FAT16_H

#include "../drivers/ata.h"

struct fat16_volume {
    const struct ata_disk* disk;
    unsigned int fat_start, fat_sectors, fat_copies, root_start, data_start;
    unsigned int clusters, root_entries, sectors_per_cluster;
};

struct fat16_file {
    char name[13];
    unsigned int first_cluster, size;
};

struct fat16_dir {
    unsigned int index;
    unsigned char sector[ATA_SECTOR_SIZE];
};

int fat16_mount(struct fat16_volume* volume, const struct ata_disk* disk);
/* Returns 1 for an entry, 0 at end of directory and -1 on read failure. */
int fat16_next(const struct fat16_volume* volume, struct fat16_dir* dir,
    struct fat16_file* file);
int fat16_read(const struct fat16_volume* volume, const struct fat16_file* file,
    void (*consume)(void*, const unsigned char*, unsigned int),
    void* context);

enum fat16_edit_result {
    FAT16_EDIT_OK, FAT16_EDIT_NAME, FAT16_EDIT_SIZE, FAT16_EDIT_MISSING,
    FAT16_EDIT_PROTECTED, FAT16_EDIT_FULL, FAT16_EDIT_IO, FAT16_EDIT_CLEANUP
};

enum fat16_edit_result fat16_write(const struct fat16_volume* volume,
    const unsigned char* name, unsigned int name_size,
    const unsigned char* text, unsigned int length);
enum fat16_edit_result fat16_remove(const struct fat16_volume* volume,
    const unsigned char* name, unsigned int name_size);

#endif
