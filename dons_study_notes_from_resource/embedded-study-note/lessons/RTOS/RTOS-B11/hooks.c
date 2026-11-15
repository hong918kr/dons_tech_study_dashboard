#include <stdio.h>
#include <stdlib.h>
#include "FreeRTOS.h"
#include "task.h"
__attribute__((weak)) void vApplicationMallocFailedHook(void) { fprintf(stderr, "[MallocFailedHook]\n"); exit(252); }
