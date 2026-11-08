/* C-A11 demo — 스니펫 1·2 의 넘침을 red zone 없이 (진짜 크기 배열로) ASan 에 보여 준다.
 *
 *   cc -std=c11 -g -O1 -fsanitize=address -fno-omit-frame-pointer demo_overrun.c -o demo_overrun
 *   ./demo_overrun 1     # 스니펫 1: rx_buf[RX_SIZE] 쓰기 (off-by-one)
 *   ./demo_overrun 2     # 스니펫 2: len 검사 없는 memcpy
 *   ./demo_overrun 0     # ASan 없이 빌드하면: 아무 일도 없던 것처럼 끝난다 (그게 문제)
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RX_SIZE 8u
static uint8_t  rx_buf[RX_SIZE];               /* 실제 크기 — red zone 없음 */
static uint32_t rx_head;
uint32_t        neighbour = 0x11111111u;       /* 바로 뒤에 놓일 수도 있는 다른 전역 */

static void uart_isr_buggy(uint8_t c) {        /* 스니펫 1 원본 */
    rx_buf[rx_head] = c;
    if (++rx_head > RX_SIZE) rx_head = 0;
}

#define PKT_MAX 240u
typedef struct { uint8_t type; uint16_t len; uint32_t ts; uint8_t payload[PKT_MAX]; } pkt_t;

static int pkt_copy_buggy(const char *rx, size_t n, pkt_t *p) {   /* 스니펫 2 원본의 len/memcpy 부분 */
    p->len = (uint16_t)((rx[2] << 8) | rx[3]);
    if (p->len > n - 8u) return -1;
    memcpy(p->payload, rx + 8, p->len);
    return 0;
}

int main(int argc, char **argv) {
    int which = argc > 1 ? atoi(argv[1]) : 0;
    if (which == 1 || which == 0) {
        for (int i = 0; i < 9; i++) uart_isr_buggy((uint8_t)('A' + i));   /* 9번째가 rx_buf[8] */
        printf("snippet1: rx_head=%u rx_buf[0]=%c neighbour=0x%08X\n",
               (unsigned)rx_head, rx_buf[0], (unsigned)neighbour);
    }
    if (which == 2 || which == 0) {
        static char frame[8 + 272];
        frame[2] = 0x01; frame[3] = 0x10; frame[8] = 7;                    /* len = 272 > 240 */
        pkt_t *p = malloc(sizeof *p);
        int rc = pkt_copy_buggy(frame, sizeof frame, p);
        printf("snippet2: rc=%d len=%u sizeof(pkt_t)=%zu payload[0]=%u\n",
               rc, (unsigned)p->len, sizeof *p, p->payload[0]);   /* 읽어야 memcpy 가 안 지워진다 */
        free(p);
    }
    return 0;
}
