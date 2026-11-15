#include "ata.h"

enum {
    DATA = 0x1f0, COUNT = 0x1f2, LBA_LOW = 0x1f3, LBA_MID = 0x1f4,
    LBA_HIGH = 0x1f5, DRIVE = 0x1f6, COMMAND = 0x1f7, CONTROL = 0x3f6,
    ERROR = 1, DRQ = 8, DEVICE_FAULT = 32, BUSY = 128,
    LBA28_LIMIT = 1 << 28
};

static void out_byte(unsigned short port, unsigned char value)
{
    __asm__ volatile ("outb %0, %w1" : : "a"(value), "Nd"(port));
}

static unsigned char in_byte(unsigned short port)
{
    unsigned char value;
    __asm__ volatile ("inb %w1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static unsigned short in_word(unsigned short port)
{
    unsigned short value;
    __asm__ volatile ("inw %w1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static void out_word(unsigned short port, unsigned short value)
{
    __asm__ volatile ("outw %0, %w1" : : "a"(value), "Nd"(port));
}

static void settle(void)
{
    for (unsigned int i = 0; i < 4; ++i)
        in_byte(CONTROL);
}

static int wait_status(unsigned int mask, unsigned int wanted, int check_error)
{
    for (unsigned int i = 0; i < 1000000; ++i) {
        const unsigned int status = in_byte(CONTROL);
        if (!status || status == 0xff)
            return 0;
        if (status & BUSY)
            continue;
        if (check_error && (status & (ERROR | DEVICE_FAULT)))
            return 0;
        if ((status & mask) == wanted)
            return 1;
    }
    return 0;
}

int ata_identify(struct ata_disk *disk)
{
    unsigned short words[256];

    /* Primary IDE master, with device IRQs disabled for PIO polling. */
    out_byte(CONTROL, 2);
    out_byte(DRIVE, 0xa0);
    settle();
    if (!wait_status(DRQ, 0, 0))
        return 0;
    out_byte(COUNT, 0);
    out_byte(LBA_LOW, 0);
    out_byte(LBA_MID, 0);
    out_byte(LBA_HIGH, 0);
    out_byte(COMMAND, 0xec);
    settle();
    if (!wait_status(DRQ, DRQ, 1) || in_byte(LBA_MID) || in_byte(LBA_HIGH))
        return 0;
    for (unsigned int i = 0; i < 256; ++i)
        words[i] = in_word(DATA);
    settle();
    if (!wait_status(DRQ, 0, 1) || !(words[49] & (1 << 9)))
        return 0;

    unsigned int sectors = words[60] | ((unsigned int)words[61] << 16);
    if (!sectors)
        return 0;
    disk->sectors = sectors > LBA28_LIMIT ? LBA28_LIMIT : sectors;
    for (unsigned int i = 0; i < 20; ++i) {
        disk->model[2 * i] = words[27 + i] >> 8;
        disk->model[2 * i + 1] = words[27 + i] & 255;
    }
    unsigned int end = 40;
    while (end && disk->model[end - 1] == ' ')
        --end;
    disk->model[end] = 0;
    return 1;
}

static int begin_transfer(const struct ata_disk *disk, unsigned int lba,
                          unsigned char command)
{
    if (lba >= disk->sectors || lba >= LBA28_LIMIT)
        return 0;
    out_byte(DRIVE, 0xe0 | (lba >> 24));
    settle();
    if (!wait_status(DRQ, 0, 0))
        return 0;
    out_byte(COUNT, 1);
    out_byte(LBA_LOW, lba);
    out_byte(LBA_MID, lba >> 8);
    out_byte(LBA_HIGH, lba >> 16);
    out_byte(COMMAND, command);
    settle();
    return wait_status(DRQ, DRQ, 1);
}

int ata_read_sector(const struct ata_disk *disk, unsigned int lba,
                    unsigned char data[ATA_SECTOR_SIZE])
{
    if (!begin_transfer(disk, lba, 0x20))
        return 0;
    for (unsigned int i = 0; i < ATA_SECTOR_SIZE; i += 2) {
        const unsigned short word = in_word(DATA);
        data[i] = word & 255;
        data[i + 1] = word >> 8;
    }
    settle();
    return wait_status(DRQ, 0, 1);
}

int ata_write_sector(const struct ata_disk *disk, unsigned int lba,
                     const unsigned char data[ATA_SECTOR_SIZE])
{
    if (!begin_transfer(disk, lba, 0x30))
        return 0;
    for (unsigned int i = 0; i < ATA_SECTOR_SIZE; i += 2)
        out_word(DATA, data[i] | ((unsigned short)data[i + 1] << 8));
    settle();
    if (!wait_status(DRQ, 0, 1))
        return 0;
    out_byte(COMMAND, 0xe7);
    settle();
    return wait_status(DRQ, 0, 1);
}
