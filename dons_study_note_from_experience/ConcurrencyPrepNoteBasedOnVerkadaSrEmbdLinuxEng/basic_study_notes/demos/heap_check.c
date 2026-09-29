/* heap_check.c — 노트 C7의 힙/top-k/lazy-update 힙을 brute force와 맞춰본다.
 *   cc -std=c11 -O2 -Wall -Wextra -o heap_check heap_check.c && ./heap_check
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t rs = 0x9E3779B97F4A7C15ull;
static uint32_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (uint32_t)(rs >> 11); }

/* ================= 1. 정수 min-heap: push/pop 이 정렬을 만드는가 ================= */
#define HN 10000
static int h[HN];
static int hn;

static void up(int i)
{
    while (i > 0) {
        int p = (i - 1) / 2;
        if (h[p] <= h[i]) return;
        int t = h[p]; h[p] = h[i]; h[i] = t;
        i = p;
    }
}
static void down(int i)
{
    for (;;) {
        int l = 2 * i + 1, r = l + 1, m = i;
        if (l < hn && h[l] < h[m]) m = l;
        if (r < hn && h[r] < h[m]) m = r;
        if (m == i) return;
        int t = h[m]; h[m] = h[i]; h[i] = t;
        i = m;
    }
}
static void push(int v) { h[hn] = v; up(hn); hn++; }
static int pop(void) { int top = h[0]; h[0] = h[--hn]; down(0); return top; }

static int cmp_int(const void *a, const void *b)
{
    int x = *(const int *)a, y = *(const int *)b;
    return (x > y) - (x < y);
}

static int test_heapsort(void)
{
    static int ref[HN];
    hn = 0;
    for (int i = 0; i < HN; i++) { int v = (int)(rnd() % 100000u); ref[i] = v; push(v); }
    qsort(ref, HN, sizeof(int), cmp_int);
    int bad = 0;
    for (int i = 0; i < HN; i++) {
        int got = pop();
        if (got != ref[i]) { if (!bad) printf("  MISMATCH at %d: %d != %d\n", i, got, ref[i]); bad++; }
    }
    printf("test1 heap push x%d then pop x%d vs qsort : mismatches=%d  (heap empty=%s)\n",
           HN, HN, bad, hn == 0 ? "yes" : "NO");
    return bad == 0 && hn == 0;
}

/* ================= 2. top-k: 크기 k min-heap vs 전부 정렬 ================= */
typedef struct { float score; uint32_t key; } ent_t;

static long g_cmp;                     /* weaker() 호출 횟수 */

static bool weaker(const ent_t *a, const ent_t *b)
{
    g_cmp++;
    if (a->score != b->score) return a->score < b->score;
    return a->key > b->key;            /* 동점이면 key 작은 쪽이 이긴다 */
}
static void eswap(ent_t *a, ent_t *b) { ent_t t = *a; *a = *b; *b = t; }
static void eup(ent_t *a, size_t i)
{
    while (i > 0) { size_t p = (i - 1) / 2; if (!weaker(&a[i], &a[p])) return; eswap(&a[i], &a[p]); i = p; }
}
static void edown(ent_t *a, size_t m, size_t i)
{
    for (;;) {
        size_t l = 2 * i + 1, r = l + 1, w = i;
        if (l < m && weaker(&a[l], &a[w])) w = l;
        if (r < m && weaker(&a[r], &a[w])) w = r;
        if (w == i) return;
        eswap(&a[i], &a[w]); i = w;
    }
}
/* 09_wifi_scan_snapshot/ap_table_solution.c 의 wifi_top_k 와 같은 골격 */
static size_t top_k(const ent_t *in, size_t n, ent_t *out, size_t k)
{
    size_t m = 0;
    for (size_t i = 0; i < n; i++) {
        if (m < k) { out[m] = in[i]; eup(out, m); m++; }
        else if (weaker(&out[0], &in[i])) { out[0] = in[i]; edown(out, m, 0); }
    }
    for (size_t i = 1; i < m; i++) {          /* 힙은 부분 정렬 -> 강한 순으로 정렬 */
        ent_t v = out[i]; size_t j = i;
        while (j > 0 && weaker(&out[j - 1], &v)) { out[j] = out[j - 1]; j--; }
        out[j] = v;
    }
    return m;
}
static int cmp_strong(const void *pa, const void *pb)
{
    const ent_t *a = pa, *b = pb;
    g_cmp++;
    if (a->score != b->score) return a->score < b->score ? 1 : -1;   /* 내림차순 */
    return (a->key > b->key) - (a->key < b->key);                    /* 동점: key 오름 */
}

static int test_topk(void)
{
    enum { N = 10000, K = 8 };
    static ent_t in[N], mine[K], ref[N];
    int bad = 0;

    for (int i = 0; i < N; i++) {
        /* 동점을 일부러 많이 만든다: 점수를 0.5 dB 단위 40단계로만 준다 */
        in[i].score = -30.0f - (float)(rnd() % 40u) * 0.5f;
        in[i].key = (uint32_t)i * 7919u;
    }
    g_cmp = 0;
    size_t m = top_k(in, N, mine, K);
    long cmp_heap = g_cmp;

    memcpy(ref, in, sizeof in);
    g_cmp = 0;
    qsort(ref, N, sizeof(ent_t), cmp_strong);
    long cmp_sort = g_cmp;

    for (size_t i = 0; i < m; i++)
        if (mine[i].score != ref[i].score || mine[i].key != ref[i].key) {
            printf("  MISMATCH rank %zu: heap(%.1f,%u) sort(%.1f,%u)\n",
                   i, mine[i].score, mine[i].key, ref[i].score, ref[i].key);
            bad++;
        }
    printf("test2 top-%d of N=%d : mismatches=%d  compares heap=%ld  compares full sort=%ld  (%.1fx)\n",
           K, N, bad, cmp_heap, cmp_sort, (double)cmp_sort / (double)cmp_heap);

    /* k > N, k == 0, N == 0 경계 */
    size_t m2 = top_k(in, 3, mine, K);
    size_t m3 = top_k(in, 0, mine, K);
    printf("test2 edges      : n=3,k=8 -> %zu   n=0,k=8 -> %zu\n", m2, m3);
    return bad == 0 && m == K && m2 == 3 && m3 == 0;
}

/* ================= 3. lazy update 힙 (10_heartbeat_watchdog) ================= */
#define W 32
typedef struct { uint64_t key; uint32_t id; } node_t;
static node_t hp[W];
static int hp_n;
static int pos[W];
static uint64_t last_seen[W], deadline[W];
static bool in_use[W];
static long g_sifts;

static void hswap(int a, int b)
{
    node_t t = hp[a]; hp[a] = hp[b]; hp[b] = t;
    pos[hp[a].id] = a; pos[hp[b].id] = b;
}
static void hup(int i) { while (i > 0) { int p = (i - 1) / 2; if (hp[p].key <= hp[i].key) break; hswap(p, i); i = p; } }
static void hdown(int i)
{
    for (;;) {
        int l = 2 * i + 1, r = l + 1, m = i;
        if (l < hp_n && hp[l].key < hp[m].key) m = l;
        if (r < hp_n && hp[r].key < hp[m].key) m = r;
        if (m == i) return;
        hswap(m, i); i = m;
    }
}
static void hins(uint32_t id, uint64_t key) { int i = hp_n++; hp[i].id = id; hp[i].key = key; pos[id] = i; hup(i); }
static void hrem(uint32_t id)
{
    int i = pos[id]; if (i < 0) return;
    pos[id] = -1;
    int last = --hp_n;
    if (i != last) { hp[i] = hp[last]; pos[hp[i].id] = i; hdown(i); hup(i); }
}
static uint64_t truth(uint32_t id) { return last_seen[id] + deadline[id]; }

/* 루트만 고쳐서 정확한 전역 최솟값을 얻는다 */
static bool refresh_root(uint64_t *out_key, uint32_t *out_id)
{
    while (hp_n > 0) {
        uint32_t id = hp[0].id;
        uint64_t t = truth(id);
        if (t <= hp[0].key) { *out_key = hp[0].key; *out_id = id; return true; }
        hp[0].key = t; hdown(0); g_sifts++;
    }
    return false;
}

static int test_lazy_heap(void)
{
    memset(pos, -1, sizeof pos);
    memset(in_use, 0, sizeof in_use);
    hp_n = 0; g_sifts = 0;
    uint64_t now = 1000000ull;
    long rounds = 0, checks = 0, bad = 0, beats = 0;

    for (int i = 0; i < 8; i++) {                  /* 처음 8명 등록 */
        in_use[i] = true; deadline[i] = 100000ull + (uint64_t)(rnd() % 400000u);
        last_seen[i] = now; hins((uint32_t)i, now + deadline[i]);
    }

    for (int step = 0; step < 10000; step++) {
        now += 1 + rnd() % 20000u;
        uint32_t r = rnd() % 100u;
        if (r < 70) {                              /* heartbeat: 힙은 건드리지 않는다 */
            uint32_t id = rnd() % W;
            if (in_use[id]) { last_seen[id] = now; beats++; }
        } else if (r < 85) {                       /* register */
            for (uint32_t id = 0; id < W; id++)
                if (!in_use[id]) {
                    in_use[id] = true; deadline[id] = 100000ull + (uint64_t)(rnd() % 400000u);
                    last_seen[id] = now; hins(id, now + deadline[id]);
                    break;
                }
        } else {                                   /* unregister */
            uint32_t id = rnd() % W;
            if (in_use[id]) { hrem(id); in_use[id] = false; }
        }

        uint64_t k = 0; uint32_t id = 0;
        bool ok = refresh_root(&k, &id);

        uint64_t bf = UINT64_MAX; bool any = false;      /* brute force */
        for (uint32_t i = 0; i < W; i++)
            if (in_use[i]) { any = true; if (truth(i) < bf) bf = truth(i); }

        checks++;
        if (ok != any || (ok && k != bf)) {
            if (!bad) printf("  MISMATCH step=%d heap=%llu bf=%llu\n", step,
                             (unsigned long long)k, (unsigned long long)bf);
            bad++;
        }
        /* 불변식: 모든 key <= 그 워커의 진짜 deadline */
        for (int i = 0; i < hp_n; i++)
            if (hp[i].key > truth(hp[i].id)) { bad++; break; }
        /* 힙 성질 */
        for (int i = 1; i < hp_n; i++)
            if (hp[(i - 1) / 2].key > hp[i].key) { bad++; break; }
        rounds++;
    }
    printf("test3 lazy heap  : rounds=%ld heartbeats=%ld checks=%ld mismatches=%ld  "
           "root repairs=%ld (%.2f per round)\n",
           rounds, beats, checks, bad, g_sifts, (double)g_sifts / (double)rounds);
    return bad == 0;
}

int main(void)
{
    int ok = 1;
    ok &= test_heapsort();
    ok &= test_topk();
    ok &= test_lazy_heap();
    printf("%s\n", ok ? "ALL OK" : "FAILED");
    return ok ? 0 : 1;
}
