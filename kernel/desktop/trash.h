#ifndef GPT_R_TRASH_H
#define GPT_R_TRASH_H

#ifdef GPT_R_GRAPHICS
#include "../fs/fat16_path.h"

enum { TRASH_WIDTH = 800, TRASH_HEIGHT = 394 };

const char *trash_send(const struct fat16_volume *volume, const struct fat16_path *directory,
    const struct fat16_file *file);
void trash_init(void);
int trash_refresh(void);
int trash_key(unsigned int key);
int trash_click(unsigned int x, unsigned int y);
void trash_draw(unsigned int x, unsigned int y);
#endif
#endif
