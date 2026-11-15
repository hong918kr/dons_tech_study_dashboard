/* COMM-B01 추가 연습 — 한 번에 검증
 *
 * 연습 1: 전송 시간 계산 uart_tx_time_us (프레임 + 문자 간 간격)
 * 연습 2: STM32 식 레지스터 설정으로 바꾸기 (word length 에 parity 포함) — 이 파일의 모의 정의, 대표값
 * 연습 3: "첫 stop 만 검사" 하는 2-stop 수신기 (lenient) vs 엄격한 수신기
 * 연습 4: 재동기화에 필요한 최소 idle 간격 = data + parity 비트 — 충분함/필요함을 실험으로
 * 연습 5: 캡처를 보고 설정 추측 (오류 수 -> 인쇄 가능 문자 수 -> 후보 순서)
 * 본문 solution.c 의 인코더/디코더/수신기 축약판을 담아 독립적으로 빌드된다.
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c && ./a.out
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

/* ---------------- 연습 1: 전송 시간 ---------------- */
/* nbytes 문자를 baud 로 보낼 때 µs (반올림). 문자마다 frame_bits + gap 비트 시간 */
static uint64_t uart_tx_time_us(uint64_t nbytes, uint32_t baud, const uart_cfg_t *c, unsigned gap) {
    uint64_t bits = nbytes * (uint64_t)((unsigned)frame_bits(c) + gap);
    return (bits * 1000000u + baud / 2) / baud;
}

/* ---------------- 연습 2: STM32 식 설정 (이 파일의 모의 정의 — 대표값, RM 확인 권장) ---------------- */
typedef struct { uint8_t word_bits; uint8_t pce, ps; uint8_t stop_code; } stm32_uart_cfg_t;
/* word_bits = data + parity (STM32 의 M 비트가 정하는 "word length" 는 패리티를 포함).
 * has_m1 = M1 비트가 있는 계열(7비트 word 가능). stop_code: 1 stop = 0b00, 2 stop = 0b10. 실패 -1 */
static int stm32_map(const uart_cfg_t *c, bool has_m1, stm32_uart_cfg_t *o) {
    if (frame_bits(c) < 0) return -1;
    unsigned w = c->data_bits + (c->parity != PAR_NONE);
    if (w > 9 || w < (has_m1 ? 7u : 8u)) return -1;
    o->word_bits = (uint8_t)w;
    o->pce = c->parity != PAR_NONE;
    o->ps = c->parity == PAR_ODD;
    o->stop_code = c->stop_bits == 2 ? 2 : 0;
    return 0;
}

/* ---------------- 연습 4: 최소 재동기화 간격 ---------------- */
static unsigned min_resync_gap(const uart_cfg_t *c) { return c->data_bits + (c->parity != PAR_NONE); }

/* ---------------- 연습 5: 설정 추측 ---------------- */
static bool printable(uint16_t d) { return (d >= 0x20 && d <= 0x7E) || d == '\r' || d == '\n' || d == '\t'; }
static int uart_guess_cfg(const uint8_t *b, size_t n, const uart_cfg_t *cand, int nc) {
    static uint16_t out[1024];
    int best = -1;
    long best_err = 0, best_pr = 0;
    for (int i = 0; i < nc; i++) {
        stats_t st;
        size_t got = rx_run(b, n, &cand[i], false, out, 1024, &st);
        long err = (long)st.perr + (long)st.ferr + st.partial, pr = 0;
        for (size_t k = 0; k < got; k++) pr += printable(out[k]);
        if (best < 0 || err < best_err || (err == best_err && pr > best_pr)) { best = i; best_err = err; best_pr = pr; }
    }
    return best;
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t g_rng = 0xC0FFEEu;
static uint32_t xs32(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }

static const uart_cfg_t C8N1 = {8, PAR_NONE, 1}, C7E1 = {7, PAR_EVEN, 1}, C8E1 = {8, PAR_EVEN, 1},
                        C8O1 = {8, PAR_ODD, 1}, C8N2 = {8, PAR_NONE, 2}, C8O2 = {8, PAR_ODD, 2},
                        C7O1 = {7, PAR_ODD, 1}, C5N1 = {5, PAR_NONE, 1}, C9N1 = {9, PAR_NONE, 1};

int main(void) {
    static uint8_t line[8192];
    static uint16_t rxb[1024];
    stats_t st;

    /* 연습 1 */
    CHECK(uart_tx_time_us(4096, 115200, &C8N1, 0) == 355556);   /* 40960 비트 / 115200 */
    CHECK(uart_tx_time_us(1, 9600, &C8N1, 0) == 1042);          /* 1.0417 ms */
    CHECK(uart_tx_time_us(100, 9600, &C8E1, 0) == 114583);
    CHECK(uart_tx_time_us(4096, 115200, &C8N1, 2) == 426667);   /* 간격 2 비트 = +20% */
    CHECK(uart_tx_time_us(1000000, 921600, &C8N1, 0) == 10850694);

    /* 연습 2 */
    {
        stm32_uart_cfg_t o;
        CHECK(stm32_map(&C8N1, false, &o) == 0 && o.word_bits == 8 && o.pce == 0 && o.stop_code == 0);
        CHECK(stm32_map(&C8E1, false, &o) == 0 && o.word_bits == 9 && o.pce == 1 && o.ps == 0);
        CHECK(stm32_map(&C7E1, false, &o) == 0 && o.word_bits == 8 && o.pce == 1);
        CHECK(stm32_map(&C8O2, false, &o) == 0 && o.word_bits == 9 && o.ps == 1 && o.stop_code == 2);
        CHECK(stm32_map(&(uart_cfg_t){7, PAR_NONE, 1}, false, &o) == -1);
        CHECK(stm32_map(&(uart_cfg_t){7, PAR_NONE, 1}, true, &o) == 0 && o.word_bits == 7);
        CHECK(stm32_map(&(uart_cfg_t){9, PAR_EVEN, 1}, true, &o) == -1);
        CHECK(stm32_map(&C5N1, true, &o) == -1);
        CHECK(stm32_map(&C9N1, false, &o) == 0 && o.word_bits == 9 && o.pce == 0);
    }

    /* 연습 3: 8N1 back-to-back 을 8N2 수신기로 */
    {
        const char *m = "Hello, UART!\r\n";
        size_t n = strlen(m);
        int nb = tx_stream((const uint8_t *)m, n, &C8N1, 2, 0, line, sizeof line);
        size_t g1 = rx_run(line, (size_t)nb, &C8N2, false, rxb, 1024, &st);
        uint32_t strict_fe = st.ferr;
        size_t g2 = rx_run(line, (size_t)nb, &C8N2, true, rxb, 1024, &st);
        bool same = g2 == n;
        for (size_t k = 0; same && k < n; k++) same = rxb[k] == (uint8_t)m[k];
        printf("ex3: 8N1 back-to-back -> 8N2 strict ok=%zu FE=%u | lenient ok=%zu FE=%u\n",
               g1, strict_fe, g2, st.ferr);
        CHECK(strict_fe > 0 && g1 < n);
        CHECK(same && st.ferr == 0);
        nb = tx_stream((const uint8_t *)m, n, &C8N2, 2, 0, line, sizeof line);   /* 진짜 8N2 도 받는다 */
        CHECK(rx_run(line, (size_t)nb, &C8N2, true, rxb, 1024, &st) == n && st.ok == n);
    }

    /* 연습 4: 무작위 prefix(문자 1..6개, back-to-back) + idle gap + 꼬리 "OK" 를
     * prefix 의 모든 비트 오프셋에서 듣기 시작. gap = D+P 이면 꼬리를 항상 받고, D+P-1 이면 놓치는 경우가 있다 */
    {
        const uart_cfg_t *cs[] = {&C8N1, &C7E1, &C8E1, &C8N2, &C8O2, &C5N1};
        for (size_t ci = 0; ci < sizeof cs / sizeof cs[0]; ci++) {
            const uart_cfg_t *c = cs[ci];
            unsigned g0 = min_resync_gap(c);
            long fail_at[2] = {0, 0}, runs = 0;
            for (int t = 0; t < 600; t++) {
                uint8_t pre[6];
                size_t np = 1 + xs32() % 6;
                for (size_t j = 0; j < np; j++) pre[j] = (uint8_t)(xs32() & ((1u << c->data_bits) - 1u));
                for (int w = 0; w < 2; w++) {
                    unsigned gap = w == 0 ? g0 : g0 - 1;
                    int n1 = tx_stream(pre, np, c, 0, 0, line, sizeof line);
                    int n2 = tx_stream((const uint8_t *)"\x0F\x0A", 2, c, gap, 0, line + n1, sizeof line - (size_t)n1);
                    for (int k = 0; k < n1; k++) {
                        size_t got = rx_run(line + k, (size_t)(n1 + n2 - k), c, false, rxb, 1024, &st);
                        bool ok = got >= 2 && rxb[got - 2] == 0x0F && rxb[got - 1] == 0x0A && !st.partial;
                        if (!ok) fail_at[w]++;
                        if (w == 0) runs++;
                    }
                }
            }
            printf("ex4: cfg %u%c%u  gap=%u (D+P): %ld/%ld offsets lose the tail | gap=%u: %ld\n",
                   c->data_bits, "NEO"[c->parity], c->stop_bits, g0, fail_at[0], runs, g0 - 1, fail_at[1]);
            CHECK(fail_at[0] == 0 && runs > 0);
            CHECK(fail_at[1] > 0);
        }
    }

    /* 연습 5: 설정 추측 */
    {
        const uart_cfg_t cand[] = {C8N1, C8E1, C8O1, C7E1, C7O1, C8N2};
        const char *txt = "Hello, UART!\r\nAT+BAUD?\r\n+BAUD: 115200\r\nOK\r\n";
        const uart_cfg_t *truth[] = {&C8N1, &C8E1, &C8O1, &C7E1, &C7O1};
        int good = 0, total = 0;
        for (size_t i = 0; i < 5; i++)
            for (unsigned gap = 0; gap <= 1; gap++) {
                int nb = tx_stream((const uint8_t *)txt, strlen(txt), truth[i], 4, gap, line, sizeof line);
                int g = uart_guess_cfg(line, (size_t)nb, cand, 6);
                total++;
                good += g >= 0 && memcmp(&cand[g], truth[i], sizeof(uart_cfg_t)) == 0;
            }
        CHECK(good == total);
        int nb = tx_stream((const uint8_t *)txt, strlen(txt), &C8N2, 4, 0, line, sizeof line);
        CHECK(uart_guess_cfg(line, (size_t)nb, cand, 6) == 0);       /* 8N2 캡처 -> 8N1 (수신엔 동등) */
        /* 인쇄 가능 점수가 없으면 7E1 캡처를 8N1 로 오판한다 (오류 0 동점) */
        nb = tx_stream((const uint8_t *)txt, strlen(txt), &C7E1, 4, 0, line, sizeof line);
        size_t got = rx_run(line, (size_t)nb, &C8N1, false, rxb, 1024, &st);
        CHECK(st.perr == 0 && st.ferr == 0 && got == strlen(txt));
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
