/* ARCH-M15 demo — RBAR/RASR 값이 컴파일 타임 상수로 접히고, MPU 레지스터 쓰기가 어떤 명령이 되나
 * (cross-target asm 관찰용, 실행 안 함. 실제 코어에서 켜고 폴트 잡기는 BM-A08)
 *   mpu_guard_on : 영역 7 = 32 B 스택 가드(AP=000, XN) 를 RBAR(VALID)/RASR 로 쓰고 DSB/ISB
 *   mpu_load     : (RBAR, RASR) 표를 차례로 쓴다 — 컨텍스트 스위치 때 태스크 영역을 바꾸는 루프
 * clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_mpu_asm.c -o -
 */
#include <stdint.h>

/* ARMv7-M MPU 레지스터 (System Control Space) */
#define MPU_CTRL (*(volatile uint32_t *)0xE000ED94u)
#define MPU_RBAR (*(volatile uint32_t *)0xE000ED9Cu)
#define MPU_RASR (*(volatile uint32_t *)0xE000EDA0u)

#define RASR(xn, ap, scb, srd, size_f) \
    (((uint32_t)(xn) << 28) | ((uint32_t)(ap) << 24) | ((uint32_t)(scb) << 16) | \
     ((uint32_t)(srd) << 8) | ((uint32_t)(size_f) << 1) | 1u)
#define RBAR(base, n) ((uint32_t)(base) | (1u << 4) | (uint32_t)(n))

void mpu_guard_on(uint32_t stack_lo) {
    MPU_RBAR = RBAR(stack_lo & ~31u, 7);
    MPU_RASR = RASR(1, 0, 0, 0, 4);              /* 32 B, no access, XN */
    MPU_CTRL = (1u << 2) | 1u;                   /* PRIVDEFENA | ENABLE (HFNMIENA=0) */
    __builtin_arm_dsb(0xF);
    __builtin_arm_isb(0xF);
}

void mpu_load(const uint32_t (*tbl)[2], int n) {
    for (int i = 0; i < n; i++) {
        MPU_RBAR = tbl[i][0];
        MPU_RASR = tbl[i][1];
    }
}
