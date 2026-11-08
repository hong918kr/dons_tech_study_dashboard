/* DS-M13 demo — 조회 시간: 선형 탐색 vs 이진 트라이 vs Patricia (이 Mac, -O2)
 * solution.c 의 자료구조를 그대로 쓰기 위해 main 이름만 바꿔 include 한다.
 *   cc -std=c11 -Wall -Wextra -Werror -O2 demo_lpm_bench.c -o demo_lpm_bench && ./demo_lpm_bench
 */
#include <time.h>
#define main solution_main
#include "solution.c"
#undef main

static double now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}

#define NADDR 200000u
static uint32_t g_addr[NADDR];

int main(void) {
    static const uint32_t Ns[3] = { 100, 1000, 3000 };
    printf("%5s %6s | %12s %12s %12s | %s\n", "N", "routes", "linear ns", "binary ns", "patricia ns", "checksum");
    for (int t = 0; t < 3; t++) {
        bt_init(&g_bt, 0); pt_init(&g_pt, 0); g_ref_n = 0; rng_state = 777u + (uint32_t)t;
        for (uint32_t i = 0; i < Ns[t]; i++) {
            uint32_t p, l; gen_bgp(&p, &l);
            uint16_t nh = (uint16_t)(i % 64u);
            bt_insert(&g_bt, p, l, nh); pt_insert(&g_pt, p, l, nh); ref_set(p, l, nh);
        }
        bt_insert(&g_bt, 0, 0, 63); pt_insert(&g_pt, 0, 0, 63); ref_set(0, 0, 63);
        for (uint32_t i = 0; i < NADDR; i++) g_addr[i] = gen_addr();

        uint32_t lin_n = Ns[t] >= 1000u ? 20000u : NADDR;      /* 선형은 느려서 표본을 줄임 */
        uint64_t s1 = 0, s2 = 0, s3 = 0;
        double t0 = now_ns();
        for (uint32_t i = 0; i < lin_n; i++) s1 += ref_lookup(g_addr[i], NULL);
        double t1 = now_ns();
        for (uint32_t i = 0; i < NADDR; i++) s2 += bt_lookup(&g_bt, g_addr[i], NULL);
        double t2 = now_ns();
        for (uint32_t i = 0; i < NADDR; i++) s3 += pt_lookup(&g_pt, g_addr[i], NULL);
        double t3 = now_ns();
        uint64_t s2p = 0;                                       /* 선형과 같은 표본에서의 합 = 일치 확인 */
        for (uint32_t i = 0; i < lin_n; i++) s2p += pt_lookup(&g_pt, g_addr[i], NULL);
        printf("%5u %6u | %12.1f %12.1f %12.1f | %s\n", Ns[t], g_ref_n,
               (t1 - t0) / lin_n, (t2 - t1) / NADDR, (t3 - t2) / NADDR,
               (s1 == s2p && s2 == s3) ? "same" : "DIFF");
    }
    return 0;
}
