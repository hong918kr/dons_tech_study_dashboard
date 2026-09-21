#ifndef TICKLESS_IDLE_H
#define TICKLESS_IDLE_H
#include "rtos_types.h"

typedef struct {
    rtos_tick_t next_wake_tick;
    rtos_tick_t safety_margin;
    rtos_tick_t max_sleep_ticks;
} tickless_ctx_t;

rtos_tick_t tickless_compute_sleep(tickless_ctx_t* c, rtos_tick_t now, rtos_tick_t earliest_wake);
void tickless_account(rtos_tick_t slept, rtos_tick_t* now_mut);
#endif