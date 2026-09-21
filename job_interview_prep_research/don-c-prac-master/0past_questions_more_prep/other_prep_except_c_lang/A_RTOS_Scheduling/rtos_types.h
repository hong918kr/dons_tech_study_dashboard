#ifndef RTOS_TYPES_H
#define RTOS_TYPES_H
#include <stdint.h>
#include <stddef.h>

typedef uint32_t rtos_tick_t;
typedef int32_t  rtos_err_t;

enum {
    RTOS_OK = 0,
    RTOS_ERR_ARG = -1,
    RTOS_ERR_FULL = -2,
    RTOS_ERR_EMPTY = -3,
    RTOS_ERR_TIMEOUT = -4,
    RTOS_ERR_EXIST = -5,
    RTOS_ERR_NOTFOUND = -6,
    RTOS_ERR_STATE = -7
};

#endif