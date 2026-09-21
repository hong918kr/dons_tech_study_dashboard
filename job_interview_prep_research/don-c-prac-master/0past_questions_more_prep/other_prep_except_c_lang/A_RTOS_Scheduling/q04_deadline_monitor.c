#ifndef DEADLINE_MONITOR_H
#define DEADLINE_MONITOR_H
#include "rtos_types.h"

#define DL_MAX_TASKS 16

typedef struct {
    uint8_t used;
    rtos_tick_t period;
    rtos_tick_t next_release;
    rtos_tick_t deadline;
    uint32_t miss_count;
    rtos_tick_t worst_lateness;
    rtos_tick_t total_jitter;
    uint32_t sample_count;
} dl_task_stat_t;

typedef struct {
    dl_task_stat_t stats[DL_MAX_TASKS];
} dl_monitor_t;

void dl_monitor_init(dl_monitor_t* m);
int  dl_monitor_register(dl_monitor_t* m, int tid, rtos_tick_t period, rtos_tick_t deadline);
void dl_on_release(dl_monitor_t* m, int tid, rtos_tick_t now);
void dl_on_complete(dl_monitor_t* m, int tid, rtos_tick_t now);
void dl_report(const dl_monitor_t* m);
#endif