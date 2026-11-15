/* BM-B02 demo: REG_READ 에서 volatile 을 빼면 폴링 루프가 어떻게 컴파일되나 (타깃 오브젝트 관찰용)
 *
 * clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -Os -ffreestanding -std=c11 \
 *       -Wall -Wextra -Werror -c demo_novolatile.c -o /tmp/BM-B02_work/nv.o
 * xcrun llvm-objdump -d /tmp/BM-B02_work/nv.o
 * (호스트에서 실행하는 프로그램이 아니다 — 0x4000_4400 은 맥에서 접근할 수 없는 주소)
 */
#include <stdint.h>

#define SR_ADDR 0x40004400u                      /* USART2_SR (RM0090 대표값) */
#define DR_ADDR 0x40004404u
#define TXE     (1u << 7)

#define REG_READ_V(a)      (*(volatile uint32_t *)(uintptr_t)(a))   /* 정석 */
#define REG_WRITE_V(a, v)  (*(volatile uint32_t *)(uintptr_t)(a) = (v))
#define REG_READ_P(a)      (*(uint32_t *)(uintptr_t)(a))            /* 버그: volatile 없음 */
#define REG_WRITE_P(a, v)  (*(uint32_t *)(uintptr_t)(a) = (v))

void putc_volatile(uint8_t c) {
    while (!(REG_READ_V(SR_ADDR) & TXE)) { }     /* 매 반복 SR 을 다시 읽는다 */
    REG_WRITE_V(DR_ADDR, c);
}

void putc_plain(uint8_t c) {
    while (!(REG_READ_P(SR_ADDR) & TXE)) { }     /* 컴파일러: "아무도 안 바꾸는 메모리" */
    REG_WRITE_P(DR_ADDR, c);
}

void two_writes_plain(void) {                    /* 같은 주소 두 번 쓰기 → 하나로 합쳐진다 */
    REG_WRITE_P(DR_ADDR, 'A');
    REG_WRITE_P(DR_ADDR, 'B');
}

void two_writes_volatile(void) {
    REG_WRITE_V(DR_ADDR, 'A');
    REG_WRITE_V(DR_ADDR, 'B');
}
