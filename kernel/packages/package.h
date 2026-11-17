#ifndef GPT_R_PACKAGE_H
#define GPT_R_PACKAGE_H

enum {
    PKG_HEADER_SIZE = 16, PKG_ENTRY_SIZE = 20, PKG_MANIFEST_LIMIT = 1024,
    PKG_FILES = 16, PKG_SIZE_LIMIT = 256 * 1024,
    PKG_METADATA_LIMIT = PKG_HEADER_SIZE + PKG_MANIFEST_LIMIT + PKG_FILES * PKG_ENTRY_SIZE
};

enum pkg_status {
    PKG_OK, PKG_MISSING, PKG_IO, PKG_FORMAT, PKG_VERSION, PKG_LIMIT, PKG_CRC,
    PKG_APP, PKG_CONFLICT, PKG_DUPLICATE, PKG_FULL, PKG_PENDING, PKG_RUNNING
};
enum pkg_state { PKG_INACTIVE, PKG_INSTALLING, PKG_INSTALLED, PKG_REMOVING };

struct pkg_source {
    void *context;
    unsigned int size;
    int (*read)(void *, unsigned int, unsigned char *, unsigned int);
};

struct pkg_entry {
    char name[13];
    unsigned int offset, size, crc;
};

struct pkg_manifest {
    char id[9], name[81], version[25], description[257];
    unsigned int count, main, metadata_size;
    struct pkg_entry files[PKG_FILES];
};

struct pkg_file {
    unsigned int size, location;
};

struct pkg_storage {
    void *context;
    enum pkg_status (*folder)(void *, const char *, int, unsigned int *);
    enum pkg_status (*lookup)(void *, unsigned int, const char *, struct pkg_file *);
    int (*read)(void *, const struct pkg_file *, unsigned int, unsigned char *, unsigned int);
    enum pkg_status (*write)(void *, unsigned int, const char *, const struct pkg_source *, int);
    enum pkg_status (*remove)(void *, unsigned int, const char *);
    int (*space)(void *, unsigned int *, unsigned int *);
    int (*running)(void *, unsigned int);
};

int pkg_alias_equal(const char *a, const char *b);
int pkg_valid_id(const char *id);
unsigned int pkg_utf8(const unsigned char *text, unsigned int size, unsigned int *cp);
enum pkg_status pkg_parse(const struct pkg_source *source, struct pkg_manifest *pkg, int metadata);
enum pkg_status pkg_validate(const struct pkg_source *source, struct pkg_manifest *pkg);
enum pkg_status pkg_load(const struct pkg_storage *disk, unsigned int directory, const char *id,
    struct pkg_manifest *pkg, enum pkg_state *state);
enum pkg_status pkg_install(const struct pkg_storage *disk, const struct pkg_source *source);
enum pkg_status pkg_remove(const struct pkg_storage *disk, unsigned int directory, const char *id,
    unsigned int *preserved);
const char *pkg_error(enum pkg_status status);

#endif
