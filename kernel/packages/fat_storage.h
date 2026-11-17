#ifndef GPT_R_PACKAGE_FAT_STORAGE_H
#define GPT_R_PACKAGE_FAT_STORAGE_H

#include "package.h"
#include "../fs/fat16.h"

struct pkg_fat {
    struct ata_disk disk;
    struct fat16_volume volume;
    struct fat16_read_cursor cursor;
    unsigned int apps;
};

struct pkg_fat_source {
    const struct fat16_volume *volume;
    struct fat16_file file;
    struct fat16_read_cursor cursor;
};

enum pkg_status pkg_fat_open(struct pkg_fat *fat);
struct pkg_storage pkg_fat_storage(struct pkg_fat *fat);
struct pkg_source pkg_fat_reader(struct pkg_fat_source *source);

#endif
