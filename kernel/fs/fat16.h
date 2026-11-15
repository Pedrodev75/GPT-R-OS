#ifndef GPT_R_FAT16_H
#define GPT_R_FAT16_H

#include "../drivers/ata.h"

struct fat16_volume {
    const struct ata_disk *disk;
    unsigned int fat_start, root_start, data_start;
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

int fat16_mount(struct fat16_volume *volume, const struct ata_disk *disk);
/* Returns 1 for an entry, 0 at end of directory and -1 on read failure. */
int fat16_next(const struct fat16_volume *volume, struct fat16_dir *dir,
               struct fat16_file *file);
int fat16_read(const struct fat16_volume *volume, const struct fat16_file *file,
               void (*consume)(void *, const unsigned char *, unsigned int),
               void *context);

#endif
