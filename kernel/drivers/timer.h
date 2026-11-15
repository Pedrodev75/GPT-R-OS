#ifndef GPT_R_TIMER_H
#define GPT_R_TIMER_H

void timer_init(void);
unsigned int timer_ticks(void);
unsigned long long timer_uptime_seconds(void);

#endif
