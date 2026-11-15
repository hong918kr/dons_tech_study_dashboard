/* RTOS-M07 demo_bench.c — 타이머 10,000 개: 정렬 리스트(FreeRTOS 방식) vs hashed timing wheel, 호스트 ns 측정
 * BUILD: cc -std=c11 -Wall -Wextra -O2 demo_bench.c -o demo_bench && ./demo_bench
 * (_POSIX_C_SOURCE 는 clock_gettime(CLOCK_MONOTONIC) 선언용.)
 * 측정값은 이 Mac 의 비율로만 읽는다 (캐시가 큰 호스트 CPU, 부하에 따라 흔들림). 결정적인 비교는 solution.c 의 ops.
 * 각 항목: 같은 작업을 5번(매번 새 구조) 돌려 중앙값. 결과 체크섬을 출력해 컴파일러가 일을 지우지 못하게 한다.
 */
#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define NT 10000u
#define TICK_MAX 0xFFFFFFFFu

typedef struct tlist tlist_t;
typedef struct tnode { struct tnode *next, *prev; tlist_t *owner; uint32_t expiry, id; } tnode_t;
struct tlist { tnode_t end; uint32_t n; };
static void list_init(tlist_t *l) { l->end.next = l->end.prev = &l->end; l->end.owner = l; l->n = 0; }
static void list_insert_before(tlist_t *l, tnode_t *pos, tnode_t *x) { x->next = pos; x->prev = pos->prev; pos->prev->next = x; pos->prev = x; x->owner = l; l->n++; }
static void list_remove(tnode_t *x) { x->prev->next = x->next; x->next->prev = x->prev; x->owner->n--; x->owner = NULL; }

/* ---- 정렬 리스트 (solution.c Part A 와 같은 코드, ops 카운터만 뺌) ---- */
typedef struct { tlist_t list[2]; tlist_t *cur, *ovf; uint32_t now, next_unblock; tnode_t node[NT]; } slist_t;
static void sl_init(slist_t *s, uint32_t now) {
    list_init(&s->list[0]); list_init(&s->list[1]); s->cur = &s->list[0]; s->ovf = &s->list[1];
    s->now = now; s->next_unblock = TICK_MAX;
    for (uint32_t i = 0; i < NT; i++) { s->node[i].id = i; s->node[i].owner = NULL; }
}
static void sl_start(slist_t *s, uint32_t id, uint32_t d) {
    tnode_t *x = &s->node[id]; uint32_t wake = s->now + d; tlist_t *l = wake < s->now ? s->ovf : s->cur;
    x->expiry = wake;
    tnode_t *it = l->end.next; while (it != &l->end && it->expiry <= wake) it = it->next;
    list_insert_before(l, it, x);
    if (l == s->cur && wake < s->next_unblock) s->next_unblock = wake;
}
static void sl_cancel(slist_t *s, uint32_t id) { if (s->node[id].owner) list_remove(&s->node[id]); }
static int sl_tick(slist_t *s, uint32_t *out) {
    int n = 0; s->now++;
    if (s->now == 0) { tlist_t *t = s->cur; s->cur = s->ovf; s->ovf = t; s->next_unblock = s->cur->n ? s->cur->end.next->expiry : TICK_MAX; }
    if (s->now >= s->next_unblock)
        for (;;) {
            if (!s->cur->n) { s->next_unblock = TICK_MAX; break; }
            tnode_t *h = s->cur->end.next;
            if (s->now < h->expiry) { s->next_unblock = h->expiry; break; }
            list_remove(h); out[n++] = h->id;
        }
    return n;
}
static uint32_t sl_next(slist_t *s) { return s->cur->n ? s->cur->end.next->expiry - s->now : s->ovf->n ? s->ovf->end.next->expiry - s->now : 0; }

/* ---- 휠 (solution.c Part B 와 같은 코드) ---- */
typedef struct { tlist_t slot[4096]; uint32_t mask, nslots, now, count; tnode_t node[NT]; } wheel_t;
static void hw_init(wheel_t *w, uint32_t nslots, uint32_t now) {
    w->nslots = nslots; w->mask = nslots - 1; w->now = now; w->count = 0;
    for (uint32_t i = 0; i < nslots; i++) list_init(&w->slot[i]);
    for (uint32_t i = 0; i < NT; i++) { w->node[i].id = i; w->node[i].owner = NULL; }
}
static void hw_start(wheel_t *w, uint32_t id, uint32_t d) {
    tnode_t *x = &w->node[id]; x->expiry = w->now + d; tlist_t *l = &w->slot[x->expiry & w->mask];
    list_insert_before(l, &l->end, x); w->count++;
}
static void hw_cancel(wheel_t *w, uint32_t id) { if (w->node[id].owner) { list_remove(&w->node[id]); w->count--; } }
static int hw_tick(wheel_t *w, uint32_t *out) {
    int n = 0; w->now++; tlist_t *l = &w->slot[w->now & w->mask];
    for (tnode_t *it = l->end.next, *nx; it != &l->end; it = nx) { nx = it->next; if (it->expiry == w->now) { list_remove(it); w->count--; out[n++] = it->id; } }
    return n;
}
static uint32_t hw_next(wheel_t *w) {
    if (!w->count) return 0;
    uint32_t best = TICK_MAX;
    for (uint32_t k = 1; k <= w->nslots; k++) {
        tlist_t *l = &w->slot[(w->now + k) & w->mask];
        for (tnode_t *it = l->end.next; it != &l->end; it = it->next) { uint32_t d = it->expiry - w->now; if (d < best) best = d; }
        if (best == k) break;
    }
    return best;
}

/* ---- 구조 하나를 고르는 얇은 디스패치 (kind 0 = sorted, 그 외 = 휠 슬롯 수) ---- */
static slist_t S; static wheel_t W;
static int g_kind;
static void q_init(uint32_t now) { if (g_kind) hw_init(&W, (uint32_t)g_kind, now); else sl_init(&S, now); }
static void q_start(uint32_t id, uint32_t d) { if (g_kind) hw_start(&W, id, d); else sl_start(&S, id, d); }
static void q_cancel(uint32_t id) { if (g_kind) hw_cancel(&W, id); else sl_cancel(&S, id); }
static int q_tick(uint32_t *o) { return g_kind ? hw_tick(&W, o) : sl_tick(&S, o); }
static uint32_t q_next(void) { return g_kind ? hw_next(&W) : sl_next(&S); }

static double now_ns(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (double)t.tv_sec * 1e9 + (double)t.tv_nsec; }
static uint32_t g_rng;
static uint32_t rnd(uint32_t n) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng % n; }
static uint32_t g_out[NT], g_delay[NT], g_perm[NT], g_period[NT];
static volatile uint64_t g_sink;

static int cmpd(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return (x > y) - (x < y); }
#define RUNS 5
typedef struct { double v[RUNS]; } samp_t;
static double med(samp_t *s) { qsort(s->v, RUNS, sizeof s->v[0], cmpd); return s->v[RUNS / 2]; }

int main(void) {
    const int kinds[4] = { 0, 256, 1024, 4096 };
    const char *kname[4] = { "sorted list", "wheel N=256", "wheel N=1024", "wheel N=4096" };
    g_rng = 2026;
    for (uint32_t i = 0; i < NT; i++) { g_delay[i] = 1 + rnd(10000); g_period[i] = 100 + rnd(9901); g_perm[i] = i; }
    for (uint32_t i = NT - 1; i > 0; i--) { uint32_t j = rnd(i + 1), t = g_perm[i]; g_perm[i] = g_perm[j]; g_perm[j] = t; }

    double c0 = now_ns(), cs = 0;                       /* 시계 읽기 2번의 비용 (steady tick 칸에 섞여 있다) */
    for (int j = 0; j < 100000; j++) { double a = now_ns(); cs += now_ns() - a; }
    printf("clock_gettime pair overhead: %.1f ns (loop %.1f ms)\n", cs / 100000, (now_ns() - c0) / 1e6);
    printf("10,000 timers, median of %d runs, ns per operation (host ratios only)\n", RUNS);
    printf("%-13s | %10s | %10s | %12s %12s | %11s | %13s\n", "structure", "insert", "cancel", "steady tick", "  + re-arm", "RX reset", "next(), 5 tmr");
    for (int k = 0; k < 4; k++) {
        g_kind = kinds[k];
        samp_t ins, can, tk, rearm, rst, nx;
        for (int r = 0; r < RUNS; r++) {
            /* 1) 빈 구조에 10,000 개 삽입 (지연 1..10000 무작위) */
            q_init(0xFFFFFFFFu - 3000u);
            double t0 = now_ns();
            for (uint32_t i = 0; i < NT; i++) q_start(i, g_delay[i]);
            ins.v[r] = (now_ns() - t0) / NT;
            /* 2) 무작위 순서로 전부 취소 */
            t0 = now_ns();
            for (uint32_t i = 0; i < NT; i++) q_cancel(g_perm[i]);
            can.v[r] = (now_ns() - t0) / NT;
            /* 3) 정상 상태: 10,000 개 주기 타이머(100..10000 tick), 만료되면 재장전. 20,000 tick (wrap 포함).
             *    tick 처리 시간과 재장전(start) 시간을 따로 잰다 */
            q_init(0xFFFFFFFFu - 10000u);
            for (uint32_t i = 0; i < NT; i++) q_start(i, g_period[i]);
            double tsum = 0, rsum = 0; uint64_t fired = 0;
            for (int t = 0; t < 20000; t++) {
                double a = now_ns();
                int n = q_tick(g_out);
                double b = now_ns();
                for (int j = 0; j < n; j++) q_start(g_out[j], g_period[g_out[j]]);
                rsum += now_ns() - b; tsum += b - a; fired += (uint64_t)n;
            }
            tk.v[r] = tsum / 20000; rearm.v[r] = fired ? rsum / (double)fired : 0; g_sink += fired;
            /* 4) 게이트웨이: 10,000 개가 걸린 상태에서 무작위 신호 수신 → reset(cancel + start(3000)) 20,000 번 */
            g_rng = 77;
            t0 = now_ns();
            for (int j = 0; j < 20000; j++) { uint32_t id = rnd(NT); q_cancel(id); q_start(id, 3000); }
            rst.v[r] = (now_ns() - t0) / 20000;
            /* 5) tickless next(): 타이머 5개(100/250/1000/5000/60000)만 있는 노드에서 100,000 번 */
            q_init(0);
            const uint32_t per[5] = { 100, 250, 1000, 5000, 60000 };
            for (uint32_t i = 0; i < 5; i++) q_start(i, per[i]);
            t0 = now_ns(); uint64_t acc = 0;
            for (int j = 0; j < 100000; j++) acc += q_next();
            nx.v[r] = (now_ns() - t0) / 100000; g_sink += acc;
        }
        printf("%-13s | %10.1f | %10.1f | %12.1f %12.1f | %11.1f | %13.1f\n", kname[k], med(&ins), med(&can), med(&tk), med(&rearm), med(&rst), med(&nx));
    }
    printf("(checksum %llu)\n", (unsigned long long)g_sink);
    return 0;
}
