#include "package.h"
#include "../apps/app.h"
#include "../fs/file_type.h"

static unsigned int le16(const unsigned char *p)
{
    return p[0] | (p[1] << 8);
}

static unsigned int le32(const unsigned char *p)
{
    return le16(p) | (le16(p + 2) << 16);
}

static char upper(char ch)
{
    return ch >= 'a' && ch <= 'z' ? ch - ('a' - 'A') : ch;
}

int pkg_alias_equal(const char *a, const char *b)
{
    while (*a && upper(*a) == upper(*b)) {
        ++a;
        ++b;
    }
    return upper(*a) == upper(*b);
}

static int name_byte(char ch)
{
    const char uc = upper(ch);
    return (uc >= 'A' && uc <= 'Z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '_';
}

int pkg_valid_id(const char *id)
{
    unsigned int length = 0;
    while (id[length]) {
        if (length == 8 || !name_byte(id[length]) || upper(id[length]) != id[length])
            return 0;
        ++length;
    }
    return length != 0;
}

static int file_name(const char *name)
{
    unsigned int length = 0, limit = 8, letter_case = 0;
    for (; *name; ++name) {
        const char ch = *name;
        if (ch == '.' && limit == 8 && length) {
            length = letter_case = 0;
            limit = 3;
            continue;
        }
        if (!name_byte(ch) || ++length > limit)
            return 0;
        if (ch >= 'A' && ch <= 'Z')
            letter_case |= 1;
        if (ch >= 'a' && ch <= 'z')
            letter_case |= 2;
        if (letter_case == 3)
            return 0;
    }
    return length != 0;
}

unsigned int pkg_utf8(const unsigned char *text, unsigned int size, unsigned int *cp)
{
    if (!size)
        return 0;
    unsigned int value = text[0], length = 1, minimum = 0;
    if (value >= 0xc2 && value <= 0xdf) {
        value &= 31;
        length = 2;
        minimum = 0x80;
    } else if (value >= 0xe0 && value <= 0xef) {
        value &= 15;
        length = 3;
        minimum = 0x800;
    } else if (value >= 0xf0 && value <= 0xf4) {
        value &= 7;
        length = 4;
        minimum = 0x10000;
    } else if (value >= 0x80) {
        return 0;
    }
    if (length > size)
        return 0;
    for (unsigned int i = 1; i < length; ++i) {
        if ((text[i] & 0xc0) != 0x80)
            return 0;
        value = (value << 6) | (text[i] & 63);
    }
    if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff))
        return 0;
    *cp = value;
    return length;
}

static int manifest_fields(struct pkg_manifest *pkg, const unsigned char *text, unsigned int size)
{
    const struct {
        const char *key;
        char *value;
        unsigned int limit;
    } fields[] = {
        {"id", pkg->id, sizeof(pkg->id)}, {"name", pkg->name, sizeof(pkg->name)},
        {"version", pkg->version, sizeof(pkg->version)},
        {"description", pkg->description, sizeof(pkg->description)}
    };
    unsigned int seen = 0;
    for (unsigned int at = 0; at < size;) {
        unsigned int end = at;
        while (end < size && text[end] != '\n')
            ++end;
        const unsigned int next = end + (end < size);
        if (end > at && text[end - 1] == '\r')
            --end;
        unsigned int field = 0, first = at;
        for (; field < sizeof(fields) / sizeof(fields[0]); ++field) {
            const char *key = fields[field].key;
            first = at;
            while (*key && first < end && text[first] == (unsigned char)*key) {
                ++first;
                ++key;
            }
            if (!*key && first < end && text[first] == '=')
                break;
        }
        if (field == sizeof(fields) / sizeof(fields[0]) || (seen & (1u << field)))
            return 0;
        ++first;
        const unsigned int length = end - first;
        if (length >= fields[field].limit || (!length && field < 3))
            return 0;
        for (unsigned int i = first; i < end;) {
            unsigned int cp;
            const unsigned int bytes = pkg_utf8(text + i, end - i, &cp);
            if (!bytes || cp < ' ' || (cp >= 127 && cp <= 159))
                return 0;
            i += bytes;
        }
        for (unsigned int i = 0; i < length; ++i)
            fields[field].value[i] = field == 0 ? upper(text[first + i]) : text[first + i];
        fields[field].value[length] = 0;
        seen |= 1u << field;
        at = next;
    }
    return (seen & 7) == 7 && pkg_valid_id(pkg->id);
}

enum pkg_status pkg_parse(const struct pkg_source *source, struct pkg_manifest *pkg, int metadata)
{
    unsigned char header[PKG_HEADER_SIZE], manifest[PKG_MANIFEST_LIMIT];
    const unsigned int limit = metadata ? PKG_METADATA_LIMIT : PKG_SIZE_LIMIT;
    if (source->size < sizeof(header))
        return PKG_FORMAT;
    if (source->size > limit)
        return PKG_LIMIT;
    if (!source->read(source->context, 0, header, sizeof(header)))
        return PKG_IO;
    if (le32(header) != 0x50525447u)
        return PKG_FORMAT;
    if (le16(header + 4) != 1)
        return PKG_VERSION;
    const unsigned int length = le32(header + 8), count = le16(header + 12), main = le16(header + 14);
    if (le16(header + 6) != sizeof(header) || !length || length > sizeof(manifest) ||
        !count || count > PKG_FILES || main >= count)
        return PKG_FORMAT;
    unsigned int table = sizeof(header) + length;
    const unsigned int data = table + count * PKG_ENTRY_SIZE;
    if (data > source->size || (metadata && data != source->size))
        return PKG_FORMAT;
    if (!source->read(source->context, sizeof(header), manifest, length))
        return PKG_IO;
    *pkg = (struct pkg_manifest){ .count = count, .main = main, .metadata_size = data };
    if (!manifest_fields(pkg, manifest, length))
        return PKG_FORMAT;
    unsigned int offset = data;
    for (unsigned int i = 0; i < count; ++i, table += PKG_ENTRY_SIZE) {
        unsigned char item[PKG_ENTRY_SIZE];
        if (!source->read(source->context, table, item, sizeof(item)))
            return PKG_IO;
        struct pkg_entry *file = &pkg->files[i];
        unsigned int size = 0;
        while (size < 12 && item[size]) {
            file->name[size] = item[size];
            ++size;
        }
        while (size < 12)
            if (item[size++])
                return PKG_FORMAT;
        if (!file_name(file->name) || pkg_alias_equal(file->name, "PKG.DAT"))
            return PKG_FORMAT;
        for (unsigned int j = 0; j < i; ++j)
            if (pkg_alias_equal(file->name, pkg->files[j].name))
                return PKG_FORMAT;
        file->offset = offset;
        file->size = le32(item + 12);
        file->crc = le32(item + 16);
        if (file->size > (unsigned int)PKG_SIZE_LIMIT - offset ||
            (!metadata && file->size > source->size - offset))
            return PKG_FORMAT;
        offset += file->size;
    }
    const struct pkg_entry *app = &pkg->files[main];
    if (!file_has_extension(app->name, ".APP") || app->size <= APP_HEADER_SIZE ||
        app->size > APP_HEADER_SIZE + APP_IMAGE_LIMIT)
        return PKG_APP;
    return metadata || offset == source->size ? PKG_OK : PKG_FORMAT;
}

enum pkg_status pkg_check_crc(const struct pkg_source *source, unsigned int offset,
    unsigned int size, unsigned int expected)
{
    unsigned char bytes[512];
    unsigned int crc = 0xffffffffu;
    while (size) {
        const unsigned int count = size < sizeof(bytes) ? size : sizeof(bytes);
        if (!source->read(source->context, offset, bytes, count))
            return PKG_IO;
        for (unsigned int i = 0; i < count; ++i) {
            crc ^= bytes[i];
            for (unsigned int bit = 0; bit < 8; ++bit)
                crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
        }
        offset += count;
        size -= count;
    }
    return (crc ^ 0xffffffffu) == expected ? PKG_OK : PKG_CRC;
}

enum pkg_status pkg_validate(const struct pkg_source *source, struct pkg_manifest *pkg)
{
    enum pkg_status status = pkg_parse(source, pkg, 0);
    if (status != PKG_OK)
        return status;
    for (unsigned int i = 0; i < pkg->count; ++i) {
        const struct pkg_entry *file = &pkg->files[i];
        status = pkg_check_crc(source, file->offset, file->size, file->crc);
        if (status != PKG_OK)
            return status;
    }
    struct app_info info;
    unsigned char header[APP_HEADER_SIZE];
    const struct pkg_entry *app = &pkg->files[pkg->main];
    if (!source->read(source->context, app->offset, header, sizeof(header)))
        return PKG_IO;
    return app_validate_header(header, app->size, &info) ? PKG_OK : PKG_APP;
}
