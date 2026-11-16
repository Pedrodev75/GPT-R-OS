#ifndef GPT_R_FAT16_PATH_H
#define GPT_R_FAT16_PATH_H

#include "fat16.h"

enum { FAT16_PATH_DEPTH = 16, FAT16_PATH_CAPACITY = 222 };

struct fat16_folder {
    unsigned int cluster;
    char name[13];
};

struct fat16_path {
    unsigned int depth;
    struct fat16_folder folders[FAT16_PATH_DEPTH];
};

struct fat16_location {
    unsigned int directory;
    unsigned char name[13];
};

unsigned int fat16_path_cluster(const struct fat16_path *path);
int fat16_path_enter(struct fat16_path *path, const struct fat16_file *folder);
int fat16_resolve_dir(const struct fat16_volume *vol, const struct fat16_path *cwd,
    const unsigned char *text, struct fat16_path *result);
int fat16_split_path(const struct fat16_volume *vol, const struct fat16_path *cwd,
    const unsigned char *text, struct fat16_path *parent, unsigned char basename[13]);
int fat16_resolve_parent(const struct fat16_volume *vol, const struct fat16_path *cwd,
    const unsigned char *text, struct fat16_location *result);
int fat16_resolve_file(const struct fat16_volume *vol, const struct fat16_path *cwd,
    const unsigned char *text, struct fat16_location *location, struct fat16_file *file);
void fat16_format_path(const struct fat16_path *path, char text[FAT16_PATH_CAPACITY]);

#endif
