/* ARCH-B14 demo — 실제 Cortex-M4 에서 리셋 원인 읽기/지우기와 소프트웨어 리셋이 어떤 명령이 되나
 * 빌드(asm 만 본다):
 *   clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_cm4_asm.c -o -
 * 주소는 e.g. — STM32F4 RCC_CSR (RCC 0x4002_3800 + 0x74), nRF52 POWER->RESETREAS (0x4000_0000 + 0x400),
 * SCB->AIRCR (0xE000_ED0C, Armv7-M 아키텍처 고정). 실제 칩은 레퍼런스 매뉴얼 확인.
 */
#include <stdint.h>

#define RCC_CSR       (*(volatile uint32_t *)0x40023874u)
#define RCC_CSR_RMVF  (1u << 24)
#define NRF_RESETREAS (*(volatile uint32_t *)0x40000400u)
#define NRF_DOG       (1u << 1)
#define SCB_AIRCR     (*(volatile uint32_t *)0xE000ED0Cu)

/* ST: 읽고 → RMVF 로 전부 지우기 (RMW 지만 플래그는 RO 라 안전, LSION 보존) */
uint32_t st_read_and_clear(void) {
    uint32_t v = RCC_CSR;
    RCC_CSR = v | RCC_CSR_RMVF;
    return v;
}

/* nRF: 읽은 비트만 W1C */
uint32_t nrf_read_and_clear(void) {
    uint32_t v = NRF_RESETREAS;
    NRF_RESETREAS = v;
    return v;
}

/* 버그: W1C 레지스터에 |= → 읽은 1 들을 전부 다시 써서 다 지운다 */
void nrf_clear_dog_wrong(void) { NRF_RESETREAS |= NRF_DOG; }
/* 정답: 지울 비트만 1 */
void nrf_clear_dog_right(void) { NRF_RESETREAS = NRF_DOG; }

/* CMSIS NVIC_SystemReset() 과 같은 순서: DSB → AIRCR(VECTKEY|PRIGROUP 보존|SYSRESETREQ) → DSB → 대기 */
__attribute__((noreturn)) void system_reset(void) {
    __asm volatile("dsb 0xF" ::: "memory");
    SCB_AIRCR = (0x05FAu << 16) | (SCB_AIRCR & (7u << 8)) | (1u << 2);
    __asm volatile("dsb 0xF" ::: "memory");
    for (;;) __asm volatile("nop");
}
