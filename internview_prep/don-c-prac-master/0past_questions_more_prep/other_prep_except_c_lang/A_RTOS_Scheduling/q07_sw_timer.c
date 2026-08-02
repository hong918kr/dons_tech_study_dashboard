#ifndef SW_TIMER_H
#define SW_TIMER_H
#include "rtos_types.h"

#define SW_TIMER_MAX 32
typedef void (*sw_timer_cb_t)(void*);

typedef struct {
    uint8_t      used;
    rtos_tick_t  expire_tick;
    rtos_tick_t  period;
    sw_timer_cb_t cb;
    void*        arg;
} sw_timer_t;

typedef struct {
    sw_timer_t timers[SW_TIMER_MAX];
    int heap[SW_TIMER_MAX];
    size_t heap_size;
} sw_timer_mgr_t;

void sw_timer_init(sw_timer_mgr_t* m);
int  sw_timer_start(sw_timer_mgr_t* m, rtos_tick_t now, rtos_tick_t delay, rtos_tick_t period, sw_timer_cb_t cb, void* arg);
int  sw_timer_stop(sw_timer_mgr_t* m, int id);
void sw_timer_tick(sw_timer_mgr_t* m, rtos_tick_t now);
#endif