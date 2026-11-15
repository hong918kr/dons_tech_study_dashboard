/* ALGO-M14 demo: 최적화 유무에 따른 find 깊이·포인터 이동(hop) 수·시간
 *
 * 6 변형 = 합치기 {naive: parent[ra] = rb, by size} x find {압축 없음, 경로 반감, 완전 압축(2-pass)}
 * 3 작업 (n 원소):
 *   chain    union(i, i+1), i = 0..n-2        — naive 링크면 길이 n 사슬
 *   random   n 번 union(무작위 a, 무작위 b)
 *   doubling s = 1,2,4..: union(i, i+s) (i 는 2s 배수) — by size 면 이항 트리, 깊이 = log2 n
 * 그 뒤 무작위 노드 n 개에 find. 측정: union 직후 최대 깊이, op 당 평균 hop, 단일 find 최대 hop,
 * ns/op (워밍업 1 + 3회 중앙값). hop = find 루프 반복 수 (반감은 한 반복에 두 칸, 완전 압축은 1-pass 반복 수).
 * naive 링크의 chain·random 은 n = 2^20 에서 생략 (2^16 에서 이미 op 당 수천 hop → 몇 분). 이 맥(Apple Silicon M 시리즈, -O2) 값 — 절대값보다 비율을 본다.
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_depth.c -o demo_depth && ./demo_depth
 */
#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define NMAX (1 << 20)
static int32_t parent[NMAX], sz[NMAX], dep[NMAX], stk[NMAX];
static uint64_t hops, max_hops;

enum { LINK_NAIVE, LINK_SIZE };
enum { FIND_PLAIN, FIND_HALVE, FIND_FULL };

static inline int32_t find(int32_t x, int mode) {
    uint64_t h = 0;
    int32_t r;
    if (mode == FIND_PLAIN) {
        while (parent[x] != x) { x = parent[x]; h++; }
        r = x;
    } else if (mode == FIND_HALVE) {
        while (parent[x] != x) { parent[x] = parent[parent[x]]; x = parent[x]; h++; }
        r = x;
    } else {
        r = x;
        while (parent[r] != r) { r = parent[r]; h++; }
        while (parent[x] != r) { int32_t nx = parent[x]; parent[x] = r; x = nx; }
    }
    hops += h;
    if (h > max_hops) max_hops = h;
    return r;
}
static inline void unite(int32_t a, int32_t b, int link, int fm) {
    int32_t ra = find(a, fm), rb = find(b, fm);
    if (ra == rb) return;
    if (link == LINK_NAIVE) { parent[ra] = rb; return; }
    if (sz[ra] < sz[rb]) { int32_t t = ra; ra = rb; rb = t; }
    parent[rb] = ra; sz[ra] += sz[rb];
}

/* 부모 포인터만 보고(변경 없이) 최대 깊이: 메모 + 명시적 스택, O(n) */
static int32_t max_depth(int n) {
    for (int i = 0; i < n; i++) dep[i] = -1;
    int32_t md = 0;
    for (int x = 0; x < n; x++) {
        int sp = 0, y = x;
        while (dep[y] < 0 && parent[y] != y) { stk[sp++] = y; y = parent[y]; }
        if (dep[y] < 0) dep[y] = 0;                  /* 루트 */
        while (sp) { int z = stk[--sp]; dep[z] = dep[parent[z]] + 1; }
        if (dep[x] > md) md = dep[x];
    }
    return md;
}

static uint32_t rs;
static uint32_t xs(void) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }
static double now_ns(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e9 + t.tv_nsec; }

enum { W_CHAIN, W_RANDOM, W_DOUBLING };
static const char *wname[] = { "chain", "random", "doubling" };
static const char *vname[] = { "naive", "halving only", "full compr only", "size only", "size+halving", "size+full compr" };
static const int vlink[] = { LINK_NAIVE, LINK_NAIVE, LINK_NAIVE, LINK_SIZE, LINK_SIZE, LINK_SIZE };
static const int vfind[] = { FIND_PLAIN, FIND_HALVE, FIND_FULL, FIND_PLAIN, FIND_HALVE, FIND_FULL };

static volatile int64_t sink;
/* 한 번 실행: 반환 = 경과 ns, *ops = union+find 횟수, *depth = union 직후 최대 깊이 */
static double run(int n, int w, int v, uint64_t *ops, int32_t *depth) {
    for (int i = 0; i < n; i++) { parent[i] = i; sz[i] = 1; }
    hops = 0; max_hops = 0; rs = 0x2545F491u;
    uint64_t k = 0;
    double t0 = now_ns();
    if (w == W_CHAIN)       for (int i = 0; i + 1 < n; i++, k++) unite(i, i + 1, vlink[v], vfind[v]);
    else if (w == W_RANDOM) for (int i = 0; i < n; i++, k++) unite((int32_t)(xs() % (uint32_t)n), (int32_t)(xs() % (uint32_t)n), vlink[v], vfind[v]);
    else for (int s = 1; s < n; s *= 2) for (int i = 0; i + s < n; i += 2 * s, k++) unite(i, i + s, vlink[v], vfind[v]);
    double t1 = now_ns();
    *depth = max_depth(n);                            /* 시간 측정에서 제외 */
    double t2 = now_ns();
    int64_t acc = 0;
    for (int i = 0; i < n; i++, k++) acc += find((int32_t)(xs() % (uint32_t)n), vfind[v]);
    double t3 = now_ns();
    sink = acc;
    *ops = k;
    return (t1 - t0) + (t3 - t2);
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    const int sizes[] = { 1 << 16, 1 << 20 };
    for (int si = 0; si < 2; si++) {
        int n = sizes[si];
        printf("n = %d (2^%d), then %d random finds\n", n, si ? 20 : 16, n);
        printf("%-9s %-16s %9s %12s %12s %9s\n", "workload", "variant", "depth", "hops/op", "max hops", "ns/op");
        for (int w = 0; w < 3; w++) {
            for (int v = 0; v < 6; v++) {
                if (n > (1 << 16) && w != W_DOUBLING && v == 0) {   /* 2^16 에서 이미 op 당 수천 hop: 2^20 은 생략 */
                    printf("%-9s %-16s %9s %12s %12s %9s\n", wname[w], vname[v], "-", "-", "-", "skipped");
                    continue;
                }
                uint64_t ops = 0; int32_t d = 0;
                double t[3];
                run(n, w, v, &ops, &d);                   /* 워밍업 */
                for (int r = 0; r < 3; r++) t[r] = run(n, w, v, &ops, &d);
                for (int a = 0; a < 3; a++) for (int b = a + 1; b < 3; b++) if (t[b] < t[a]) { double x = t[a]; t[a] = t[b]; t[b] = x; }
                printf("%-9s %-16s %9d %12.2f %12llu %9.1f\n", wname[w], vname[v], d,
                       (double)hops / (double)ops, (unsigned long long)max_hops, t[1] / (double)ops);
            }
        }
        printf("\n");
    }
    return 0;
}
