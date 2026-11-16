#ifndef GPT_R_FILE_BROWSER_H
#define GPT_R_FILE_BROWSER_H

#ifdef GPT_R_GRAPHICS
enum { BROWSER_WIDTH = 736, BROWSER_HEIGHT = 498 };

void browser_init(void);
int browser_busy(void);
int browser_step(void);
int browser_open_target(const char *target);
int browser_context(unsigned int x, unsigned int y);
int browser_click(unsigned int x, unsigned int y);
int browser_key(unsigned int key);
void browser_draw(unsigned int x, unsigned int y, unsigned int clip_y, unsigned int clip_height);
#endif
#endif
