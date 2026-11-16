#include "fat16_path.h"
#include "fat16_internal.h"

unsigned int fat16_path_cluster(const struct fat16_path *path)
{
    return path->depth ? path->folders[path->depth - 1].cluster : 0;
}

int fat16_path_enter(struct fat16_path *path, const struct fat16_file *folder)
{
    if (!folder->is_directory || !folder->first_cluster || path->depth == FAT16_PATH_DEPTH)
        return 0;
    for (unsigned int i = 0; i < path->depth; ++i)
        if (path->folders[i].cluster == folder->first_cluster)
            return 0;
    struct fat16_folder *part = &path->folders[path->depth++];
    part->cluster = folder->first_cluster;
    for (unsigned int i = 0; i < sizeof(part->name); ++i)
        part->name[i] = folder->name[i];
    return 1;
}

static int resolve(const struct fat16_volume *vol, const struct fat16_path *cwd,
    const unsigned char *text, struct fat16_path *path, unsigned char *basename)
{
    const unsigned char *p = text;
    unsigned int length = 0;
    while (length < FAT16_PATH_CAPACITY && p[length])
        ++length;
    if (length == FAT16_PATH_CAPACITY)
        return 0;
    *path = *cwd;
    if (*p == '/')
        path->depth = 0;
    while (*p) {
        while (*p == '/')
            ++p;
        if (!*p)
            break;
        unsigned char part[13];
        unsigned int size = 0;
        while (*p && *p != '/') {
            if (size == sizeof(part) - 1 || *p <= ' ')
                return 0;
            part[size++] = *p++;
        }
        part[size] = 0;
        const int dot = part[0] == '.' && (size == 1 || (size == 2 && part[1] == '.'));
        if (basename && !*p) {
            if (dot)
                return 0;
            for (unsigned int i = 0; i <= size; ++i)
                basename[i] = part[i];
            return 1;
        }
        if (dot) {
            if (size == 2 && path->depth)
                --path->depth;
            continue;
        }
        struct fat16_file folder;
        const int found = fat16_find_at(vol, fat16_path_cluster(path), part, &folder);
        if (found <= 0)
            return found;
        if (!fat16_cluster_valid(vol, folder.first_cluster) || !fat16_path_enter(path, &folder))
            return 0;
    }
    return basename ? 0 : 1;
}

int fat16_resolve_dir(const struct fat16_volume *vol, const struct fat16_path *cwd,
    const unsigned char *text, struct fat16_path *result)
{
    return resolve(vol, cwd, text, result, 0);
}

int fat16_split_path(const struct fat16_volume *vol, const struct fat16_path *cwd,
    const unsigned char *text, struct fat16_path *parent, unsigned char basename[13])
{
    return resolve(vol, cwd, text, parent, basename);
}

int fat16_resolve_parent(const struct fat16_volume *vol, const struct fat16_path *cwd,
    const unsigned char *text, struct fat16_location *result)
{
    struct fat16_path parent;
    const int found = fat16_split_path(vol, cwd, text, &parent, result->name);
    if (found > 0)
        result->directory = fat16_path_cluster(&parent);
    return found;
}

int fat16_resolve_file(const struct fat16_volume *vol, const struct fat16_path *cwd,
    const unsigned char *text, struct fat16_location *location, struct fat16_file *file)
{
    struct fat16_location entry;
    if (!location)
        location = &entry;
    const int result = fat16_resolve_parent(vol, cwd, text, location);
    return result == 1 ? fat16_lookup_at(vol, location->directory, location->name, file) : result;
}

void fat16_format_path(const struct fat16_path *path, char text[FAT16_PATH_CAPACITY])
{
    unsigned int index = 0;
    text[index++] = '/';
    for (unsigned int i = 0; i < path->depth; ++i) {
        if (i)
            text[index++] = '/';
        const char *name = path->folders[i].name;
        for (; *name; ++name)
            text[index++] = *name;
    }
    text[index] = 0;
}
