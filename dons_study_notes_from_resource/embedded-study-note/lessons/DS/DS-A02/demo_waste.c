/* DS-A02 demo_waste.c — 연속 할당의 대가: 같은 부하에서 bip buffer 와 "둘로 쪼개는 링" 비교 (결정적)
 *
 * cc -std=c11 -O2 demo_waste.c -o dw && ./dw
 *
 * 부하: 메시지 크기는 [1, M] 균등분포. 예약 전에 가장 오래된 메시지부터 소비해 live <= D 로 맞춘다.
 *       링: 전체 여유(N - live)만 있으면 성공 (경계에서 조각 2개로 쪼개도 된다).
 *       bip: 연속 영역이 있어야 성공.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

static uint32_t g_rng;
static uint32_t rnd(void) { uint32_t x = g_rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; g_rng = x; return x; }

typedef struct { uint32_t r, w, wm, cap; bool res_wrap; uint32_t res_start; } bip_t;
static uint32_t bip_live(const bip_t *b) { return b->w < b->r ? (b->wm - b->r) + b->w : b->w - b->r; }
static bool bip_try(bip_t *b, uint32_t n, uint64_t *hole) {
    uint32_t w = b->w, r = b->r;
    if (w < r) { if (n >= r - w) return false; b->w = w + n; return true; }
    if (b->cap - w >= n) { b->w = w + n; return true; }
    if (n < r) { *hole += b->cap - w; b->wm = w; b->w = n; return true; }
    return false;
}
static void bip_consume(bip_t *b, uint32_t n) {
    uint32_t r = b->r;
    if (b->w < r && r == b->wm) r = 0;
    r += n;
    if (b->w < r && r == b->wm) r = 0;
    b->r = r;
}
#define Q 4096u
typedef struct { uint32_t len[Q]; uint32_t h, t; } q_t;

static void run(uint32_t N, uint32_t D, uint32_t M, uint32_t msgs) {
    bip_t b = { 0, 0, 0, N, false, 0 };
    static q_t q, rq;                                  /* 두 구조는 같은 크기 수열을 받지만 상태는 따로 진행한다 */
    q.h = q.t = 0; rq.h = rq.t = 0;
    uint64_t hole = 0, fail = 0, done = 0, ring_fail = 0;
    uint32_t ring_live = 0;
    g_rng = 0x9E3779B9u;
    for (uint32_t i = 0; i < msgs; i++) {
        uint32_t n = 1u + rnd() % M;
        while (bip_live(&b) > D && q.t != q.h) bip_consume(&b, q.len[q.t++ % Q]);   /* 오래된 메시지부터 소비 */
        if (bip_try(&b, n, &hole)) { q.len[q.h++ % Q] = n; done++; }
        else fail++;
        while (ring_live > D && rq.t != rq.h) ring_live -= rq.len[rq.t++ % Q];
        if (N - ring_live >= n) { ring_live += n; rq.len[rq.h++ % Q] = n; }
        else ring_fail++;
    }
    printf("| %4u | %3u | %3u | %4u | %4u | %6.2f%% | %6.2f%% | %6.2f |\n",
           N, D, M, D + M, D + 2u * M,
           100.0 * (double)fail / (double)msgs,
           100.0 * (double)ring_fail / (double)msgs,
           done ? (double)hole / (double)done : 0.0);
}

int main(void) {
    printf("| N | D | M | D+M (링 보장선) | D+2M (bip 보장선) | bip 실패 | 링 실패 | 구멍 B/메시지 |\n");
    printf("|---|---|---|---|---|---|---|---|\n");
    run(256, 64, 96, 200000u);   /* N = D + 2M 정확히 */
    run(256, 64, 97, 200000u);   /* 보장선보다 1 위: 무작위 부하는 최악 상태를 잘 못 찾는다 */
    run(256, 64, 128, 200000u);
    run(256, 64, 180, 200000u);
    run(256, 128, 64, 200000u);
    run(256, 128, 126, 200000u);
    run(256, 128, 200, 200000u); /* D + M > N: 링도 실패 */
    run(1024, 512, 64, 200000u);
    run(1024, 512, 256, 200000u);
    return 0;
}
