#ifndef PI_MUTEX_H
#define PI_MUTEX_H
#include "rtos_types.h"

typedef struct {
    uint8_t locked;
    uint8_t owner_tid;
    uint32_t original_owner_prio;
    uint32_t wait_mask;
} pi_mutex_t;

void pi_mutex_init(pi_mutex_t* m);
int  pi_mutex_lock(pi_mutex_t* m, int tid, uint8_t (*get_prio)(int), int (*set_prio)(int,uint8_t));
int  pi_mutex_unlock(pi_mutex_t* m, int tid, int (*set_prio)(int,uint8_t));
#endif