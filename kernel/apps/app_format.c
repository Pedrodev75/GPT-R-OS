#include "app.h"

static unsigned int read_u16(const unsigned char *p)
{
    return p[0] | (p[1] << 8);
}

static unsigned int read_u32(const unsigned char *p)
{
    return read_u16(p) | ((unsigned int)read_u16(p + 2) << 16);
}

static void write_u32(unsigned char *p, unsigned int value)
{
    for (unsigned int i = 0; i < 4; ++i)
        p[i] = (unsigned char)(value >> (i * 8));
}

int app_header_size(const unsigned char *header, unsigned int available)
{
    if (available < APP_HEADER_PREFIX || read_u32(header) != 0x41525447u)
        return 0;
    const unsigned int version = read_u16(header + 4), size = read_u16(header + 6);
    if (version == APP_FORMAT_VERSION)
        return size == APP_HEADER_SIZE ? APP_HEADER_SIZE : 0;
    if (version == APP_V11_VERSION)
        return size == APP_V11_HEADER_SIZE ? APP_V11_HEADER_SIZE : 0;
    return -2;
}

int app_validate_header(const unsigned char *header, unsigned int available,
    unsigned int file_size, struct app_info *info)
{
    const int size = app_header_size(header, available);
    if (size <= 0 || available < (unsigned int)size || file_size <= (unsigned int)size ||
        file_size > APP_FILE_MAX)
        return 0;

    struct app_info parsed = { .version = read_u16(header + 4), .header_size = size };
    unsigned int reserved = 16;
    if (parsed.version == APP_FORMAT_VERSION) {
        parsed.code_size = read_u32(header + 8);
        parsed.entry = read_u32(header + 12);
        if (parsed.code_size > APP_IMAGE_LIMIT || parsed.code_size != file_size - APP_HEADER_SIZE)
            return 0;
    } else {
        parsed.code_size = read_u32(header + 12);
        parsed.data_size = read_u32(header + 20);
        parsed.bss_size = read_u32(header + 24);
        parsed.entry = read_u32(header + 28);
        reserved = 32;
        const unsigned long long data_offset = APP_V11_HEADER_SIZE + (unsigned long long)parsed.code_size;
        if (read_u32(header + 8) != APP_V11_HEADER_SIZE || read_u32(header + 16) != data_offset ||
            data_offset + parsed.data_size != file_size ||
            (unsigned long long)parsed.code_size + parsed.data_size + parsed.bss_size > APP_V11_IMAGE_LIMIT)
            return 0;
    }
    if (parsed.entry >= parsed.code_size)
        return 0;
    for (unsigned int i = reserved; i < parsed.header_size; ++i)
        if (header[i])
            return 0;
    *info = parsed;
    return 1;
}

void app_make_header(unsigned char *header, const struct app_info *info)
{
    for (unsigned int i = 0; i < info->header_size; ++i)
        header[i] = 0;
    write_u32(header, 0x41525447u);
    write_u32(header + 4, (info->header_size << 16) | info->version);
    if (info->version == APP_FORMAT_VERSION) {
        write_u32(header + 8, info->code_size);
        write_u32(header + 12, info->entry);
        return;
    }
    write_u32(header + 8, APP_V11_HEADER_SIZE);
    write_u32(header + 12, info->code_size);
    write_u32(header + 16, APP_V11_HEADER_SIZE + info->code_size);
    write_u32(header + 20, info->data_size);
    write_u32(header + 24, info->bss_size);
    write_u32(header + 28, info->entry);
}
