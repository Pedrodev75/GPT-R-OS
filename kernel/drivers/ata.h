#ifndef GPT_R_ATA_H
#define GPT_R_ATA_H

enum { ATA_SECTOR_SIZE = 512 };

struct ata_disk {
    unsigned int sectors;
    char model[41];
};

int ata_identify(struct ata_disk *disk);
int ata_read_sector(const struct ata_disk *disk, unsigned int lba,
                    unsigned char data[ATA_SECTOR_SIZE]);

#endif
