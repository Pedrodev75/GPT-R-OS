#ifndef GPT_R_FILE_ICONS_H
#define GPT_R_FILE_ICONS_H

#ifdef GPT_R_GRAPHICS

enum file_icon_kind { FILE_ICON_GENERIC, FILE_ICON_FOLDER, FILE_ICON_ASM, FILE_ICON_APP, FILE_ICON_TEXT };

enum file_icon_kind file_icon_kind(const char *name, int folder);
unsigned int file_icon_color(enum file_icon_kind kind);
void file_icon_draw(enum file_icon_kind kind, unsigned int x, unsigned int y,
    unsigned int scale, unsigned int background);

#endif
#endif
