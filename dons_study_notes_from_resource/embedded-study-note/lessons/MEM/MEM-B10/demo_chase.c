/* MEM-B10 demo_chase.c — 포인터 체이스 실측: working set 4 KiB … 1 GiB 에서 ns/load.
 * solution.c 를 main 이름만 바꿔 포함한다 → 거기서 검사한 sattolo / chain_build / ws_sizes /
 * ticks_to_ns / median / find_plateaus / hierarchy_check 를 그대로 쓴다.
 *
 *   cc -std=c11 -Wall -Wextra -Werror -O2 demo_chase.c -o demo_chase && ./demo_chase        (기본 9 회, 최대 1 GiB)
 *   ./demo_chase 7 268435456                                                              (7 회, 최대 256 MiB)
 *
 * 모드 (같은 크기 목록, 2^k 와 √2 단계):
 *   rand128 : 128 B 원소(= hw.cachelinesize), Sattolo 무작위 한 사이클      → 계층별 load-to-use 지연
 *   rand64  : 64 B 원소, 무작위 (같은 W 에 원소 2 배, 한 줄에 원소 2 개)
 *   seq128  : 128 B 원소, 0→1→2→… 순차 (의존 load 그대로, 주소만 예측 가능) → 프리페처 효과
 *   seq64   : 64 B 원소, 순차 (줄당 load 2 번)
 *   page    : 16 KiB 페이지마다 원소 하나 (칸 회전), 무작위 → 같은 주소 범위, 줄은 1/128 → TLB reach
 *   mlp     : 256 MiB rand128 체인 하나 위에 k 개의 독립 커서 (k = 1..16) → 겹친 미스 (Little 의 법칙)
 *   migration probe : 2/4/8/16/64 MiB 에서 짧은 rep 400 번, 코어에 머문 rep 과 옮겨 간 rep 의 ns/load 비교
 * 값 = reps 번 (각 2^19 load) 잰 ns/load 의 중앙값 [최소]. 시간은 mach_absolute_time (tick → ns 는 ticks_to_ns).
 * onCPU% = 스레드 CPU 시간 / 벽시계 (rand128 의 중앙값) — 100 보다 작으면 그만큼 선점당한 것.
 */
#define main solution_main
#include "solution.c"
#undef main
#include <mach/mach_time.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/sysctl.h>
#include <time.h>

#define MAXREPS 15
static volatile uintptr_t g_sink;                 /* 체인 끝 주소를 여기 써서 루프가 지워지지 않게 */
static mach_timebase_info_data_t g_tb;

static size_t sysctl_sz(const char *name) {
    uint64_t v = 0; size_t len = sizeof v;
    if (sysctlbyname(name, &v, &len, NULL, 0) != 0) return 0;
    return len == 4 ? (size_t)(uint32_t)v : (size_t)v;
}

typedef struct { double med, min, oncpu; } meas;

/* 체인 하나: 예열 (최대 2^21 걸음, 작은 체인은 몇 바퀴) → reps 번 steps 걸음.
 * 벽시계(mach_absolute_time) 와 함께 스레드 CPU 시간을 재서 on-CPU 비율 = cpu / wall 을 본다.
 * 공유 머신에서 선점(preemption) 당하면 벽시계 ns/load 가 부풀고 이 비율이 1 아래로 떨어진다. */
static meas time_chain(void *p, size_t n, size_t steps, int reps) {
    size_t warm = n * 2 < ((size_t)1 << 21) ? n * 2 : ((size_t)1 << 21);
    p = chain_walk(p, warm);
    double t[MAXREPS], f[MAXREPS];
    for (int r = 0; r < reps; r++) {
        uint64_t c0 = clock_gettime_nsec_np(CLOCK_THREAD_CPUTIME_ID);
        uint64_t t0 = mach_absolute_time();
        p = chain_walk(p, steps);
        uint64_t t1 = mach_absolute_time();
        uint64_t c1 = clock_gettime_nsec_np(CLOCK_THREAD_CPUTIME_ID);
        t[r] = ns_per_load(t1 - t0, g_tb.numer, g_tb.denom, steps);
        f[r] = (double)(c1 - c0) / (double)ticks_to_ns(t1 - t0, g_tb.numer, g_tb.denom);
    }
    g_sink += (uintptr_t)p;
    double mn = t[0];
    for (int r = 1; r < reps; r++) if (t[r] < mn) mn = t[r];
    return (meas){ median(t, (size_t)reps), mn, median(f, (size_t)reps) };
}

static uint8_t *g_buf;
static size_t g_cap;
static uint32_t *g_nx;
static uint8_t *g_seen;

/* 한 점: 순서(무작위/순차) → 사이클 검사 → 체인 → 측정 */
static meas run_point(size_t n, layout_t lay, size_t node, bool random, size_t steps, int reps, uint64_t *seed) {
    if (random) sattolo(g_nx, n, seed); else seq_cycle(g_nx, n);
    size_t longest;
    if (count_cycles(g_nx, n, g_seen, &longest) != 1) { fprintf(stderr, "not one cycle\n"); exit(1); }
    void *s = chain_build(g_buf, g_cap, g_nx, n, lay, node, 16384);
    if (!s) { fprintf(stderr, "chain_build failed n=%zu\n", n); exit(1); }
    return time_chain(s, n, steps, reps);
}

/* k 개의 독립 커서: 한 사이클 위에서 n/k 간격으로 출발 → 서로 다른 구간을 동시에 걷는다 */
static double run_mlp(size_t n, unsigned k, size_t steps, int reps) {
    void *cur[16];
    void *p = g_buf;                                        /* 원소 0 (chain_build 가 이미 깔아 둠) */
    for (unsigned j = 0; j < k; j++) { cur[j] = p; p = chain_walk(p, n / k); }
    size_t iters = steps / k;
    double t[MAXREPS];
    for (int r = 0; r < reps; r++) {
        uint64_t t0 = mach_absolute_time();
        for (size_t s = 0; s < iters; s++)
            for (unsigned j = 0; j < k; j++) cur[j] = *(void **)cur[j];
        uint64_t t1 = mach_absolute_time();
        t[r] = ns_per_load(t1 - t0, g_tb.numer, g_tb.denom, iters * k);
    }
    for (unsigned j = 0; j < k; j++) g_sink += (uintptr_t)cur[j];
    return median(t, (size_t)reps);
}

int main(int argc, char **argv) {
    int reps = argc > 1 ? atoi(argv[1]) : 9;
    size_t maxw = argc > 2 ? (size_t)strtoull(argv[2], NULL, 0) : ((size_t)1 << 30);
    if (reps < 1 || reps > MAXREPS) reps = 9;
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);   /* 빠른 코어 선호 — 고정은 아님 */
    mach_timebase_info(&g_tb);

    double la[3];
    getloadavg(la, 3);
    printf("timebase %u/%u (%.2f ns/tick)  cachelinesize %zu  pagesize %zu  load %.2f %.2f %.2f\n",
           g_tb.numer, g_tb.denom, (double)g_tb.numer / g_tb.denom, sysctl_sz("hw.cachelinesize"),
           sysctl_sz("hw.pagesize"), la[0], la[1], la[2]);
    size_t l1 = sysctl_sz("hw.perflevel0.l1dcachesize"), l2 = sysctl_sz("hw.perflevel0.l2cachesize");
    printf("perflevel0 L1D %zu  L2 %zu | perflevel1 L1D %zu L2 %zu | perflevel2 L1D %zu L2 %zu\n", l1, l2,
           sysctl_sz("hw.perflevel1.l1dcachesize"), sysctl_sz("hw.perflevel1.l2cachesize"),
           sysctl_sz("hw.perflevel2.l1dcachesize"), sysctl_sz("hw.perflevel2.l2cachesize"));

    g_cap = maxw;
    g_buf = mmap(NULL, g_cap, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    g_nx = malloc((maxw / 64) * sizeof *g_nx);
    g_seen = malloc(maxw / 64);
    if (g_buf == MAP_FAILED || !g_nx || !g_seen) { perror("alloc"); return 1; }

    size_t xs[64];
    size_t nw = ws_sizes(4096, maxw, 128, xs, 64);
    const size_t steps = (size_t)1 << 19;
    uint64_t seed = 0xC0FFEEull;
    static const char *mname[5] = { "rand128", "rand64", "seq128", "seq64", "page" };
    meas m[5][64];
    size_t npg[64];
    for (size_t i = 0; i < nw; i++) {
        m[0][i] = run_point(xs[i] / 128, LAY_LINE, 128, true, steps, reps, &seed);
        m[1][i] = run_point(xs[i] / 64, LAY_LINE, 64, true, steps, reps, &seed);
        m[2][i] = run_point(xs[i] / 128, LAY_LINE, 128, false, steps, reps, &seed);
        m[3][i] = run_point(xs[i] / 64, LAY_LINE, 64, false, steps, reps, &seed);
        npg[i] = xs[i] / 16384;
        if (npg[i] >= 1 && (i == 0 || npg[i] != npg[i - 1]))
            m[4][i] = run_point(npg[i], LAY_PAGE, 128, true, steps, reps, &seed);
        else { m[4][i] = (meas){ -1, -1, 0 }; npg[i] = 0; }
        char b[16]; fmt_bytes(xs[i], b, sizeof b);
        fprintf(stderr, "  %s done\n", b);
    }

    printf("\nns/load, median of %d [min]   (%zu loads per rep)\n", reps, steps);
    printf("%-9s", "W");
    for (int k = 0; k < 5; k++) printf(" | %-15s", mname[k]);
    printf(" | pages | onCPU%% r128\n");
    for (size_t i = 0; i < nw; i++) {
        char b[16]; fmt_bytes(xs[i], b, sizeof b);
        printf("%-9s", b);
        for (int k = 0; k < 5; k++) {
            if (m[k][i].med < 0) printf(" | %-15s", "-");
            else { char c[32]; snprintf(c, sizeof c, "%.2f [%.2f]", m[k][i].med, m[k][i].min); printf(" | %-15s", c); }
        }
        printf(" | %5zu | %3.0f\n", npg[i], 100.0 * m[0][i].oncpu);
    }

    /* 평탄 구간 → 이름 (perflevel0 = Super 코어 기준) */
    double y[64];
    for (size_t i = 0; i < nw; i++) y[i] = m[0][i].med;
    plateau_t p[16];
    size_t np = find_plateaus(y, nw, 1.4, 2, p, 16);
    level_cfg cfg[2] = { { "L1D", l1 }, { "L2", l2 } };
    printf("\nrand128 plateaus (ratio 1.4, min_len 2):\n");
    for (size_t k = 0; k < np; k++) {
        char a[16], b[16]; fmt_bytes(xs[p[k].first], a, sizeof a); fmt_bytes(xs[p[k].last], b, sizeof b);
        printf("  %-8s .. %-8s level %7.2f ns  (sysctl capacity of first W says: %s)\n", a, b, p[k].level, name_level(xs[p[k].first], cfg, 2));
    }
    hier_report hr = hierarchy_check(xs, y, nw, cfg, 2, 3.0);
    printf("hierarchy_check: levels=%zu seen_mask=0x%x cache_off_suspect=%d\n", hr.nlevels, hr.seen_mask,
           hr.cache_off_suspect);

    /* MLP: 256 MiB (또는 maxw) 무작위 체인에 커서 k 개 */
    size_t wm = maxw < ((size_t)256 << 20) ? maxw : ((size_t)256 << 20);
    size_t n = wm / 128;
    sattolo(g_nx, n, &seed);
    if (!chain_build(g_buf, g_cap, g_nx, n, LAY_LINE, 128, 16384)) return 1;
    chain_walk(g_buf, (size_t)1 << 21);
    char b[16]; fmt_bytes(wm, b, sizeof b);
    printf("\nMLP at %s rand128 (k independent cursors, ns per load overall, median of %d):\n", b, reps);
    static const unsigned ks[5] = { 1, 2, 4, 8, 16 };
    for (int j = 0; j < 5; j++) {
        double ns = run_mlp(n, ks[j], steps, reps);
        printf("  k=%-2u  %6.2f ns/load  -> %5.2f GB/s of 128 B lines\n", ks[j], ns, chain_gbps(128, ns));
    }

    /* 코어 이동(migration) 탐지: 짧은 rep (2^16 load) 400 번, 각 rep 앞뒤로 pthread_cpu_number_np.
     * "stable" = 이번 rep 시작·끝 CPU 가 직전 rep 끝 CPU 와 같음 (같은 코어에 머묾), 나머지 = moved. */
    printf("\nmigration probe rand128 (400 reps x 65536 loads, median ns/load):\n");
    static const size_t pw[5] = { (size_t)2 << 20, (size_t)4 << 20, (size_t)8 << 20, (size_t)16 << 20, (size_t)64 << 20 };
    for (int j = 0; j < 5 && pw[j] <= maxw; j++) {
        size_t nn = pw[j] / 128;
        sattolo(g_nx, nn, &seed);
        void *q = chain_build(g_buf, g_cap, g_nx, nn, LAY_LINE, 128, 16384);
        if (!q) return 1;
        q = chain_walk(q, 2 * nn);
        static double st[400], mv[400];
        size_t nst = 0, nmv = 0, prev = SIZE_MAX;
        for (int r = 0; r < 400; r++) {
            size_t c0 = 0, c1 = 0;
            pthread_cpu_number_np(&c0);
            uint64_t t0 = mach_absolute_time();
            q = chain_walk(q, (size_t)1 << 16);
            uint64_t t1 = mach_absolute_time();
            pthread_cpu_number_np(&c1);
            double v = ns_per_load(t1 - t0, g_tb.numer, g_tb.denom, (size_t)1 << 16);
            if (c0 == c1 && c0 == prev) st[nst++] = v; else mv[nmv++] = v;
            prev = c1;
        }
        g_sink += (uintptr_t)q;
        fmt_bytes(pw[j], b, sizeof b);
        printf("  %-7s stable n=%3zu median %7.2f | moved n=%3zu median %7.2f\n", b, nst, median(st, nst), nmv,
               median(mv, nmv));
    }

    getloadavg(la, 3);
    printf("\nload at end %.2f %.2f %.2f   sink %lx\n", la[0], la[1], la[2], (unsigned long)g_sink);
    munmap(g_buf, g_cap);
    free(g_nx); free(g_seen);
    return 0;
}
