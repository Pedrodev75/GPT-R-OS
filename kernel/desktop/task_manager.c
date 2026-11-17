#include "task_manager.h"

#ifdef GPT_R_GRAPHICS
#include "tool_view.h"
#include "../drivers/keyboard.h"
#include "../drivers/timer.h"

enum { TASK_TOP = 97, TASK_ROW = 44 };

static struct desktop_task snapshot[APP_PROCESS_LIMIT];
static unsigned int selected, sampled_at;
static unsigned int cpu_baseline[APP_PROCESS_LIMIT], cpu_percent[APP_PROCESS_LIMIT];
static const char *notice;

void tasks_init(void)
{
    selected = 0;
    notice = 0;
    desktop_get_tasks(snapshot);
    sampled_at = timer_ticks();
    for (unsigned int i = 0; i < APP_PROCESS_LIMIT; ++i) {
        cpu_baseline[i] = snapshot[i].app.cpu_ticks;
        cpu_percent[i] = 0;
    }
}

int tasks_refresh(void)
{
    struct desktop_task updated[APP_PROCESS_LIMIT];
    desktop_get_tasks(updated);
    const unsigned int now = timer_ticks(), elapsed = now - sampled_at;
    const int sample = elapsed >= TIMER_TICKS_PER_SECOND;
    int changed = 0, selection_exists = 0;
    for (unsigned int i = 0; i < APP_PROCESS_LIMIT; ++i) {
        const struct desktop_task *old = &snapshot[i], *next = &updated[i];
        if (old->app.pid != next->app.pid || old->app.state != next->app.state ||
            old->minimized != next->minimized ||
            old->app.memory_bytes != next->app.memory_bytes ||
            old->app.elapsed_ticks / TIMER_TICKS_PER_SECOND !=
            next->app.elapsed_ticks / TIMER_TICKS_PER_SECOND)
            changed = 1;
        if (old->app.pid != next->app.pid || next->app.state == APP_TASK_FINISHED) {
            changed |= cpu_percent[i] != 0;
            cpu_percent[i] = 0;
            cpu_baseline[i] = next->app.cpu_ticks;
        } else if (sample) {
            const unsigned int ticks = next->app.cpu_ticks - cpu_baseline[i];
            const unsigned int percent = ticks >= elapsed ? 100 :
                (unsigned long long)ticks * 100 / elapsed;
            changed |= percent != cpu_percent[i];
            cpu_percent[i] = percent;
            cpu_baseline[i] = next->app.cpu_ticks;
        }
        if (selected && selected == next->app.pid)
            selection_exists = 1;
        snapshot[i] = *next;
    }
    if (sample)
        sampled_at = now;
    if (selected && !selection_exists) {
        selected = 0;
        notice = 0;
        changed = 1;
    }
    return changed;
}

static int can_end(void)
{
    for (unsigned int i = 0; i < APP_PROCESS_LIMIT; ++i)
        if (selected && snapshot[i].app.pid == selected)
            return snapshot[i].app.state != APP_TASK_FINISHED;
    return 0;
}

static int end_selected(void)
{
    if (!can_end())
        return 0;
    notice = desktop_end_task(selected) ? "Programa encerrado" : "APP ja terminou";
    tasks_refresh();
    return 1;
}

int tasks_click(unsigned int x, unsigned int y)
{
    if (tool_hit(x, y, 360, 288, 164, 33))
        return selected && desktop_focus_task(selected);
    if (tool_hit(x, y, 535, 288, 189, 33))
        return end_selected();
    if (!tool_hit(x, y, 16, TASK_TOP, TASKS_WIDTH - 32, APP_PROCESS_LIMIT * TASK_ROW))
        return 0;
    unsigned int row = (y - TASK_TOP) / TASK_ROW;
    for (unsigned int i = 0; i < APP_PROCESS_LIMIT; ++i) {
        if (!snapshot[i].app.pid)
            continue;
        if (!row--) {
            selected = snapshot[i].app.pid;
            notice = 0;
            return 1;
        }
    }
    return 0;
}

int tasks_key(unsigned int key)
{
    if (key == '\n')
        return selected && desktop_focus_task(selected);
    if (key == KEY_DELETE)
        return end_selected();
    if (key != KEY_UP && key != KEY_DOWN)
        return 0;
    unsigned int current = APP_PROCESS_LIMIT;
    for (unsigned int i = 0; i < APP_PROCESS_LIMIT; ++i)
        if (selected && snapshot[i].app.pid == selected)
            current = i;
    for (unsigned int step = 0; step < APP_PROCESS_LIMIT; ++step) {
        const unsigned int i = key == KEY_DOWN ? step : APP_PROCESS_LIMIT - 1 - step;
        if (snapshot[i].app.pid && (current == APP_PROCESS_LIMIT ||
            (key == KEY_DOWN ? i > current : i < current))) {
            selected = snapshot[i].app.pid;
            notice = 0;
            return 1;
        }
    }
    return 0;
}

void tasks_draw(unsigned int x, unsigned int y)
{
    unsigned int open = 0, active = 0;
    for (unsigned int i = 0; i < APP_PROCESS_LIMIT; ++i) {
        if (!snapshot[i].app.pid)
            continue;
        ++open;
        active += snapshot[i].app.state != APP_TASK_FINISHED;
    }
    vga_graphics_fill(x, y, TASKS_WIDTH, TASKS_HEIGHT, TOOL_BG);
    vga_graphics_fill(x, y, TASKS_WIDTH, 60, TOOL_PANEL);
    vga_graphics_fill(x + 16, y + 16, 294, 27, TOOL_HILITE);
    tool_number(x + 25, y + 19, active, TOOL_GREEN);
    tool_label(x + 52, y + 19, "Processos ativos", TOOL_TEXT);
    vga_graphics_fill(x + 324, y + 16, 231, 27, TOOL_ROW);
    tool_number(x + 333, y + 19, open, TOOL_BLUE);
    tool_label(x + 360, y + 19, "Janelas APP", TOOL_TEXT);
    tool_label(x + 28, y + 70, "PID", TOOL_MUTED);
    tool_label(x + 107, y + 70, "Programa", TOOL_MUTED);
    tool_label(x + 324, y + 70, "Estado", TOOL_MUTED);
    tool_label(x + 484, y + 70, "CPU%", TOOL_MUTED);
    tool_label(x + 558, y + 70, "KiB", TOOL_MUTED);
    tool_label(x + 618, y + 70, "Tempo s", TOOL_MUTED);

    unsigned int row = 0;
    for (unsigned int i = 0; i < APP_PROCESS_LIMIT; ++i) {
        const struct desktop_task *task = &snapshot[i];
        if (!task->app.pid)
            continue;
        const unsigned int top = y + TASK_TOP + row * TASK_ROW;
        const int highlighted = selected == task->app.pid;
        vga_graphics_fill(x + 16, top, TASKS_WIDTH - 32, TASK_ROW,
            highlighted ? TOOL_HILITE : row % 2 ? TOOL_ROW : TOOL_BG);
        if (highlighted)
            vga_graphics_fill(x + 16, top + 6, 3, TASK_ROW - 12, TOOL_BLUE);
        tool_number(x + 25, top + 12, task->app.pid, TOOL_TEXT);
        tool_label(x + 107, top + 12, task->name, highlighted ? TOOL_BLUE : TOOL_TEXT);
        if (task->minimized)
            vga_graphics_fill(x + 304, top + 23, 10, 2, TOOL_MUTED);
        const enum app_task_state state = task->app.state;
        const char *status = state == APP_TASK_FINISHED ? "Encerrado" :
            state == APP_TASK_WAITING ? "Espera" : "Rodando";
        tool_label(x + 324, top + 12, status, state == APP_TASK_FINISHED ? TOOL_MUTED :
            state == APP_TASK_WAITING ? TOOL_GOLD : TOOL_GREEN);
        tool_number(x + 484, top + 12, cpu_percent[i], TOOL_TEXT);
        tool_number(x + 558, top + 12, task->app.memory_bytes / 1024, TOOL_TEXT);
        tool_number(x + 618, top + 12, task->app.elapsed_ticks / TIMER_TICKS_PER_SECOND, TOOL_TEXT);
        ++row;
    }
    if (!open)
        tool_label(x + 28, y + TASK_TOP + 27, "Nenhum programa aberto", TOOL_MUTED);
    vga_graphics_fill(x, y + 279, TASKS_WIDTH, 54, TOOL_PANEL);
    tool_label(x + 17, y + 294, notice ? notice : "Selecione um programa", TOOL_MUTED);
    tool_button(x + 360, y + 288, 164, "Trazer", TOOL_BLUE, selected != 0);
    tool_button(x + 535, y + 288, 189, "Encerrar", TOOL_RED, can_end());
}

#endif
