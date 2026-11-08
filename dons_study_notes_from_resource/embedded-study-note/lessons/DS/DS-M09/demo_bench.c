/* DS-M09 demo_bench — 최고 우선순위 탐색 3가지 비교
 *   bits   : 비트 하나씩 1023 → 0 (단순 선형 탐색)
 *   words  : 워드 하나씩 31 → 0, 0 이 아닌 첫 워드에 CLZ (DS-B08 방식, 요약 없음)
 *   2-level: top 요약에 CLZ → leaf 에 CLZ (이 레슨)
 * 읽은 비트/워드 수(결정적)와 질의당 시간(이 Mac, -O2)을 출력한다.
 * cc -std=c11 -Wall -Wextra -Werror -O2 demo_bench.c -o demo_bench && ./demo_bench
 */
#define _POSIX_C_SOURCE 199309L
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

typedef struct { uint32_t top; uint32_t leaf[32]; } hbm_t;

static uint32_t g_work;                  /* 마지막 질의가 읽은 비트/워드 수 */

__attribute__((noinline)) static int find_bits(const hbm_t *h) {
    for (uint32_t p = 1024; p-- > 0;)
        if ((h->leaf[p >> 5] >> (p & 31u)) & 1u) { g_work = 1024u - p; return (int)p; }
    g_work = 1024; return -1;
}
__attribute__((noinline)) static int find_words(const hbm_t *h) {
    for (uint32_t g = 32; g-- > 0;)
        if (h->leaf[g]) { g_work = 32u - g; return (int)(g * 32u + 31u - (uint32_t)__builtin_clz(h->leaf[g])); }
    g_work = 32; return -1;
}
__attribute__((noinline)) static int find_2level(const hbm_t *h) {
    if (h->top == 0) { g_work = 1; return -1; }
    uint32_t g = 31u - (uint32_t)__builtin_clz(h->top);
    g_work = 2;
    return (int)(g * 32u + 31u - (uint32_t)__builtin_clz(h->leaf[g]));
}

static void set(hbm_t *h, uint32_t p) { h->leaf[p >> 5] |= 1u << (p & 31u); h->top |= 1u << (p >> 5); }

static uint32_t rng_state = 7u;
static uint32_t xorshift32(void) {
    uint32_t x = rng_state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return rng_state = x;
}

#define NMAP 256
static hbm_t g_maps[NMAP];

static void build(int scen) {
    memset(g_maps, 0, sizeof g_maps);
    for (int i = 0; i < NMAP; i++) {
        hbm_t *h = &g_maps[i];
        switch (scen) {
        case 0: set(h, xorshift32() % 1024u); break;                       /* 비트 1개, 위치 균등 */
        case 1: set(h, xorshift32() % 32u); break;                         /* 비트 1개, 0..31 (idle 만 ready) */
        case 2: for (uint32_t p = 0; p < 1024; p++) if (xorshift32() & 1u) set(h, p); break;  /* 밀도 50% */
        default: set(h, 0); for (int k = 0; k < 3; k++) set(h, xorshift32() % 128u); break; /* idle + 낮은 대역 3개 */
        }
    }
}

typedef int (*find_fn)(const hbm_t *);

static double now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}

/* 질의당 ns (3번 재서 최솟값), 평균/최대 작업량 */
static void run(find_fn f, double *ns, double *avg_work, uint32_t *max_work, int *check) {
    const int rounds = 2000;
    uint64_t wsum = 0; uint32_t wmax = 0; int sum = 0;
    for (int i = 0; i < NMAP; i++) { sum += f(&g_maps[i]); wsum += g_work; if (g_work > wmax) wmax = g_work; }
    *avg_work = (double)wsum / NMAP; *max_work = wmax; *check = sum;
    double best = 1e30;
    for (int rep = 0; rep < 3; rep++) {
        volatile int sink = 0;
        double t0 = now_ns();
        for (int r = 0; r < rounds; r++)
            for (int i = 0; i < NMAP; i++) sink += f(&g_maps[i]);
        double t = (now_ns() - t0) / ((double)rounds * NMAP);
        if (t < best) best = t;
        (void)sink;
    }
    *ns = best;
}

int main(void) {
    static const char *name[] = { "1 bit, uniform p", "1 bit, p<32 (idle)", "dense 50%", "idle + 3 in 0..127" };
    static const find_fn fn[3] = { find_bits, find_words, find_2level };
    printf("%-20s | %-26s | %-26s | %-22s\n", "scenario", "bits: ns  avg/max bits", "words: ns  avg/max words", "2-level: ns  words");
    for (int s = 0; s < 4; s++) {
        build(s);
        double ns[3], avg[3]; uint32_t mx[3]; int chk[3];
        for (int m = 0; m < 3; m++) run(fn[m], &ns[m], &avg[m], &mx[m], &chk[m]);
        printf("%-20s | %6.1f  %7.1f / %4u     | %6.2f  %6.1f / %2u        | %6.2f  %u%s\n", name[s],
               ns[0], avg[0], mx[0], ns[1], avg[1], mx[1], ns[2], mx[2],
               (chk[0] == chk[1] && chk[1] == chk[2]) ? "" : "  RESULT MISMATCH");
    }
    return 0;
}
