#ifndef GPT_R_TIMER_H
#define GPT_R_TIMER_H

enum { TIMER_TICKS_PER_SECOND = 100 };

void timer_init(void);
unsigned int timer_ticks(void);
unsigned long long timer_uptime_seconds(void);
int rtc_read_time(unsigned int *hour, unsigned int *minute);

#endif
