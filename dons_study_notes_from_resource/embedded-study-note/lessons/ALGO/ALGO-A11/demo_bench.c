/* ALGO-A11 demo_bench — 리미터별 결정 1건당 시간과 상태 크기 (호스트 Apple Silicon, -O2).
 * 같은 무작위 트래픽(10^7 요청, 틱당 0~3개)을 네 리미터에 흘린다. 워밍업 1회 + 7회 측정, 중앙값과 최소~최대.
 * 절대 ns 보다 비율을 볼 것 (다른 작업이 같이 도는 기계). 통과 수를 출력해 컴파일러가 일을 지우지 못하게 한다.
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_bench.c -o demo_bench && ./demo_bench
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct { uint32_t level, cap, num, den, burst, full_ticks, last; } tbucket_t;
static void tb_init(tbucket_t *tb, uint32_t burst, uint32_t num, uint32_t den, uint32_t now) {
    memset(tb, 0, sizeof *tb);
    tb->burst = burst; tb->num = num; tb->den = den; tb->cap = burst * den;
    tb->full_ticks = tb->cap / num + (tb->cap % num != 0); tb->level = tb->cap; tb->last = now;
}
static bool tb_try(tbucket_t *tb, uint32_t now, uint32_t cost) {
    uint32_t dt = now - tb->last;
    if (dt != 0 && dt <= 0x7FFFFFFFu) {
        tb->last = now;
        if (dt >= tb->full_ticks) tb->level = tb->cap;
        else { uint32_t lv = tb->level + dt * tb->num; tb->level = lv < tb->cap ? lv : tb->cap; }
    }
    if (cost > tb->burst) return false;
    uint32_t need = cost * tb->den;
    if (tb->level < need) return false;
    tb->level -= need;
    return true;
}
typedef struct { uint32_t start, count, N, W; } fixwin_t;
static bool fw_try(fixwin_t *f, uint32_t now) {
    uint32_t el = now - f->start;
    if (el >= f->W) { f->start += el - el % f->W; f->count = 0; }
    if (f->count >= f->N) return false;
    f->count++; return true;
}
#define SLOG_MAX 256
typedef struct { uint32_t ts[SLOG_MAX]; uint32_t head, n, N, W; } slog_t;
static bool sl_try(slog_t *s, uint32_t now) {
    if (s->n < s->N) { s->ts[(s->head + s->n) % s->N] = now; s->n++; return true; }
    if (now - s->ts[s->head] < s->W) return false;
    s->ts[s->head] = now; s->head = (s->head + 1) % s->N;
    return true;
}
typedef struct { uint32_t start, prev, cur, N, W; } swc_t;
static bool swc_try(swc_t *c, uint32_t now) {
    uint32_t el = now - c->start;
    if (el >= c->W) {
        uint32_t k = el / c->W;
        c->prev = (k == 1) ? c->cur : 0; c->cur = 0; c->start += k * c->W; el -= k * c->W;
    }
    if (c->prev * (c->W - el) + c->cur * c->W >= c->N * c->W) return false;
    c->cur++; return true;
}

#define NREQ 10000000
static uint32_t *g_t;
static double now_s(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec + ts.tv_nsec * 1e-9; }
static int cmpd(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return (x > y) - (x < y); }

static uint64_t run(int kind) {
    static tbucket_t tb; static fixwin_t fw; static slog_t sl; static swc_t sc;
    uint64_t acc = 0;
    switch (kind) {
    case 0: tb_init(&tb, 100, 1, 10, g_t[0]); for (int i = 0; i < NREQ; i++) acc += tb_try(&tb, g_t[i], 1); break;
    case 1: fw = (fixwin_t){ g_t[0], 0, 100, 1000 }; for (int i = 0; i < NREQ; i++) acc += fw_try(&fw, g_t[i]); break;
    case 2: memset(&sl, 0, sizeof sl); sl.N = 100; sl.W = 1000; for (int i = 0; i < NREQ; i++) acc += sl_try(&sl, g_t[i]); break;
    case 3: sc = (swc_t){ g_t[0], 0, 0, 100, 1000 }; for (int i = 0; i < NREQ; i++) acc += swc_try(&sc, g_t[i]); break;
    }
    return acc;
}

int main(void) {
    g_t = malloc(NREQ * sizeof *g_t);
    if (!g_t) return 1;
    uint32_t x = 2463534242u, t = 0xFFFF0000u;
    for (int i = 0; i < NREQ; ) {                     /* 틱당 0~3 요청 (평균 1.5) → 한도 0.1/틱의 15배 과부하 */
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        int k = (int)(x & 3);
        while (k-- > 0 && i < NREQ) g_t[i++] = t;
        t++;
    }
    static const char *name[4] = { "token bucket (b=100, r=1/10)", "fixed window (N=100, W=1000)", "sliding log  (N=100, W=1000)", "sliding counter (N=100, W=1000)" };
    static const size_t state[4] = { sizeof(tbucket_t), sizeof(fixwin_t), sizeof(uint32_t) * 100 + 16, sizeof(swc_t) };
    printf("%d requests over %u ticks\n", NREQ, t - 0xFFFF0000u);
    for (int k = 0; k < 4; k++) {
        uint64_t acc = run(k);                        /* 워밍업 */
        double ns[7];
        for (int r = 0; r < 7; r++) { double t0 = now_s(); acc = run(k); ns[r] = (now_s() - t0) * 1e9 / NREQ; }
        qsort(ns, 7, sizeof ns[0], cmpd);
        printf("  %-32s state %4zu B  passed %8llu  ns/decision median %.2f (min %.2f, max %.2f)\n",
               name[k], state[k], (unsigned long long)acc, ns[3], ns[0], ns[6]);
    }
    free(g_t);
    return 0;
}
