#include "package.h"

enum { RECORD_PREFIX = 8, RECORD_LIMIT = RECORD_PREFIX + PKG_METADATA_LIMIT };
static const char receipt[] = "PKG.DAT";

enum pkg_status pkg_check_crc(const struct pkg_source *source, unsigned int offset,
    unsigned int size, unsigned int expected);

struct memory_source {
    const unsigned char *bytes;
    unsigned int size;
};

static int memory_read(void *context, unsigned int offset, unsigned char *out, unsigned int size)
{
    const struct memory_source *source = context;
    if (offset > source->size || size > source->size - offset)
        return 0;
    for (unsigned int i = 0; i < size; ++i)
        out[i] = source->bytes[offset + i];
    return 1;
}

struct slice {
    const struct pkg_source *source;
    unsigned int offset;
};

static int slice_read(void *context, unsigned int offset, unsigned char *out, unsigned int size)
{
    const struct slice *slice = context;
    return slice->source->read(slice->source->context, slice->offset + offset, out, size);
}

struct stored_source {
    const struct pkg_storage *disk;
    struct pkg_file file;
};

static int stored_read(void *context, unsigned int offset, unsigned char *out, unsigned int size)
{
    const struct stored_source *source = context;
    return source->disk->read(source->disk->context, &source->file, offset, out, size);
}

static enum pkg_status load_record(const struct pkg_storage *disk, unsigned int directory,
    const char *id, unsigned char *record, unsigned int *size, struct pkg_manifest *pkg,
    enum pkg_state *state)
{
    struct pkg_file file;
    enum pkg_status result = disk->lookup(disk->context, directory, receipt, &file);
    if (result != PKG_OK)
        return result;
    if (file.size <= RECORD_PREFIX || file.size > RECORD_LIMIT)
        return PKG_FORMAT;
    if (!disk->read(disk->context, &file, 0, record, file.size))
        return PKG_IO;
    if (record[0] != 'G' || record[1] != 'T' || record[2] != 'P' || record[3] != 'I' ||
        record[4] != 1 || record[5] > PKG_REMOVING || record[6] || record[7])
        return PKG_FORMAT;
    struct memory_source memory = { record + RECORD_PREFIX, file.size - RECORD_PREFIX };
    const struct pkg_source source = { &memory, memory.size, memory_read };
    result = pkg_parse(&source, pkg, 1);
    if (result != PKG_OK || !pkg_alias_equal(pkg->id, id))
        return result == PKG_OK ? PKG_FORMAT : result;
    *size = file.size;
    *state = record[5];
    return PKG_OK;
}

enum pkg_status pkg_load(const struct pkg_storage *disk, unsigned int directory, const char *id,
    struct pkg_manifest *pkg, enum pkg_state *state)
{
    unsigned char record[RECORD_LIMIT];
    unsigned int size;
    return load_record(disk, directory, id, record, &size, pkg, state);
}

static enum pkg_status put_record(const struct pkg_storage *disk, unsigned int directory,
    unsigned char *record, unsigned int size, enum pkg_state state, int exclusive)
{
    record[5] = state;
    struct memory_source memory = { record, size };
    const struct pkg_source source = { &memory, size, memory_read };
    const enum pkg_status status = disk->write(disk->context, directory, receipt, &source, exclusive);
    if (status != PKG_OK)
        return status;
    struct pkg_file stored;
    unsigned char copy[RECORD_LIMIT];
    if (disk->lookup(disk->context, directory, receipt, &stored) != PKG_OK ||
        stored.size != size || !disk->read(disk->context, &stored, 0, copy, size))
        return PKG_IO;
    for (unsigned int i = 0; i < size; ++i)
        if (copy[i] != record[i])
            return PKG_IO;
    return PKG_OK;
}

static enum pkg_status verify_file(const struct pkg_storage *disk, unsigned int directory,
    const struct pkg_entry *entry)
{
    struct stored_source stored = { .disk = disk };
    enum pkg_status result = disk->lookup(disk->context, directory, entry->name, &stored.file);
    if (result != PKG_OK)
        return result;
    if (stored.file.size != entry->size)
        return PKG_CRC;
    const struct pkg_source source = { &stored, entry->size, stored_read };
    return pkg_check_crc(&source, 0, entry->size, entry->crc);
}

static enum pkg_status remove_owned(const struct pkg_storage *disk, unsigned int directory,
    const struct pkg_manifest *pkg, unsigned int *preserved)
{
    *preserved = 0;
    for (unsigned int i = 0; i < pkg->count; ++i) {
        const struct pkg_entry *entry = &pkg->files[i];
        const enum pkg_status status = verify_file(disk, directory, entry);
        if (status == PKG_MISSING)
            continue;
        if (status == PKG_CRC || status == PKG_CONFLICT) {
            ++*preserved;
            continue;
        }
        if (status != PKG_OK)
            return status;
        const enum pkg_status removed = disk->remove(disk->context, directory, entry->name);
        if (removed != PKG_OK)
            return removed;
    }
    return PKG_OK;
}

enum pkg_status pkg_remove(const struct pkg_storage *disk, unsigned int directory, const char *id,
    unsigned int *preserved)
{
    unsigned char record[RECORD_LIMIT];
    unsigned int size;
    struct pkg_manifest pkg;
    enum pkg_state state;
    enum pkg_status status = load_record(disk, directory, id, record, &size, &pkg, &state);
    if (status != PKG_OK)
        return status;
    if (state == PKG_INACTIVE)
        return PKG_MISSING;
    if (disk->running(disk->context, directory))
        return PKG_RUNNING;
    if (state == PKG_INSTALLED) {
        status = put_record(disk, directory, record, size, PKG_REMOVING, 0);
        if (status != PKG_OK)
            return status;
    }
    status = remove_owned(disk, directory, &pkg, preserved);
    return status == PKG_OK ? put_record(disk, directory, record, size, PKG_INACTIVE, 0) : status;
}

enum pkg_status pkg_install(const struct pkg_storage *disk, const struct pkg_source *source)
{
    struct pkg_manifest pkg;
    enum pkg_status status = pkg_validate(source, &pkg);
    if (status != PKG_OK)
        return status;

    unsigned int directory;
    status = disk->folder(disk->context, pkg.id, 0, &directory);
    const int create = status == PKG_MISSING;
    if (!create) {
        if (status != PKG_OK)
            return status;
        struct pkg_manifest previous;
        enum pkg_state state;
        status = pkg_load(disk, directory, pkg.id, &previous, &state);
        if (status != PKG_OK)
            return status == PKG_IO ? status : PKG_CONFLICT;
        if (state != PKG_INACTIVE)
            return state == PKG_INSTALLED ? PKG_DUPLICATE : PKG_PENDING;
        for (unsigned int i = 0; i < pkg.count; ++i) {
            struct pkg_file existing;
            status = disk->lookup(disk->context, directory, pkg.files[i].name, &existing);
            if (status != PKG_MISSING)
                return status == PKG_IO ? status : PKG_CONFLICT;
        }
    }
    const unsigned int size = RECORD_PREFIX + pkg.metadata_size;
    unsigned int available, block;
    if (!disk->space(disk->context, &available, &block) || !block)
        return PKG_IO;
    unsigned int needed = 2 * ((size + block - 1) / block) + create;
    for (unsigned int i = 0; i < pkg.count; ++i)
        needed += (pkg.files[i].size + block - 1) / block;
    if (needed > available)
        return PKG_FULL;

    unsigned char record[RECORD_LIMIT] = {'G', 'T', 'P', 'I', 1};
    if (!source->read(source->context, 0, record + RECORD_PREFIX, pkg.metadata_size))
        return PKG_IO;
    if (create) {
        status = disk->folder(disk->context, pkg.id, 1, &directory);
        if (status != PKG_OK)
            return status;
    }
    status = put_record(disk, directory, record, size, PKG_INSTALLING, create);
    if (status != PKG_OK)
        return status;

    for (unsigned int i = 0; i < pkg.count; ++i) {
        const struct pkg_entry *entry = &pkg.files[i];
        struct slice slice = { source, entry->offset };
        const struct pkg_source part = { &slice, entry->size, slice_read };
        status = disk->write(disk->context, directory, entry->name, &part, 1);
        if (status == PKG_OK)
            status = verify_file(disk, directory, entry);
        if (status != PKG_OK) {
            unsigned int preserved;
            if (pkg_remove(disk, directory, pkg.id, &preserved) != PKG_OK)
                return PKG_PENDING;
            return status;
        }
    }
    /* If publication is ambiguous, keep the verified payload for inspection on reopen. */
    return put_record(disk, directory, record, size, PKG_INSTALLED, 0);
}

const char *pkg_error(enum pkg_status status)
{
    static const char *const messages[] = {
        [PKG_MISSING] = "Arquivo ou pasta ausente",
        [PKG_IO] = "Falha de leitura ou gravacao",
        [PKG_FORMAT] = "Pacote ou registro invalido",
        [PKG_VERSION] = "Versao do pacote incompativel",
        [PKG_LIMIT] = "Pacote excede o limite de tamanho",
        [PKG_CRC] = "Conteudo com CRC incorreto",
        [PKG_APP] = "Executavel GTRA invalido",
        [PKG_CONFLICT] = "Conflito com arquivo ou pasta existente",
        [PKG_DUPLICATE] = "Aplicativo ja instalado",
        [PKG_FULL] = "Espaco insuficiente",
        [PKG_PENDING] = "Operacao incompleta. Abra a Central.",
        [PKG_RUNNING] = "Feche o aplicativo antes de remover"
    };
    return messages[status];
}
