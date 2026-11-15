/* RTOS-B06 추가 연습 — 한 번에 검증
 *   연습 1  FreeRTOS 의 두 리스트 트릭 (pxDelayedTaskList / pxOverflowDelayedTaskList) 을 32비트로 구현하고
 *           wrap 안전 비교를 쓰는 단일 리스트와 무작위 시나리오에서 "깨어나는 (tick, id) 순서" 가 같은지 교차 검증
 *   연습 2  "delay(P - 경과시간)" 손 계산 주기: 시계를 읽은 뒤 delay 를 부르기 전에 지나간 시간 때문에 드리프트
 *   연습 3  16비트 tick: wrap 안전 비교는 지연 < 2^15 까지만, 두 리스트 트릭은 그보다 긴 지연도 맞다
 *   연습 4  xTaskPeriodicDelay (V11.3.2 tasks.c:2365) 의미를 계산식으로 구현, xTaskDelayUntil 과 overrun 처리 비교
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t g_rng = 0xC0FFEE06u;
static uint32_t xs32(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }

static bool reached32(uint32_t now, uint32_t wake) { return (uint32_t)(now - wake) < 0x80000000u; }
static bool reached16(uint16_t now, uint16_t wake) { return (uint16_t)(now - wake) < 0x8000u; }

/* ===================== 연습 1: 두 리스트 vs wrap 안전 단일 리스트 ===================== */
#define NODES 256
typedef struct { uint32_t wake; int id, next; } node_t;   /* 배열 인덱스 링크 (-1 = 끝) */
static node_t g_nd[NODES];

/* 오름차순 삽입. less(a, b) 가 거짓인 동안(= a 가 b 보다 앞이 아님) 지나간다 → 같은 값은 뒤에 붙는다(FIFO).
 * FreeRTOS vListInsert 도 "<= 동안 지나간다" (list.c:192) */
static void ins(int *head, int n, bool (*before)(uint32_t a, uint32_t b, uint32_t now), uint32_t now) {
    int *pp = head;
    while (*pp >= 0 && !before(g_nd[n].wake, g_nd[*pp].wake, now)) pp = &g_nd[*pp].next;
    g_nd[n].next = *pp; *pp = n;
}
static bool before_raw(uint32_t a, uint32_t b, uint32_t now) { (void)now; return a < b; }
static bool before_safe(uint32_t a, uint32_t b, uint32_t now) { return (uint32_t)(a - now) < (uint32_t)(b - now); }

typedef struct { int cur, ovf; uint32_t now; int swaps, swap_nonempty; } dl2_t;     /* FreeRTOS 방식 */
static void dl2_insert(dl2_t *d, int n, uint32_t ticks) {
    g_nd[n].wake = d->now + ticks;
    if (g_nd[n].wake < d->now) ins(&d->ovf, n, before_raw, d->now);   /* wake 가 넘쳤다 → overflow 리스트 */
    else                       ins(&d->cur, n, before_raw, d->now);   /* 평범한 < 비교로 충분 */
}
static int dl2_tick(dl2_t *d, int *out) {          /* tick 1 증가 후 깨어난 id 들을 out 에 */
    int k = 0;
    d->now++;
    if (d->now == 0) {                              /* taskSWITCH_DELAYED_LISTS() (tasks.c:267-278) */
        if (d->cur >= 0) d->swap_nonempty++;        /* configASSERT( listLIST_IS_EMPTY(pxDelayedTaskList) ) */
        int t = d->cur; d->cur = d->ovf; d->ovf = t; d->swaps++;
    }
    while (d->cur >= 0 && d->now >= g_nd[d->cur].wake) { out[k++] = g_nd[d->cur].id; d->cur = g_nd[d->cur].next; }
    return k;
}
typedef struct { int head; uint32_t now; } dl1_t;                  /* 이 레슨 본문 방식 */
static void dl1_insert(dl1_t *d, int n, uint32_t ticks) {
    g_nd[n].wake = d->now + ticks;
    ins(&d->head, n, before_safe, d->now);
}
static int dl1_tick(dl1_t *d, int *out) {
    int k = 0;
    d->now++;
    while (d->head >= 0 && reached32(d->now, g_nd[d->head].wake)) { out[k++] = g_nd[d->head].id; d->head = g_nd[d->head].next; }
    return k;
}

static void ex1(void) {
    int mism = 0, swaps = 0, nonempty = 0; long events = 0;
    for (int sc = 0; sc < 200; sc++) {
        uint32_t st = 0xFFFFFFFFu - xs32() % 4000;          /* 시작은 wrap 직전 */
        dl2_t a = { -1, -1, st, 0, 0 };
        dl1_t b = { -1, st };
        int nid = 0;
        for (int t = 0; t < 9000; t++) {
            if (nid < NODES / 2 && xs32() % 50 == 0) {      /* 같은 지연 요청을 두 구조에 (노드는 따로) */
                uint32_t ticks = 1 + xs32() % 3000;
                g_nd[nid * 2]     = (node_t){ 0, nid, -1 };
                g_nd[nid * 2 + 1] = (node_t){ 0, nid, -1 };
                dl2_insert(&a, nid * 2, ticks);
                dl1_insert(&b, nid * 2 + 1, ticks);
                nid++;
            }
            int oa[NODES], ob[NODES];
            int ka = dl2_tick(&a, oa), kb = dl1_tick(&b, ob);
            if (ka != kb || memcmp(oa, ob, sizeof(int) * (size_t)ka) != 0) mism++;
            events += ka;
        }
        swaps += a.swaps; nonempty += a.swap_nonempty;
    }
    printf("ex1 two-list vs wrap-safe: 200 scenarios x 9000 ticks, wake events=%ld, mismatching ticks=%d, "
           "list swaps=%d, swap with non-empty current list=%d\n", events, mism, swaps, nonempty);
    CHECK(mism == 0 && swaps == 200 && nonempty == 0 && events > 10000);
}

/* ===================== 연습 2: delay(P - elapsed) 손 계산 ===================== */
/* 작은 tick 커널: HI(p2) 는 13 tick 마다 3 tick, PER(p1) 은 주기 10.
 * PER 한 주기 = 작업 c tick → [시계 읽기] → 정리(로그) 1 tick → 지연 호출. 정리 중 HI 에 선점될 수 있다. */
typedef struct { int prio, blocked, pc, left, k, mode; uint32_t wake, last, rel0, readt, rel[40]; } mt_t;
static mt_t g_mt[2];
static uint32_t g_now2;
static void mt_delay(mt_t *t, uint32_t n) { t->blocked = 1; t->wake = g_now2 + n; }
static bool mt_until(mt_t *t, uint32_t *last, uint32_t p) {
    uint32_t el = g_now2 - *last, w = *last + p; *last = w;
    if (el < p) { t->blocked = 1; t->wake = w; return true; }
    return false;
}
static void hi_step(mt_t *t) {
    if (t->pc == 0) { t->left = 3; t->pc = 1; }
    if (--t->left > 0) return;
    t->pc = 0; mt_until(t, &t->last, 13);
}
static void per_step2(mt_t *t) {
    static const int cost[] = { 2, 1, 3, 2, 1, 3, 3, 1, 2, 2 };
    if (t->pc == 0) { t->rel[t->k] = g_now2 - t->rel0; t->left = cost[t->k % 10]; t->pc = 1; }
    if (t->pc == 1) {                                /* 작업 */
        if (--t->left > 0) return;
        t->readt = g_now2;                           /* elapsed = now - rel 을 이때 읽는다 */
        t->pc = 2; return;
    }
    t->pc = 0; t->k++;                               /* pc 2: 정리 1 tick 후 지연 호출 */
    if (t->mode == 0) mt_delay(t, 10 - ((t->readt - t->rel0) - t->rel[t->k - 1]));   /* naive */
    else              mt_until(t, &t->last, 10);
}
static void ex2_run(int mode, uint32_t *rel) {
    memset(g_mt, 0, sizeof g_mt);
    g_now2 = 0xFFFFFFF8u;
    g_mt[0].prio = 2; g_mt[0].last = g_now2;
    g_mt[1].prio = 1; g_mt[1].last = g_now2; g_mt[1].rel0 = g_now2; g_mt[1].mode = mode;
    while (g_mt[1].k < 31) {
        for (int i = 0; i < 2; i++) if (g_mt[i].blocked && reached32(g_now2, g_mt[i].wake)) g_mt[i].blocked = 0;
        if (!g_mt[0].blocked) hi_step(&g_mt[0]); else if (!g_mt[1].blocked) per_step2(&g_mt[1]);
        g_now2++;
    }
    memcpy(rel, g_mt[1].rel, sizeof(uint32_t) * 31);
}
static void ex2(void) {
    uint32_t rn[31], ru[31];
    ex2_run(0, rn); ex2_run(1, ru);
    printf("ex2 k    :"); for (int k = 0; k <= 30; k += 5) printf(" %4d", k);
    printf("\nex2 naive:"); for (int k = 0; k <= 30; k += 5) printf(" %4d", (int)rn[k] - 10 * k);
    printf("   (drift = release - 10k)\nex2 until:"); for (int k = 0; k <= 30; k += 5) printf(" %4d", (int)ru[k] - 10 * k);
    int maxl = 0; for (int k = 0; k <= 30; k++) if ((int)ru[k] - 10 * k > maxl) maxl = (int)ru[k] - 10 * k;
    printf("\nex2 naive drift after 30 periods = %d ticks, delay_until max lateness = %d\n", (int)rn[30] - 300, maxl);
    CHECK((int)rn[30] - 300 >= 30);           /* 정리 1 tick 이 매 주기 새어 나간다 + 선점 */
    CHECK(maxl <= 3 && (int)ru[30] - 300 <= 3);
}

/* ===================== 연습 3: 16비트 tick 의 긴 지연 ===================== */
static long ex3_safe(uint16_t start, uint16_t ticks) {        /* wrap 안전 비교: 몇 tick 뒤에 깨나 */
    uint16_t now = start, wake = (uint16_t)(start + ticks);
    for (long t = 0; t < 200000; t++, now++) if (reached16(now, wake)) return t;
    return -1;
}
static long ex3_twolist(uint16_t start, uint16_t ticks) {     /* FreeRTOS 방식 (16비트 TickType_t) */
    uint16_t now = start, wake = (uint16_t)(start + ticks);
    bool in_ovf = wake < now;                                   /* 넘쳤으면 overflow 리스트 */
    for (long t = 0; t < 200000; t++) {
        if (t > 0) { now++; if (now == 0 && in_ovf) in_ovf = false; }   /* wrap 때 리스트 교체 */
        if (!in_ovf && now >= wake) return t;
    }
    return -1;
}
static void ex3(void) {
    struct { uint16_t start, ticks; } c[] = { { 100, 0x7FFF }, { 100, 40000 }, { 60000, 40000 }, { 60000, 0xFFFF } };
    for (unsigned i = 0; i < 4; i++) {
        long s = ex3_safe(c[i].start, c[i].ticks), w = ex3_twolist(c[i].start, c[i].ticks);
        printf("ex3 16-bit start=%5u delay=%5u: wrap-safe compare woke after %6ld, two-list after %6ld\n",
               c[i].start, c[i].ticks, s, w);
    }
    CHECK(ex3_safe(100, 0x7FFF) == 0x7FFF && ex3_twolist(100, 0x7FFF) == 0x7FFF);
    CHECK(ex3_safe(100, 40000) == 0 && ex3_twolist(100, 40000) == 40000);        /* 비교: 즉시 깨는 버그 */
    CHECK(ex3_safe(60000, 40000) == 0 && ex3_twolist(60000, 40000) == 40000);
    CHECK(ex3_twolist(60000, 0xFFFF) == 0xFFFF);
}

/* ===================== 연습 4: xTaskPeriodicDelay vs xTaskDelayUntil ===================== */
/* tasks.c:2378-2390 를 그대로 옮긴 계산: 반환 = 더한 주기 수, *wait = 블록할 tick (항상 1..P) */
static uint32_t periodic_delay_calc(uint32_t now, uint32_t *last, uint32_t p, uint32_t *wait) {
    uint32_t elapsed = now - *last;
    uint32_t inc = elapsed / p;
    *last += inc * p;
    *wait = p - (elapsed - inc * p);
    return inc;
}
/* 가장 높은 우선순위 태스크 하나: 작업 먼저, 그다음 지연. 작업 c tick → c-1 tick 뒤에 지연 호출.
 * policy 0 = xTaskDelayUntil (pdFALSE 면 같은 우선순위에 다른 태스크가 없으니 같은 tick 에 바로 다음 작업),
 * policy 1 = xTaskPeriodicDelay. 진짜 FreeRTOS 데모(demo_freertos_delay.c 절 3)와 같은 숫자가 나와야 한다. */
static int ex4_sim(int policy, int long_k, uint32_t long_c, uint32_t *rel, uint32_t *ret, int n) {
    uint32_t start = 0xFFFFFFF0u, t = start, last = start;
    for (int k = 0; k < n; k++) {
        rel[k] = t - start;
        uint32_t call = t + ((k == long_k) ? long_c : 2u) - 1;
        if (policy == 0) {
            uint32_t el = call - last, w = last + 10; last = w;
            ret[k] = el < 10;                        /* pdTRUE = 막혔다 */
            t = ret[k] ? w : call;
        } else {
            uint32_t wait;
            ret[k] = periodic_delay_calc(call, &last, 10, &wait);
            t = call + wait;
        }
    }
    return n;
}
static void ex4(void) {
    uint32_t r0[9], r1[9], v0[9], v1[9], e0[9], e1[9], f0[9], f1[9];
    ex4_sim(0, 3, 25, r0, v0, 9); ex4_sim(1, 3, 25, r1, v1, 9);
    ex4_sim(0, 3, 11, e0, f0, 9); ex4_sim(1, 3, 11, e1, f1, 9);
    const char *nm[] = { "DelayUntil   ", "PeriodicDelay" };
    uint32_t *R[4] = { r0, r1, e0, e1 }, *V[4] = { v0, v1, f0, f1 };
    for (int i = 0; i < 4; i++) {
        printf("ex4 %s k=3 cost %2u: releases", nm[i & 1], i < 2 ? 25u : 11u);
        for (int k = 0; k < 9; k++) printf(" %3u", (unsigned)R[i][k]);
        printf("  | returns");
        for (int k = 0; k < 9; k++) printf(" %u", (unsigned)V[i][k]);
        printf("\n");
    }
    uint32_t x0[] = { 0, 10, 20, 30, 54, 55, 60, 70, 80 }, x1[] = { 0, 10, 20, 30, 60, 70, 80, 90, 100 };
    CHECK(memcmp(r0, x0, sizeof x0) == 0 && v0[3] == 0 && v0[4] == 0 && v0[5] == 1);
    CHECK(memcmp(r1, x1, sizeof x1) == 0 && v1[0] == 0 && v1[1] == 1 && v1[3] == 3);
    uint32_t y0[] = { 0, 10, 20, 30, 40, 50, 60, 70, 80 }, y1[] = { 0, 10, 20, 30, 50, 60, 70, 80, 90 };
    CHECK(memcmp(e0, y0, sizeof y0) == 0 && f0[3] == 0);          /* 마감 == now → pdFALSE 지만 그 tick 에 바로 실행 = 제때 */
    CHECK(memcmp(e1, y1, sizeof y1) == 0 && f1[3] == 2);          /* 정확히 걸친 격자점(40)도 건너뛴다 */
    uint32_t last = 0xFFFFFFFAu, w;
    CHECK(periodic_delay_calc(0x00000003u, &last, 10, &w) == 0 && last == 0xFFFFFFFAu && w == 1);  /* wrap 너머 */
}

int main(void) {
    ex1();
    ex2();
    ex3();
    ex4();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
