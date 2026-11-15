#ifndef GPT_R_ATA_H
#define GPT_R_ATA_H

enum { ATA_SECTOR_SIZE = 512 };

struct ata_disk {
    unsigned int sectors;
    char model[41];
};

enum ata_probe_result { ATA_MISSING, ATA_READY, ATA_PROBE_FAILED };
enum ata_probe_result ata_probe(struct ata_disk *disk);
int ata_identify(struct ata_disk *disk);
int ata_read_sector(const struct ata_disk *disk, unsigned int lba,
                    unsigned char data[ATA_SECTOR_SIZE]);

/* Transfer one sector and wait for the device to flush its write cache. */
int ata_write_sector(const struct ata_disk *disk, unsigned int lba,
                     const unsigned char data[ATA_SECTOR_SIZE]);

#endif
