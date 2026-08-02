#ifndef RUNTIME_TRACE_H
#define RUNTIME_TRACE_H
#include "rtos_types.h"

#define TRACE_MAX_EVENTS 256

typedef struct {
    rtos_tick_t ts;
    uint8_t     event_id;
    uint8_t     arg;
} trace_event_t;

typedef struct {
    trace_event_t buf[TRACE_MAX_EVENTS];
    uint16_t head;
    uint16_t count;
} trace_log_t;

typedef struct {
    uint64_t run_ticks_acc[RTOS_MAX_TASKS];
    rtos_tick_t last_switch_tick;
    int last_tid;
} cpu_account_t;

void trace_init(trace_log_t* t);
void trace_log(trace_log_t* t, rtos_tick_t now, uint8_t event_id, uint8_t arg);

void cpu_account_init(cpu_account_t* a);
void cpu_account_on_switch(cpu_account_t* a, rtos_tick_t now, int new_tid);
void cpu_account_snapshot(const cpu_account_t* a, rtos_tick_t window, float* out_percent, size_t task_count);
#endif