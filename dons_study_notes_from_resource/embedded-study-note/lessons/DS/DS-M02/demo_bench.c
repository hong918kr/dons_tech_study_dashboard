/* DS-M02 데모: 풀 vs malloc 평균 시간 (같은 랜덤 할당/해제 패턴)
 * 빌드: cc -std=c11 -O2 demo_bench.c -o demo_bench && ./demo_bench
 * 절대 ns 는 이 맥(Apple Silicon) 값일 뿐 — 비율과 "풀은 패턴과 무관하게 일정" 만 볼 것.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define BS     48u
#define NBLK   1024u
#define OPS    4000000u
static _Alignas(max_align_t) uint8_t g_mem[NBLK * BS];
static void *g_head;
static void *g_hold[NBLK];

static void pool_init(void) {
    g_head = NULL;
    for (unsigned i = NBLK; i-- > 0;) { void *b = g_mem + i * BS; memcpy(b, &g_head, sizeof g_head); g_head = b; }
}
static inline void *pool_alloc(void) {
    void *b = g_head;
    if (b) memcpy(&g_head, b, sizeof g_head);
    return b;
}
static inline void pool_free(void *b) { memcpy(b, &g_head, sizeof g_head); g_head = b; }

static double now_ns(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec * 1e9 + (double)t.tv_nsec;
}

static uint32_t rng;
static uint32_t xs(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

/* mode: 1 = 풀, 0 = malloc, 2 = 하네스만 (할당기 없음: 블록 n 을 그대로 씀) — 루프·PRNG·g_hold 비용 측정용.
 * alloc_pct: 할당 확률(%) — 50 이면 보유 개수가 랜덤워크, 60 이면 거의 가득 찬 상태에서 진동 */
static double run(int mode, unsigned alloc_pct, uint32_t *sink) {
    uint32_t n = 0, s = 0;
    rng = 12345u;
    if (mode == 1) pool_init();
    double t0 = now_ns();
    for (uint32_t op = 0; op < OPS; op++) {
        uint32_t r = xs();
        if ((r % 100u) < alloc_pct && n < NBLK) {
            uint8_t *b = mode == 1 ? pool_alloc() : mode == 2 ? g_mem + n * BS : malloc(BS);
            if (!b) break;
            b[0] = (uint8_t)r;                       /* 한 바이트는 실제로 쓴다 */
            g_hold[n++] = b;
        } else if (n > 0) {
            uint32_t k = (r >> 8) % n;
            uint8_t *b = g_hold[k];
            s += b[0];
            if (mode == 1) pool_free(b); else if (mode == 0) free(b);
            g_hold[k] = g_hold[--n];
        }
    }
    double dt = now_ns() - t0;
    while (n > 0) { n--; if (mode == 1) pool_free(g_hold[n]); else if (mode == 0) free(g_hold[n]); }
    *sink += s;
    return dt / OPS;
}

int main(void) {
    uint32_t sink = 0;
    printf("block %u B, %u ops (alloc or free), Apple Silicon -O2\n", BS, OPS);
    printf("pattern           | harness | pool  | malloc | pool-h | malloc-h | (ns/op)\n");
    static const unsigned pct[] = { 50, 60 };
    static const char *name[] = { "random 50/50", "random 60/40 full" };
    for (int i = 0; i < 2; i++) {
        double h = run(2, pct[i], &sink), p = run(1, pct[i], &sink), m = run(0, pct[i], &sink);
        printf("%-17s | %7.2f | %5.2f | %6.2f | %6.2f | %8.2f |\n", name[i], h, p, m, p - h, m - h);
    }
    printf("(sink %u)\n", sink);
    return 0;
}
