/* RTOS-M06 데모용 훅 (RTOS-B11 과 같음). vApplicationTickHook 은 demo_freertos_queue_wait.c 에 있다. */
#include <stdio.h>
#include <stdlib.h>
#include "FreeRTOS.h"
#include "task.h"
__attribute__((weak)) void vApplicationMallocFailedHook(void) { fprintf(stderr, "[MallocFailedHook]\n"); exit(252); }
