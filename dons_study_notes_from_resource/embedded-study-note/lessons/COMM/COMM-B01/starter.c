/* COMM-B01 UART 프레임: start/data/parity/stop 비트 직렬화
 *
 * 모델: 선로(line)를 "비트 시간 하나 = 배열 원소 하나" 로 본다. 값은 0(low) / 1(high).
 *       idle = 1, start = 0, data LSB first, (parity), stop = 1 (1개 또는 2개).
 *       설정: data 5..9 비트 x parity N/E/O x stop 1/2 = 30 가지. 프레임 최대 1+9+1+2 = 13 비트.
 * Part A: 프레임 길이·효율, 패리티 비트, 인코더(문자 -> 비트), 디코더(비트 -> 문자)
 * Part B: 수신 검사(start / parity / framing 플래그), 비트 위치 -> 필드 표, 결함 주입 전수 테스트
 * Part C: 문자열 -> 선로 타임라인(앞 idle + 문자 사이 간격), 비트당 샘플 1개를 받는 수신 상태 기계,
 *         스트림 수신 + 통계(오류 문자는 버리고 센다), 설정 불일치(7E1 -> 8N1 등), 중간 시작 재동기화
 * 오버샘플링(16x)·글리치·overrun 은 COMM-B02, baud 오차는 COMM-B03.
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c && ./a.out
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum { PAR_NONE = 0, PAR_EVEN = 1, PAR_ODD = 2 };

typedef struct {
    uint8_t data_bits; /* 5..9                        */
    uint8_t parity;    /* PAR_NONE / PAR_EVEN / PAR_ODD */
    uint8_t stop_bits; /* 1 또는 2                     */
} uart_cfg_t;

#define UART_MAX_FRAME 13 /* start 1 + data 9 + parity 1 + stop 2 */

/* 음수 = API 오류 (프레임을 볼 수조차 없음) */
#define UART_E_CFG   (-1) /* 설정이 범위 밖                 */
#define UART_E_CAP   (-2) /* 출력 버퍼가 작다               */
#define UART_E_DATA  (-3) /* 값이 data_bits 에 안 들어간다  */
#define UART_E_LEVEL (-4) /* 비트 값이 0/1 이 아니다         */
#define UART_E_SHORT (-5) /* 입력 비트가 한 프레임보다 짧다 */
/* 0 이상 = 수신 상태 플래그 (여러 개가 동시에 켜질 수 있다, 상태 레지스터처럼) */
#define UART_F_START  0x1 /* start 자리가 1 — 프레임이 아님 */
#define UART_F_PARITY 0x2 /* PE: 패리티 불일치               */
#define UART_F_FRAME  0x4 /* FE: stop 자리 중 0 이 있음      */

static bool uart_cfg_ok(const uart_cfg_t *c) {
    return c && c->data_bits >= 5 && c->data_bits <= 9 && c->parity <= PAR_ODD &&
           (c->stop_bits == 1 || c->stop_bits == 2);
}

/* ---------------- Part A: 한 프레임 ---------------- */

/* 프레임 전체 비트 수 (start + data + parity + stop). 잘못된 설정이면 UART_E_CFG */
static int uart_frame_bits(const uart_cfg_t *c) {
    (void)c; (void)uart_cfg_ok;
    return 0; /* TODO: 잘못된 설정이면 UART_E_CFG, 아니면 1 + data + (parity?1:0) + stop */
}

/* 효율 = data 비트 / 프레임 비트, 천분율 반올림 (8N1 -> 800) */
static int uart_eff_permille(const uart_cfg_t *c) {
    (void)c;
    return 0; /* TODO: data*1000/L 반올림. 설정 오류는 그대로 전달 */
}

/* 보낼 패리티 비트. EVEN: data+parity 의 1 개수가 짝수, ODD: 홀수. NONE 이면 0 */
static unsigned uart_parity_bit(uint16_t data, unsigned nbits, unsigned parity) {
    (void)data; (void)nbits; (void)parity;
    return 0; /* TODO: 하위 nbits 의 1 개수로 EVEN/ODD 패리티 비트 */
}

/* 문자 하나 -> 비트 배열. 반환: 쓴 비트 수(= 프레임 길이) 또는 음수 오류. 오류면 아무것도 쓰지 않는다 */
static int uart_encode(uint16_t data, const uart_cfg_t *c, uint8_t *bits, size_t cap) {
    (void)data; (void)c; (void)bits; (void)cap;
    return 0; /* TODO: 검사(CFG -> DATA -> CAP) 후 start 0, data LSB first, parity, stop 1 */
}

static int uart_check(const uint8_t *bits, const uart_cfg_t *c, uint16_t data);

/* 비트 배열 -> 문자 하나. 반환: 음수 API 오류, 또는 0 이상 상태 플래그(0 = 정상).
 * 플래그가 켜져도 *out 에는 data 자리에서 읽은 값을 넣는다 (수신 데이터 레지스터처럼). */
static int uart_decode(const uint8_t *bits, size_t n, const uart_cfg_t *c, uint16_t *out) {
    (void)bits; (void)n; (void)c; (void)out; (void)uart_check;
    return 0; /* TODO: CFG -> SHORT -> LEVEL 검사, data 를 LSB first 로 모아 *out, 플래그는 uart_check */
}

/* ---------------- Part B: 오류 검사와 프레임 지도 ---------------- */

/* 프레임(이미 길이·레벨 검사 끝남)의 start / parity / stop 을 검사해 플래그를 돌려준다 */
static int uart_check(const uint8_t *bits, const uart_cfg_t *c, uint16_t data) {
    (void)bits; (void)c; (void)data;
    return 0; /* TODO: start != 0 -> F_START, 패리티 불일치 -> F_PARITY, stop 중 0 -> F_FRAME (OR) */
}

enum { FLD_NONE = -1, FLD_START = 0, FLD_DATA = 1, FLD_PARITY = 2, FLD_STOP = 3 };

/* 프레임 안 위치 pos(0 = start) 가 어느 필드인지. *index = 그 필드 안 번호(data 면 비트 번호 d0..) */
static int uart_field_at(const uart_cfg_t *c, int pos, int *index) {
    (void)c; (void)pos; (void)index;
    return FLD_NONE; /* TODO: 0 = START, 1..d = DATA(d0..), PARITY, 나머지 STOP. 범위 밖 FLD_NONE */
}

/* ---------------- Part C: 스트림 송신 · 비트당 1샘플 수신기 ---------------- */

/* 선로 타임라인: [idle 1 x lead] + 문자마다 [프레임][idle 1 x gap]. 반환 = 총 비트 수 또는 음수 오류 */
static int uart_tx_stream(const uint8_t *msg, size_t n, const uart_cfg_t *c,
                          unsigned lead, unsigned gap, uint8_t *bits, size_t cap) {
    (void)msg; (void)n; (void)c; (void)lead; (void)gap; (void)bits; (void)cap;
    return 0; /* TODO: idle x lead, 문자마다 uart_encode + idle x gap. cap 밖에 쓰지 말 것 */
}

enum { RX_WAIT_HIGH = 0, RX_IDLE = 1, RX_FRAME = 2 };

typedef struct {
    uart_cfg_t cfg;
    uint8_t state;                 /* RX_WAIT_HIGH: 선이 1 이 되길 기다림 (시작 직후, FE/break 뒤) */
    uint8_t n;                     /* 지금 프레임에서 모은 비트 수 */
    uint8_t buf[UART_MAX_FRAME];
} uart_rx_t;

static void uart_rx_init(uart_rx_t *rx, const uart_cfg_t *c) {
    memset(rx, 0, sizeof *rx);
    rx->cfg = *c;
    rx->state = RX_WAIT_HIGH;      /* 캡처가 프레임 한가운데서 시작할 수 있으니 1 -> 0 에지만 start 로 본다 */
}

/* 비트 시간마다 한 번 호출 (비트 중앙에서 잰 레벨 하나). 프레임이 끝나면 1 을 돌려주고
 * *data, *flags 를 채운다 (flags = uart_decode 결과). 그 밖에는 0. */
static int uart_rx_step(uart_rx_t *rx, uint8_t level, uint16_t *data, int *flags) {
    (void)rx; (void)level; (void)data; (void)flags;
    return 0; /* TODO: WAIT_HIGH -> IDLE -> (0 이면) FRAME -> L 비트 모이면 uart_decode, 1 반환 */
}

typedef struct {
    uint32_t frames, ok, parity_err, frame_err, overflow;
    uint8_t partial;               /* 캡처가 프레임 도중에 끝났다 */
} uart_stats_t;

/* 비트 타임라인 전체를 수신기에 흘린다. 정상 문자만 out 에 (cap 까지), 오류 문자는 버리고 센다.
 * 반환 = out 에 넣은 문자 수 */
static size_t uart_rx_run(const uint8_t *bits, size_t n, const uart_cfg_t *c,
                          uint16_t *out, size_t cap, uart_stats_t *st) {
    (void)bits; (void)n; (void)c; (void)out; (void)cap; (void)uart_rx_init; (void)uart_rx_step;
    memset(st, 0, sizeof *st);
    return 0; /* TODO: uart_rx_init, 비트마다 uart_rx_step, 통계, 정상 문자만 out (넘치면 overflow++) */
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static const uart_cfg_t C8N1 = {8, PAR_NONE, 1}, C7E1 = {7, PAR_EVEN, 1}, C8E1 = {8, PAR_EVEN, 1},
                        C8O1 = {8, PAR_ODD, 1}, C8N2 = {8, PAR_NONE, 2}, C8O2 = {8, PAR_ODD, 2},
                        C9N1 = {9, PAR_NONE, 1}, C5N1 = {5, PAR_NONE, 1}, C7O1 = {7, PAR_ODD, 1};

static int all_cfgs(uart_cfg_t *v) {           /* data 5..9 x N/E/O x stop 1/2 = 30 */
    int k = 0;
    for (uint8_t d = 5; d <= 9; d++)
        for (uint8_t p = 0; p <= 2; p++)
            for (uint8_t s = 1; s <= 2; s++) v[k++] = (uart_cfg_t){d, p, s};
    return k;
}

static void cfg_name(const uart_cfg_t *c, char *s) {
    snprintf(s, 8, "%u%c%u", c->data_bits, "NEO"[c->parity % 3], c->stop_bits);
}

static bool bits_eq(const uint8_t *a, const char *s) { /* "0100000101" 와 비교 */
    for (size_t i = 0; s[i]; i++)
        if (a[i] != (uint8_t)(s[i] - '0')) return false;
    return true;
}

static size_t nz(int nb) { return nb < 0 ? 0 : (size_t)nb; } /* 음수 오류를 길이 0 으로 */

static unsigned pop7(uint8_t c) { unsigned n = 0; for (int i = 0; i < 7; i++) n += (c >> i) & 1u; return n; }

int main(void) {
    uint8_t b[64];
    uint16_t d = 0;
    uart_cfg_t cfgs[30];
    int ncfg = all_cfgs(cfgs);

    /* ===== Part A ===== */
    CHECK(uart_frame_bits(&C8N1) == 10);
    CHECK(uart_frame_bits(&C7E1) == 10);
    CHECK(uart_frame_bits(&C8E1) == 11);
    CHECK(uart_frame_bits(&C8O2) == 12);
    CHECK(uart_frame_bits(&C5N1) == 7);
    CHECK(uart_eff_permille(&C8N1) == 800);
    CHECK(uart_eff_permille(&C7E1) == 700);
    CHECK(uart_eff_permille(&C8E1) == 727);
    CHECK(uart_eff_permille(&C8O2) == 667);
    CHECK(uart_eff_permille(&C9N1) == 818);
    CHECK(uart_parity_bit(0x41, 8, PAR_EVEN) == 0);   /* 'A' = 0100 0001: 1 이 2개 */
    CHECK(uart_parity_bit(0x41, 8, PAR_ODD) == 1);
    CHECK(uart_parity_bit(0x43, 8, PAR_EVEN) == 1);   /* 'C' = 0100 0011: 1 이 3개 */
    CHECK(uart_parity_bit(0x1FF, 9, PAR_EVEN) == 1);
    CHECK(uart_parity_bit(0x00, 8, PAR_ODD) == 1);
    CHECK(uart_parity_bit(0x43, 8, PAR_NONE) == 0);

    /* 'A' = 0x41 의 선로 비트 (start, d0..d7, [parity], stop...) */
    CHECK(uart_encode(0x41, &C8N1, b, sizeof b) == 10 && bits_eq(b, "0100000101"));
    CHECK(uart_encode(0x41, &C7E1, b, sizeof b) == 10 && bits_eq(b, "0100000101"));
    CHECK(uart_encode(0x41, &C8E1, b, sizeof b) == 11 && bits_eq(b, "01000001001"));
    CHECK(uart_encode(0x41, &C8O2, b, sizeof b) == 12 && bits_eq(b, "010000010111"));
    CHECK(uart_encode(0x43, &C7E1, b, sizeof b) == 10 && bits_eq(b, "0110000111"));
    CHECK(uart_encode(0x55, &C8N1, b, sizeof b) == 10 && bits_eq(b, "0101010101"));
    CHECK(uart_decode((const uint8_t *)"\0\1\0\0\0\0\0\1\0\1", 10, &C8N1, &d) == 0 && d == 0x41);

    /* 전수 왕복: 30 설정 x 2^data 값 (5 비트 32개 ~ 9 비트 512개) */
    {
        long frames = 0, bad = 0;
        for (int ci = 0; ci < ncfg; ci++) {
            const uart_cfg_t *c = &cfgs[ci];
            int L = uart_frame_bits(c);
            for (unsigned v = 0; v < (1u << c->data_bits); v++) {
                uint16_t out = 0xFFFF;
                int r = uart_encode((uint16_t)v, c, b, sizeof b);
                int f = uart_decode(b, (size_t)r, c, &out);
                unsigned ones = 0;
                for (int i = 0; i < r; i++) ones += b[i];
                bool ok = L > 0 && r == L && f == 0 && out == v && b[0] == 0 && b[L - 1] == 1;
                if (c->parity == PAR_EVEN) ok = ok && ((ones - c->stop_bits) % 2 == 0);
                if (c->parity == PAR_ODD) ok = ok && ((ones - c->stop_bits) % 2 == 1);
                frames++;
                if (!ok && bad++ == 0) {
                    char nm[8]; cfg_name(c, nm);
                    printf("  first round-trip mismatch: cfg %s value 0x%X r=%d f=%d out=0x%X\n", nm, v, r, f, out);
                }
            }
        }
        printf("round trip: %d configs, %ld frames, bad=%ld\n", ncfg, frames, bad);
        CHECK(frames == 6 * (32 + 64 + 128 + 256 + 512) && bad == 0);
    }

    /* ===== Part B ===== */
    /* API 오류: 각각 다른 코드 */
    CHECK(uart_frame_bits(&(uart_cfg_t){4, PAR_NONE, 1}) == UART_E_CFG);
    CHECK(uart_frame_bits(&(uart_cfg_t){10, PAR_NONE, 1}) == UART_E_CFG);
    CHECK(uart_frame_bits(&(uart_cfg_t){8, 3, 1}) == UART_E_CFG);
    CHECK(uart_frame_bits(&(uart_cfg_t){8, PAR_NONE, 0}) == UART_E_CFG);
    CHECK(uart_frame_bits(&(uart_cfg_t){8, PAR_NONE, 3}) == UART_E_CFG);
    CHECK(uart_encode(0x80, &C7E1, b, sizeof b) == UART_E_DATA);       /* 7 비트에 0x80 */
    CHECK(uart_encode(0x100, &C8N1, b, sizeof b) == UART_E_DATA);
    CHECK(uart_encode(0x1FF, &C9N1, b, sizeof b) == 11);
    memset(b, 0xEE, sizeof b);
    CHECK(uart_encode(0x41, &C8E1, b, 10) == UART_E_CAP && b[0] == 0xEE && b[10] == 0xEE); /* 아무것도 안 씀 */
    CHECK(uart_encode(0x41, &C8E1, NULL, 64) == UART_E_CAP);
    uart_encode(0x41, &C8N1, b, sizeof b);
    CHECK(uart_decode(b, 9, &C8N1, &d) == UART_E_SHORT);
    b[3] = 2;
    CHECK(uart_decode(b, 10, &C8N1, &d) == UART_E_LEVEL);

    /* 필드 지도 */
    {
        int idx = -9;
        CHECK(uart_field_at(&C8E1, 0, &idx) == FLD_START);
        CHECK(uart_field_at(&C8E1, 1, &idx) == FLD_DATA && idx == 0);
        CHECK(uart_field_at(&C8E1, 8, &idx) == FLD_DATA && idx == 7);
        CHECK(uart_field_at(&C8E1, 9, &idx) == FLD_PARITY);
        CHECK(uart_field_at(&C8E1, 10, &idx) == FLD_STOP && idx == 0);
        CHECK(uart_field_at(&C8N2, 10, &idx) == FLD_STOP && idx == 1);
        CHECK(uart_field_at(&C8N1, 9, &idx) == FLD_STOP && idx == 0);
        CHECK(uart_field_at(&C8N1, 10, &idx) == FLD_NONE && uart_field_at(&C8N1, -1, &idx) == FLD_NONE);
    }

    /* 결함 주입 전수: 30 설정 x 모든 값 x 프레임의 모든 비트 위치 하나 뒤집기.
     * 기대: start -> F_START, parity -> F_PARITY, stop -> F_FRAME,
     *       data -> 패리티 있으면 F_PARITY, 없으면 플래그 0 (조용히 틀린 문자) */
    {
        long trials = 0, bad = 0, silent = 0, silent_expected = 0;
        for (int ci = 0; ci < ncfg; ci++) {
            const uart_cfg_t *c = &cfgs[ci];
            int L = uart_frame_bits(c);
            for (unsigned v = 0; v < (1u << c->data_bits); v++) {
                uint8_t fr[UART_MAX_FRAME];
                uart_encode((uint16_t)v, c, fr, sizeof fr);
                for (int pos = 0; pos < L; pos++) {
                    int idx = 0, fld = uart_field_at(c, pos, &idx), want;
                    unsigned want_d = v;
                    fr[pos] ^= 1u;
                    uint16_t out = 0;
                    int f = uart_decode(fr, (size_t)L, c, &out);
                    fr[pos] ^= 1u;
                    if (fld == FLD_START) want = UART_F_START;
                    else if (fld == FLD_PARITY) want = UART_F_PARITY;
                    else if (fld == FLD_STOP) want = UART_F_FRAME;
                    else {
                        want = c->parity != PAR_NONE ? UART_F_PARITY : 0;
                        want_d = v ^ (1u << idx);
                        if (c->parity == PAR_NONE) silent_expected++;
                    }
                    if (f == 0 && out != v) silent++;
                    trials++;
                    if ((f != want || out != want_d) && bad++ == 0) {
                        char nm[8]; cfg_name(c, nm);
                        printf("  first inject mismatch: cfg %s v=0x%X pos=%d f=%d want=%d out=0x%X\n",
                               nm, v, pos, f, want, out);
                    }
                }
            }
        }
        printf("1-bit inject: %ld trials, bad=%ld, silent wrong chars=%ld (all in parity=N data bits)\n",
               trials, bad, silent);
        CHECK(bad == 0 && trials > 0);
        CHECK(silent == silent_expected && silent > 0);
    }

    /* 동시 오류: 상태 레지스터처럼 플래그가 겹친다. 그리고 패리티는 2비트 오류를 못 본다 (ALGO-B13) */
    uart_encode(0x41, &C8E1, b, sizeof b);
    b[9] ^= 1u; b[10] = 0;
    CHECK(uart_decode(b, 11, &C8E1, &d) == (UART_F_PARITY | UART_F_FRAME) && d == 0x41);
    uart_encode(0x41, &C8E1, b, sizeof b);
    b[1] ^= 1u; b[2] ^= 1u;
    CHECK(uart_decode(b, 11, &C8E1, &d) == 0 && d == 0x42);          /* 0x41 -> 0x42, 아무도 모름 */
    memset(b, 0, 12);                                                 /* break: 프레임 전체가 0 */
    CHECK(uart_decode(b, 10, &C8N1, &d) == (UART_F_FRAME) && d == 0);
    CHECK(uart_decode(b, 11, &C8O1, &d) == (UART_F_PARITY | UART_F_FRAME) && d == 0);
    memset(b, 1, 12);                                                 /* idle: 프레임이 아님 */
    CHECK(uart_decode(b, 10, &C8N1, &d) & UART_F_START);

    /* ===== Part C ===== */
    static uint8_t line[4096];
    static uint16_t rxb[256];
    const char *msg = "Hello, UART!\r\n";
    const size_t mlen = strlen(msg);
    const uint8_t *m8 = (const uint8_t *)msg;
    uart_stats_t st;

    /* 같은 설정으로 왕복 (간격 0 = back-to-back, 1, 3) */
    {
        const uart_cfg_t *cs[] = {&C8N1, &C7E1, &C8E1, &C8O2, &C9N1, &C7O1};
        int bad = 0;
        for (size_t i = 0; i < sizeof cs / sizeof cs[0]; i++)
            for (unsigned gap = 0; gap <= 3; gap += (gap ? 2 : 1)) {
                int nb = uart_tx_stream(m8, mlen, cs[i], 3, gap, line, sizeof line);
                size_t got = uart_rx_run(line, nz(nb), cs[i], rxb, 256, &st);
                bool ok = nb == (int)(3 + mlen * ((size_t)uart_frame_bits(cs[i]) + gap)) && got == mlen &&
                          st.frames == mlen && st.ok == mlen && !st.partial;
                for (size_t k = 0; ok && k < mlen; k++) ok = rxb[k] == m8[k];
                if (!ok) bad++;
            }
        CHECK(bad == 0);
    }

    /* 송신 버퍼 경계: 정확히 맞는 cap 은 성공, 1 작으면 E_CAP 이고 cap 밖은 안 건드림 */
    {
        int need = 2 + (int)mlen * 11;                                  /* 8N1, lead 2, gap 1 */
        memset(line, 0xEE, 400);
        CHECK(uart_tx_stream(m8, mlen, &C8N1, 2, 1, line, (size_t)need) == need);
        memset(line, 0xEE, 400);
        CHECK(uart_tx_stream(m8, mlen, &C8N1, 2, 1, line, (size_t)need - 1) == UART_E_CAP &&
              line[need - 1] == 0xEE);
        CHECK(uart_tx_stream((const uint8_t *)"\x80", 1, &C7E1, 0, 0, line, 64) == UART_E_DATA);
    }

    /* 수신 버퍼 넘침과 잘린 캡처 */
    {
        int nb = uart_tx_stream(m8, mlen, &C8N1, 1, 0, line, sizeof line);
        size_t got = uart_rx_run(line, nz(nb), &C8N1, rxb, 4, &st);
        CHECK(got == 4 && st.ok == mlen && st.overflow == mlen - 4 && rxb[3] == 'l');
        got = uart_rx_run(line, nz(nb - 5), &C8N1, rxb, 256, &st);      /* 마지막 문자 도중에 끊김 */
        CHECK(got == mlen - 1 && st.partial == 1);
    }

    /* break 가 끼어도 그 뒤 문자를 되찾는다: "AB" + 0 x 25 + "CD" */
    {
        int n1 = uart_tx_stream((const uint8_t *)"AB", 2, &C8N1, 1, 0, line, sizeof line);
        memset(line + nz(n1), 0, 25);
        int n2 = uart_tx_stream((const uint8_t *)"CD", 2, &C8N1, 1, 0, line + nz(n1) + 25, sizeof line - 100);
        size_t got = uart_rx_run(line, nz(n1) + 25 + nz(n2), &C8N1, rxb, 256, &st);
        CHECK(got == 4 && rxb[0] == 'A' && rxb[1] == 'B' && rxb[2] == 'C' && rxb[3] == 'D');
        CHECK(st.frames == 5 && st.frame_err == 1 && st.parity_err == 0);
    }

    /* 설정 불일치 1: 7E1 로 보내고 8N1 로 받기 -> 오류 0, 하지만 bit7 = 패리티 */
    {
        int nb = uart_tx_stream(m8, mlen, &C7E1, 2, 0, line, sizeof line);
        size_t got = uart_rx_run(line, nz(nb), &C8N1, rxb, 256, &st);
        bool ok = got == mlen && st.parity_err == 0 && st.frame_err == 0;
        printf("7E1 -> 8N1 rx:");
        for (size_t k = 0; k < got; k++) {
            printf(" %02X", rxb[k]);
            ok = ok && rxb[k] == (m8[k] | (pop7(m8[k]) & 1u) << 7);
        }
        printf("\n");
        CHECK(ok);
    }
    /* 불일치 2: 8N1 -> 7E1. 1 이 홀수 개인 문자만 PE (d7=0 이 패리티 자리로 읽힘) */
    {
        unsigned odd = 0;
        for (size_t k = 0; k < mlen; k++) odd += pop7(m8[k]) & 1u;
        int nb = uart_tx_stream(m8, mlen, &C8N1, 2, 0, line, sizeof line);
        size_t got = uart_rx_run(line, nz(nb), &C7E1, rxb, 256, &st);
        printf("8N1 -> 7E1 rx: frames=%u ok=%u PE=%u FE=%u\n", st.frames, st.ok, st.parity_err, st.frame_err);
        CHECK(st.frames == mlen && st.parity_err == odd && st.frame_err == 0 && got == mlen - odd);
    }
    /* 불일치 3: 8E1 -> 8N1 (back-to-back). 패리티 비트가 stop 자리 -> 패리티 0 인 문자만 FE */
    {
        unsigned even = 0;
        for (size_t k = 0; k < mlen; k++) even += !(pop7(m8[k]) & 1u);
        int nb = uart_tx_stream(m8, mlen, &C8E1, 2, 0, line, sizeof line);
        size_t got = uart_rx_run(line, nz(nb), &C8N1, rxb, 256, &st);
        printf("8E1 -> 8N1 rx: frames=%u ok=%u PE=%u FE=%u\n", st.frames, st.ok, st.parity_err, st.frame_err);
        CHECK(st.frames == mlen && st.frame_err == even && got == mlen - even);
    }
    /* 불일치 4: 8N2 -> 8N1 은 문제없음. 8N1 -> 8N2 는 간격 1 이면 OK, back-to-back 이면 깨짐 */
    {
        int nb = uart_tx_stream(m8, mlen, &C8N2, 2, 0, line, sizeof line);
        size_t got = uart_rx_run(line, nz(nb), &C8N1, rxb, 256, &st);
        CHECK(got == mlen && st.ok == mlen);
        nb = uart_tx_stream(m8, mlen, &C8N1, 2, 1, line, sizeof line);
        got = uart_rx_run(line, nz(nb), &C8N2, rxb, 256, &st);
        CHECK(got == mlen && st.ok == mlen);
        nb = uart_tx_stream(m8, mlen, &C8N1, 2, 0, line, sizeof line);
        got = uart_rx_run(line, nz(nb), &C8N2, rxb, 256, &st);
        printf("8N1 -> 8N2 rx, back-to-back: frames=%u ok=%u FE=%u\n", st.frames, st.ok, st.frame_err);
        CHECK(st.frame_err > 0 && st.ok < mlen);
    }

    /* 중간 시작: back-to-back "Hello, UART!\r\n" 의 아무 비트에서 듣기 시작해도,
     * 한 문자 시간(10 비트) idle 뒤의 "OK\r\n" 은 반드시 정확히 받는다 */
    {
        const char *tail = "OK\r\n";
        int n1 = uart_tx_stream(m8, mlen, &C8N1, 0, 0, line, sizeof line);
        int n2 = uart_tx_stream((const uint8_t *)tail, 4, &C8N1, 10, 0, line + nz(n1), sizeof line - nz(n1));
        CHECK(n1 == (int)mlen * 10 && n2 == 10 + 4 * 10);
        int bad = 0, junk = 0;
        for (int k = 0; k < n1; k++) {
            size_t got = uart_rx_run(line + k, nz(n1 + n2 - k), &C8N1, rxb, 256, &st);
            bool ok = got >= 4 && !st.partial;
            for (int j = 0; ok && j < 4; j++) ok = rxb[got - 4 + (size_t)j] == (uint8_t)tail[j];
            if (!ok) { bad++; continue; }
            size_t pre = got - 4;                     /* 간격 앞에서 받은 문자: msg 의 뒷부분이어야 깨끗 */
            bool clean = st.frame_err == 0 && st.parity_err == 0 && pre <= mlen;
            for (size_t j = 0; clean && j < pre; j++) clean = rxb[j] == m8[mlen - pre + j];
            if (!clean) junk++;
        }
        printf("mid-stream start: %d offsets, tail lost at %d, offsets with junk/errors before the gap: %d\n",
               n1, bad, junk);
        CHECK(bad == 0);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
