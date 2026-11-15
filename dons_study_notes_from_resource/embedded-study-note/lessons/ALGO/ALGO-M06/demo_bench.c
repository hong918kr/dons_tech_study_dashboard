/* ALGO-M06 demo_bench — 다익스트라 세 가지 구현 비교
 *   indexed : indexed min-PQ + decrease-key (PQ <= V)
 *   lazy    : (d, v) 이진 힙, 개선마다 push, pop 때 stale 버림 (PQ <= E + 1)
 *   array   : PQ 없음. 매번 미확정 정점 중 최소를 선형 탐색 — O(V^2 + E)
 * 그래프: 랜덤 희소/중간/조밀(V=1000, E=4V/16V/64V, w 1..1000), 완전 그래프(V=300),
 *         적대적 그래프(V=700, 모든 간선 완화가 성공하도록 만든 DAG — lazy 힙이 E 에 가깝게 커진다).
 * 시간: 이 맥 (Apple Silicon, -O2), 워밍업 2회 + 표본 11개(각 ≈4 ms 분량 반복의 평균)의 중앙값. 비율을 본다.
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_bench.c -o demo_bench && ./demo_bench
 */
#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_V 1000
#define MAX_E 250000
#define INF   INT32_MAX
#define NONE  (-1)

static int     nv, ne;
static int32_t head[MAX_V], nxt[MAX_E], to[MAX_E], wt[MAX_E];
static int32_t dist[MAX_V];
static int32_t prev_[MAX_V];

static void g_init(int n) { nv = n; ne = 0; for (int i = 0; i < n; i++) head[i] = NONE; }
static void g_add(int u, int v, int32_t w) { to[ne] = v; wt[ne] = w; nxt[ne] = head[u]; head[u] = ne; ne++; }

typedef struct { unsigned long pushes, pops, dec, stale, maxq; } stats_t;

/* ---- indexed PQ keyed by dist[] ---- */
static int32_t hp[MAX_V], pos[MAX_V], qn;
static void sw(int i, int j) { int32_t t = hp[i]; hp[i] = hp[j]; hp[j] = t; pos[hp[i]] = i; pos[hp[j]] = j; }
static void up(int i) { while (i > 0) { int p = (i - 1) / 2; if (dist[hp[p]] <= dist[hp[i]]) break; sw(i, p); i = p; } }
static void down(int i) {
    for (;;) {
        int l = 2 * i + 1, r = l + 1, m = i;
        if (l < qn && dist[hp[l]] < dist[hp[m]]) m = l;
        if (r < qn && dist[hp[r]] < dist[hp[m]]) m = r;
        if (m == i) return;
        sw(i, m); i = m;
    }
}
static void run_indexed(int src, stats_t *s) {
    static uint8_t done[MAX_V];
    memset(s, 0, sizeof *s);
    for (int v = 0; v < nv; v++) { dist[v] = INF; prev_[v] = NONE; pos[v] = NONE; done[v] = 0; }
    qn = 0; dist[src] = 0; hp[qn] = src; pos[src] = qn++; s->pushes = 1; s->maxq = 1;
    while (qn) {
        int u = hp[0]; qn--;
        if (qn) { hp[0] = hp[qn]; pos[hp[0]] = 0; down(0); }
        pos[u] = NONE; done[u] = 1; s->pops++;
        for (int e = head[u]; e != NONE; e = nxt[e]) {
            int v = to[e];
            if (done[v]) continue;
            int32_t nd = dist[u] + wt[e];
            if (nd >= dist[v]) continue;
            dist[v] = nd; prev_[v] = u;
            if (pos[v] == NONE) { hp[qn] = v; pos[v] = qn++; up(qn - 1); s->pushes++; }
            else { up(pos[v]); s->dec++; }
            if ((unsigned long)qn > s->maxq) s->maxq = (unsigned long)qn;
        }
    }
}

/* ---- lazy (d, v) heap ---- */
typedef struct { int32_t d, v; } ent_t;
static ent_t lh[MAX_E + 1];
static int ln;
static void lpush(int32_t d, int32_t v) {
    int i = ln++;
    while (i > 0 && lh[(i - 1) / 2].d > d) { lh[i] = lh[(i - 1) / 2]; i = (i - 1) / 2; }
    lh[i].d = d; lh[i].v = v;
}
static ent_t lpop(void) {
    ent_t top = lh[0], last = lh[--ln];
    int i = 0;
    for (;;) {
        int c = 2 * i + 1;
        if (c >= ln) break;
        if (c + 1 < ln && lh[c + 1].d < lh[c].d) c++;
        if (lh[c].d >= last.d) break;
        lh[i] = lh[c]; i = c;
    }
    if (ln) lh[i] = last;
    return top;
}
static void run_lazy(int src, stats_t *s) {
    memset(s, 0, sizeof *s);
    for (int v = 0; v < nv; v++) { dist[v] = INF; prev_[v] = NONE; }
    ln = 0; dist[src] = 0; lpush(0, src); s->pushes = 1; s->maxq = 1;
    while (ln) {
        ent_t t = lpop(); s->pops++;
        if (t.d > dist[t.v]) { s->stale++; continue; }
        for (int e = head[t.v]; e != NONE; e = nxt[e]) {
            int v = to[e];
            int32_t nd = t.d + wt[e];
            if (nd >= dist[v]) continue;
            dist[v] = nd; prev_[v] = t.v;
            lpush(nd, v); s->pushes++;
            if ((unsigned long)ln > s->maxq) s->maxq = (unsigned long)ln;
        }
    }
}

/* ---- array O(V^2): 확정 안 된 정점 중 최소를 선형 탐색 ---- */
static void run_array(int src, stats_t *s) {
    static uint8_t done[MAX_V];
    memset(s, 0, sizeof *s);
    for (int v = 0; v < nv; v++) { dist[v] = INF; prev_[v] = NONE; done[v] = 0; }
    dist[src] = 0;
    for (;;) {
        int u = NONE;
        for (int v = 0; v < nv; v++) if (!done[v] && dist[v] != INF && (u == NONE || dist[v] < dist[u])) u = v;
        if (u == NONE) break;
        done[u] = 1; s->pops++;
        for (int e = head[u]; e != NONE; e = nxt[e]) {
            int v = to[e];
            if (done[v]) continue;
            int32_t nd = dist[u] + wt[e];
            if (nd < dist[v]) { dist[v] = nd; prev_[v] = u; }
        }
    }
}

static uint32_t rng = 0x12345678u;
static uint32_t xs(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

static double now_us(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e6 + ts.tv_nsec / 1e3;
}
static int cmpd(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return (x > y) - (x < y); }

static uint64_t dist_sum(void) { uint64_t s = 0; for (int v = 0; v < nv; v++) s = s * 31 + (uint32_t)dist[v]; return s; }

static void bench(const char *name) {
    void (*fn[3])(int, stats_t *) = { run_indexed, run_lazy, run_array };
    const char *mn[3] = { "indexed", "lazy", "array" };
    uint64_t chk[3];
    printf("%s  (V=%d, E=%d)\n", name, nv, ne);
    for (int m = 0; m < 3; m++) {
        stats_t s;
        double t[11];
        double w0 = now_us(); fn[m](0, &s); fn[m](0, &s);  /* warm-up 2회 */
        int reps = (int)(4000.0 / ((now_us() - w0) / 2 + 1)); /* 한 표본 ≈ 4 ms 가 되게 반복 */
        if (reps < 1) reps = 1;
        for (int r = 0; r < 11; r++) {
            double t0 = now_us();
            for (int k = 0; k < reps; k++) fn[m](0, &s);
            t[r] = (now_us() - t0) / reps;
        }
        qsort(t, 11, sizeof t[0], cmpd);
        chk[m] = dist_sum();
        if (m == 0)      printf("  %-7s %9.1f us  pushes=%7lu pops=%7lu decrease=%7lu max_pq=%7lu\n", mn[m], t[5], s.pushes, s.pops, s.dec, s.maxq);
        else if (m == 1) printf("  %-7s %9.1f us  pushes=%7lu pops=%7lu stale   =%7lu max_pq=%7lu\n", mn[m], t[5], s.pushes, s.pops, s.stale, s.maxq);
        else             printf("  %-7s %9.1f us  (no PQ: %lu linear scans of V)\n", mn[m], t[5], s.pops);
    }
    printf("  dist identical across methods: %s\n", (chk[0] == chk[1] && chk[1] == chk[2]) ? "yes" : "NO");
}

int main(void) {
    const int mult[3] = { 4, 16, 64 };
    for (int k = 0; k < 3; k++) {
        g_init(1000);
        for (int i = 1; i < nv; i++) g_add((int)(xs() % (uint32_t)i), i, 1 + (int32_t)(xs() % 1000));  /* 0 에서 전부 도달 */
        while (ne < mult[k] * nv) g_add((int)(xs() % 1000u), (int)(xs() % 1000u), 1 + (int32_t)(xs() % 1000));
        char name[64]; snprintf(name, sizeof name, "random E=%dV", mult[k]);
        bench(name);
    }
    g_init(300);
    for (int u = 0; u < nv; u++) for (int v = 0; v < nv; v++) if (u != v) g_add(u, v, 1 + (int32_t)(xs() % 1000));
    bench("complete graph");
    /* 적대적: i→i+1 가중치 1, i→j (j > i+1) 가중치 3n - 2i. 정점 i 가 dist i 로 확정될 때마다
     * 남은 모든 j 가 1 씩 개선된다 → 모든 간선 완화가 성공 (decrease = E - (V-1)) */
    g_init(700);
    for (int i = 0; i < nv; i++) {
        for (int j = nv - 1; j > i + 1; j--) g_add(i, j, 3 * nv - 2 * i);
        if (i + 1 < nv) g_add(i, i + 1, 1);
    }
    bench("adversarial DAG");
    return 0;
}
