/* ARM-A06 demo: CRC 처리량 비교 (GB/s) — 1 KiB / 64 KiB / 1 MiB 버퍼
 * 빌드: cc -std=c11 -O2 demo_bench.c solution.S shared_ref.c shared_bugs.c -lz -o /tmp/ARM-A06_bench && /tmp/ARM-A06_bench
 * solution.c 의 함수를 그대로 쓰려고 main 이름만 바꿔서 #include 한다 (같은 레슨 안의 파일).
 * 방법: 표본 1개 ≈ 8 MiB(비트 단위 1 MiB) 처리. 한 라운드 = 모든 행 × 크기를 번갈아 각각 best-of-5.
 *       7 라운드의 중앙값과 [최소..최대] 를 출력한다. 공유 맥에서 간섭은 느려지는 쪽으로만 작용하므로
 *       best-of-5 로 선점을 걸러내고, 라운드 간 중앙값으로 코어 배정(P/E)·클럭 변화를 걸러낸다.
 *       모든 구현은 측정 전에 기준(slicing-8 / zlib)과 값이 같은지 확인한다. 결과는 volatile sink 로.
 * 주의: 이 맥은 다른 작업과 공유 중 (load avg 를 같이 출력). 절대값보다 비율을 볼 것.
 */
#define _DARWIN_C_SOURCE
#define main solution_main
#include "solution.c"
#undef main
#include <pthread.h>
#include <time.h>

static double now_ns(void) { return (double)clock_gettime_nsec_np(CLOCK_UPTIME_RAW); }   /* 24 MHz 틱 = 41.7 ns 해상도 */
static int cmpd(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return (x > y) - (x < y); }
static volatile uint32_t sink;

static uint32_t f_bitwise(uint32_t c, const void *p, size_t n) { return ref_bitwise(c, p, n, POLY_C_R); }
static uint32_t f_table(uint32_t c, const void *p, size_t n)   { return ref_table(c, p, n, POLY_C_R); }
static uint32_t f_slice8(uint32_t c, const void *p, size_t n)  { return ref_slice8(c, p, n, POLY_C_R); }
static uint32_t f_zlib(uint32_t c, const void *p, size_t n)    { return (uint32_t)crc32(c, p, (uInt)n); }

/* 연습 2 의 fold-ahead 3-way (practice.c 와 같은 알고리즘): 세 체인 모두 0 에서 시작, 이어받은 상태는 x^(24B) 로 */
static const uint32_t K3FA[2] = { 0x359674F7u, 0xD270F1A2u };   /* x^(24B−33) mod P_c, B = 1024, 128 */
HW static uint32_t fold_ahead(uint32_t crc, const void *buf, size_t n) {
    const uint8_t *p = buf;
    uint32_t c = ~crc;
    size_t head = (size_t)(-(uintptr_t)p & 7);
    if (head > n) head = n;
    c = crc32c_raw_hw(c, p, head); p += head; n -= head;
    for (int t = 0; t < 2; t++) {
        size_t B = K3C[t].blk;
        while (n >= 3 * B) {
            uint32_t c0 = 0, c1 = 0, c2 = 0;
            for (size_t i = 0; i < B; i += 8) {
                c0 = __crc32cd(c0, ld64(p + i));
                c1 = __crc32cd(c1, ld64(p + B + i));
                c2 = __crc32cd(c2, ld64(p + 2 * B + i));
            }
            c = crc_mulk(c, K3FA[t], 1) ^ crc_mulk(c0, K3C[t].k2, 1) ^ crc_mulk(c1, K3C[t].k1, 1) ^ c2;
            p += 3 * B; n -= 3 * B;
        }
    }
    return ~crc32c_raw_hw(c, p, n);
}

/* 표본 S 개 중 최댓값 (GB/s) */
static double best_gbps(crc_fn f, const uint8_t *p, size_t n, size_t target, int S) {
    size_t reps = target / n ? target / n : 1;
    double best = 0;
    for (size_t k = 0; k < reps; k++) sink += f(0, p, n);          /* 워밍업 */
    for (int r = 0; r < S; r++) {
        double t = now_ns();
        for (size_t k = 0; k < reps; k++) sink += f(0, p, n);
        double g = (double)(reps * n) / (now_ns() - t);             /* bytes/ns = GB/s */
        if (g > best) best = g;
    }
    return best;
}

int main(void) {
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
    ref_init();
    enum { MAXN = 1 << 20 };
    uint8_t *buf = aligned_alloc(64, MAXN + 64);
    for (size_t i = 0; i < MAXN + 64; i++) buf[i] = (uint8_t)(i * 2654435761u >> 13);
    struct { const char *name; crc_fn f; int ieee; size_t off; size_t target; } R[] = {
        { "bitwise (C, CRC-32C)",        f_bitwise,   0, 0, 1u << 20 },
        { "byte table (C)",              f_table,     0, 0, 8u << 20 },
        { "slicing-by-8 (C)",            f_slice8,    0, 0, 8u << 20 },
        { "crc32c_arm 1-way (intrin)",   crc32c_arm,  0, 0, 8u << 20 },
        { "crc32c_asm 1-way (.S)",       crc32c_asm,  0, 0, 8u << 20 },
        { "crc32c_3way (intrin+PMULL)",  crc32c_3way, 0, 0, 8u << 20 },
        { "crc32c_3way, p+1 unaligned",  crc32c_3way, 0, 1, 8u << 20 },
        { "3-way fold-ahead (ex. 2)",   fold_ahead,  0, 0, 8u << 20 },
        { "crc32_arm 1-way (IEEE)",      crc32_arm,   1, 0, 8u << 20 },
        { "zlib crc32() (IEEE)",         f_zlib,      1, 0, 8u << 20 },
    };
    const int NR = (int)(sizeof R / sizeof R[0]);
    for (int i = 0; i < NR; i++)                                   /* 측정 전 값 확인 */
        for (size_t n = 0; n < 3000; n += 37) {
            uint32_t want = R[i].ieee ? f_zlib(0, buf + R[i].off, n) : f_slice8(0, buf + R[i].off, n);
            if (R[i].f(0, buf + R[i].off, n) != want) { printf("MISMATCH %s n=%zu\n", R[i].name, n); return 1; }
        }
    enum { ROUNDS = 7, NS = 3 };
    static double res[16][NS][ROUNDS];
    const size_t sz[NS] = { 1024, 64 * 1024, MAXN };
    double la[3];
    getloadavg(la, 3);
    for (int rd = 0; rd < ROUNDS; rd++)                            /* 라운드마다 모든 행을 번갈아 → 부하 변화가 고르게 */
        for (int i = 0; i < NR; i++)
            for (int s = 0; s < NS; s++)
                res[i][s][rd] = best_gbps(R[i].f, buf + R[i].off, sz[s], R[i].target, 5);
    printf("zlib %s | load avg at start %.1f %.1f %.1f | GB/s = median over %d rounds of (best of 5 samples) [min..max]\n",
           zlibVersion(), la[0], la[1], la[2], ROUNDS);
    printf("%-28s %21s %21s %21s\n", "implementation", "1 KiB", "64 KiB", "1 MiB");
    for (int i = 0; i < NR; i++) {
        printf("%-28s", R[i].name);
        for (int s = 0; s < NS; s++) {
            qsort(res[i][s], ROUNDS, sizeof(double), cmpd);
            printf(" %6.2f [%5.2f..%5.2f]", res[i][s][ROUNDS / 2], res[i][s][0], res[i][s][ROUNDS - 1]);
        }
        printf("\n");
    }
    free(buf);
    return 0;
}
