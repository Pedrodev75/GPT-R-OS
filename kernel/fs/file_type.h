#ifndef GPT_R_FILE_TYPE_H
#define GPT_R_FILE_TYPE_H

static inline int file_has_extension(const char *path, const char *extension)
{
    const char *dot = 0;
    for (; *path; ++path) {
        if (*path == '/')
            dot = 0;
        else if (*path == '.')
            dot = path;
    }
    if (!dot)
        return 0;
    while (*dot && *extension) {
        char ch = *dot++;
        if (ch >= 'a' && ch <= 'z')
            ch -= 'a' - 'A';
        if (ch != *extension++)
            return 0;
    }
    return !*dot && !*extension;
}

#endif
