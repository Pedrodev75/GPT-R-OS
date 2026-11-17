#ifndef GPT_R_APP_CENTER_H
#define GPT_R_APP_CENTER_H

#ifdef GPT_R_GRAPHICS
#include "../fs/fat16.h"

enum { CENTER_WIDTH = 828, CENTER_HEIGHT = 408 };
void center_init(void);
void center_open_package(unsigned int directory, const struct fat16_file *file);
int center_key(unsigned int key);
int center_click(unsigned int x, unsigned int y);
void center_draw(unsigned int x, unsigned int y);
#endif
#endif
