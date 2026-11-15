/* BM-B03 demo_target.c — 같은 드라이버 패턴을 타깃(Cortex-M4)용으로 컴파일해서 asm 을 본다.
 * UNIT_TEST 가 없으므로 REG_READ/REG_WRITE 는 진짜 volatile MMIO 접근이다.
 *   clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_target.c -o -
 * 호스트에서도 경고 없이 컴파일만은 된다 (실행하는 파일이 아니다):
 *   cc -std=c11 -Wall -Wextra -Werror -O2 -c demo_target.c -o /tmp/BM-B03_demo.o
 * 주소: RM0090 (STM32F4) 기준 대표값 — RM 확인 권장.
 */
#include <stdint.h>

#define REG_READ(addr)        (*(volatile uint32_t *)(uintptr_t)(addr))
#define REG_WRITE(addr, val)  (*(volatile uint32_t *)(uintptr_t)(addr) = (val))

#define RCC_AHB1ENR  0x40023830u
#define GPIOA_MODER  0x40020000u

#if defined(__thumb__)
#define DSB() __asm volatile("dsb 0xF" ::: "memory")
#else
#define DSB() __asm volatile("" ::: "memory")   /* 호스트 컴파일용 자리표시 */
#endif

/* (1) 정답: RMW + 되읽기 + 첫 접근 */
void enable_sync(void) {
    REG_WRITE(RCC_AHB1ENR, REG_READ(RCC_AHB1ENR) | 1u);
    (void)REG_READ(RCC_AHB1ENR);
    REG_WRITE(GPIOA_MODER, (REG_READ(GPIOA_MODER) & ~(3u << 10)) | (1u << 10));
}

/* (2) 버그: 되읽기 없음 — 쓰기 바로 다음 명령이 GPIOA 접근 */
void enable_nosync(void) {
    REG_WRITE(RCC_AHB1ENR, REG_READ(RCC_AHB1ENR) | 1u);
    REG_WRITE(GPIOA_MODER, (REG_READ(GPIOA_MODER) & ~(3u << 10)) | (1u << 10));
}

/* (3) 버그: volatile 이 아닌 포인터 — 컴파일러가 "쓸모없는" 되읽기를 지운다 */
void enable_plain_ptr(void) {
    uint32_t *enr = (uint32_t *)(uintptr_t)RCC_AHB1ENR;
    *enr |= 1u;
    uint32_t dummy = *enr;
    (void)dummy;
    REG_WRITE(GPIOA_MODER, (REG_READ(GPIOA_MODER) & ~(3u << 10)) | (1u << 10));
}

/* (4) ST HAL 의 __HAL_RCC_GPIOA_CLK_ENABLE() 모양 (volatile 지역 변수 tmpreg 에 담는다) */
void enable_hal_style(void) {
    volatile uint32_t tmpreg;
    REG_WRITE(RCC_AHB1ENR, REG_READ(RCC_AHB1ENR) | 1u);
    tmpreg = REG_READ(RCC_AHB1ENR) & 1u;
    (void)tmpreg;
    REG_WRITE(GPIOA_MODER, (REG_READ(GPIOA_MODER) & ~(3u << 10)) | (1u << 10));
}

/* (5) DSB 변형: 버스 쓰기 완료까지 파이프라인을 멈춘다 */
void enable_dsb(void) {
    REG_WRITE(RCC_AHB1ENR, REG_READ(RCC_AHB1ENR) | 1u);
    DSB();
    REG_WRITE(GPIOA_MODER, (REG_READ(GPIOA_MODER) & ~(3u << 10)) | (1u << 10));
}
