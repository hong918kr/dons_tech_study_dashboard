/* ARM-M05 demo_cm_barrier.c — Cortex-M 의 DSB/ISB 자리: MPU 켜기, VTOR 옮기기, CONTROL, FPU(CPACR)
 * 프리스탠딩(헤더는 stdint 만). 맥에서는 asm 만 본다:
 *   clang -std=c11 --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_cm_barrier.c -o -
 *   clang -std=c11 --target=thumbv6m-none-eabi -mcpu=cortex-m0plus -mthumb -O2 -S demo_cm_barrier.c -o -
 * __DSB/__ISB/__DMB 는 CMSIS-Core(cmsis_gcc.h) 와 같은 모양: asm + "memory" 클로버 (컴파일러 배리어 겸용).
 */
#include <stdint.h>

#define __DSB() __asm__ __volatile__("dsb 0xF" ::: "memory")
#define __ISB() __asm__ __volatile__("isb 0xF" ::: "memory")
#define __DMB() __asm__ __volatile__("dmb 0xF" ::: "memory")
#define REG(a) (*(volatile uint32_t *)(a))
#define SCB_VTOR   REG(0xE000ED08u)
#define SCB_CPACR  REG(0xE000ED88u)
#define MPU_CTRL   REG(0xE000ED94u)
#define MPU_RNR    REG(0xE000ED98u)
#define MPU_RBAR   REG(0xE000ED9Cu)
#define MPU_RASR   REG(0xE000EDA0u)

/* MPU 영역 하나 설정 후 켜기 — CMSIS mpu_armv7.h ARM_MPU_Enable() 과 같은 자리의 DSB; ISB (버전마다 다를 수 있음) */
void mpu_setup(uint32_t base, uint32_t rasr) {
    __DMB();                       /* 바꾸기 전: 진행 중 메모리 접근을 먼저 정리 */
    MPU_CTRL = 0u;
    MPU_RNR = 0u;
    MPU_RBAR = base;
    MPU_RASR = rasr;
    MPU_CTRL = 1u | 4u;            /* ENABLE | PRIVDEFENA */
    __DSB();                       /* MPU 쓰기 완료 */
    __ISB();                       /* 이후 명령은 새 MPU 설정으로 가져온다 */
}
/* 벡터 테이블을 SRAM 으로 옮기기 */
void vtor_move(uint32_t new_table) {
    SCB_VTOR = new_table;
    __DSB();                       /* 다음 예외가 새 테이블을 쓰도록 쓰기 완료 */
}
/* 스레드 모드를 PSP 로 (CONTROL.SPSEL=1) */
void use_psp(void) {
    uint32_t c;
    __asm__ __volatile__("mrs %0, control" : "=r"(c));
    c |= 2u;
    __asm__ __volatile__("msr control, %0" :: "r"(c) : "memory");
    __ISB();                       /* 이후 명령이 새 스택 포인터를 쓰게 */
}
/* FPU 켜기: CP10/CP11 full access */
void fpu_enable(void) {
    SCB_CPACR |= 0xFu << 20;
    __DSB();
    __ISB();                       /* 이 뒤 첫 FP 명령이 UsageFault(NOCP) 가 나지 않게 */
}
