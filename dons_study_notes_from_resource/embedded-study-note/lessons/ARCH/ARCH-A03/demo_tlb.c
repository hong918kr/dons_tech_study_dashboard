/* ARCH-A03 demo_tlb.c — Part B: TLB 도달 범위(reach) 실측. 포인터 체이스로 ns/접근 vs 페이지 수 P.
 * solution.c 를 main 이름만 바꿔 포함한다 → 체크된 sattolo / chase_offset / chase_build / find_knees 로 잰다.
 *
 *   cc -std=c11 -O2 demo_tlb.c -o demo_tlb && ./demo_tlb              (기본: 5 회, P = 4 .. 65536)
 *   ./demo_tlb 7 16384                                                (7 회, P 최대 16384)
 *
 * 세 가지 배치 (layout), 같은 무작위 한-사이클 순서:
 *   spread : 원소 i = 페이지 i, 그 안 줄 (i mod 128)  → P 페이지, P 줄 (TLB 가 P 페이지를 덮어야 함)
 *   packed : 원소 i = 줄 i (빽빽)                       → P 줄, P/128 페이지 (같은 캐시 발자국, TLB 부담 최소)
 *   same   : 원소 i = 페이지 i, 오프셋 0                → P 페이지인데 모든 줄이 같은 L1 세트 (aliasing 대조군)
 *   quad   : 한 페이지에 원소 4 개 (4 KB 간격 조각)       → P 줄, P/4 페이지. TLB 가 원인이면 무릎이 P 기준 4 배 뒤로
 * spread − packed ≈ 페이지가 많아서 생긴 비용 (TLB 미스 + 페이지 테이블 워크). 값 = 실행들의 중앙값.
 * 매핑은 만들 때(chase_build) 모든 페이지를 써서 폴트를 끝낸다 → 타이밍 구간에 페이지 폴트 없음.
 */
#define main solution_main
#include "solution.c"
#undef main
#include <pthread.h>
#include <sys/sysctl.h>
#include <time.h>

#define MAXRUNS 15
#define MAXP 64
static uint64_t now_ns(void) { return clock_gettime_nsec_np(CLOCK_UPTIME_RAW); }
static int cmpd(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}
static double median(double *v, int n) {
    qsort(v, (size_t)n, sizeof v[0], cmpd);
    return n % 2 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

static volatile uintptr_t g_sink;

/* 한 (layout, P) 측정: 매핑 → 체인 → 2 바퀴 예열 → runs 번 steps 걸음 시간 → ns/접근 중앙값 */
static double measure(size_t P, size_t pg, int layout, int runs, uint32_t *nx, uint64_t *seed) {
    size_t span = chase_span(P, pg, LINE, layout);
    uint8_t *m = map_anon(span);
    if (!m) { perror("mmap"); exit(1); }
    if (prefault(m, span, pg, PF_TOUCH) != 0) exit(1);       /* 폴트는 타이밍 밖에서 끝낸다 */
    sattolo(nx, P, seed);
    if (!is_single_cycle(nx, P)) { fprintf(stderr, "not a single cycle\n"); exit(1); }
    void *p = chase_build(m, nx, P, pg, LINE, layout);
    p = chase_walk(p, 2 * P);
    size_t steps = P * 4 > ((size_t)1 << 21) ? P * 4 : ((size_t)1 << 21);
    double t[MAXRUNS];
    for (int r = 0; r < runs; r++) {
        uint64_t t0 = now_ns();
        p = chase_walk(p, steps);
        uint64_t t1 = now_ns();
        t[r] = (double)(t1 - t0) / (double)steps;
    }
    g_sink = (uintptr_t)p;
    munmap(m, span);
    return median(t, runs);
}

int main(int argc, char **argv) {
    int runs = argc > 1 ? atoi(argv[1]) : 5;
    if (runs < 1 || runs > MAXRUNS) runs = 5;
    size_t pmax = argc > 2 ? (size_t)strtoull(argv[2], NULL, 10) : 65536;
    size_t same_max = pmax < 16384 ? pmax : 16384;          /* same 은 aliasing 만 보면 되니 16384 (256 MiB) 까지 */

    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
    char brand[64] = "?"; size_t bl = sizeof brand;
    sysctlbyname("machdep.cpu.brand_string", brand, &bl, NULL, 0);
    double la[3] = {0, 0, 0};
    getloadavg(la, 3);
    size_t pg = sys_page_size();
    printf("%s, page %zu B, line %d B, clang %s, runs %d, load avg %.2f %.2f %.2f (start)\n", brand, pg, LINE,
           __clang_version__, runs, la[0], la[1], la[2]);

    /* P = 4, 6, 8, 12, 16, 24, ... (2^k 와 1.5·2^k) */
    size_t Ps[MAXP];
    int nP = 0;
    for (size_t b = 4; b <= pmax && nP < MAXP; b *= 2) {
        Ps[nP++] = b;
        if (b + b / 2 <= pmax && nP < MAXP) Ps[nP++] = b + b / 2;
    }
    static uint32_t nx[1u << 20];
    uint64_t seed = 0x2545F4914F6CDD1Dull;
    double y[L_N][MAXP];
    printf("\n%7s %11s %10s | %8s %8s %8s %8s | %9s\n", "P", "P*page", "P*line", "spread", "quad", "packed", "same",
           "spr-pack");
    for (int i = 0; i < nP; i++) {
        size_t P = Ps[i];
        y[L_SPREAD][i] = measure(P, pg, L_SPREAD, runs, nx, &seed);
        y[L_QUAD][i] = measure(P, pg, L_QUAD, runs, nx, &seed);
        y[L_PACKED][i] = measure(P, pg, L_PACKED, runs, nx, &seed);
        y[L_SAME][i] = P <= same_max ? measure(P, pg, L_SAME, runs, nx, &seed) : -1.0;
        char same[16];
        if (y[L_SAME][i] < 0) snprintf(same, sizeof same, "%8s", "-");
        else snprintf(same, sizeof same, "%8.2f", y[L_SAME][i]);
        printf("%7zu %8zu KiB %6zu KiB | %8.2f %8.2f %8.2f %s | %9.2f\n", P, P * pg >> 10, P * LINE >> 10,
               y[L_SPREAD][i], y[L_QUAD][i], y[L_PACKED][i], same, y[L_SPREAD][i] - y[L_PACKED][i]);
        fflush(stdout);
    }
    const double ratios[] = {1.25, 1.5};
    for (size_t r = 0; r < 2; r++) {
        size_t k[16];
        size_t nk = find_knees(y[L_SPREAD], (size_t)nP, ratios[r], k, 16);
        printf("\nknees (spread, ratio %.2f):", ratios[r]);
        for (size_t j = 0; j < nk; j++)
            printf("  P=%zu (prev P=%zu, %.2f -> %.2f ns; coverage between %zu and %zu KiB)", Ps[k[j]], Ps[k[j] - 1],
                   y[L_SPREAD][k[j] - 1], y[L_SPREAD][k[j]], Ps[k[j] - 1] * pg >> 10, Ps[k[j]] * pg >> 10);
        nk = find_knees(y[L_QUAD], (size_t)nP, ratios[r], k, 16);
        printf("\nknees (quad,   ratio %.2f):", ratios[r]);
        for (size_t j = 0; j < nk; j++) printf("  P=%zu (pages %zu)", Ps[k[j]], Ps[k[j]] / 4);
        nk = find_knees(y[L_PACKED], (size_t)nP, ratios[r], k, 16);
        printf("\nknees (packed, ratio %.2f):", ratios[r]);
        for (size_t j = 0; j < nk; j++)
            printf("  P=%zu (lines %zu KiB, pages %zu)", Ps[k[j]], Ps[k[j]] * LINE >> 10, pages_for(Ps[k[j]] * LINE, pg));
    }
    getloadavg(la, 3);
    printf("\n\nload avg %.2f %.2f %.2f (end)\n", la[0], la[1], la[2]);
    return 0;
}
