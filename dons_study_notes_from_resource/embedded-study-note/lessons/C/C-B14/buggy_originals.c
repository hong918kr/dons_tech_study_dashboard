/* C-B14 buggy_originals.c — Part B/C 의 "원본(버그) 코드"를 그대로 돌려 본다.
 * 경고 보기:  cc -std=c11 -Wall -Wextra -fsyntax-only buggy_originals.c
 * 실행:       cc -std=c11 -w -O0 buggy_originals.c -o buggy0 && ./buggy0
 *             cc -std=c11 -w -O2 buggy_originals.c -o buggy2 && ./buggy2   (B2 는 UB → 결과가 바뀔 수 있다)
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define SR_RXNE (1u << 5)

/* B1 */ static bool rxne_set(uint32_t sr) { return sr & SR_RXNE == SR_RXNE; }
/* B2 */ static uint16_t make_u16(uint8_t hi, uint8_t lo) { return (uint16_t)(hi << 8 + lo); }
/* B3 */ static bool flags_clear(uint32_t sr, uint32_t mask) { return !sr & mask; }
/* B4 */ static void err_bump(uint32_t *cnt) { *cnt++; }

typedef struct { uint8_t fifo[8]; unsigned head, count; } fake_uart;
static bool uart_pop(fake_uart *u, uint8_t *out) {
    if (u->count == 0) return false;
    *out = u->fifo[u->head]; u->head = (u->head + 1u) % 8u; u->count--;
    return true;
}
/* B5 */ static size_t uart_drain(fake_uart *u, uint8_t *out, size_t max) {
    size_t n = 0;
    while (uart_pop(u, &out[n]) && n < max) n++;       /* 피연산자 순서가 반대 */
    return n;
}
typedef struct { const uint8_t *rx; unsigned idx; } fake_spi;
static uint8_t spi_xfer(fake_spi *s) { return s->rx[s->idx++]; }
/* B6 */ static uint16_t spi_read_be16(fake_spi *s) { return (uint16_t)(spi_xfer(s) << 8 | spi_xfer(s)); }

/* C */
typedef struct { uint16_t len; uint8_t cmd; uint8_t payload[16]; } frame_t;
static int parse_frame(const uint8_t *p, size_t n, frame_t *f) {
    const uint8_t *s = p;
    if (n < 5 || *p++ != 0xA5) return -1;
    f->len = *p++ << 8 | *p++;                         /* ① */
    f->cmd = *p++;
    if (f->len > 16 || n != f->len + 5u) return -1;
    uint8_t chk = s[1] ^ s[2] ^ s[3];
    for (size_t i = 0; i < f->len; ) {
        chk ^= p[i];
        f->payload[i] = p[i++];                        /* ② */
    }
    if (chk ^ p[f->len] != 0) return -1;               /* ③ */
    return 0;
}

int main(void) {
    printf("B1 rxne_set(0x20)=%d  rxne_set(0x01)=%d   (want 1, 0)\n", rxne_set(0x20), rxne_set(0x01));
    printf("B2 make_u16(0x12,0x34)=0x%04X            (want 0x1234)\n", make_u16(0x12, 0x34));
    printf("B3 flags_clear(0,0x04)=%d                 (want 1)\n", flags_clear(0, 0x04));
    uint32_t errs[2] = {7, 100}; err_bump(&errs[0]);
    printf("B4 errs after bump = {%u,%u}            (want {8,100})\n", errs[0], errs[1]);
    fake_uart u = { {1, 2, 3, 4, 5}, 0, 5 }; uint8_t out[4] = {0, 0, 0, 0xEE};
    size_t got = uart_drain(&u, out, 3);
    printf("B5 drain(max=3)=%zu out[3]=0x%02X left=%u   (want 3, 0xEE, 2)\n", got, out[3], u.count);
    const uint8_t rx[] = {0x12, 0x34}; fake_spi s = { rx, 0 };
    printf("B6 spi_read_be16=0x%04X                 (want 0x1234; clang 은 우연히 맞음)\n", spi_read_be16(&s));
    const uint8_t ok[] = {0xA5, 0x00, 0x03, 0x10, 0x01, 0x02, 0x03, 0x13};
    frame_t f = {0};
    int r = parse_frame(ok, sizeof ok, &f);
    printf("C  parse_frame(ok)=%d len=%u payload={%u,%u,%u} (want 0, 3, {1,2,3})\n",
           r, f.len, f.payload[0], f.payload[1], f.payload[2]);
    return 0;
}
