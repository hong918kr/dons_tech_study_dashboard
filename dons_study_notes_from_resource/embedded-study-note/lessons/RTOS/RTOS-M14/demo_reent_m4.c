/* RTOS-M14 demo: "태스크별 C 런타임 상태" 가 Cortex-M 에서 얼마나 싼가 (freestanding, libc 없음)
 * 빌드: clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_reent_m4.c -o -
 * newlib 의 _impure_ptr / struct _reent 와 FreeRTOS configSET_TLS_BLOCK 을 흉내 낸 축소 모델. */
#include <stdint.h>

struct reent_m {                     /* struct _reent 의 일부만 */
    int err;                         /* _errno */
    char *strtok_last;               /* _strtok_last */
};
struct tcb_m {
    uint32_t *top_of_stack;          /* FreeRTOS TCB 첫 멤버 (pxTopOfStack) 자리 */
    uint32_t prio;
    struct reent_m reent;            /* xTLSBlock */
};

struct reent_m *impure_ptr;          /* newlib: struct _reent *_impure_ptr */

/* configSET_TLS_BLOCK( pxCurrentTCB->xTLSBlock ) == ( _impure_ptr = &( xTLSBlock ) ) */
void switch_hook(struct tcb_m *next) { impure_ptr = &next->reent; }

/* 옛 strtok: 상태 주소가 링크 타임 상수 */
char **strtok_state_static(void) { static char *last; return &last; }

/* newlib strtok: &_REENT_STRTOK_LAST(_REENT) — 포인터 하나를 더 읽는다 */
char **strtok_state_reent(void) { return &impure_ptr->strtok_last; }

/* newlib errno: __errno() { return &_REENT_ERRNO(_REENT); } */
int *errno_addr(void) { return &impure_ptr->err; }
