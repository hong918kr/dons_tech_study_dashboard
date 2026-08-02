#ifndef AGING_HOOK_H
#define AGING_HOOK_H
#include "rtos_types.h"

typedef struct {
    uint32_t wait_ticks;
    uint8_t  base_prio;
    uint8_t  current_prio;
} aging_task_info_t;

void aging_on_wait_tick(aging_task_info_t* info);
void aging_apply_policy(aging_task_info_t* info, uint32_t threshold, uint8_t boost_limit);
void aging_reset(aging_task_info_t* info);
#endif