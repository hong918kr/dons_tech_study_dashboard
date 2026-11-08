/* SIV-M09 추가 연습 — 한 번에 검증
 *
 * 연습 1: guard(canary) 워드 — CPU checker 없이도 DMA 꼬리 overrun 을 단독 실행에서 잡기
 * 연습 2: 0 실패의 의미 — 깨끗한 밤 n 번이 말해 주는 상한 (rule of three) 과 soak 시간
 * 연습 3: W1C 와 RMW 의 경쟁 — spurious 인터럽트가 생기는 인터리빙을 전수 탐색
 * 연습 4: 진행 감시(heartbeat) — HANG 이 났을 때 "누가" 멈췄는지 이름 붙이기
 * 연습 5: 검출 매트릭스에서 최소 재현 조합 고르기
 * (본문 solution.c 의 모델을 단일 스레드로 축약해 넣었다.)
 */
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

/* ---------- 연습 1: guard 워드 ---------- */
#define AREA      64u
#define NGUARD    4u
#define GUARD_PAT 0xA5A5A5A5u
static uint32_t mem1[AREA + NGUARD];

static void guard_arm(uint32_t *m, uint32_t area, uint32_t ng) {
    for (uint32_t i = 0; i < ng; i++) m[area + i] = GUARD_PAT ^ i;    /* 워드마다 다르게: 복사 반복도 구분 */
}
/* 깨진 첫 guard 의 인덱스(0..ng-1), 멀쩡하면 -1 */
static int guard_scan(const uint32_t *m, uint32_t area, uint32_t ng) {
    for (uint32_t i = 0; i < ng; i++)
        if (m[area + i] != (GUARD_PAT ^ i)) return (int)i;
    return -1;
}
static void dma_copy1(uint32_t *m, uint32_t dst, const uint32_t *src, uint32_t n, bool overrun) {
    uint32_t v = 0;
    for (uint32_t i = 0; i < n; i++) m[dst + i] = v = src[i];
    if (overrun) m[dst + n] = v;                                      /* 꼬리 beat 반복 */
}

/* ---------- 연습 2: 0 실패 상한 ---------- */
static double p_upper_zero(double n, double cl) { return 1.0 - pow(1.0 - cl, 1.0 / n); }
static double soak_seconds(double mttd_s, double cl) { return -log1p(-cl) * mttd_s; }   /* 지수분포 가정 */

/* ---------- 연습 3: W1C vs RMW 전수 탐색 ---------- */
#define B_TMR  (1u << 2)
#define B_DONE (1u << 0)
typedef struct { unsigned schedules, spurious; } ex3_t;
/* agent 0 = DMA setter (rmw: LOAD r; STORE r|DONE  / atomic: OR DONE)
 * agent 1 = ISR (LOAD s; X &= ~s). 시작 상태 X = TMR (처리 대기 중).
 * spurious = ISR 가 TMR 을 가져갔는데(s 에 TMR) 끝났을 때 X 에 TMR 이 다시 서 있음 */
static void ex3_rec(bool rmw, uint32_t x, int pc0, int pc1, uint32_t r0, uint32_t s1, ex3_t *o) {
    int len0 = rmw ? 2 : 1;
    bool any = false;
    if (pc0 < len0) {
        any = true;
        if (!rmw) ex3_rec(rmw, x | B_DONE, pc0 + 1, pc1, r0, s1, o);
        else if (pc0 == 0) ex3_rec(rmw, x, 1, pc1, x, s1, o);
        else ex3_rec(rmw, r0 | B_DONE, 2, pc1, r0, s1, o);
    }
    if (pc1 < 2) {
        any = true;
        if (pc1 == 0) ex3_rec(rmw, x, pc0, 1, r0, x, o);
        else ex3_rec(rmw, x & ~s1, pc0, 2, r0, s1, o);
    }
    if (!any) {
        o->schedules++;
        if ((s1 & B_TMR) && (x & B_TMR)) o->spurious++;
    }
}

/* ---------- 연습 4: heartbeat 감시 ---------- */
/* prev/cur = agent 별 진행 카운터 스냅숏, done = 끝난 agent 마스크. 멈춘 agent 마스크를 돌려준다 */
static unsigned stalled_agents(const unsigned *prev, const unsigned *cur, unsigned done, int n) {
    unsigned m = 0;
    for (int a = 0; a < n; a++)
        if (!(done & (1u << a)) && cur[a] == prev[a]) m |= 1u << a;
    return m;
}

/* ---------- 연습 5: 최소 재현 조합 ---------- */
/* cfg[i] = agent 마스크, hits[i] = 그 조합에서 검출된 시드 수. 검출된 조합 중 agent 가 가장 적은 것
 * (같으면 검출률 높은 것, 그다음 앞의 것). 없으면 -1 */
static int popcnt(unsigned x) { int c = 0; while (x) { x &= x - 1; c++; } return c; }
static int minimal_repro(const unsigned *cfg, const int *hits, int n) {
    int best = -1;
    for (int i = 0; i < n; i++) {
        if (hits[i] == 0) continue;
        if (best < 0 || popcnt(cfg[i]) < popcnt(cfg[best]) ||
            (popcnt(cfg[i]) == popcnt(cfg[best]) && hits[i] > hits[best]))
            best = i;
    }
    return best;
}

int main(void) {
    /* 연습 1 */
    uint32_t src[8];
    for (uint32_t i = 0; i < 8; i++) src[i] = 0xD1000000u | i;
    memset(mem1, 0, sizeof mem1);
    guard_arm(mem1, AREA, NGUARD);
    for (uint32_t b = 0; b < AREA / 8; b++) dma_copy1(mem1, b * 8, src, 8, false);
    CHECK(guard_scan(mem1, AREA, NGUARD) == -1);
    for (uint32_t b = 0; b < AREA / 8; b++) dma_copy1(mem1, b * 8, src, 8, true);
    CHECK(guard_scan(mem1, AREA, NGUARD) == 0 && mem1[AREA] == 0xD1000007u);   /* 단독으로도 잡힘 + 범인 서명 */
    guard_arm(mem1, AREA, NGUARD);
    dma_copy1(mem1, 0, src, 8, true);                                           /* 안쪽 블록: guard 는 무사 */
    CHECK(guard_scan(mem1, AREA, NGUARD) == -1);

    /* 연습 2 */
    CHECK(fabs(p_upper_zero(300, 0.95) - 0.009936) < 1e-5);
    CHECK(fabs(p_upper_zero(3000, 0.95) * 3000 - 3.0) < 0.01);                   /* rule of three: ≈ 3/n */
    CHECK(fabs(soak_seconds(15.6, 0.95) - 46.73) < 0.01);                       /* MTTD 15.6 s → 95% 에 46.7 s */
    CHECK(fabs(soak_seconds(15.6, 0.99) - 71.84) < 0.01);

    /* 연습 3 */
    ex3_t rmw = {0, 0}, at = {0, 0};
    ex3_rec(true, B_TMR, 0, 0, 0, 0, &rmw);
    ex3_rec(false, B_TMR, 0, 0, 0, 0, &at);
    CHECK(rmw.schedules == 6 && rmw.spurious == 2);
    CHECK(at.schedules == 3 && at.spurious == 0);
    printf("[ex3] W1C vs RMW setter: spurious %u/%u, atomic setter %u/%u\n", rmw.spurious, rmw.schedules,
           at.spurious, at.schedules);

    /* 연습 4: CPU0 CPU1 DMA0 DMA1 TMR ISR */
    unsigned p0[6] = {100, 120, 40, 41, 9, 300}, p1[6] = {180, 200, 40, 77, 9, 420};
    CHECK(stalled_agents(p0, p1, 0, 6) == ((1u << 2) | (1u << 4)));             /* DMA0, TMR 멈춤 */
    CHECK(stalled_agents(p0, p1, 1u << 4, 6) == (1u << 2));                     /* TMR 은 끝난 것 */

    /* 연습 5: 본문 Part C 매트릭스 (CPU, DMA1, TMR, DMA0+1, DMA+TMR, ALL) */
    const unsigned cfg[6] = {0x03, 0x08, 0x10, 0x0C, 0x1C, 0x1F};
    const int cq[6] = {0, 0, 0, 7, 8, 8}, irq[6] = {0, 0, 0, 0, 8, 8}, ovr[6] = {0, 0, 0, 0, 0, 7}, none[6] = {0};
    CHECK(minimal_repro(cfg, cq, 6) == 3);
    CHECK(minimal_repro(cfg, irq, 6) == 4);
    CHECK(minimal_repro(cfg, ovr, 6) == 5);
    CHECK(minimal_repro(cfg, none, 6) == -1);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
