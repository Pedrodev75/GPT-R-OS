#include "fat16.h"

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

int fat16_next(const struct fat16_volume* volume, struct fat16_dir* dir,
    struct fat16_file* file)
{
    while (dir->index < volume->root_entries) {
        const unsigned int index = dir->index;
        if (!(index % 16) &&
            !ata_read_sector(volume->disk, volume->root_start + index / 16, dir->sector))
            return -1;
        ++dir->index;
        const unsigned char* entry = dir->sector + (index % 16) * 32;
        if (!entry[0]) {
            dir->index = volume->root_entries;
            return 0;
        }
        /* Ignore deleted, volume-label, long-name and subdirectory entries. */
        if (entry[0] == 0xe5 || (entry[11] & 0x18))
            continue;

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
        return 1;
    }
    return 0;
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
    unsigned char data[512];
    if ((unsigned long long)remaining > (unsigned long long)volume->clusters *
        volume->sectors_per_cluster * 512)
        return FAT16_READ_ERROR;

    while (remaining) {
        if (cluster < 2 || cluster >= volume->clusters + 2 || cluster >= 0xfff0)
            return FAT16_READ_ERROR;
        const unsigned int first = volume->data_start +
            (cluster - 2) * volume->sectors_per_cluster;
        for (unsigned int i = 0; i < volume->sectors_per_cluster && remaining; ++i) {
            if (!ata_read_sector(volume->disk, first + i, data))
                return FAT16_READ_ERROR;
            const unsigned int count = remaining < 512 ? remaining : 512;
            if (!consume(context, data, count))
                return FAT16_READ_STOPPED;
            remaining -= count;
        }
        if (!ata_read_sector(volume->disk, volume->fat_start + cluster / 256, data))
            return FAT16_READ_ERROR;
        cluster = little16(data + (cluster % 256) * 2);
        if (!remaining)
            return cluster >= 0xfff8 ? FAT16_READ_OK : FAT16_READ_ERROR;
    }
    return FAT16_READ_OK;
}
