/* MEM-B02 추가 연습 — 한 번에 검증
 *   연습 1  MT/s 로 정확히 변환 (DDR4-2400T): tCK 를 833 ps 로 반올림하면 tWR/tRTP 가 1 사이클 틀린다
 *   연습 2  도착 시각 기준 읽기 지연: hit / empty / conflict / conflict+tRAS 대기 / 쓰기 뒤 conflict
 *   연습 3  tRC 는 DDR4-3200AA 단일 뱅크에서 중복 규칙인가 (전수 검사) + tRC 가 더 긴 가상 부품
 *   연습 4  한 뱅크 대역폭: 랜덤 row (tRC 마다 64 B) vs row hit 스트리밍, 필요한 뱅크 수
 *   연습 5  double ceil 의 함정: ceil(14.07 / 0.938) = 16 (정답 15)
 * 본문 모델(뱅크 상태머신 + 체커 + earliest_issue)은 solution.c 축약판을 그대로 넣었다.
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>

/* ---------- solution.c 축약판 ---------- */
typedef struct { uint32_t cl, cwl, bl2, trcd, trp, tras, trc, twr, trtp; } ddr_timing;
typedef enum { CMD_ACT, CMD_RD, CMD_WR, CMD_PRE } cmd_kind;
typedef struct { uint32_t cyc; cmd_kind cmd; uint32_t row; } cmd_t;
typedef enum { R_TRCD, R_TRAS, R_TRP, R_TRC, R_TWR, R_TRTP, R_STATE, R_ORDER, R_COUNT } rule_t;

#define NEVER (INT64_MIN / 4)
typedef struct { int active; uint32_t row; int64_t t_act, t_pre, t_rd, t_wr; } bank_t;

static void bank_init(bank_t *b) { b->active = 0; b->row = 0; b->t_act = b->t_pre = b->t_rd = b->t_wr = NEVER; }
static int bank_state_ok(const bank_t *b, cmd_kind c) {
    return c == CMD_PRE ? 1 : c == CMD_ACT ? !b->active : b->active;
}
static void bank_apply(bank_t *b, const cmd_t *c) {
    switch (c->cmd) {
    case CMD_ACT: b->active = 1; b->row = c->row; b->t_act = c->cyc; b->t_rd = b->t_wr = NEVER; break;
    case CMD_RD: b->t_rd = c->cyc; break;
    case CMD_WR: b->t_wr = c->cyc; break;
    case CMD_PRE: if (b->active) { b->active = 0; b->t_pre = c->cyc; } break;
    }
}
static int64_t max64(int64_t a, int64_t b) { return a > b ? a : b; }
static int64_t earliest_issue(const ddr_timing *t, const bank_t *b, cmd_kind c) {
    int64_t e = 0;
    if (c == CMD_ACT) { e = max64(e, b->t_pre + t->trp); e = max64(e, b->t_act + t->trc); }
    else if (c == CMD_RD || c == CMD_WR) e = max64(e, b->t_act + t->trcd);
    else if (b->active) {
        e = max64(e, b->t_act + t->tras); e = max64(e, b->t_rd + t->trtp);
        e = max64(e, b->t_wr + t->cwl + t->bl2 + t->twr);
    }
    return e;
}
/* 위반 규칙을 비트마스크로 (1u << rule) — 연습 3 용 */
static uint32_t check_mask(const ddr_timing *t, const cmd_t *tr, uint32_t n) {
    bank_t b; bank_init(&b); uint32_t m = 0; int64_t prev = -1;
    for (uint32_t i = 0; i < n; i++) {
        const cmd_t *c = &tr[i];
        if ((int64_t)c->cyc <= prev) { m |= 1u << R_ORDER; continue; }
        prev = c->cyc;
        if (!bank_state_ok(&b, c->cmd)) { m |= 1u << R_STATE; continue; }
        int64_t x = c->cyc;
        if (c->cmd == CMD_ACT) {
            if (x - b.t_pre < t->trp) m |= 1u << R_TRP;
            if (x - b.t_act < t->trc) m |= 1u << R_TRC;
        } else if (c->cmd != CMD_PRE) {
            if (x - b.t_act < t->trcd) m |= 1u << R_TRCD;
        } else if (b.active) {
            if (x - b.t_act < t->tras) m |= 1u << R_TRAS;
            if (x - b.t_rd < t->trtp) m |= 1u << R_TRTP;
            if (x - b.t_wr < t->cwl + t->bl2 + t->twr) m |= 1u << R_TWR;
        }
        bank_apply(&b, c);
    }
    return m;
}
static const ddr_timing T3200 = { 22, 16, 4, 22, 22, 52, 74, 24, 12 };   /* DDR4-3200AA (본문 Part A) */

/* ---------- 연습 1: MT/s 기준 정확 변환 ---------- */
/* tCK = 2 / (MT/s) us = 2 000 000 / MTs ps (정확한 유리수). cycles = ceil(ps * MTs / 2 000 000) */
static uint32_t ps_to_cyc_mts(uint32_t ps, uint32_t mts) {
    if (mts == 0) return UINT32_MAX;
    return (uint32_t)(((uint64_t)ps * mts + 1999999u) / 2000000u);
}
static uint32_t ps_to_cyc(uint32_t ps, uint32_t tck_ps) {           /* 본문 Part A (반올림된 tCK) */
    return (uint32_t)(((uint64_t)ps + tck_ps - 1u) / tck_ps);
}

/* ---------- 연습 2: 도착 시각 기준 읽기 지연 ---------- */
/* prefix 를 그대로 재생(합법 가정)한 뒤, arrive 에 row 읽기 요청이 오면
 * 필요한 PRE/ACT/RD 를 가장 이른 합법 사이클에 낸다. 반환 = 마지막 data beat 끝 - arrive */
static int64_t read_latency(const ddr_timing *t, const cmd_t *prefix, uint32_t n, uint32_t arrive, uint32_t row) {
    bank_t b; bank_init(&b); int64_t prev = -1;
    for (uint32_t i = 0; i < n; i++) { bank_apply(&b, &prefix[i]); prev = prefix[i].cyc; }
    int64_t at = max64(arrive, prev + 1);
    if (b.active && b.row != row) {                                   /* conflict: PRE 먼저 */
        at = max64(at, earliest_issue(t, &b, CMD_PRE));
        cmd_t p = { (uint32_t)at, CMD_PRE, 0 }; bank_apply(&b, &p); at++;
    }
    if (!b.active) {                                                  /* empty: ACT */
        at = max64(at, earliest_issue(t, &b, CMD_ACT));
        cmd_t a = { (uint32_t)at, CMD_ACT, row }; bank_apply(&b, &a); at++;
    }
    at = max64(at, earliest_issue(t, &b, CMD_RD));
    return at + t->cl + t->bl2 - arrive;
}

/* ---------- 연습 4: 대역폭 ---------- */
/* bytes 를 cyc 사이클마다 옮길 때 MB/s (10^6 B/s), 내림. tCK = tck_ps */
static uint64_t mbps(uint64_t bytes, uint64_t cyc, uint64_t tck_ps) {
    return bytes * 1000000ull / (cyc * tck_ps);
}

/* ---------- 하네스 ---------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main(void) {
    /* 연습 1 — DDR4-2400T (17-17-17): tRCD=tRP=14.16 ns, tRAS 32, tRC 46.16, tWR 15, tRTP 7.5 (대표값) */
    {
        const uint32_t ps[6] = { 14160, 14160, 32000, 46160, 15000, 7500 };
        const uint32_t exact[6] = { 17, 17, 39, 56, 18, 9 };
        uint32_t r[6], q[6];
        for (int i = 0; i < 6; i++) { r[i] = ps_to_cyc_mts(ps[i], 2400); q[i] = ps_to_cyc(ps[i], 833); }
        int ok = 1; for (int i = 0; i < 6; i++) ok &= r[i] == exact[i];
        CHECK(ok);
        CHECK(q[0] == 17 && q[2] == 39 && q[3] == 56);       /* 여기까지는 같다          */
        CHECK(q[4] == 19 && q[5] == 10);                     /* tWR, tRTP 는 833 ps 로 1 큼 */
        printf("ex1 DDR4-2400T exact(MT/s): tRCD %u tRP %u tRAS %u tRC %u tWR %u tRTP %u | tCK=833ps: tWR %u tRTP %u\n",
               r[0], r[1], r[2], r[3], r[4], r[5], q[4], q[5]);
        /* DDR4-3200 은 tCK = 625 ps 가 정확하므로 두 방법이 같다 */
        CHECK(ps_to_cyc_mts(13750, 3200) == 22 && ps_to_cyc_mts(32000, 3200) == 52 &&
              ps_to_cyc_mts(45750, 3200) == 74 && ps_to_cyc_mts(15000, 3200) == 24 && ps_to_cyc_mts(7500, 3200) == 12);
        CHECK(ps_to_cyc_mts(1, 0) == UINT32_MAX);
    }
    /* 연습 2 — 지연 표 (사이클, ns = cyc * 0.625) */
    {
        const cmd_t open1[] = { {0, CMD_ACT, 1} };
        const cmd_t open1rd[] = { {0, CMD_ACT, 1}, {22, CMD_RD, 0} };
        const cmd_t open1wr[] = { {0, CMD_ACT, 1}, {22, CMD_WR, 0} };
        int64_t hit = read_latency(&T3200, open1, 1, 200, 1);
        int64_t empty = read_latency(&T3200, NULL, 0, 10, 1);
        int64_t conf = read_latency(&T3200, open1, 1, 200, 2);
        int64_t conf_ras = read_latency(&T3200, open1rd, 2, 30, 2);
        int64_t conf_wr = read_latency(&T3200, open1wr, 2, 30, 2);
        CHECK(hit == 26 && empty == 48 && conf == 70);
        CHECK(conf_ras == 92 && conf_wr == 106);
        printf("ex2 latency cyc: hit %lld empty %lld conflict %lld conflict+tRAS %lld after-WR %lld\n",
               (long long)hit, (long long)empty, (long long)conf, (long long)conf_ras, (long long)conf_wr);
    }
    /* 연습 3 — tRC 단독 위반은 존재하는가: ACT@0, PRE@p, ACT@a 전수 */
    {
        uint32_t only_trc = 0, trc_any = 0, only_trc80 = 0;
        ddr_timing t80 = T3200; t80.trc = 80;
        for (uint32_t p = 1; p <= 150; p++)
            for (uint32_t a = p + 1; a <= 250; a++) {
                const cmd_t tr[] = { {0, CMD_ACT, 1}, {p, CMD_PRE, 0}, {a, CMD_ACT, 2} };
                uint32_t m = check_mask(&T3200, tr, 3);
                if (m & (1u << R_TRC)) {
                    trc_any++;
                    if (!(m & ((1u << R_TRAS) | (1u << R_TRP)))) only_trc++;
                }
                uint32_t m80 = check_mask(&t80, tr, 3);
                if ((m80 & (1u << R_TRC)) && !(m80 & ((1u << R_TRAS) | (1u << R_TRP)))) only_trc80++;
            }
        CHECK(trc_any > 0 && only_trc == 0);
        CHECK(only_trc80 == 21);
        /* ceil 의 준가법성: ceil(x+y) <= ceil(x)+ceil(y) — ps 격자 전수 (tCK 625) */
        uint32_t bad = 0;
        for (uint32_t x = 0; x <= 40000; x += 5)
            for (uint32_t y = 10000; y <= 16000; y += 250)
                if (ps_to_cyc(x + y, 625) > ps_to_cyc(x, 625) + ps_to_cyc(y, 625)) bad++;
        CHECK(bad == 0);
        printf("ex3 tRC violations=%u, tRC-only=%u (DDR4-3200AA); tRC-only with tRC=80: %u\n",
               trc_any, only_trc, only_trc80);
    }
    /* 연습 4 — 한 뱅크 대역폭 */
    {
        uint64_t rnd = mbps(64, T3200.trc, 625);          /* 64 B / tRC     */
        uint64_t hit = mbps(64, T3200.bl2, 625);          /* 64 B / 4 tCK   */
        uint32_t banks = (T3200.trc + T3200.bl2 - 1) / T3200.bl2;
        CHECK(rnd == 1383 && hit == 25600 && banks == 19);
        printf("ex4 one bank random-row %llu MB/s, row-hit stream %llu MB/s, banks to hide tRC %u (x8 DDR4 has 16)\n",
               (unsigned long long)rnd, (unsigned long long)hit, banks);
    }
    /* 연습 5 — double ceil 의 함정 (tCK 0.938 ns, DDR4-2133 근사) */
    {
        double q = 14.07 / 0.938;
        CHECK((long)ceil(q) == 16 && ps_to_cyc(14070, 938) == 15);
        uint32_t mism = 0;
        for (uint32_t p = 1000; p <= 60000; p += 10)
            if ((uint32_t)ceil((p / 1000.0) / 0.938) != ps_to_cyc(p, 938)) mism++;
        CHECK(mism == 8);
        printf("ex5 14.07/0.938 = %.17g -> ceil %ld (int %u); mismatches in 1..60 ns (10 ps step): %u\n",
               q, (long)ceil(q), ps_to_cyc(14070, 938), mism);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
