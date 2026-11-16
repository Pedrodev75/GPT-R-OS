#ifndef GPT_R_FAT16_INTERNAL_H
#define GPT_R_FAT16_INTERNAL_H

#include "fat16.h"

enum { FAT16_ROOT = 0, FAT16_END = 0xffff };

static inline int fat16_cluster_valid(const struct fat16_volume *vol, unsigned int cluster)
{
    return cluster >= 2 && cluster < vol->clusters + 2 && cluster < 0xfff0;
}

static inline unsigned int fat16_cluster_lba(const struct fat16_volume *vol, unsigned int cluster)
{
    return vol->data_start + (cluster - 2) * vol->sectors_per_cluster;
}

int fat16_get_link(const struct fat16_volume *vol, unsigned int cluster, unsigned int *next);

/* Read one physical slot, including deleted and empty entries. */
int fat16_slot(const struct fat16_volume *vol, struct fat16_dir *dir,
    const unsigned char **slot);

#endif
