/* DS-M11 관찰용 — indexed PQ(decrease-key) vs lazy deletion(중복 push 후 꺼낼 때 버리기)
 * 1) Dijkstra: V=1000, E 를 늘려 가며 PQ 연산 수 / 힙 최대 크기 / 비교 횟수 / 시간, + O(V^2) 배열 Dijkstra 시간
 * 2) 재전송 타이머 64개를 ACK 마다 재시작: 힙 최대 크기, 버려지는(stale) pop 수
 * cc -std=c11 -Wall -Wextra -Werror -O2 demo_lazy_vs_indexed.c -o demo && ./demo
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define V     1000u
#define EMAX  64000u
#define CAP   1024u
#define NONE  0xFFFFu
#define INF   UINT32_MAX

static uint32_t rng = 1u;
static uint32_t xorshift32(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static uint64_t g_cmp;

/* ---- indexed min-heap (본문과 같은 구조) ---- */
typedef struct { uint16_t heap[CAP], pos[CAP]; uint32_t key[CAP]; uint32_t n; } ipq_t;
static bool ib(const ipq_t *q, uint32_t a, uint32_t b) { g_cmp++; return q->key[a] != q->key[b] ? q->key[a] < q->key[b] : a < b; }
static void isw(ipq_t *q, uint32_t i, uint32_t j) {
    uint16_t t = q->heap[i]; q->heap[i] = q->heap[j]; q->heap[j] = t;
    q->pos[q->heap[i]] = (uint16_t)i; q->pos[q->heap[j]] = (uint16_t)j;
}
static void iup(ipq_t *q, uint32_t i) { while (i > 0 && ib(q, q->heap[i], q->heap[(i - 1u) / 2u])) { isw(q, i, (i - 1u) / 2u); i = (i - 1u) / 2u; } }
static void idown(ipq_t *q, uint32_t i) {
    for (;;) {
        uint32_t l = 2u * i + 1u, r = l + 1u, m = i;
        if (l < q->n && ib(q, q->heap[l], q->heap[m])) m = l;
        if (r < q->n && ib(q, q->heap[r], q->heap[m])) m = r;
        if (m == i) return;
        isw(q, i, m); i = m;
    }
}
static void iinit(ipq_t *q) { q->n = 0; for (uint32_t i = 0; i < CAP; i++) q->pos[i] = NONE; }
static void iins(ipq_t *q, uint32_t id, uint32_t k) { q->key[id] = k; q->heap[q->n] = (uint16_t)id; q->pos[id] = (uint16_t)q->n; iup(q, q->n++); }
static void idec(ipq_t *q, uint32_t id, uint32_t k) { q->key[id] = k; iup(q, q->pos[id]); }
static void iupd(ipq_t *q, uint32_t id, uint32_t k) { bool d = k < q->key[id]; q->key[id] = k; if (d) iup(q, q->pos[id]); else idown(q, q->pos[id]); }
static uint32_t ipop(ipq_t *q) {
    uint32_t top = q->heap[0];
    q->pos[top] = NONE;
    if (--q->n > 0) { q->heap[0] = q->heap[q->n]; q->pos[q->heap[0]] = 0; idown(q, 0); }
    return top;
}

/* ---- lazy: (key, id) 원소의 평범한 min-heap, 중복 허용 ---- */
typedef struct { uint32_t key; uint16_t id; uint16_t gen; } ent_t;
static ent_t g_lz[EMAX + 1u];
static uint32_t g_ln;
static bool lb(ent_t a, ent_t b) { g_cmp++; return a.key != b.key ? a.key < b.key : a.id < b.id; }
static void lpush(ent_t e) {
    uint32_t i = g_ln++;
    g_lz[i] = e;
    while (i > 0 && lb(g_lz[i], g_lz[(i - 1u) / 2u])) { ent_t t = g_lz[i]; g_lz[i] = g_lz[(i - 1u) / 2u]; g_lz[(i - 1u) / 2u] = t; i = (i - 1u) / 2u; }
}
static ent_t lpop(void) {
    ent_t top = g_lz[0];
    g_lz[0] = g_lz[--g_ln];
    for (uint32_t i = 0;;) {
        uint32_t l = 2u * i + 1u, r = l + 1u, m = i;
        if (l < g_ln && lb(g_lz[l], g_lz[m])) m = l;
        if (r < g_ln && lb(g_lz[r], g_lz[m])) m = r;
        if (m == i) break;
        ent_t t = g_lz[i]; g_lz[i] = g_lz[m]; g_lz[m] = t; i = m;
    }
    return top;
}

/* ---- 그래프 (CSR) ---- */
static uint32_t g_head[V + 1u];
static uint16_t g_to[EMAX], g_w[EMAX];
static uint32_t g_wm[V][V];
static void make_graph(uint32_t ne, uint32_t seed) {
    static uint16_t eu[EMAX], ev[EMAX], ew[EMAX];
    rng = seed;
    memset(g_head, 0, sizeof g_head);
    for (uint32_t u = 0; u < V; u++) for (uint32_t v = 0; v < V; v++) g_wm[u][v] = INF;
    for (uint32_t e = 0; e < ne; e++) {
        eu[e] = (uint16_t)(xorshift32() % V); ev[e] = (uint16_t)(xorshift32() % V); ew[e] = (uint16_t)(1u + xorshift32() % 1000u);
        g_head[eu[e] + 1u]++;
        if (ew[e] < g_wm[eu[e]][ev[e]]) g_wm[eu[e]][ev[e]] = ew[e];
    }
    for (uint32_t u = 0; u < V; u++) g_head[u + 1u] += g_head[u];
    uint32_t fill[V];
    memcpy(fill, g_head, sizeof fill);
    for (uint32_t e = 0; e < ne; e++) { uint32_t s = fill[eu[e]]++; g_to[s] = ev[e]; g_w[s] = ew[e]; }
}

typedef struct { uint32_t pq_ops, maxn, stale; uint64_t cmp; } st_t;
static ipq_t g_q;
static uint32_t g_dist[V];

static st_t dij_indexed(void) {
    st_t s = { 0, 0, 0, 0 };
    g_cmp = 0;
    for (uint32_t v = 0; v < V; v++) g_dist[v] = INF;
    iinit(&g_q); g_dist[0] = 0; iins(&g_q, 0, 0); s.pq_ops++;
    while (g_q.n) {
        uint32_t u = ipop(&g_q), du = g_dist[u];
        for (uint32_t e = g_head[u]; e < g_head[u + 1u]; e++) {
            uint32_t v = g_to[e], nd = du + g_w[e];
            if (nd >= g_dist[v]) continue;
            g_dist[v] = nd;
            if (g_q.pos[v] != NONE) idec(&g_q, v, nd); else iins(&g_q, v, nd);
            s.pq_ops++;
            if (g_q.n > s.maxn) s.maxn = g_q.n;
        }
    }
    s.cmp = g_cmp;
    return s;
}
static st_t dij_lazy(void) {
    st_t s = { 0, 0, 0, 0 };
    g_cmp = 0;
    for (uint32_t v = 0; v < V; v++) g_dist[v] = INF;
    g_ln = 0; g_dist[0] = 0; lpush((ent_t){ 0, 0, 0 }); s.pq_ops++;
    while (g_ln) {
        ent_t t = lpop();
        if (t.key > g_dist[t.id]) { s.stale++; continue; }               /* 낡은 엔트리 — 버린다 */
        for (uint32_t e = g_head[t.id]; e < g_head[t.id + 1u]; e++) {
            uint32_t v = g_to[e], nd = t.key + g_w[e];
            if (nd >= g_dist[v]) continue;
            g_dist[v] = nd;
            lpush((ent_t){ nd, (uint16_t)v, 0 }); s.pq_ops++;
            if (g_ln > s.maxn) s.maxn = g_ln;
        }
    }
    s.cmp = g_cmp;
    return s;
}
static void dij_array(void) {                                            /* O(V^2), 힙 없음 */
    static bool done[V];
    memset(done, 0, sizeof done);
    for (uint32_t v = 0; v < V; v++) g_dist[v] = INF;
    g_dist[0] = 0;
    for (uint32_t it = 0; it < V; it++) {
        uint32_t u = V;
        for (uint32_t v = 0; v < V; v++) if (!done[v] && g_dist[v] != INF && (u == V || g_dist[v] < g_dist[u])) u = v;
        if (u == V) break;
        done[u] = true;
        for (uint32_t v = 0; v < V; v++) if (g_wm[u][v] != INF && g_dist[u] + g_wm[u][v] < g_dist[v]) g_dist[v] = g_dist[u] + g_wm[u][v];
    }
}

static double now_us(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e6 + t.tv_nsec / 1e3; }

int main(void) {
    static const uint32_t es[3] = { 4000u, 16000u, 64000u };
    static uint32_t d_ref[V];
    const int R = 20;
    printf("Dijkstra, V=%u, weights 1..1000, src 0 (times: avg of %d runs, this Mac, -O2)\n", V, R);
    printf("E      | method  | PQ ops | max PQ | stale pops | compares | us/run\n");
    for (int gi = 0; gi < 3; gi++) {
        make_graph(es[gi], 1234u + (uint32_t)gi);
        st_t a = { 0, 0, 0, 0 }, b = { 0, 0, 0, 0 };
        double t0 = now_us();
        for (int r = 0; r < R; r++) a = dij_indexed();
        double t1 = now_us();
        memcpy(d_ref, g_dist, sizeof d_ref);
        for (int r = 0; r < R; r++) b = dij_lazy();
        double t2 = now_us();
        bool same = memcmp(d_ref, g_dist, sizeof d_ref) == 0;
        for (int r = 0; r < R; r++) dij_array();
        double t3 = now_us();
        same = same && memcmp(d_ref, g_dist, sizeof d_ref) == 0;
        printf("%-6u | indexed | %6u | %6u | %10u | %8llu | %6.1f\n", es[gi], a.pq_ops, a.maxn, a.stale, (unsigned long long)a.cmp, (t1 - t0) / R);
        printf("%-6u | lazy    | %6u | %6u | %10u | %8llu | %6.1f\n", es[gi], b.pq_ops, b.maxn, b.stale, (unsigned long long)b.cmp, (t2 - t1) / R);
        printf("%-6u | O(V^2)  |      - |      - |          - |        - | %6.1f   dist all equal: %s\n", es[gi], (t3 - t2) / R, same ? "yes" : "NO");
    }

    /* 2) 재전송 타이머: 연결 64개, 각 연결은 ACK 마다 RTO 타이머를 now+200 으로 재시작 */
    {
        const uint32_t NC = 64, EV = 100000;
        static uint16_t gen[64];
        uint32_t now = 0, maxi = 0, maxl = 0, stale = 0, fired_i = 0, fired_l = 0;
        iinit(&g_q); g_ln = 0;
        rng = 42u;
        for (uint32_t c = 0; c < NC; c++) { iins(&g_q, c, 200); lpush((ent_t){ 200, (uint16_t)c, 0 }); }
        for (uint32_t ev = 0; ev < EV; ev++) {
            now++;
            uint32_t r = xorshift32();
            if (r % 100u < 98u) {                                       /* 98%: ACK 도착 → 재시작 */
                uint32_t c = (r >> 8) % NC;
                iupd(&g_q, c, now + 200u);
                gen[c]++;
                lpush((ent_t){ now + 200u, (uint16_t)c, gen[c] });
            }
            while (g_q.n && g_q.key[g_q.heap[0]] <= now) { uint32_t c = ipop(&g_q); fired_i++; iins(&g_q, c, now + 200u); }
            while (g_ln && g_lz[0].key <= now) {
                ent_t t = lpop();
                if (t.gen != gen[t.id]) { stale++; continue; }         /* 재시작으로 무효가 된 엔트리 */
                fired_l++; gen[t.id]++;
                lpush((ent_t){ now + 200u, t.id, gen[t.id] });
            }
            if (g_q.n > maxi) maxi = g_q.n;
            if (g_ln > maxl) maxl = g_ln;
        }
        printf("\nRTO timers: %u connections, %u events (98%% restarts)\n", NC, EV);
        printf("indexed: max heap %u entries, fired %u\n", maxi, fired_i);
        printf("lazy   : max heap %u entries, fired %u, stale pops %u\n", maxl, fired_l, stale);
    }
    return 0;
}
