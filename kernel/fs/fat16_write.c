#include "fat16_internal.h"

enum { EMPTY_SLOT = 0, DELETED_SLOT = 0xe5 };

enum { MUTATION_WINDOW = 16 };
static unsigned int mutations, modified_dir[MUTATION_WINDOW];

unsigned int fat16_mutations(void)
{
    return mutations;
}

int fat16_dir_changed(unsigned int directory, unsigned int since)
{
    const unsigned int count = mutations - since;
    if (count > MUTATION_WINDOW)
        return 1;
    for (unsigned int i = 0; i < count; ++i)
        if (modified_dir[(since + i) % MUTATION_WINDOW] == directory)
            return 1;
    return 0;
}

struct edit_entry {
    unsigned int index, lba, next_lba, tail, directory;
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

struct short_name {
    unsigned char alias[11];
    unsigned int lower, upper;
};

static int encode_name(struct short_name *name, const unsigned char *text,
    unsigned int length)
{
    for (unsigned int i = 0; i < 11; ++i)
        name->alias[i] = ' ';
    name->lower = name->upper = 0;
    unsigned int position = 0, part = FAT16_LOWER_BASE;
    if (!length)
        return 0;
    for (unsigned int i = 0; i < length; ++i) {
        unsigned char ch = text[i];
        if (ch == '.' && part == FAT16_LOWER_BASE && position) {
            position = 8;
            part = FAT16_LOWER_EXTENSION;
            continue;
        }
        if (ch >= 'a' && ch <= 'z') {
            name->lower |= part;
            ch -= 'a' - 'A';
        } else if (ch >= 'A' && ch <= 'Z') {
            name->upper |= part;
        } else if (!((ch >= '0' && ch <= '9') || ch == '_' || ch == '-')) {
            return 0;
        }
        if (position == (part == FAT16_LOWER_EXTENSION ? 11u : 8u))
            return 0;
        name->alias[position++] = ch;
    }
    return (part == FAT16_LOWER_BASE || position > 8) &&
        !(name->lower & name->upper);
}

static enum fat16_edit_result find_entry(const struct fat16_volume *vol, unsigned int directory,
    const struct short_name *name, struct edit_entry *entry, int exclusive)
{
    struct fat16_dir dir = { .first_cluster = directory };
    *entry = (struct edit_entry){ .directory = directory };
    const unsigned char *slot;
    int long_name = 0, result;
    while ((result = fat16_slot(vol, &dir, &slot)) > 0) {
        if (slot[0] == EMPTY_SLOT || slot[0] == DELETED_SLOT) {
            if (!entry->lba) {
                entry->index = dir.index - 1;
                entry->lba = dir.lba;
                if (slot[0] == EMPTY_SLOT && !(dir.index % 16)) {
                    if (!directory) {
                        if (dir.index < vol->root_entries)
                            entry->next_lba = dir.lba + 1;
                    } else if (dir.cluster_sector + 1 < vol->sectors_per_cluster) {
                        entry->next_lba = dir.lba + 1;
                    } else {
                        unsigned int next;
                        if (!fat16_get_link(vol, dir.cluster, &next))
                            return FAT16_EDIT_IO;
                        if (next < 0xfff8) {
                            if (next == dir.cluster || !fat16_cluster_valid(vol, next))
                                return FAT16_EDIT_IO;
                            entry->next_lba = fat16_cluster_lba(vol, next);
                        }
                    }
                }
            }
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
        while (match < 11 && slot[match] == name->alias[match])
            ++match;
        if (match == 11) {
            if (exclusive)
                return FAT16_EDIT_EXISTS;
            if (((slot[12] ^ name->lower) & (name->lower | name->upper)) != 0)
                return FAT16_EDIT_CASE;
            if (long_name || (slot[11] & 0x19))
                return FAT16_EDIT_PROTECTED;
            *entry = (struct edit_entry){
                .lba = dir.lba, .index = dir.index - 1, .exists = 1, .directory = directory,
                .first_cluster = get16(slot + 26),
                .size = get16(slot + 28) | (get16(slot + 30) << 16)
            };
            return FAT16_EDIT_OK;
        }
        long_name = 0;
    }
    entry->tail = dir.cluster;
    return result < 0 ? FAT16_EDIT_IO : FAT16_EDIT_OK;
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

static int find_free_from(const struct fat16_volume *vol, unsigned int first, unsigned int *free)
{
    unsigned char sector[512];
    for (unsigned int cluster = first; fat16_cluster_valid(vol, cluster); ++cluster) {
        if ((cluster == first || !(cluster % 256)) &&
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
        if (!fat16_cluster_valid(vol, cluster) || !fat16_get_link(vol, cluster, &cluster))
            return 0;
    return cluster >= 0xfff8;
}

static int release_chain(const struct fat16_volume *vol, unsigned int cluster)
{
    while (fat16_cluster_valid(vol, cluster)) {
        unsigned int next;
        if (!fat16_get_link(vol, cluster, &next) || !fat_set(vol, cluster, next, 0))
            return 0;
        cluster = next;
    }
    return !cluster || cluster >= 0xfff8;
}

static int init_cluster(const struct fat16_volume *vol, unsigned int cluster,
    const unsigned char *first_sector)
{
    const unsigned char empty[512] = {0};
    const unsigned int first = fat16_cluster_lba(vol, cluster);
    for (unsigned int i = 0; i < vol->sectors_per_cluster; ++i)
        if (!ata_write_sector(vol->disk, first + i, i == 0 && first_sector ? first_sector : empty))
            return 0;
    return fat_set(vol, cluster, 0, FAT16_END);
}

static enum fat16_edit_result prepare_slot(const struct fat16_volume *vol,
    struct edit_entry *entry)
{
    if (entry->lba)
        return FAT16_EDIT_OK;
    if (!entry->tail)
        return FAT16_EDIT_FULL;

    unsigned int next, end;
    if (!fat16_get_link(vol, entry->tail, &end) || end < 0xfff8 ||
        !find_free_from(vol, 2, &next))
        return FAT16_EDIT_IO;
    if (!next)
        return FAT16_EDIT_FULL;
    if (!init_cluster(vol, next, 0) || !fat_set(vol, entry->tail, end, next))
        return FAT16_EDIT_IO;
    entry->lba = fat16_cluster_lba(vol, next);
    entry->index = 0;
    return FAT16_EDIT_OK;
}

static void write_entry(unsigned char *slot, const unsigned char *alias,
    unsigned char lower, unsigned char attr, unsigned int cluster, unsigned int length)
{
    for (unsigned int i = 0; i < 32; ++i)
        slot[i] = i < 11 ? alias[i] : 0;
    slot[11] = attr;
    slot[12] = lower;
    /* Jan 1, 1980 is a valid default without an RTC driver. */
    put16(slot + 16, 0x21);
    put16(slot + 18, 0x21);
    put16(slot + 24, 0x21);
    put16(slot + 26, cluster);
    put32(slot + 28, length);
}

static int update_directory(const struct fat16_volume *vol,
    const struct edit_entry *entry, const struct short_name *name,
    unsigned int cluster, unsigned int length, unsigned char attr)
{
    unsigned char sector[512];
    if (!ata_read_sector(vol->disk, entry->lba, sector))
        return 0;
    unsigned char *slot = sector + (entry->index % 16) * 32;
    if (!name) {
        slot[0] = DELETED_SLOT;
    } else if (entry->exists) {
        slot[11] |= attr;
        put16(slot + 26, cluster);
        put32(slot + 28, length);
    } else {
        if (slot[0] == EMPTY_SLOT) {
            if (entry->index % 16 != 15) {
                slot[32] = EMPTY_SLOT;
            } else if (entry->next_lba) {
                unsigned char next[512];
                if (!ata_read_sector(vol->disk, entry->next_lba, next))
                    return 0;
                if (next[0] != EMPTY_SLOT) {
                    next[0] = EMPTY_SLOT;
                    if (!ata_write_sector(vol->disk, entry->next_lba, next))
                        return 0;
                }
            }
        }
        write_entry(slot, name->alias, name->lower, attr, cluster, length);
    }
    if (!ata_write_sector(vol->disk, entry->lba, sector))
        return 0;
    modified_dir[mutations % MUTATION_WINDOW] = entry->directory;
    ++mutations;
    return 1;
}

enum fat16_edit_result fat16_mkdir(const struct fat16_volume *vol, unsigned int directory,
    const unsigned char *text_name, unsigned int name_size)
{
    struct short_name name;
    if (!encode_name(&name, text_name, name_size) || name.alias[8] != ' ')
        return FAT16_EDIT_NAME;
    struct edit_entry entry;
    enum fat16_edit_result result = find_entry(vol, directory, &name, &entry, 1);
    if (result != FAT16_EDIT_OK)
        return result;
    result = prepare_slot(vol, &entry);
    if (result != FAT16_EDIT_OK)
        return result;

    unsigned int cluster;
    if (!find_free_from(vol, 2, &cluster))
        return FAT16_EDIT_IO;
    if (!cluster)
        return FAT16_EDIT_FULL;
    unsigned char first_sector[512] = {0};
    write_entry(first_sector, (const unsigned char *)".          ", 0, 0x10, cluster, 0);
    write_entry(first_sector + 32, (const unsigned char *)"..         ", 0, 0x10, directory, 0);
    if (!init_cluster(vol, cluster, first_sector) ||
        !update_directory(vol, &entry, &name, cluster, 0, 0x10))
        return FAT16_EDIT_IO;
    return FAT16_EDIT_OK;
}

static int uppercase_txt(const struct short_name *name)
{
    return (name->upper & FAT16_LOWER_EXTENSION) &&
        name->alias[8] == 'T' && name->alias[9] == 'X' && name->alias[10] == 'T';
}

enum fat16_edit_result fat16_check_write(const struct fat16_volume *vol, unsigned int directory,
    const unsigned char *text_name, unsigned int name_size)
{
    struct short_name name;
    if (!encode_name(&name, text_name, name_size))
        return FAT16_EDIT_NAME;
    struct edit_entry entry;
    const enum fat16_edit_result result = find_entry(vol, directory, &name, &entry, 0);
    if (result != FAT16_EDIT_OK)
        return result;
    if (!entry.exists && uppercase_txt(&name))
        return FAT16_EDIT_TEXT_EXTENSION;
    if (entry.exists && !valid_chain(vol, &entry))
        return FAT16_EDIT_IO;
    return entry.lba || entry.tail ? FAT16_EDIT_OK : FAT16_EDIT_FULL;
}

enum { WRITE_EXCLUSIVE = 1, WRITE_ORIGINAL_NAME = 2 };

static enum fat16_edit_result write_source(const struct fat16_volume *vol, unsigned int directory,
    const unsigned char *text_name, unsigned int name_size, unsigned int length,
    fat16_sector_source source, void *context, unsigned int flags)
{
    struct short_name name;
    if (!encode_name(&name, text_name, name_size))
        return FAT16_EDIT_NAME;
    const unsigned int cluster_size = vol->sectors_per_cluster * 512;
    if (length > (unsigned long long)vol->clusters * cluster_size)
        return FAT16_EDIT_SIZE;

    struct edit_entry entry;
    enum fat16_edit_result result = find_entry(vol, directory, &name, &entry, flags & WRITE_EXCLUSIVE);
    if (result != FAT16_EDIT_OK)
        return result;
    if (!entry.exists && !(flags & WRITE_ORIGINAL_NAME) && uppercase_txt(&name))
        return FAT16_EDIT_TEXT_EXTENSION;
    result = prepare_slot(vol, &entry);
    if (result != FAT16_EDIT_OK)
        return result;
    if (entry.exists && !valid_chain(vol, &entry))
        return FAT16_EDIT_IO;

    unsigned int cluster = 0, tail = 0, written = 0;
    while (written < length) {
        unsigned int next;
        if (!find_free_from(vol, tail ? tail + 1 : 2, &next)) {
            result = FAT16_EDIT_IO;
            break;
        }
        if (!next) {
            result = FAT16_EDIT_FULL;
            break;
        }

        const unsigned int first = fat16_cluster_lba(vol, next);
        unsigned char sector[512];
        for (unsigned int i = 0; i < vol->sectors_per_cluster && written < length; ++i) {
            const unsigned int size = length - written < sizeof(sector) ?
                length - written : sizeof(sector);
            if (!source(context, sector, size)) {
                result = FAT16_EDIT_IO;
                break;
            }
            for (unsigned int j = size; j < sizeof(sector); ++j)
                sector[j] = 0;
            if (!ata_write_sector(vol->disk, first + i, sector)) {
                result = FAT16_EDIT_IO;
                break;
            }
            written += size;
        }
        if (result != FAT16_EDIT_OK)
            break;
        if (!fat_set(vol, next, 0, FAT16_END)) {
            result = FAT16_EDIT_IO;
            break;
        }
        if (tail && !fat_set(vol, tail, FAT16_END, next)) {
            unsigned int link;
            if (fat16_get_link(vol, tail, &link) && link >= 0xfff8)
                fat_set(vol, next, FAT16_END, 0);
            result = FAT16_EDIT_IO;
            break;
        }
        if (!cluster)
            cluster = next;
        tail = next;
    }
    if (result != FAT16_EDIT_OK) {
        if (cluster && !release_chain(vol, cluster))
            return FAT16_EDIT_IO;
        return result;
    }

    /* Publish the new chain before releasing the old one. */
    if (!update_directory(vol, &entry, &name, cluster, length, 0x20)) {
        /* Release the chain only if the directory still points elsewhere. */
        unsigned char sector[512];
        if (cluster && ata_read_sector(vol->disk, entry.lba, sector) &&
            get16(sector + (entry.index % 16) * 32 + 26) != cluster)
            release_chain(vol, cluster);
        return FAT16_EDIT_IO;
    }
    if (entry.exists && !release_chain(vol, entry.first_cluster))
        return FAT16_EDIT_CLEANUP;
    return FAT16_EDIT_OK;
}

static int memory_sector(void *context, unsigned char sector[512], unsigned int size)
{
    const unsigned char **data = context;
    for (unsigned int i = 0; i < size; ++i)
        sector[i] = (*data)[i];
    *data += size;
    return 1;
}

enum fat16_edit_result fat16_write_stream_at(const struct fat16_volume *vol, unsigned int directory,
    const unsigned char *name, unsigned int name_size, unsigned int length,
    fat16_sector_source source, void *context)
{
    return write_source(vol, directory, name, name_size, length, source, context, 0);
}

enum fat16_edit_result fat16_write_at(const struct fat16_volume *vol, unsigned int directory,
    const unsigned char *name, unsigned int name_size, const unsigned char *text, unsigned int length)
{
    return fat16_write_stream_at(vol, directory, name, name_size, length, memory_sector, &text);
}

enum fat16_edit_result fat16_create(const struct fat16_volume *vol, unsigned int directory,
    const unsigned char *name, unsigned int name_size)
{
    return write_source(vol, directory, name, name_size, 0, 0, 0, WRITE_EXCLUSIVE);
}

enum fat16_edit_result fat16_write(const struct fat16_volume *vol,
    const unsigned char *name, unsigned int name_size, const unsigned char *text, unsigned int length)
{
    return fat16_write_at(vol, FAT16_ROOT, name, name_size, text, length);
}

enum fat16_edit_result fat16_remove_at(const struct fat16_volume *vol, unsigned int directory,
    const unsigned char *text_name, unsigned int name_size)
{
    struct short_name name;
    if (!encode_name(&name, text_name, name_size))
        return FAT16_EDIT_NAME;
    struct edit_entry entry;
    enum fat16_edit_result result = find_entry(vol, directory, &name, &entry, 0);
    if (result == FAT16_EDIT_CASE)
        return FAT16_EDIT_MISSING;
    if (result != FAT16_EDIT_OK)
        return result;
    if (!entry.exists)
        return FAT16_EDIT_MISSING;
    if (!valid_chain(vol, &entry) || !update_directory(vol, &entry, 0, 0, 0, 0))
        return FAT16_EDIT_IO;
    return release_chain(vol, entry.first_cluster) ? FAT16_EDIT_OK : FAT16_EDIT_CLEANUP;
}


enum fat16_edit_result fat16_remove(const struct fat16_volume *vol,
    const unsigned char *name, unsigned int name_size)
{
    return fat16_remove_at(vol, FAT16_ROOT, name, name_size);
}

struct file_source {
    const struct fat16_volume *volume;
    unsigned int cluster, sector;
};

static int disk_sector(void *context, unsigned char sector[512], unsigned int size)
{
    struct file_source *source = context;
    const struct fat16_volume *vol = source->volume;
    (void)size;
    if (source->sector == vol->sectors_per_cluster) {
        if (!fat16_get_link(vol, source->cluster, &source->cluster))
            return 0;
        source->sector = 0;
    }
    if (!fat16_cluster_valid(vol, source->cluster))
        return 0;
    const unsigned int lba = fat16_cluster_lba(vol, source->cluster) + source->sector++;
    return ata_read_sector(vol->disk, lba, sector);
}

static unsigned int name_length(const unsigned char *name)
{
    unsigned int length = 0;
    while (name[length])
        ++length;
    return length;
}

static int same_alias(const struct short_name *one, const struct short_name *other)
{
    for (unsigned int i = 0; i < sizeof(one->alias); ++i)
        if (one->alias[i] != other->alias[i])
            return 0;
    return 1;
}

static enum fat16_edit_result rename_entry(const struct fat16_volume *vol,
    const struct edit_entry *entry, const struct short_name *name)
{
    unsigned char sector[512];
    if (!ata_read_sector(vol->disk, entry->lba, sector))
        return FAT16_EDIT_IO;
    unsigned char *slot = sector + (entry->index % 16) * 32;
    for (unsigned int i = 0; i < sizeof(name->alias); ++i)
        slot[i] = name->alias[i];
    slot[12] = (slot[12] & ~(FAT16_LOWER_BASE | FAT16_LOWER_EXTENSION)) | name->lower;
    if (!ata_write_sector(vol->disk, entry->lba, sector))
        return FAT16_EDIT_IO;
    modified_dir[mutations % MUTATION_WINDOW] = entry->directory;
    ++mutations;
    return FAT16_EDIT_OK;
}

static enum fat16_edit_result transfer_file(const struct fat16_volume *vol,
    unsigned int from, const unsigned char *old_name, unsigned int to,
    const unsigned char *new_name, int move, unsigned int flags)
{
    struct short_name old, next;
    const unsigned int old_length = name_length(old_name), new_length = name_length(new_name);
    if (!encode_name(&old, old_name, old_length) || !encode_name(&next, new_name, new_length))
        return FAT16_EDIT_NAME;
    if (!(flags & WRITE_ORIGINAL_NAME) && uppercase_txt(&next))
        return FAT16_EDIT_TEXT_EXTENSION;
    struct edit_entry origin, destination;
    enum fat16_edit_result result = find_entry(vol, from, &old, &origin, 0);
    if (result != FAT16_EDIT_OK)
        return result;
    if (!origin.exists)
        return FAT16_EDIT_MISSING;
    if (!valid_chain(vol, &origin))
        return FAT16_EDIT_IO;

    if (move && from == to) {
        if (!same_alias(&old, &next)) {
            result = find_entry(vol, to, &next, &destination, 1);
            if (result != FAT16_EDIT_OK)
                return result;
        }
        return rename_entry(vol, &origin, &next);
    }

    /* Give the destination its own FAT chain before touching the source entry. */
    struct file_source source = { .volume = vol, .cluster = origin.first_cluster };
    result = write_source(vol, to, new_name, new_length, origin.size, disk_sector, &source,
        flags | WRITE_EXCLUSIVE);
    if (result != FAT16_EDIT_OK || !move)
        return result;
    if (!update_directory(vol, &origin, 0, 0, 0, 0))
        return FAT16_EDIT_PARTIAL;
    return release_chain(vol, origin.first_cluster) ? FAT16_EDIT_OK : FAT16_EDIT_CLEANUP;
}

enum fat16_edit_result fat16_transfer(const struct fat16_volume *vol,
    unsigned int from, const unsigned char *old_name, unsigned int to,
    const unsigned char *new_name, int move)
{
    return transfer_file(vol, from, old_name, to, new_name, move, 0);
}

enum fat16_edit_result fat16_restore_file(const struct fat16_volume *vol,
    unsigned int from, const unsigned char *old_name, unsigned int to, const unsigned char *original_name)
{
    return transfer_file(vol, from, old_name, to, original_name, 1, WRITE_ORIGINAL_NAME);
}
