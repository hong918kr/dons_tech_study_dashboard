/* demo_sizeof_param.c — 함수 인자 sizeof 함정 관찰용. 일부러 경고가 나는 코드다 (-Werror 없이 빌드).
 *   cc -std=c11 -Wall -Wextra -O2 demo_sizeof_param.c -o demo_sizeof_param && ./demo_sizeof_param
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define NAIVE_ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static uint8_t rx_buf[64];

/* [64] 라고 써도 파라미터는 uint8_t * 로 "조정"된다 (C11 6.7.6.3p7) */
static void clear_buf(uint8_t buf[64]) {
    memset(buf, 0, sizeof(buf));                         /* 64 가 아니라 8 바이트만 지운다 */
    printf("clear_buf : sizeof(buf) = %zu, NAIVE_ARRAY_SIZE(buf) = %zu\n",
           sizeof(buf), NAIVE_ARRAY_SIZE(buf));
}

/* 포인터로 선언하면 같은 버그인데 sizeof-array-argument 경고가 안 나온다 */
static size_t ring_bytes(const uint32_t *ring) {
    return sizeof ring;                                   /* 조용히 8 */
}

int main(void) {
    uint32_t ring[16];
    memset(rx_buf, 0xAA, sizeof rx_buf);
    printf("main      : sizeof(rx_buf) = %zu\n", sizeof rx_buf);
    clear_buf(rx_buf);
    printf("after clear: rx_buf[7] = 0x%02X, rx_buf[8] = 0x%02X\n", rx_buf[7], rx_buf[8]);
    printf("sizeof(rx_buf + 0) = %zu\n", sizeof(rx_buf + 0));  /* + 연산이 decay 를 일으킨다 */
    printf("ring_bytes(ring) = %zu (진짜는 %zu)\n", ring_bytes(ring), sizeof ring);
    return 0;
}
