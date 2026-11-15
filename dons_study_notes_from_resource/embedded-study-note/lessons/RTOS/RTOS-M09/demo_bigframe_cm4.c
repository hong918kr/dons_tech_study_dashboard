/* RTOS-M09 데모 — 큰 지역 배열 프레임을 Cortex-M4 는 어떻게 잡나 (어셈블리 관찰용, 실행 안 함)
 *
 *   clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mfloat-abi=hard -O2 -S demo_bigframe_cm4.c -o -
 *   clang -O2 -S demo_bigframe_cm4.c -o -                      # 같은 코드, macOS arm64 (비교)
 *
 * comms 태스크의 상태 응답 포맷터 모양: 큰 버퍼를 잡고 맨 앞(가장 낮은 주소)부터 몇 바이트만 쓴다.
 * 프리스탠딩 헤더만 쓴다 (thumbv7em 타깃엔 libc 헤더가 없다).
 */
#include <stdint.h>

void uart_send(const volatile uint8_t *p, uint32_t n);   /* 외부 함수: 버퍼를 줄이지 못하게 */

uint32_t format_status(uint32_t temp_dc) {
    volatile uint8_t reply[384];                 /* 스택 512 B 태스크에 384 B 배열 */
    reply[0] = 'T';
    reply[1] = (uint8_t)('0' + temp_dc / 100);
    uart_send(reply, 2);
    return reply[0];
}

uint32_t format_status_big(uint32_t temp_dc) {   /* 페이지(4 KB)보다 큰 프레임: 호스트는 프로브, M4 는? */
    volatile uint8_t reply[8192];
    reply[0] = (uint8_t)temp_dc;
    uart_send(reply, 1);
    return reply[0];
}
