/* ARCH-M03 demo_bench.c — Apple M6 에서 분기 미스 비용 실측 (CHECK 없음, 관찰용)
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_bench.c demo_kernels.c -o bench
 *        (커널은 demo_kernels.c 에 따로 → 인라인되지 않고, cc -O2 -S demo_kernels.c 로 본 asm 그대로 돈다)
 * run:   ./bench clock     dependent add 사슬로 클럭(GHz) 추정
 *        ./bench main      정렬 vs 랜덤 × 커널 6종 (u8 16M, i32 4M), 7회 중앙값
 *        ./bench sweep     랜덤 u8 에서 임계값 t 스윕 → 미스율 vs ns 직선 맞춤 → 미스당 ns
 *        ./bench period    주기 P 로 반복되는 랜덤 패턴 → 실제 예측기가 몇 개까지 외우나
 *        ./bench max       달리는 최댓값: branchy vs csel (csel 이 루프 사슬에 들어가는 경우)
 *        ./bench hash      조건부 FNV-1a 갱신: -O2 가 스스로 만든 csel 이 정렬 데이터에서 지는 경우
 * 모든 모드는 QoS USER_INTERACTIVE 를 요청한다 (macOS 는 코어 고정 불가 — 힌트일 뿐).
 * 실행 중 본 코어 번호(pthread_cpu_number_np)를 찍는다: 이 맥에서 0–5 = E, 6–7 = Super, 8–11 = P (ARCH-M08 측정).
 */
#define _DARWIN_C_SOURCE
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

uint64_t k_auto_u8(const uint8_t *a, size_t n, unsigned t);
uint64_t k_scalar_u8(const uint8_t *a, size_t n, unsigned t);
uint64_t k_branchy_u8(const uint8_t *a, size_t n, unsigned t);
uint64_t k_csel_u8(const uint8_t *a, size_t n, unsigned t);
uint64_t k_mask_u8(const uint8_t *a, size_t n, unsigned t);
int64_t k_branchy_i32(const int32_t *a, size_t n, int32_t t);
int64_t k_csel_i32(const int32_t *a, size_t n, int32_t t);
uint32_t k_max_branchy(const uint32_t *a, size_t n);
uint32_t k_max_csel(const uint32_t *a, size_t n);
uint32_t k_hash_natural(const uint8_t *a, size_t n, unsigned t);
uint32_t k_hash_branchy(const uint8_t *a, size_t n, unsigned t);
uint32_t k_hash_csel(const uint8_t *a, size_t n, unsigned t);

#define REPS 7

static uint32_t xs = 0x2545F491u;
static uint32_t xorshift32(void) { xs ^= xs << 13; xs ^= xs >> 17; xs ^= xs << 5; return xs; }

static double now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}

static int cmp_d(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}
static double median(double *v, int n) { qsort(v, (size_t)n, sizeof v[0], cmp_d); return v[n / 2]; }

static int cpu_lo = 99, cpu_hi = -1;
static void note_cpu(void) {
    size_t c = 0;
    if (pthread_cpu_number_np(&c) == 0) {
        if ((int)c < cpu_lo) cpu_lo = (int)c;
        if ((int)c > cpu_hi) cpu_hi = (int)c;
    }
}

static volatile uint64_t g_sink;

static void sort_u8(uint8_t *a, size_t n) {
    size_t cnt[256] = {0};
    for (size_t i = 0; i < n; i++) cnt[a[i]]++;
    size_t k = 0;
    for (int v = 0; v < 256; v++) for (size_t c = 0; c < cnt[v]; c++) a[k++] = (uint8_t)v;
}
static int cmp_i32(const void *a, const void *b) {
    int32_t x = *(const int32_t *)a, y = *(const int32_t *)b;
    return (x > y) - (x < y);
}

typedef uint64_t (*ku8_t)(const uint8_t *, size_t, unsigned);

/* 한 번 측정 = 배열 전체를 한 번 훑기. 앞에 한 번 데우고 REPS 회 중앙값 (ns/원소). */
static double time_u8(ku8_t k, const uint8_t *a, size_t n, unsigned t) {
    double v[REPS];
    g_sink += k(a, n, t);
    for (int r = 0; r < REPS; r++) {
        note_cpu();
        double t0 = now_ns();
        g_sink += k(a, n, t);
        v[r] = (now_ns() - t0) / (double)n;
    }
    return median(v, REPS);
}

/* 단일 2비트 카운터 (00/01 = NT 예측, 10/11 = T 예측) 의 미스 수 — solution.c 의 sim_2bit 와 같다 */
static size_t sim_2bit(const uint8_t *a, size_t n, unsigned t, unsigned init) {
    unsigned c = init; size_t miss = 0;
    for (size_t i = 0; i < n; i++) {
        unsigned taken = a[i] >= t, pred = c >> 1;
        miss += pred != taken;
        if (taken) { if (c < 3) c++; } else { if (c > 0) c--; }
    }
    return miss;
}

static void mode_clock(void) {
    /* add x, x, #1 을 100개 이어 붙인 블록: 각 add 는 앞 add 의 결과가 필요 → 1 사이클/add (정수 add 지연 1 가정) */
    const uint64_t iters = 2000000;
    double v[REPS];
    for (int r = 0; r < REPS; r++) {
        uint64_t x = 0;
        note_cpu();
        double t0 = now_ns();
        for (uint64_t i = 0; i < iters; i++)
            __asm__ volatile(".rept 100\n\tadd %0, %0, #1\n\t.endr" : "+r"(x));
        v[r] = (now_ns() - t0) / (double)(iters * 100);
        g_sink += x;
    }
    double ns = median(v, REPS);
    printf("dependent add: %.4f ns/add -> ~%.2f GHz (median of %d, cpus %d-%d)\n", ns, 1.0 / ns, REPS, cpu_lo, cpu_hi);
}

static void mode_main(void) {
    const size_t N = 16u << 20;     /* 16M bytes */
    const size_t NI = 4u << 20;     /* 4M int32 = 16 MB */
    uint8_t *r8 = malloc(N), *s8 = malloc(N);
    int32_t *r32 = malloc(NI * 4), *s32 = malloc(NI * 4);
    if (!r8 || !s8 || !r32 || !s32) { puts("oom"); exit(1); }
    for (size_t i = 0; i < N; i++) r8[i] = (uint8_t)xorshift32();
    memcpy(s8, r8, N); sort_u8(s8, N);
    for (size_t i = 0; i < NI; i++) r32[i] = (int32_t)xorshift32();
    memcpy(s32, r32, NI * 4); qsort(s32, NI, 4, cmp_i32);

    struct { const char *name; ku8_t k; } ks[] = {
        {"auto (-O2, NEON)", k_auto_u8}, {"scalar (-O2 if-conv)", k_scalar_u8},
        {"branchy (b.hi)", k_branchy_u8}, {"csel (ternary)", k_csel_u8}, {"mask -(x>=t)&x", k_mask_u8},
    };
    printf("u8  N=%zu t=128   ns/elem: sorted   random   random/sorted\n", N);
    for (size_t i = 0; i < sizeof ks / sizeof ks[0]; i++) {
        double so = time_u8(ks[i].k, s8, N, 128), ra = time_u8(ks[i].k, r8, N, 128);
        printf("  %-22s        %7.3f  %7.3f   %5.2fx\n", ks[i].name, so, ra, ra / so);
    }
    /* int32: 임계값 0 (부호 있는 랜덤의 절반) */
    double v[REPS];
    const int32_t *arr[2] = {s32, r32};
    const char *nm[2] = {"sorted", "random"};
    printf("i32 N=%zu t=0     ns/elem\n", NI);
    for (int which = 0; which < 2; which++) {
        double res[2];
        for (int kind = 0; kind < 2; kind++) {
            g_sink += (uint64_t)(kind ? k_csel_i32(arr[which], NI, 0) : k_branchy_i32(arr[which], NI, 0));
            for (int r = 0; r < REPS; r++) {
                note_cpu();
                double t0 = now_ns();
                int64_t s = kind ? k_csel_i32(arr[which], NI, 0) : k_branchy_i32(arr[which], NI, 0);
                v[r] = (now_ns() - t0) / (double)NI;
                g_sink += (uint64_t)s;
            }
            res[kind] = median(v, REPS);
        }
        printf("  %-6s branchy %7.3f   csel %7.3f\n", nm[which], res[0], res[1]);
    }
    printf("checksums equal: %s (cpus seen %d-%d)\n",
           (k_auto_u8(r8, N, 128) == k_branchy_u8(r8, N, 128) && k_csel_u8(r8, N, 128) == k_mask_u8(r8, N, 128) &&
            k_branchy_i32(r32, NI, 0) == k_csel_i32(r32, NI, 0)) ? "yes" : "NO", cpu_lo, cpu_hi);
    free(r8); free(s8); free(r32); free(s32);
}

static void mode_sweep(void) {
    const size_t N = 4u << 20;
    uint8_t *a = malloc(N);
    if (!a) { puts("oom"); exit(1); }
    for (size_t i = 0; i < N; i++) a[i] = (uint8_t)xorshift32();
    double xs_[17], ys[17]; int m = 0;
    printf("random u8 N=%zu   t   p(taken)  min(p,1-p)  2bit-sim  branchy ns  csel ns\n", N);
    for (unsigned t = 0; t <= 256; t += 16) {
        size_t taken = 0;
        for (size_t i = 0; i < N; i++) taken += a[i] >= t;
        double p = (double)taken / (double)N, mr = p < 0.5 ? p : 1 - p;
        double sim = (double)sim_2bit(a, N, t, 1) / (double)N;
        double b = time_u8(k_branchy_u8, a, N, t), c = time_u8(k_csel_u8, a, N, t);
        printf("                 %3u   %.4f    %.4f     %.4f    %7.3f    %7.3f\n", t, p, mr, sim, b, c);
        xs_[m] = mr; ys[m] = b; m++;
    }
    /* 최소제곱 직선 y = a + b x  (x = min(p,1-p)) */
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    for (int i = 0; i < m; i++) { sx += xs_[i]; sy += ys[i]; sxx += xs_[i] * xs_[i]; sxy += xs_[i] * ys[i]; }
    double slope = (m * sxy - sx * sy) / (m * sxx - sx * sx), icpt = (sy - slope * sx) / m;
    printf("fit branchy ns = %.3f + %.3f * missrate  -> %.2f ns per miss (cpus %d-%d)\n", icpt, slope, slope, cpu_lo, cpu_hi);
    free(a);
}

static void mode_period(void) {
    const size_t N = 4u << 20;
    uint8_t *a = malloc(N);
    if (!a) { puts("oom"); exit(1); }
    printf("period P: random block of P bytes repeated to N=%zu, t=128\n", N);
    printf("     P   branchy ns   csel ns   2bit-sim missrate\n");
    for (size_t P = 1; P <= (1u << 20); P *= 4) {
        xs = 0x2545F491u;
        for (size_t i = 0; i < P && i < N; i++) a[i] = (uint8_t)xorshift32();
        for (size_t i = P; i < N; i++) a[i] = a[i - P];
        double b = time_u8(k_branchy_u8, a, N, 128), c = time_u8(k_csel_u8, a, N, 128);
        printf("%7zu   %8.3f   %8.3f      %.4f\n", P, b, c, (double)sim_2bit(a, N, 128, 1) / (double)N);
    }
    printf("(cpus %d-%d)\n", cpu_lo, cpu_hi);
    free(a);
}

static void mode_max(void) {
    const size_t N = 16u << 20;
    uint32_t *r = malloc(N * 4), *up = malloc(N * 4);
    if (!r || !up) { puts("oom"); exit(1); }
    for (size_t i = 0; i < N; i++) { r[i] = xorshift32(); up[i] = (uint32_t)i; }
    const uint32_t *arr[2] = {r, up};
    const char *nm[2] = {"random   ", "ascending"};
    printf("running max N=%zu   ns/elem   branchy   csel\n", N);
    for (int w = 0; w < 2; w++) {
        double res[2], v[REPS];
        for (int kind = 0; kind < 2; kind++) {
            g_sink += kind ? k_max_csel(arr[w], N) : k_max_branchy(arr[w], N);
            for (int i = 0; i < REPS; i++) {
                note_cpu();
                double t0 = now_ns();
                g_sink += kind ? k_max_csel(arr[w], N) : k_max_branchy(arr[w], N);
                v[i] = (now_ns() - t0) / (double)N;
            }
            res[kind] = median(v, REPS);
        }
        size_t upd = 0; uint32_t m = 0;
        for (size_t i = 0; i < N; i++) if (arr[w][i] > m) { m = arr[w][i]; upd++; }
        printf("  %s (updates %8zu)          %7.3f  %7.3f\n", nm[w], upd, res[0], res[1]);
    }
    printf("(cpus %d-%d)\n", cpu_lo, cpu_hi);
    free(r); free(up);
}

static uint64_t hash_wrap_n(const uint8_t *a, size_t n, unsigned t) { return k_hash_natural(a, n, t); }
static uint64_t hash_wrap_b(const uint8_t *a, size_t n, unsigned t) { return k_hash_branchy(a, n, t); }
static uint64_t hash_wrap_c(const uint8_t *a, size_t n, unsigned t) { return k_hash_csel(a, n, t); }

static void mode_hash(void) {
    const size_t N = 16u << 20;
    uint8_t *r8 = malloc(N), *s8 = malloc(N);
    if (!r8 || !s8) { puts("oom"); exit(1); }
    for (size_t i = 0; i < N; i++) r8[i] = (uint8_t)xorshift32();
    memcpy(s8, r8, N); sort_u8(s8, N);
    struct { const char *name; ku8_t k; } ks[] = {
        {"natural (-O2 -> csel)", hash_wrap_n}, {"branchy (b.hi)", hash_wrap_b}, {"csel (unpredictable)", hash_wrap_c},
    };
    unsigned ts[] = {128, 224};
    for (int ti = 0; ti < 2; ti++) {
        printf("cond. FNV-1a u8 N=%zu t=%u  ns/elem: sorted   random\n", N, ts[ti]);
        for (size_t i = 0; i < 3; i++)
            printf("  %-24s           %7.3f  %7.3f\n", ks[i].name, time_u8(ks[i].k, s8, N, ts[ti]), time_u8(ks[i].k, r8, N, ts[ti]));
    }
    printf("hashes equal: %s (cpus %d-%d)\n",
           (k_hash_natural(r8, N, 128) == k_hash_branchy(r8, N, 128) && k_hash_csel(r8, N, 128) == k_hash_branchy(r8, N, 128)) ? "yes" : "NO",
           cpu_lo, cpu_hi);
    free(r8); free(s8);
}

int main(int argc, char **argv) {
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
    const char *m = argc > 1 ? argv[1] : "main";
    if (!strcmp(m, "clock")) mode_clock();
    else if (!strcmp(m, "main")) mode_main();
    else if (!strcmp(m, "sweep")) mode_sweep();
    else if (!strcmp(m, "period")) mode_period();
    else if (!strcmp(m, "max")) mode_max();
    else if (!strcmp(m, "hash")) mode_hash();
    else { fprintf(stderr, "usage: %s clock|main|sweep|period|max|hash\n", argv[0]); return 2; }
    return 0;
}
