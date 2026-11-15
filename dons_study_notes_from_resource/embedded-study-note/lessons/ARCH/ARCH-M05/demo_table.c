/* ARCH-M05 관찰용: 연관도 {1,2,4,8,FA} x 정책 4종 x 트레이스 4개의 미스 + 3C 표, FIFO Belady 이상 탐색.
 * solution.c 의 모델을 그대로 쓴다 (main 만 빼고 포함).
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_table.c -o table
 * run:   ./table table      # 비교 표 (4 KB, 32 B 라인 = 128 라인)
 *        ./table belady     # FA FIFO: 라인 수를 늘렸는데 미스가 느는 트레이스 찾기
 *        ./table plru       # A B C D A E C 를 4-way LRU / PLRU 로 한 단계씩
 */
#define ARCH_M05_NO_MAIN
#include "solution.c"

static void table(void) {
    static const char *const names[] = {"colwalk", "loop160", "hot_stream", "rand_hot"};
    for (int tr = 0; tr < 4; tr++) {
        size_t n = tr == 0 ? gen_colwalk(g_t)
                 : tr == 1 ? gen_loop(g_t, 160, 8, 32)
                 : tr == 2 ? gen_hot_stream(g_t, 16)
                           : gen_rand_hot(g_t, 30000, 9);
        printf("\n[%s] %zu accesses, 4 KB / 32 B lines (128 lines)\n", names[tr], n);
        printf("  W    policy  misses  miss%%   comp    cap   conf   anti  Hill-conf\n");
        for (uint32_t W = 1; W <= 128; W = W == 8 ? 128 : W * 2)
            for (int p = 0; p < POL_N; p++) {
                stats_t st;
                if (W == 1 && p > 0) continue;                       /* 직접 사상은 정책 무관 */
                sim_run(4096, 32, W, (policy_t)p, g_t, n, &st);
                printf("  %-4s %-7s %6llu %6.1f %6llu %6llu %6llu %6llu %+10lld\n",
                       W == 128 ? "FA" : W == 1 ? "1" : W == 2 ? "2" : W == 4 ? "4" : "8",
                       W == 1 ? "-" : pol_name[p], (unsigned long long)st.misses,
                       100.0 * (double)st.misses / (double)st.acc, (unsigned long long)st.comp,
                       (unsigned long long)st.cap, (unsigned long long)st.conf,
                       (unsigned long long)st.anti, (long long)st.misses - (long long)st.fa_misses);
            }
    }
    {   /* hot_stream: 핫 영역 미스만 (Part C), 스트림 non-cacheable 이면? */
        size_t n = gen_hot_stream(g_t, 16);
        printf("\nhot_stream: hot-region misses / 1024 (16 ticks x 64 hot lines)\n");
        printf("  W     LRU  FIFO Random  PLRU | LRU + stream non-cacheable\n");
        for (uint32_t W = 1; W <= 128; W = W == 8 ? 128 : W * 2) {
            printf("  %-4u", W);
            for (int p = 0; p < POL_N; p++) {
                scen_t sc = {4096, 32, W, (policy_t)p, 0, 0};
                printf(" %5llu", (unsigned long long)region_misses(&sc, g_t, n, HOT_BASE, HOT_END));
            }
            scen_t nc = {4096, 32, W, POL_LRU, STRM_BASE, STRM_END};
            printf(" | %5llu\n", (unsigned long long)region_misses(&nc, g_t, n, HOT_BASE, HOT_END));
        }
    }
    {   /* min_ways_for 를 트레이스별로 */
        printf("\nmin ways within 5%% of FA-LRU (LRU / Random / PLRU):\n");
        for (int tr = 0; tr < 4; tr++) {
            size_t n = tr == 0 ? gen_colwalk(g_t) : tr == 1 ? gen_loop(g_t, 160, 8, 32)
                     : tr == 2 ? gen_hot_stream(g_t, 16) : gen_rand_hot(g_t, 30000, 9);
            printf("  %-10s %4u %4u %4u\n", names[tr], min_ways_for(4096, 32, POL_LRU, g_t, n, 0.05),
                   min_ways_for(4096, 32, POL_RANDOM, g_t, n, 0.05), min_ways_for(4096, 32, POL_PLRU, g_t, n, 0.05));
        }
    }
}

/* FA FIFO (S = 1) 에서 lines_small → lines_big 으로 늘렸는데 미스가 는 트레이스를 결정적 랜덤 탐색 */
static void belady(uint32_t small, uint32_t big, uint32_t alphabet, uint32_t maxlen) {
    uint32_t s = 12345;
    uint32_t best[64]; size_t bestlen = 0; uint64_t bm1 = 0, bm2 = 0;
    for (uint32_t trial = 0; trial < 2000000 && !bestlen; trial++) {
        uint32_t len = 8 + xorshift32(&s) % (maxlen - 7);
        uint32_t ln[64];
        for (uint32_t i = 0; i < len; i++) ln[i] = xorshift32(&s) % alphabet;
        uint64_t m1 = misses_of(small * 32, 32, small, POL_FIFO, ln, len);
        uint64_t m2 = misses_of(big * 32, 32, big, POL_FIFO, ln, len);
        if (m2 > m1) { memcpy(best, ln, len * sizeof *ln); bestlen = len; bm1 = m1; bm2 = m2; }
    }
    printf("FIFO FA %u lines vs %u lines: ", small, big);
    if (!bestlen) { printf("no anomaly found\n"); return; }
    /* 찾은 것을 한 원소씩 지워 가며 줄인다 (이상이 유지되는 동안) */
    for (int again = 1; again;) {
        again = 0;
        for (size_t d = 0; d < bestlen; d++) {
            uint32_t tmp[64]; size_t k = 0;
            for (size_t i = 0; i < bestlen; i++) if (i != d) tmp[k++] = best[i];
            uint64_t m1 = misses_of(small * 32, 32, small, POL_FIFO, tmp, k);
            uint64_t m2 = misses_of(big * 32, 32, big, POL_FIFO, tmp, k);
            if (m2 > m1) { memcpy(best, tmp, k * sizeof *tmp); bestlen = k; bm1 = m1; bm2 = m2; again = 1; break; }
        }
    }
    printf("len %zu, misses %llu -> %llu\n  lines:", bestlen, (unsigned long long)bm1, (unsigned long long)bm2);
    for (size_t i = 0; i < bestlen; i++) printf(" %u", best[i]);
    printf("\n  LRU same trace: %llu -> %llu\n",
           (unsigned long long)misses_of(small * 32, 32, small, POL_LRU, best, bestlen),
           (unsigned long long)misses_of(big * 32, 32, big, POL_LRU, best, bestlen));
}

/* 2의 거듭제곱 제약이 없는 "k 칸 FIFO" (페이지 프레임 모델) — 고전 Belady 예 확인용 */
static uint32_t fifo_frames(uint32_t k, const uint32_t *ln, size_t len) {
    uint32_t fr[16], head = 0, used = 0, miss = 0;
    for (size_t i = 0; i < len; i++) {
        int hit = 0;
        for (uint32_t j = 0; j < used; j++) if (fr[j] == ln[i]) hit = 1;
        if (hit) continue;
        miss++;
        if (used < k) fr[used++] = ln[i];
        else { fr[head] = ln[i]; head = (head + 1) % k; }
    }
    return miss;
}
static void belady_classic(void) {
    const uint32_t b[] = {1, 2, 3, 4, 1, 2, 5, 1, 2, 3, 4, 5};
    printf("classic Belady string, FIFO 3 frames: %u misses, 4 frames: %u misses\n",
           fifo_frames(3, b, 12), fifo_frames(4, b, 12));
}

static void plru_steps(void) {
    const uint32_t s[] = {0, 1, 2, 3, 0, 4, 2};
    const char *nm = "ABCDE";
    cache_t l, p;
    cache_init(&l, 128, 32, 4, POL_LRU); cache_init(&p, 128, 32, 4, POL_PLRU);
    printf("step acc | LRU: hit way0..3          | PLRU: hit bits way0..3  victim-next\n");
    for (int i = 0; i < 7; i++) {
        int hl = cache_access(&l, s[i] * 32), hp = cache_access(&p, s[i] * 32);
        printf("%4d  %c   |      %s  ", i + 1, nm[s[i]], hl ? "H" : "m");
        for (uint32_t w = 0; w < 4; w++) printf(" %c(%llu)", l.ways[w].valid ? nm[l.ways[w].tag] : '.', (unsigned long long)l.ways[w].stamp);
        printf("  |       %s  %d%d%d ", hp ? "H" : "m", p.plru[0], p.plru[1], p.plru[2]);
        for (uint32_t w = 0; w < 4; w++) printf(" %c", p.ways[w].valid ? nm[p.ways[w].tag] : '.');
        printf("    way%u\n", plru_victim(p.plru, 4));
    }
    printf("misses: LRU %llu, PLRU %llu\n", (unsigned long long)l.misses, (unsigned long long)p.misses);
    cache_free(&l); cache_free(&p);
}

int main(int argc, char **argv) {
    const char *m = argc > 1 ? argv[1] : "table";
    if (!strcmp(m, "table")) table();
    else if (!strcmp(m, "belady")) { belady(2, 4, 6, 40); belady(4, 8, 10, 60); belady_classic(); }
    else if (!strcmp(m, "plru")) plru_steps();
    else { fprintf(stderr, "usage: %s table|belady|plru\n", argv[0]); return 2; }
    return 0;
}
