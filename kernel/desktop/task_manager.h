#ifndef GPT_R_TASK_MANAGER_H
#define GPT_R_TASK_MANAGER_H

#ifdef GPT_R_GRAPHICS
#include "../apps/app.h"

enum { TASKS_WIDTH = 740, TASKS_HEIGHT = 333 };

struct desktop_task {
    const char *name;
    struct app_task_info app;
    int minimized;
};

void desktop_get_tasks(struct desktop_task tasks[APP_PROCESS_LIMIT]);
int desktop_end_task(unsigned int pid);
void tasks_init(void);
int tasks_refresh(void);
int tasks_click(unsigned int x, unsigned int y);
int tasks_key(unsigned int key);
void tasks_draw(unsigned int x, unsigned int y);
#endif
#endif
