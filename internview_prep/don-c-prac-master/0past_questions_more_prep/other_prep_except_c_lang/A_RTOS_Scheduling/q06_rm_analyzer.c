#ifndef RM_ANALYZER_H
#define RM_ANALYZER_H
#include "rtos_types.h"

typedef struct {
    rtos_tick_t C;
    rtos_tick_t T;
} rm_task_param_t;

typedef struct {
    float utilization;
    int   schedulable_bound;
} rm_result_t;

rm_result_t rm_analyze(const rm_task_param_t* tasks, size_t count);
#endif