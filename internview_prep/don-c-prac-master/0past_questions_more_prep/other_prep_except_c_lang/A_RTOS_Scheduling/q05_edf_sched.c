#ifndef EDF_SCHED_H
#define EDF_SCHED_H
#include "rtos_types.h"

#define EDF_MAX_TASKS 12

typedef struct {
    uint8_t  used;
    uint8_t  runnable;
    rtos_tick_t abs_deadline;
    rtos_tick_t period;
    rtos_tick_t next_release;
} edf_task_t;

typedef struct {
    edf_task_t tasks[EDF_MAX_TASKS];
    int current_tid;
} edf_sched_t;

void edf_init(edf_sched_t* s);
int  edf_add_task(edf_sched_t* s, rtos_tick_t first_release, rtos_tick_t period);
void edf_release(edf_sched_t* s, int tid, rtos_tick_t now);
int  edf_pick_next(edf_sched_t* s);
void edf_complete(edf_sched_t* s, int tid, rtos_tick_t now);
#endif