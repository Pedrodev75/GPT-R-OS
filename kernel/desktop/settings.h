#ifndef GPT_R_DESKTOP_SETTINGS_H
#define GPT_R_DESKTOP_SETTINGS_H

#ifdef GPT_R_GRAPHICS
enum { SETTINGS_WIDTH = 548, SETTINGS_HEIGHT = 238 };

void settings_init(void);
int settings_green_theme(void);
int settings_clock_12h(void);
int settings_click(unsigned int x, unsigned int y);
int settings_key(unsigned int key);
void settings_draw(unsigned int x, unsigned int y);
#endif
#endif
