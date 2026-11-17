#include "app.h"

static unsigned int read_u32(const unsigned char *p)
{
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned int)p[3] << 24);
}

int app_validate_header(const unsigned char *h, unsigned int file_size, struct app_info *info)
{
    if (file_size <= APP_HEADER_SIZE || file_size > APP_HEADER_SIZE + APP_IMAGE_LIMIT ||
        read_u32(h) != 0x41525447u ||
        read_u32(h + 4) != ((APP_HEADER_SIZE << 16) | APP_FORMAT_VERSION) ||
        read_u32(h + 8) != file_size - APP_HEADER_SIZE)
        return 0;
    const unsigned int entry = read_u32(h + 12);
    if (entry >= file_size - APP_HEADER_SIZE)
        return 0;
    for (unsigned int i = 16; i < APP_HEADER_SIZE; ++i)
        if (h[i])
            return 0;
    *info = (struct app_info){ .image_size = file_size - APP_HEADER_SIZE, .entry = entry };
    return 1;
}
