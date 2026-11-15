/* ARCH-B06 추가 연습 — 한 번에 검증
 *   1. 4비트 Johnson(꼬인 링) 카운터: 8상태, 불법 상태 사이클(lock-out)
 *   2. 자동 재적재(auto-reload) 카운터 = 타이머 PSC/ARR 의 뼈대 (동기 LOAD, TC)
 *   3. 리플 카운터의 과도 상태(glitch) vs 동기 카운터
 *   4. PRBS7 (x^7 + x^6 + 1): 주기 127, 단일 오류 -> 오류 카운트 3
 *   5. CRC-16/CCITT-FALSE = 입력이 섞이는 LFSR: "123456789" -> 0x29B1, 잔여 0, 입력 0 이면 주기 32767
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr && ./pr
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

#define MAXFF 32u
typedef unsigned (*dfn_t)(const uint8_t *q, unsigned i, unsigned in);
static uint32_t pack(const uint8_t *q, unsigned n) {
    uint32_t v = 0;
    for (unsigned i = 0; i < n; i++) v |= (uint32_t)(q[i] & 1u) << i;
    return v;
}
static void unpack(uint8_t *q, unsigned n, uint32_t v) {
    for (unsigned i = 0; i < n; i++) q[i] = (uint8_t)((v >> i) & 1u);
}
static void tick(uint8_t *q, unsigned n, dfn_t dfn, unsigned in) {   /* 본문과 같은 2-phase 엔진 */
    uint8_t d[MAXFF];
    for (unsigned i = 0; i < n; i++) d[i] = (uint8_t)(dfn(q, i, in) & 1u);
    for (unsigned i = 0; i < n; i++) q[i] = d[i];
}

/* ---- 1. Johnson 카운터: d[0] = NOT q[3], d[i] = q[i-1] ---- */
static unsigned johnson4_d(const uint8_t *q, unsigned i, unsigned in) {
    (void)in;
    return i == 0 ? (q[3] ^ 1u) : q[i - 1];
}
/* start 에서 출발해 start 로 돌아올 때까지 스텝 수, 그 사이 상태 0 을 지나는지 */
static unsigned johnson_cycle(uint32_t start, bool *passes_zero) {
    uint8_t q[4];
    unpack(q, 4, start);
    unsigned n = 0;
    *passes_zero = false;
    do {
        tick(q, 4, johnson4_d, 0);
        n++;
        if (pack(q, 4) == 0) *passes_zero = true;
    } while (pack(q, 4) != start && n < 100);
    return n;
}

/* ---- 2. 자동 재적재 카운터 (워드 수준 다음 상태 함수) ----
 * cnt == arr 이고 EN 이면 다음 에지에 0 으로 돌아가며 그 사이클 동안 tc = 1 (업데이트 이벤트).
 * LOAD 는 동기 적재(우선순위 최상). */
typedef struct { uint16_t cnt; uint16_t arr; } arctr_t;
static unsigned arctr_tc(const arctr_t *c, bool en) { return en && c->cnt == c->arr; }
static void arctr_tick(arctr_t *c, bool en, bool load, uint16_t ldval) {
    uint16_t next = c->cnt;                         /* ① 다음 값 계산 */
    if (load)                    next = ldval;
    else if (en && c->cnt == c->arr) next = 0;
    else if (en)                 next = (uint16_t)(c->cnt + 1u);
    c->cnt = next;                                  /* ② 에지에서 반영 */
}

/* ---- 3. 리플 카운터: 비트 i 는 비트 i-1 의 하강 에지(1->0)에서 토글. 단계마다 지연이 있다. ----
 * v 에서 +1 할 때 거쳐가는 모든 순간 값을 trace 에 기록 (마지막 = 최종 값). 기록 개수를 돌려준다. */
static unsigned ripple_increment(uint32_t v, uint32_t *trace) {
    unsigned n = 0;
    for (unsigned i = 0; i < 4; i++) {
        unsigned old = (v >> i) & 1u;
        v ^= 1u << i;                               /* 이 단 토글 (한 단 지연 후) */
        trace[n++] = v & 0xFu;
        if (old == 0) break;                        /* 0->1 은 하강 에지가 아님 -> 전파 끝 */
    }
    return n;
}

/* ---- 4. PRBS7: b[k] = b[k-7] ^ b[k-6]. 7비트 상태: bit0 = b[k-7] (가장 옛), bit6 = b[k-1] ---- */
static uint8_t prbs7_step(uint8_t s, unsigned *out) {
    unsigned nb = (s ^ (s >> 1)) & 1u;             /* b[k-7] ^ b[k-6] */
    *out = s & 1u;
    return (uint8_t)(((unsigned)s >> 1) | (nb << 6));
}
static unsigned prbs7_check(const uint8_t *rx, unsigned n) {
    unsigned err = 0;
    for (unsigned k = 7; k < n; k++)
        if (((rx[k - 7] ^ rx[k - 6]) & 1u) != (rx[k] & 1u)) err++;
    return err;
}

/* ---- 5. CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF, 반사 없음, xorout 0 ---- */
static uint16_t crc16_ccitt_false(const uint8_t *p, size_t n) {
    uint16_t crc = 0xFFFF;
    for (size_t k = 0; k < n; k++) {
        crc ^= (uint16_t)(p[k] << 8);
        for (int b = 0; b < 8; b++)
            crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u) : (uint16_t)(crc << 1);
    }
    return crc;
}
/* 입력이 0 일 때 CRC 레지스터 한 스텝 = 왼쪽 시프트 Galois LFSR */
static uint16_t crc_reg_step(uint16_t s) {
    return (s & 0x8000u) ? (uint16_t)((s << 1) ^ 0x1021u) : (uint16_t)(s << 1);
}

int main(void) {
    /* 1 */
    static const uint32_t jseq[8] = {0x1, 0x3, 0x7, 0xF, 0xE, 0xC, 0x8, 0x0};
    uint8_t q[4] = {0};
    int ok = 1;
    for (unsigned t = 0; t < 8; t++) { tick(q, 4, johnson4_d, 0); if (pack(q, 4) != jseq[t]) ok = 0; }
    CHECK(ok);
    bool z;
    CHECK(johnson_cycle(0x0, &z) == 8 && z);
    CHECK(johnson_cycle(0x5, &z) == 8 && !z);         /* 불법 상태 0101 은 다른 8-사이클에 갇힘 */
    ok = 1;                                           /* 인접 상태는 항상 1비트만 다르다 (glitch-free 디코드) */
    for (unsigned t = 0; t < 8; t++) {
        uint32_t x = jseq[t] ^ jseq[(t + 1) % 8], c = 0;
        while (x) { c += x & 1u; x >>= 1; }
        if (c != 1) ok = 0;
    }
    CHECK(ok);

    /* 2 */
    arctr_t tm = {0, 9};
    unsigned pulses = 0, first = 0;
    for (unsigned t = 1; t <= 100; t++) {
        unsigned tc = arctr_tc(&tm, true);            /* 에지 직전 값으로 판정 */
        if (tc) { pulses++; if (!first) first = t; }
        arctr_tick(&tm, true, false, 0);
    }
    CHECK(pulses == 10 && first == 10);               /* 10 MHz -> 1 MHz 분주: ARR = 9 */
    arctr_tick(&tm, true, true, 7);
    CHECK(tm.cnt == 7);                               /* LOAD 가 EN 보다 우선 */
    arctr_tick(&tm, false, false, 0);
    CHECK(tm.cnt == 7);                               /* EN=0 유지 */

    /* 3 */
    uint32_t tr[4];
    unsigned nt = ripple_increment(0x7, tr);
    CHECK(nt == 4 && tr[0] == 0x6 && tr[1] == 0x4 && tr[2] == 0x0 && tr[3] == 0x8);
    nt = ripple_increment(0xF, tr);
    CHECK(nt == 4 && tr[3] == 0x0);
    nt = ripple_increment(0x4, tr);
    CHECK(nt == 1 && tr[0] == 0x5);                   /* 하위 비트가 0 이면 과도 상태 없음 */
    unsigned bad = 0;                                 /* 디코더 "cnt == 0" 이 거짓으로 켜지는 증가 수 */
    for (uint32_t v = 0; v < 16; v++) {
        nt = ripple_increment(v, tr);
        for (unsigned k = 0; k + 1 < nt; k++) if (tr[k] == 0) bad++;
    }
    CHECK(bad == 3);                                  /* 1->2, 3->4, 7->8 이 중간에 0000 을 지난다 */

    /* 4 */
    uint8_t s7 = 0x7F, bits[400];
    unsigned per = 0;
    do { unsigned o; s7 = prbs7_step(s7, &o); per++; } while (s7 != 0x7F && per < 1000);
    CHECK(per == 127);
    s7 = 0x7F;
    for (unsigned k = 0; k < 400; k++) { unsigned o; s7 = prbs7_step(s7, &o); bits[k] = (uint8_t)o; }
    CHECK(prbs7_check(bits, 400) == 0);
    bits[200] ^= 1u;
    CHECK(prbs7_check(bits, 400) == 3);               /* 항 3개 (x^7, x^6, 1) -> 오류 x3 */

    /* 5 */
    uint8_t msg[11] = {'1', '2', '3', '4', '5', '6', '7', '8', '9', 0, 0};
    uint16_t crc = crc16_ccitt_false(msg, 9);
    CHECK(crc == 0x29B1);
    msg[9] = (uint8_t)(crc >> 8); msg[10] = (uint8_t)crc;
    CHECK(crc16_ccitt_false(msg, 11) == 0);           /* CRC 를 붙여 다시 돌리면 나머지 0 */
    uint16_t s = 0x0001;
    unsigned p = 0;
    do { s = crc_reg_step(s); p++; } while (s != 0x0001 && p < 70000);
    CHECK(p == 32767);                                /* (x+1) 인수 때문에 65535 가 아니다 */

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
