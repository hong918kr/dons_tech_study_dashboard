/* RTOS-B04 demo: "가장 높은 ready 우선순위" 가 어떤 기계어가 되는지 본다.
 *   이 파일은 <stdint.h> 만 쓴다 → 맥에서 Cortex-M 타깃으로도 -S 가 된다 (libc 헤더 불필요).
 *
 *   arm64 (이 맥)  : cc -O2 -S -o - demo_clz_asm.c
 *   Cortex-M4      : clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -O2 -S -o - demo_clz_asm.c
 *   Cortex-M0      : clang --target=thumbv6m-none-eabi  -mcpu=cortex-m0 -O2 -S -o - demo_clz_asm.c
 *
 * 전제: bm != 0 (FreeRTOS 처럼 idle 태스크가 항상 READY 라서 비트 0 이 늘 켜져 있다고 가정).
 */
#include <stdint.h>

/* ① 컴파일러 내장 함수: __builtin_clz(0) 은 정의되지 않음 (UB) → bm != 0 전제 */
int highest_builtin(uint32_t bm)
{
    return 31 - __builtin_clz(bm);
}

/* ② FreeRTOS ARM_CM4F 포트와 같은 모양: 인라인 asm 으로 CLZ 를 직접 (Cortex-M3/M4/M7 전용) */
#if defined(__ARM_ARCH_7EM__) || defined(__ARM_ARCH_7M__)
static inline uint8_t port_clz(uint32_t x)
{
    uint8_t r;
    __asm volatile ("clz %0, %1" : "=r"(r) : "r"(x) : "memory");
    return r;
}
int highest_port_asm(uint32_t bm)
{
    return 31 - (int)port_clz(bm);
}
#endif

/* ③ 손으로 쓴 이진 탐색 CLZ (Hacker's Delight nlz) — 레슨 Part A 의 my_clz32 와 같은 코드 */
int highest_portable(uint32_t x)
{
    int n = 0;
    if (x <= 0x0000FFFFu) { n += 16; x <<= 16; }
    if (x <= 0x00FFFFFFu) { n += 8;  x <<= 8;  }
    if (x <= 0x0FFFFFFFu) { n += 4;  x <<= 4;  }
    if (x <= 0x3FFFFFFFu) { n += 2;  x <<= 2;  }
    if (x <= 0x7FFFFFFFu) { n += 1; }
    return 31 - n;
}

/* ④ 반대 방향 (작은 숫자 = 급함, Zephyr / µC/OS): 최하위 켜진 비트 = count trailing zeros */
int lowest_builtin(uint32_t bm)
{
    return __builtin_ctz(bm);
}
