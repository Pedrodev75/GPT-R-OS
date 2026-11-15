#include "fat16.h"

enum { EMPTY_SLOT = 0, DELETED_SLOT = 0xe5, END_OF_CHAIN = 0xffff };

struct edit_entry {
    unsigned int index;
    unsigned int first_cluster;
    unsigned int size;
    int exists;
};

static unsigned int get16(const unsigned char *p)
{
    return p[0] | ((unsigned int)p[1] << 8);
}

static void put16(unsigned char *p, unsigned int value)
{
    p[0] = value;
    p[1] = value >> 8;
}

static void put32(unsigned char *p, unsigned int value)
{
    put16(p, value);
    put16(p + 2, value >> 16);
}

static int encode_name(unsigned char name[11], const unsigned char *text,
                       unsigned int length)
{
    for (unsigned int i = 0; i < 11; ++i)
        name[i] = ' ';
    unsigned int position = 0;
    int extension = 0;
    if (!length)
        return 0;
    for (unsigned int i = 0; i < length; ++i) {
        unsigned char ch = text[i];
        if (ch == '.' && !extension && position) {
            position = 8;
            extension = 1;
            continue;
        }
        if (ch >= 'a' && ch <= 'z')
            ch -= 'a' - 'A';
        if (!((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
              ch == '_' || ch == '-'))
            return 0;
        if (position == (extension ? 11u : 8u))
            return 0;
        name[position++] = ch;
    }
    return !extension || position > 8;
}

static enum fat16_edit_result find_entry(const struct fat16_volume *vol,
                                         const unsigned char name[11],
                                         struct edit_entry *entry)
{
    unsigned char sector[512];
    entry->index = vol->root_entries;
    entry->first_cluster = 0;
    entry->size = 0;
    entry->exists = 0;
    int long_name = 0;
    for (unsigned int i = 0; i < vol->root_entries; ++i) {
        if (!(i % 16) && !ata_read_sector(vol->disk, vol->root_start + i / 16, sector))
            return FAT16_EDIT_IO;
        const unsigned char *slot = sector + (i % 16) * 32;
        if (slot[0] == EMPTY_SLOT || slot[0] == DELETED_SLOT) {
            if (entry->index == vol->root_entries)
                entry->index = i;
            if (slot[0] == EMPTY_SLOT)
                break;
            long_name = 0;
            continue;
        }
        if (slot[11] == 0x0f) {
            long_name = 1;
            continue;
        }
        unsigned int match = 0;
        while (match < 11 && slot[match] == name[match])
            ++match;
        if (match == 11) {
            if (long_name || (slot[11] & 0x19))
                return FAT16_EDIT_PROTECTED;
            entry->exists = 1;
            entry->index = i;
            entry->first_cluster = get16(slot + 26);
            entry->size = get16(slot + 28) | (get16(slot + 30) << 16);
            return FAT16_EDIT_OK;
        }
        long_name = 0;
    }
    return FAT16_EDIT_OK;
}

static int valid_cluster(const struct fat16_volume *vol, unsigned int cluster)
{
    return cluster >= 2 && cluster < vol->clusters + 2 && cluster < 0xfff0;
}

static int fat_get(const struct fat16_volume *vol, unsigned int cluster,
                   unsigned int *next)
{
    unsigned char sector[512];
    if (!ata_read_sector(vol->disk, vol->fat_start + cluster / 256, sector))
        return 0;
    *next = get16(sector + (cluster % 256) * 2);
    return 1;
}

static int fat_set(const struct fat16_volume *vol, unsigned int cluster,
                   unsigned int old_value, unsigned int value)
{
    unsigned char sector[512];
    const unsigned int offset = (cluster % 256) * 2;
    const unsigned int first = vol->fat_start + cluster / 256;
    for (unsigned int copy = 0; copy < vol->fat_copies; ++copy)
        if (!ata_read_sector(vol->disk, first + copy * vol->fat_sectors, sector) ||
            get16(sector + offset) != old_value)
            return 0;

    /* Update the primary copy last. A failed update is never reused blindly. */
    for (unsigned int copy = vol->fat_copies; copy-- > 0;) {
        const unsigned int lba = first + copy * vol->fat_sectors;
        if (!ata_read_sector(vol->disk, lba, sector))
            return 0;
        put16(sector + offset, value);
        if (!ata_write_sector(vol->disk, lba, sector))
            return 0;
    }
    return 1;
}

static int find_free_cluster(const struct fat16_volume *vol, unsigned int *free)
{
    unsigned char sector[512];
    for (unsigned int cluster = 2; valid_cluster(vol, cluster); ++cluster) {
        if ((cluster == 2 || !(cluster % 256)) &&
            !ata_read_sector(vol->disk, vol->fat_start + cluster / 256, sector))
            return 0;
        if (get16(sector + (cluster % 256) * 2) == 0) {
            *free = cluster;
            return 1;
        }
    }
    *free = 0;
    return 1;
}

/* Reach EOC at exactly the file length before making any metadata changes. */
static int valid_chain(const struct fat16_volume *vol, const struct edit_entry *entry)
{
    const unsigned int cluster_size = vol->sectors_per_cluster * 512;
    const unsigned int needed = entry->size / cluster_size +
                                (entry->size % cluster_size != 0);
    unsigned int cluster = entry->first_cluster;
    if (!needed)
        return !cluster;
    if (needed > vol->clusters)
        return 0;
    for (unsigned int i = 0; i < needed; ++i)
        if (!valid_cluster(vol, cluster) || !fat_get(vol, cluster, &cluster))
            return 0;
    return cluster >= 0xfff8;
}

static int release_chain(const struct fat16_volume *vol, unsigned int cluster)
{
    while (valid_cluster(vol, cluster)) {
        unsigned int next;
        if (!fat_get(vol, cluster, &next) || !fat_set(vol, cluster, next, 0))
            return 0;
        cluster = next;
    }
    return !cluster || cluster >= 0xfff8;
}

static int update_directory(const struct fat16_volume *vol,
                            const struct edit_entry *entry,
                            const unsigned char name[11], unsigned int cluster,
                            unsigned int length)
{
    unsigned char sector[512];
    const unsigned int lba = vol->root_start + entry->index / 16;
    if (!ata_read_sector(vol->disk, lba, sector))
        return 0;
    unsigned char *slot = sector + (entry->index % 16) * 32;
    if (!name) {
        slot[0] = DELETED_SLOT;
    } else {
        if (slot[0] == EMPTY_SLOT && entry->index + 1 < vol->root_entries) {
            if (entry->index % 16 != 15) {
                slot[32] = EMPTY_SLOT;
            } else {
                unsigned char next[512];
                if (!ata_read_sector(vol->disk, lba + 1, next))
                    return 0;
                if (next[0] != EMPTY_SLOT) {
                    next[0] = EMPTY_SLOT;
                    if (!ata_write_sector(vol->disk, lba + 1, next))
                        return 0;
                }
            }
        }
        if (!entry->exists) {
            for (unsigned int i = 0; i < 32; ++i)
                slot[i] = i < 11 ? name[i] : 0;
            /* Jan 1, 1980 is a valid default without an RTC driver. */
            put16(slot + 16, 0x21);
            put16(slot + 18, 0x21);
            put16(slot + 24, 0x21);
        }
        slot[11] |= 0x20;
        put16(slot + 26, cluster);
        put32(slot + 28, length);
    }
    return ata_write_sector(vol->disk, lba, sector);
}

enum fat16_edit_result fat16_write(const struct fat16_volume *vol,
                                  const unsigned char *text_name, unsigned int name_size,
                                  const unsigned char *text, unsigned int length)
{
    unsigned char name[11];
    if (!encode_name(name, text_name, name_size))
        return FAT16_EDIT_NAME;
    if (length > 512)
        return FAT16_EDIT_SIZE;

    struct edit_entry entry;
    enum fat16_edit_result result = find_entry(vol, name, &entry);
    if (result != FAT16_EDIT_OK)
        return result;
    if (entry.index == vol->root_entries)
        return FAT16_EDIT_FULL;
    if (entry.exists && !valid_chain(vol, &entry))
        return FAT16_EDIT_IO;

    unsigned int cluster = 0;
    if (length) {
        if (!find_free_cluster(vol, &cluster))
            return FAT16_EDIT_IO;
        if (!cluster)
            return FAT16_EDIT_FULL;
        unsigned char sector[512];
        for (unsigned int i = 0; i < 512; ++i)
            sector[i] = i < length ? text[i] : 0;
        const unsigned int lba = vol->data_start + (cluster - 2) * vol->sectors_per_cluster;
        if (!ata_write_sector(vol->disk, lba, sector) ||
            !fat_set(vol, cluster, 0, END_OF_CHAIN))
            return FAT16_EDIT_IO;
    }

    /* Publish the new chain before releasing the old one. */
    if (!update_directory(vol, &entry, name, cluster, length))
        return FAT16_EDIT_IO;
    if (entry.exists && !release_chain(vol, entry.first_cluster))
        return FAT16_EDIT_CLEANUP;
    return FAT16_EDIT_OK;
}

enum fat16_edit_result fat16_remove(const struct fat16_volume *vol,
                                   const unsigned char *text_name, unsigned int name_size)
{
    unsigned char name[11];
    if (!encode_name(name, text_name, name_size))
        return FAT16_EDIT_NAME;
    struct edit_entry entry;
    enum fat16_edit_result result = find_entry(vol, name, &entry);
    if (result != FAT16_EDIT_OK)
        return result;
    if (!entry.exists)
        return FAT16_EDIT_MISSING;
    if (!valid_chain(vol, &entry) || !update_directory(vol, &entry, 0, 0, 0))
        return FAT16_EDIT_IO;
    return release_chain(vol, entry.first_cluster) ? FAT16_EDIT_OK : FAT16_EDIT_CLEANUP;
}
