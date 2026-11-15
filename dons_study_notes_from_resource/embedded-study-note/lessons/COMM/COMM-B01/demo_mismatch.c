/* COMM-B01 실험: 설정 불일치 행렬 — TX 설정 x RX 설정에서 수신 측이 무엇을 보나
 *
 * 메시지: 인쇄 가능 ASCII 0x20..0x7E (95자) + "\r\n" = 97자. 간격 0 (back-to-back) 과 1 비트 두 가지.
 * 칸 = ok/PE/FE/silent  (ok = 오류 플래그 없이 받은 문자, PE/FE = 플래그 켜진 프레임 수,
 *       silent = ok 중 메시지와 순서대로 맞춰지지 않는 문자 = "오류 없이 틀린 글자")
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 demo_mismatch.c -o demo_mismatch && ./demo_mismatch
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---------------- 본문 모델 축약판 ---------------- */
enum { PAR_NONE = 0, PAR_EVEN = 1, PAR_ODD = 2 };
typedef struct { uint8_t data_bits, parity, stop_bits; } uart_cfg_t;
#define UART_MAX_FRAME 13
#define UART_F_START  0x1
#define UART_F_PARITY 0x2
#define UART_F_FRAME  0x4

static int frame_bits(const uart_cfg_t *c) {
    if (c->data_bits < 5 || c->data_bits > 9 || c->parity > PAR_ODD || c->stop_bits < 1 || c->stop_bits > 2)
        return -1;
    return 1 + c->data_bits + (c->parity != PAR_NONE) + c->stop_bits;
}
static unsigned parity_bit(unsigned v, unsigned nb, unsigned p) {
    unsigned ones = 0;
    for (unsigned i = 0; i < nb; i++) ones += (v >> i) & 1u;
    return p == PAR_NONE ? 0 : p == PAR_EVEN ? (ones & 1u) : !(ones & 1u);
}
static int encode(unsigned v, const uart_cfg_t *c, uint8_t *b, size_t cap) {
    int L = frame_bits(c), k = 0;
    if (L < 0 || (v >> c->data_bits) || cap < (size_t)L) return -1;
    b[k++] = 0;
    for (unsigned i = 0; i < c->data_bits; i++) b[k++] = (uint8_t)((v >> i) & 1u);
    if (c->parity) b[k++] = (uint8_t)parity_bit(v, c->data_bits, c->parity);
    for (unsigned i = 0; i < c->stop_bits; i++) b[k++] = 1;
    return k;
}
/* nstop_check = 검사할 stop 비트 수 (엄격 = cfg 의 stop_bits, lenient = 1) */
static int decode(const uint8_t *b, const uart_cfg_t *c, unsigned nstop_check, uint16_t *out) {
    unsigned d = 0, k = 1u + c->data_bits;
    int f = b[0] ? UART_F_START : 0;
    for (unsigned i = 0; i < c->data_bits; i++) d |= (unsigned)b[1 + i] << i;
    if (c->parity) { if (b[k] != parity_bit(d, c->data_bits, c->parity)) f |= UART_F_PARITY; k++; }
    for (unsigned i = 0; i < nstop_check; i++) if (b[k + i] != 1) f |= UART_F_FRAME;
    *out = (uint16_t)d;
    return f;
}
static int tx_stream(const uint8_t *m, size_t n, const uart_cfg_t *c, unsigned lead, unsigned gap,
                     uint8_t *b, size_t cap) {
    size_t k = 0;
    for (unsigned i = 0; i < lead; i++) { if (k >= cap) return -1; b[k++] = 1; }
    for (size_t j = 0; j < n; j++) {
        int r = encode(m[j], c, b + k, cap - k);
        if (r < 0) return -1;
        k += (size_t)r;
        for (unsigned i = 0; i < gap; i++) { if (k >= cap) return -1; b[k++] = 1; }
    }
    return (int)k;
}
typedef struct { uint32_t frames, ok, perr, ferr; uint8_t partial; } stats_t;

/* 본문 uart_rx_step/uart_rx_run 를 합친 것 + lenient 옵션 (연습 3) */
static size_t rx_run(const uint8_t *b, size_t n, const uart_cfg_t *c, bool lenient,
                     uint16_t *out, size_t cap, stats_t *st) {
    enum { WAIT_HIGH, IDLE, FRAME } s = WAIT_HIGH;
    unsigned L = (unsigned)frame_bits(c), nchk = c->stop_bits, got = 0;
    if (lenient) { L -= c->stop_bits - 1u; nchk = 1; }     /* 첫 stop 에서 프레임 끝 */
    uint8_t fr[UART_MAX_FRAME];
    size_t len = 0;
    memset(st, 0, sizeof *st);
    for (size_t i = 0; i < n; i++) {
        uint8_t lv = b[i] ? 1 : 0;
        if (s == WAIT_HIGH) { if (lv) s = IDLE; continue; }
        if (s == IDLE) { if (!lv) { got = 0; fr[got++] = 0; s = FRAME; } continue; }
        fr[got++] = lv;
        if (got < L) continue;
        uint16_t d;
        int f = decode(fr, c, nchk, &d);
        s = lv ? IDLE : WAIT_HIGH;
        st->frames++;
        if (f & UART_F_PARITY) st->perr++;
        if (f & UART_F_FRAME) st->ferr++;
        if (f) continue;
        st->ok++;
        if (len < cap) out[len++] = d;
    }
    st->partial = s == FRAME;
    return len;
}

int main(void) {
    static uint8_t line[8192], msg[128];
    static uint16_t out[1024];
    size_t n = 0;
    for (int ch = 0x20; ch <= 0x7E; ch++) msg[n++] = (uint8_t)ch;
    msg[n++] = '\r'; msg[n++] = '\n';
    const uart_cfg_t cs[] = {{8, PAR_NONE, 1}, {8, PAR_EVEN, 1}, {8, PAR_ODD, 1},
                             {7, PAR_EVEN, 1}, {7, PAR_ODD, 1}, {8, PAR_NONE, 2}};
    const int nc = (int)(sizeof cs / sizeof cs[0]);
    for (unsigned gap = 0; gap <= 1; gap++) {
        printf("\ngap = %u bit(s) between chars, %zu chars sent. cell = ok/PE/FE/silent\n", gap, n);
        printf("| TX \\ RX |");
        for (int j = 0; j < nc; j++) printf(" %u%c%u |", cs[j].data_bits, "NEO"[cs[j].parity], cs[j].stop_bits);
        printf("\n|---|");
        for (int j = 0; j < nc; j++) printf("---|");
        printf("\n");
        for (int i = 0; i < nc; i++) {
            int nb = tx_stream(msg, n, &cs[i], 4, gap, line, sizeof line);
            printf("| %u%c%u |", cs[i].data_bits, "NEO"[cs[i].parity], cs[i].stop_bits);
            for (int j = 0; j < nc; j++) {
                stats_t st;
                size_t got = rx_run(line, (size_t)nb, &cs[j], false, out, 1024, &st);
                size_t mi = 0, matched = 0;              /* 탐욕적 부분열 맞추기 */
                for (size_t k = 0; k < got; k++) {
                    size_t q = mi;
                    while (q < n && msg[q] != out[k]) q++;
                    if (q < n) { matched++; mi = q + 1; }
                }
                printf(" %u/%u/%u/%zu |", st.ok, st.perr, st.ferr, got - matched);
            }
            printf("\n");
        }
    }
    return 0;
}
