/* ARCH-B05 demo — 우선순위 인코더가 CPU 에서는 CLZ 명령 하나가 되는가?
 * freestanding (stdint.h 만) → 여러 타깃의 asm 을 비교한다:
 *   arm64 (이 맥) : cc -O2 -S -o - demo_clz.c
 *   Cortex-M4     : clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_clz.c -o -
 *   Cortex-M0     : clang --target=thumbv6m-none-eabi  -mcpu=cortex-m0 -mthumb -O2 -S demo_clz.c -o -
 */
#include <stdint.h>

/* RTOS: 가장 높은 ready 우선순위 (ready != 0 가정 — idle 태스크가 항상 ready) */
uint32_t top_ready(uint32_t ready) { return 31u - (uint32_t)__builtin_clz(ready); }

/* 0 도 안전하게: __builtin_clz(0) 은 UB 라서 분기 */
int msb_index_safe(uint32_t x) { return x ? 31 - __builtin_clz(x) : -1; }

/* 루프로 쓴 우선순위 인코더 — 컴파일러가 알아보고 CLZ 로 바꿔 줄까? */
int msb_index_loop(uint32_t x) {
    int i = -1;
    while (x) { x >>= 1; i++; }
    return i;
}

/* 반대 방향(가장 낮은 1) — Linux RT 큐, IRQ0 최우선 같은 경우 */
uint32_t lowest_set(uint32_t x) { return (uint32_t)__builtin_ctz(x); }
