#include "fat16_internal.h"

static unsigned int little16(const unsigned char* bytes)
{
    return bytes[0] | ((unsigned int)bytes[1] << 8);
}

static unsigned int little32(const unsigned char* bytes)
{
    return little16(bytes) | (little16(bytes + 2) << 16);
}

static int parse_bpb(struct fat16_volume* volume, const struct ata_disk* disk,
    const unsigned char* boot, unsigned int start,
    unsigned int available)
{
    const unsigned int spc = boot[13];
    const unsigned int reserved = little16(boot + 14);
    const unsigned int fats = boot[16];
    const unsigned int entries = little16(boot + 17);
    const unsigned int fat_sectors = little16(boot + 22);
    unsigned int total = little16(boot + 19);
    if (!total)
        total = little32(boot + 32);
    if (little16(boot + 510) != 0xaa55 || little16(boot + 11) != 512 ||
        !spc || spc > 128 || (spc & (spc - 1)) ||
        !reserved || !fats || !entries || !fat_sectors || total > available)
        return 0;

    const unsigned int root = reserved + fats * fat_sectors;
    const unsigned int data = root + (entries + 15) / 16;
    if (total <= data)
        return 0;
    const unsigned int clusters = (total - data) / spc;
    if (clusters < 4085 || clusters >= 65525 ||
        fat_sectors * 256 < clusters + 2)
        return 0;
    *volume = (struct fat16_volume){
        disk, start + reserved, fat_sectors, fats, start + root, start + data,
        clusters, entries, spc
    };
    return 1;
}

int fat16_mount(struct fat16_volume* volume, const struct ata_disk* disk)
{
    unsigned char boot[512], partition[512];
    if (!ata_read_sector(disk, 0, boot))
        return 0;
    if (parse_bpb(volume, disk, boot, 0, disk->sectors))
        return 1;
    if (little16(boot + 510) != 0xaa55)
        return 0;
    for (unsigned int i = 0; i < 4; ++i) {
        const unsigned char* entry = boot + 446 + 16 * i;
        const unsigned int type = entry[4];
        const unsigned int start = little32(entry + 8);
        const unsigned int size = little32(entry + 12);
        if ((type == 4 || type == 6 || type == 0x0e) && start &&
            start < disk->sectors && size <= disk->sectors - start &&
            ata_read_sector(disk, start, partition) &&
            parse_bpb(volume, disk, partition, start, size))
            return 1;
    }
    return 0;
}

static char name_char(unsigned char ch, int lower)
{
    return lower && ch >= 'A' && ch <= 'Z' ? ch + ('a' - 'A') : ch;
}

int fat16_get_link(const struct fat16_volume *volume, unsigned int cluster, unsigned int *next)
{
    unsigned char sector[ATA_SECTOR_SIZE];
    if (!ata_read_sector(volume->disk, volume->fat_start + cluster / 256, sector))
        return 0;
    *next = little16(sector + (cluster % 256) * 2);
    return 1;
}

int fat16_slot(const struct fat16_volume *volume, struct fat16_dir *dir,
    const unsigned char **slot)
{
    if (dir->ended)
        return 0;
    if (!dir->first_cluster && dir->index >= volume->root_entries)
        return 0;
    if (!(dir->index % 16)) {
        unsigned int lba = volume->root_start + dir->index / 16;
        if (dir->first_cluster) {
            if (!dir->index) {
                dir->cluster = dir->cycle_anchor = dir->first_cluster;
                dir->cycle_period = 1;
            } else if (++dir->cluster_sector == volume->sectors_per_cluster) {
                unsigned int next;
                if (!fat16_get_link(volume, dir->cluster, &next))
                    return -1;
                if (next >= 0xfff8) {
                    dir->ended = 1;
                    return 0;
                }
                if (next == dir->cycle_anchor || ++dir->links >= volume->clusters)
                    return -1;
                if (++dir->cycle_span == dir->cycle_period) {
                    dir->cycle_anchor = next;
                    dir->cycle_span = 0;
                    dir->cycle_period *= 2;
                }
                dir->cluster = next;
                dir->cluster_sector = 0;
            }
            if (!fat16_cluster_valid(volume, dir->cluster))
                return -1;
            lba = fat16_cluster_lba(volume, dir->cluster) + dir->cluster_sector;
        }
        if (!ata_read_sector(volume->disk, lba, dir->sector))
            return -1;
        dir->lba = lba;
    }
    *slot = dir->sector + (dir->index++ % 16) * 32;
    if (!(*slot)[0])
        dir->ended = 1;
    return 1;
}

static int decode_entry(const unsigned char *entry, struct fat16_file *file)
{
    if (!entry[0] || entry[0] == 0xe5 || entry[0] == '.' || (entry[11] & 0x08))
        return 0;
    unsigned int length = 0;
    unsigned int base = 8, extension = 3;
    while (base && entry[base - 1] == ' ')
        --base;
    while (extension && entry[8 + extension - 1] == ' ')
        --extension;
    for (unsigned int i = 0; i < base; ++i)
        file->name[length++] = name_char(entry[i], entry[12] & FAT16_LOWER_BASE);
    if (extension)
        file->name[length++] = '.';
    for (unsigned int i = 0; i < extension; ++i)
        file->name[length++] = name_char(entry[8 + i], entry[12] & FAT16_LOWER_EXTENSION);
    file->name[length] = 0;
    file->modified_date = little16(entry + 24);
    file->first_cluster = little16(entry + 26);
    file->size = little32(entry + 28);
    file->is_directory = !!(entry[11] & 0x10);
    return 1;
}

enum fat16_scan_result fat16_scan(const struct fat16_volume *volume,
    struct fat16_dir *dir, struct fat16_file *file)
{
    const unsigned char *slot;
    const int result = fat16_slot(volume, dir, &slot);
    if (result <= 0)
        return result;
    return decode_entry(slot, file) ? FAT16_SCAN_ENTRY : FAT16_SCAN_SKIP;
}

int fat16_next_at(const struct fat16_volume *volume, struct fat16_dir *dir,
    struct fat16_file *file)
{
    enum fat16_scan_result result;
    do {
        result = fat16_scan(volume, dir, file);
    } while (result == FAT16_SCAN_SKIP);
    return result;
}

int fat16_next(const struct fat16_volume *volume, struct fat16_dir *dir,
    struct fat16_file *file)
{
    int result;
    while ((result = fat16_next_at(volume, dir, file)) > 0 && file->is_directory) {}
    return result;
}

static int lookup_at(const struct fat16_volume *volume, unsigned int directory,
    const unsigned char *name, struct fat16_file *file, int include_directories)
{
    struct fat16_dir dir = { .first_cluster = directory };
    int result;
    while ((result = fat16_next_at(volume, &dir, file)) > 0) {
        if (file->is_directory && !include_directories)
            continue;
        unsigned int i = 0;
        while (name[i] && name[i] == (unsigned char)file->name[i])
            ++i;
        if (!name[i] && !file->name[i])
            break;
    }
    return result;
}

int fat16_lookup_at(const struct fat16_volume *volume, unsigned int directory,
    const unsigned char *name, struct fat16_file *file)
{
    return lookup_at(volume, directory, name, file, 0);
}

int fat16_find_at(const struct fat16_volume *volume, unsigned int directory,
    const unsigned char *name, struct fat16_file *file)
{
    return lookup_at(volume, directory, name, file, 1);
}

int fat16_space(const struct fat16_volume *volume, unsigned int *total,
    unsigned int *free)
{
    unsigned char sector[ATA_SECTOR_SIZE];
    const unsigned int end = volume->clusters + 2 < 0xfff0 ?
        volume->clusters + 2 : 0xfff0;
    unsigned int available = 0;
    for (unsigned int cluster = 2; cluster < end; ++cluster) {
        if ((cluster == 2 || !(cluster % 256)) &&
            !ata_read_sector(volume->disk, volume->fat_start + cluster / 256, sector))
            return 0;
        if (!little16(sector + (cluster % 256) * 2))
            ++available;
    }
    *total = end - 2;
    *free = available;
    return 1;
}

enum fat16_read_result fat16_read(const struct fat16_volume* volume, const struct fat16_file* file,
    int (*consume)(void*, const unsigned char*, unsigned int),
    void* context)
{
    unsigned int remaining = file->size, cluster = file->first_cluster;
    unsigned char data[512], links[512];
    unsigned int cached_fat = 0;
    if ((unsigned long long)remaining > (unsigned long long)volume->clusters *
        volume->sectors_per_cluster * 512)
        return FAT16_READ_ERROR;

    while (remaining) {
        if (!fat16_cluster_valid(volume, cluster))
            return FAT16_READ_ERROR;
        const unsigned int first = fat16_cluster_lba(volume, cluster);
        for (unsigned int i = 0; i < volume->sectors_per_cluster && remaining; ++i) {
            if (!ata_read_sector(volume->disk, first + i, data))
                return FAT16_READ_ERROR;
            const unsigned int count = remaining < 512 ? remaining : 512;
            if (!consume(context, data, count))
                return FAT16_READ_STOPPED;
            remaining -= count;
        }
        const unsigned int sector = volume->fat_start + cluster / 256;
        if (sector != cached_fat) {
            if (!ata_read_sector(volume->disk, sector, links))
                return FAT16_READ_ERROR;
            cached_fat = sector;
        }
        cluster = little16(links + (cluster % 256) * 2);
        if (!remaining)
            return cluster >= 0xfff8 ? FAT16_READ_OK : FAT16_READ_ERROR;
    }
    return FAT16_READ_OK;
}

int fat16_read_range(const struct fat16_volume *vol, const struct fat16_file *file,
    unsigned int offset, unsigned char *data, unsigned int size)
{
    if (offset > file->size || size > file->size - offset ||
        (unsigned long long)file->size > (unsigned long long)vol->clusters *
            vol->sectors_per_cluster * ATA_SECTOR_SIZE)
        return 0;
    if (!size)
        return 1;
    const int reaches_end = size == file->size - offset;
    const unsigned int cluster_bytes = vol->sectors_per_cluster * ATA_SECTOR_SIZE;
    unsigned int cluster = file->first_cluster;
    while (offset >= cluster_bytes) {
        if (!fat16_cluster_valid(vol, cluster) || !fat16_get_link(vol, cluster, &cluster))
            return 0;
        offset -= cluster_bytes;
    }
    unsigned char sector[ATA_SECTOR_SIZE];
    while (size) {
        if (!fat16_cluster_valid(vol, cluster))
            return 0;
        const unsigned int byte = offset % ATA_SECTOR_SIZE;
        const unsigned int lba = fat16_cluster_lba(vol, cluster) + offset / ATA_SECTOR_SIZE;
        if (!ata_read_sector(vol->disk, lba, sector))
            return 0;
        const unsigned int available = ATA_SECTOR_SIZE - byte;
        const unsigned int copy = size < available ? size : available;
        for (unsigned int i = 0; i < copy; ++i)
            data[i] = sector[byte + i];
        data += copy;
        size -= copy;
        offset += copy;
        if (size && offset == cluster_bytes) {
            if (!fat16_get_link(vol, cluster, &cluster))
                return 0;
            offset = 0;
        }
    }
    if (reaches_end) {
        unsigned int next;
        return fat16_get_link(vol, cluster, &next) && next >= 0xfff8;
    }
    return 1;
}
