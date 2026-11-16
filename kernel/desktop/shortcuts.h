#ifndef GPT_R_SHORTCUTS_H
#define GPT_R_SHORTCUTS_H

#ifdef GPT_R_GRAPHICS

enum {
    SHORTCUT_WIDTH = 86, SHORTCUT_HEIGHT = 75, SHORTCUT_PATH_LIMIT = 222,
    SHORTCUT_COUNT = 12
};
enum shortcut_kind { SHORTCUT_FILE, SHORTCUT_FOLDER, SHORTCUT_APP };
enum shortcut_result { SHORTCUT_SAVED, SHORTCUT_SESSION, SHORTCUT_FULL, SHORTCUT_EXISTS };

struct desktop_shortcut {
    unsigned short x, y;
    enum shortcut_kind kind;
    char target[SHORTCUT_PATH_LIMIT];
};

void shortcuts_init(void);
const struct desktop_shortcut *shortcuts_at(unsigned int index);
int shortcuts_pick(unsigned int x, unsigned int y);
void shortcuts_draw(unsigned int selected);
enum shortcut_result shortcuts_add(const char *path, enum shortcut_kind kind, unsigned int *index);
int shortcuts_move(unsigned int index, unsigned int x, unsigned int y);

#endif
#endif
