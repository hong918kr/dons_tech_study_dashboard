/* ARCH-A11 성능 모델: Amdahl과 Roofline — 체크 테스트 (결정적인 것만. 시간은 절대 CHECK 하지 않는다)
 *
 *   Part A  Amdahl 속도 향상 / Karp–Flatt 실험적 직렬 비율 / 이기종 코어(속도가 다른 코어) 모델,
 *           그리고 직렬 비율을 조절할 수 있는 pthreads 커널 (정적 분할 vs 동적 분할) — 결과가 1..12 스레드 모두 같은지.
 *   Part B  루프라인 attainable = min(peak, AI × BW), 릿지 포인트, NEON FMA FLOP 세기 (1 FMA = 2 FLOP, 4 lane fp32),
 *           STREAM 바이트 규칙 (copy/scale 2 워드, add/triad 3 워드, write-allocate 면 +1) + 네 커널.
 *   Part C  fp32 커널 vadd / matmul naive·tiled / conv1d naive·blocked 정확성 + 연산 강도(AI) 계산기 + 바운드 판정.
 *
 * 측정 (Apple M6 실측 GFLOP/s, GB/s, 속도 향상) 은 demo_amdahl.c / demo_roofline.c 에서만 한다.
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 -pthread solution.c -o so && ./so
 */
#define _DARWIN_C_SOURCE
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if defined(__aarch64__)
#include <arm_neon.h>
#endif

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; printf("PASS  %s\n", #expr); } \
                         else printf("FAIL  %s  (line %d)\n", #expr, __LINE__); } while (0)
static bool near(double a, double b, double tol) { return fabs(a - b) <= tol * (fabs(b) > 1 ? fabs(b) : 1); }
static uint64_t now_ns(void) { return clock_gettime_nsec_np(CLOCK_UPTIME_RAW); }

/* ============================ Part A — Amdahl ============================ */
/* f = 직렬 비율 (1 코어 실행 시간 중 병렬화 못 하는 부분), p = 코어 수. 모든 코어가 같은 속도라고 가정. */

/* TODO: Amdahl 속도 향상 S(p) = 1 / (f + (1-f)/p).  p < 1 이면 0. */
static double amdahl_speedup(double f, int p) {
    (void)f; (void)p;
    return 0.0;
}

/* TODO: Karp–Flatt 실험적 직렬 비율 e = (1/S - 1/p) / (1 - 1/p).  p <= 1 또는 S <= 0 이면 NAN.
 *       Amdahl 을 그대로 따르는 기계라면 e == f (모든 p 에서 상수). p 가 늘수록 e 가 커지면 오버헤드/불균형. */
static double karp_flatt(double S, int p) {
    (void)S; (void)p;
    return 0.0;
}

/* 이기종 코어: speed[i] = 코어 i 의 상대 속도 (기준 코어 = 1.0, T1 = 기준 코어 1개로 전체 일 = 1).
 * 직렬 부분은 speed[0] 코어가 한다. 병렬 부분 (1-f):
 *   SPLIT_STATIC  — p 등분 (같은 양). 가장 느린 코어가 끝나야 끝난다 → (1-f)/p / min(speed)
 *   SPLIT_DYNAMIC — 이상적인 일 나눠 갖기 (작은 조각을 먼저 끝난 코어가 가져감) → (1-f) / Σ speed  */
typedef enum { SPLIT_STATIC, SPLIT_DYNAMIC } split_t;

/* TODO: 위 정의대로 속도 향상 = 1 / (f/speed[0] + 병렬 시간).  p < 1 이면 0. */
static double hetero_speedup(double f, const double *speed, int p, split_t split) {
    (void)f; (void)speed; (void)p; (void)split;
    return 0.0;
}

/* ---- 직렬 비율을 조절하는 실제 pthreads 커널 ----
 * 일 = units 개의 단위 작업 unit_work(u) (정수 xorshift 를 spin 번 — 메모리 접근 없음, 순수 연산).
 * 단위 [0, serial_units) 는 호출 스레드가 혼자 (직렬 부분), 나머지는 p 개 스레드가 나눠서 (병렬 부분).
 * 결과 = 모든 unit_work 값의 합 (mod 2^64) → 순서와 무관하므로 어떻게 나눠도 같아야 한다. */
static uint64_t unit_work(uint64_t u, int spin) {
    uint64_t x = u * 0x9E3779B97F4A7C15ull + 1;
    for (int i = 0; i < spin; i++) { x ^= x << 13; x ^= x >> 7; x ^= x << 17; }
    return x;
}

typedef struct { long units; uint64_t ns; int cpu; } tstat_t;   /* 스레드별: 한 단위 수, 걸린 시간, 끝날 때 코어 번호 */
typedef struct {
    long base, n;          /* 병렬 부분 = 단위 [base, base + n) */
    int spin, p, chunk;
    split_t mode;
    _Atomic long next;     /* 동적 분할: 다음에 가져갈 (base 기준) 오프셋 */
} job_t;
typedef struct { job_t *job; int t; uint64_t sum; tstat_t st; } targ_t;

/* TODO: n 개를 p 조각으로 균형 있게 나눈 t 번째 조각 [*lo, *hi). 앞의 n % p 조각이 1 개씩 더 갖는다. */
static void static_range(long n, int p, int t, long *lo, long *hi) {
    (void)n; (void)p; (void)t;
    *lo = 0;
    *hi = 0;
}

static void *worker(void *v) {
    targ_t *a = v;
    job_t *j = a->job;
    uint64_t t0 = now_ns(), s = 0;
    long cnt = 0;
    if (j->mode == SPLIT_STATIC) {
        long lo, hi;
        static_range(j->n, j->p, a->t, &lo, &hi);
        for (long u = lo; u < hi; u++) s += unit_work((uint64_t)(j->base + u), j->spin);
        cnt = hi - lo;
    } else {
        /* TODO: 동적 분할 — atomic_fetch_add 로 chunk 개씩 가져와 처리, 오프셋이 n 이상이면 끝. 마지막 조각은 잘라서. */
        (void)atomic_load_explicit(&j->next, memory_order_relaxed);
    }
    a->sum = s;
    a->st.units = cnt;
    a->st.ns = now_ns() - t0;
    size_t cpu = 0;
    pthread_cpu_number_np(&cpu);
    a->st.cpu = (int)cpu;
    return NULL;
}

#define MAXT 16
/* st 가 NULL 이 아니면 st[0..p) 에 스레드별 통계, st[p] 에 직렬 부분 통계. */
static uint64_t par_kernel(long units, long serial_units, int spin, int p, split_t mode, int chunk, tstat_t *st) {
    if (p < 1 || p > MAXT || serial_units > units || chunk < 1) return 0;
    uint64_t s = 0, t0 = now_ns();
    for (long u = 0; u < serial_units; u++) s += unit_work((uint64_t)u, spin);
    if (st) { st[p].units = serial_units; st[p].ns = now_ns() - t0; st[p].cpu = -1; }
    job_t job = { .base = serial_units, .n = units - serial_units, .spin = spin, .p = p, .chunk = chunk, .mode = mode };
    atomic_init(&job.next, 0);
    targ_t args[MAXT];
    pthread_t th[MAXT];
    for (int t = 0; t < p; t++) {
        args[t] = (targ_t){ .job = &job, .t = t };
        if (pthread_create(&th[t], NULL, worker, &args[t]) != 0) { fprintf(stderr, "pthread_create\n"); exit(2); }
    }
    for (int t = 0; t < p; t++) {
        pthread_join(th[t], NULL);
        s += args[t].sum;
        if (st) st[t] = args[t].st;
    }
    return s;
}

/* ============================ Part B — Roofline ============================ */
/* TODO: attainable GFLOP/s = min(peak, ai × bw).  (peak GFLOP/s, bw GB/s, ai FLOP/byte) */
static double roofline(double peak, double bw, double ai) {
    (void)peak; (void)bw; (void)ai;
    return 0.0;
}
/* TODO: 릿지 포인트 (ridge point) = 메모리 지붕과 연산 지붕이 만나는 AI = peak / bw. */
static double ridge_point(double peak, double bw) { (void)peak; (void)bw; return 0.0; }

/* TODO: FMA 명령 n_fma 개 × lane 수 → FLOP. 1 FMA = 곱 1 + 덧셈 1 = 2 FLOP (lane 마다). */
static double fma_flops(double n_fma, int lanes) { (void)n_fma; (void)lanes; return 0.0; }

/* 피크 측정용 커널 (주어짐, 데모가 그대로 쓴다): NACC 개의 독립 누산기 × 4 lane fp32, 반복마다 acc[j] += a*b.
 * 누산기끼리 의존성이 없으므로 FMA 지연(latency)을 NACC 개가 나눠 숨긴다. FLOP = iters × NACC × 4 × 2. */
#if defined(__aarch64__)
#define DEF_PEAK(NACC)                                                                                  \
    __attribute__((noinline)) static float peak_fma_##NACC(long iters, float a, float b) {          \
        float32x4_t va = vdupq_n_f32(a), vb = vdupq_n_f32(b), acc[NACC];                                \
        for (int j = 0; j < NACC; j++) acc[j] = vdupq_n_f32((float)j);                                  \
        for (long i = 0; i < iters; i++)                                                                \
            for (int j = 0; j < NACC; j++) acc[j] = vfmaq_f32(acc[j], va, vb);                          \
        float32x4_t s = acc[0];                                                                         \
        for (int j = 1; j < NACC; j++) s = vaddq_f32(s, acc[j]);                                        \
        return vaddvq_f32(s);                                                                           \
    }
DEF_PEAK(1) DEF_PEAK(2) DEF_PEAK(4) DEF_PEAK(8) DEF_PEAK(12) DEF_PEAK(16) DEF_PEAK(24)
#endif
/* 같은 계산의 스칼라 기준 (fmaf = 한 번 반올림, NEON vfmaq 의 lane 하나와 비트 단위로 같다) */
static float peak_ref(int nacc, long iters, float a, float b) {
    float acc[32], tot = 0.0f;
    for (int j = 0; j < nacc; j++) acc[j] = (float)j;
    for (long i = 0; i < iters; i++)
        for (int j = 0; j < nacc; j++) acc[j] = fmaf(a, b, acc[j]);
    /* vaddq 로 누산기를 j 순서로 더한 뒤 vaddvq 로 4 lane 을 더한다: lane 마다 값이 같으므로 (x+x)+(x+x) */
    float lane = acc[0];
    for (int j = 1; j < nacc; j++) lane += acc[j];
    tot = (lane + lane) + (lane + lane);
    return tot;
}

/* STREAM (McCalpin) 네 커널. STREAM 규칙: copy/scale 은 원소당 2 워드, add/triad 는 3 워드를 옮긴다고 센다.
 * write-allocate 캐시는 쓰기 전에 그 라인을 읽어 오므로 (RFO) 실제 DRAM 트래픽은 원소당 1 워드 더 많을 수 있다. */
typedef enum { ST_COPY, ST_SCALE, ST_ADD, ST_TRIAD } stream_k;

/* TODO: n 원소, 원소 크기 elem 바이트일 때 옮기는 바이트. rfo 면 쓰기 배열 1 개분 (n·elem) 을 더한다. */
static double stream_bytes(stream_k k, size_t n, size_t elem, bool rfo) {
    (void)k; (void)n; (void)elem; (void)rfo;
    return 0.0;
}
/* TODO: FLOP 수: copy 0, scale n, add n, triad 2n (곱 1 + 덧셈 1). */
static double stream_flops(stream_k k, size_t n) {
    (void)k; (void)n;
    return 0.0;
}
/* 원소 [lo, hi) 구간. STREAM 과 같은 배열 역할: copy c=a, scale b=s·c, add c=a+b, triad a=b+s·c.
 * TODO: add, triad 두 케이스 (copy, scale 은 주어짐). */
static void stream_kernel(stream_k k, double *restrict a, double *restrict b, double *restrict c, double s,
                          size_t lo, size_t hi) {
    switch (k) {
    case ST_COPY:  for (size_t i = lo; i < hi; i++) c[i] = a[i]; break;
    case ST_SCALE: for (size_t i = lo; i < hi; i++) b[i] = s * c[i]; break;
    case ST_ADD:   break;
    case ST_TRIAD: break;
    }
}

/* ============================ Part C — 커널 + 연산 강도 ============================ */
/* 벡터 덧셈 (주어짐): FLOP n, 바이트 (compulsory) 12n */
__attribute__((noinline)) static void vadd(size_t n, const float *restrict a, const float *restrict b, float *restrict c) {
    for (size_t i = 0; i < n; i++) c[i] = a[i] + b[i];
}

/* 행렬 곱 naive ijk (주어짐, 행 우선 n×n fp32). 안쪽 k 가 B 를 열 방향으로 걷고, 덧셈 순서 때문에 벡터화 안 됨. */
__attribute__((noinline)) static void mm_naive(int n, const float *restrict A, const float *restrict B, float *restrict C) {
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++) {
            float s = 0.0f;
            for (int k = 0; k < n; k++) s += A[i * n + k] * B[k * n + j];
            C[i * n + j] = s;
        }
}

/* TODO: 타일링 (ARCH-M07 과 같은 구조, fp32): C 를 0 으로 지운 뒤 ii → jj → kk (간격 T) 블록 안에서 ikj.
 *       T 가 n 의 약수가 아니어도 맞아야 한다. (i, j) 마다 k 오름차순으로 더해져 naive 와 같은 순서. */
__attribute__((noinline)) static void mm_tiled(int n, int T, const float *restrict A, const float *restrict B, float *restrict C) {
    (void)n; (void)T; (void)A; (void)B; (void)C;
}

/* conv1d (valid, 상관 형태): y[i] = Σ_{k<K} w[k]·x[i+k], i ∈ [0, n). x 는 n+K-1 개.
 * naive (주어짐): 안쪽 k 가 합(reduction) 이라 -O2 (IEEE 순서 유지) 에서는 스칼라로 돈다. */
__attribute__((noinline)) static void conv1d_naive(size_t n, int K, const float *restrict x, const float *restrict w, float *restrict y) {
    for (size_t i = 0; i < n; i++) {
        float s = 0.0f;
        for (int k = 0; k < K; k++) s += w[k] * x[i + k];
        y[i] = s;
    }
}

/* TODO: 출력 16 개를 한 묶음으로 (레지스터 블로킹). 묶음마다 acc[16] = 0, k 마다 j < 16 에 대해
 *       acc[j] += w[k] * x[i + j + k]  → 안쪽 j 루프는 연속 16 원소라 NEON 4 레지스터로 벡터화된다.
 *       출력마다 k 오름차순으로 더하므로 naive 와 덧셈 순서는 같다. 단 -O2 naive 의 K>=4 경로는 fmul.4s + 스칼라 fadd
 *       (곱을 따로 반올림), blocked 는 fmla (한 번 반올림) 라 실수 입력에서는 비트 단위로 다를 수 있다. n % 16 나머지는 naive 처럼. */
#define CB 16
__attribute__((noinline)) static void conv1d_blocked(size_t n, int K, const float *restrict x, const float *restrict w, float *restrict y) {
    (void)n; (void)K; (void)x; (void)w; (void)y;
}

/* 연산 강도 (arithmetic intensity) = FLOP / DRAM 바이트. 여기서 바이트 = compulsory (입력 한 번 읽기 + 출력 한 번 쓰기).
 * rfo = true 면 출력 배열을 한 번 더 읽는다고 센다 (write-allocate). */
typedef struct { double flops, bytes; } cost_t;
static double ai_of(cost_t c) { return c.bytes > 0 ? c.flops / c.bytes : 0.0; }

/* TODO: vadd n 개 (fp32): FLOP n, 바이트 4·(2n 읽기 + n 쓰기) (+4n rfo) */
static cost_t cost_vadd(size_t n, bool rfo) {
    (void)n; (void)rfo;
    return (cost_t){ 0.0, 0.0 };
}
/* TODO: n×n matmul (fp32): FLOP 2n³, 바이트 4·(A, B 읽기 2n² + C 쓰기 n²) (+4n² rfo) */
static cost_t cost_matmul(int n, bool rfo) {
    (void)n; (void)rfo;
    return (cost_t){ 0.0, 0.0 };
}
/* TODO: conv1d n 출력, K 탭 (fp32): FLOP 2Kn, 바이트 4·(x: n+K-1, w: K, y: n) (+4n rfo) */
static cost_t cost_conv1d(size_t n, int K, bool rfo) {
    (void)n; (void)K; (void)rfo;
    return (cost_t){ 0.0, 0.0 };
}

typedef enum { BOUND_MEMORY, BOUND_COMPUTE } bound_t;
/* TODO: ai < 릿지 포인트 → 메모리 바운드, 아니면 연산 바운드 (같으면 연산). */
static bound_t bound_of(double ai, double peak, double bw) {
    (void)ai; (void)peak; (void)bw;
    return BOUND_MEMORY;
}

/* ================================ 테스트 ================================ */
static uint32_t g_rng = 2463534242u;
static uint32_t xs32(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }
static float small_int(void) { return (float)((int)(xs32() % 9u) - 4); }   /* -4..4 → 곱·합이 fp32 에서 정확 */

static void mm_ref(int n, const float *A, const float *B, float *C) {           /* 기준 (double 누적, 정수라 정확) */
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++) {
            double s = 0;
            for (int k = 0; k < n; k++) s += (double)A[i * n + k] * B[k * n + j];
            C[i * n + j] = (float)s;
        }
}

static bool mm_check(int n, int T) {
    float *A = malloc(sizeof(float) * n * n), *B = malloc(sizeof(float) * n * n);
    float *C1 = malloc(sizeof(float) * n * n), *C2 = malloc(sizeof(float) * n * n), *C3 = malloc(sizeof(float) * n * n);
    for (int i = 0; i < n * n; i++) { A[i] = small_int(); B[i] = small_int(); C2[i] = 99.0f; }
    mm_ref(n, A, B, C1);
    mm_tiled(n, T, A, B, C2);
    mm_naive(n, A, B, C3);
    bool ok = memcmp(C1, C2, sizeof(float) * n * n) == 0 && memcmp(C1, C3, sizeof(float) * n * n) == 0;
    free(A); free(B); free(C1); free(C2); free(C3);
    return ok;
}

static bool conv_check(size_t n, int K, bool integer_inputs) {
    float *x = malloc(sizeof(float) * (n + K)), *w = malloc(sizeof(float) * K);
    float *y1 = malloc(sizeof(float) * n), *y2 = malloc(sizeof(float) * n);
    for (size_t i = 0; i < n + (size_t)K; i++) x[i] = integer_inputs ? small_int() : (float)(xs32() % 20001u) / 10000.0f - 1.0f;
    for (int k = 0; k < K; k++) w[k] = integer_inputs ? small_int() : (float)(xs32() % 20001u) / 10000.0f - 1.0f;
    conv1d_naive(n, K, x, w, y1);
    for (size_t i = 0; i < n; i++) y2[i] = -12345.0f;
    conv1d_blocked(n, K, x, w, y2);
    bool ok = true;
    for (size_t i = 0; i < n; i++) {
        double tol = integer_inputs ? 0.0 : 1e-5 * K;
        if (fabs((double)y1[i] - (double)y2[i]) > tol) { ok = false; break; }
    }
    if (integer_inputs && ok) {                       /* 정수 입력이면 손 계산 기준과도 정확히 같아야 */
        for (size_t i = 0; i < n && ok; i++) {
            double s = 0;
            for (int k = 0; k < K; k++) s += (double)w[k] * x[i + (size_t)k];
            if ((float)s != y2[i]) ok = false;
        }
    }
    free(x); free(w); free(y1); free(y2);
    return ok;
}

int main(void) {
    printf("-- Part A: Amdahl, Karp-Flatt, heterogeneous cores, pthreads kernel --\n");
    CHECK(amdahl_speedup(0.0, 12) == 12.0);
    CHECK(amdahl_speedup(1.0, 12) == 1.0);
    CHECK(near(amdahl_speedup(0.05, 12), 7.741935483870968, 1e-12));
    CHECK(near(amdahl_speedup(0.1, 4), 1.0 / 0.325, 1e-12));
    CHECK(amdahl_speedup(0.1, 0) == 0.0);
    CHECK(amdahl_speedup(0.01, 1000000) < 100.0);              /* 한계 1/f */
    {
        bool inv = true;                                        /* Karp–Flatt 은 Amdahl 의 정확한 역함수 */
        static const double fs[] = { 0.0, 0.01, 0.05, 0.1, 0.25, 0.5, 0.9, 1.0 };
        for (int a = 0; a < 8; a++)
            for (int p = 2; p <= 64; p++)
                if (!near(karp_flatt(amdahl_speedup(fs[a], p), p), fs[a], 1e-9)) inv = false;
        CHECK(inv);
    }
    CHECK(isnan(karp_flatt(1.0, 1)));
    CHECK(isnan(karp_flatt(0.0, 4)));
    CHECK(near(karp_flatt(6.0, 12), (1.0 / 6 - 1.0 / 12) / (1 - 1.0 / 12), 1e-12));
    {
        double same[12];
        for (int i = 0; i < 12; i++) same[i] = 1.0;
        CHECK(near(hetero_speedup(0.05, same, 12, SPLIT_STATIC), amdahl_speedup(0.05, 12), 1e-12));
        CHECK(near(hetero_speedup(0.05, same, 12, SPLIT_DYNAMIC), amdahl_speedup(0.05, 12), 1e-12));
        /* M6 모양: Super 2 (1.0), Performance 4 (0.8), Efficiency 6 (0.4) — 속도는 예시 값 (실측은 데모) */
        double m6[12] = { 1, 1, 0.8, 0.8, 0.8, 0.8, 0.4, 0.4, 0.4, 0.4, 0.4, 0.4 };
        double st = hetero_speedup(0.0, m6, 12, SPLIT_STATIC), dy = hetero_speedup(0.0, m6, 12, SPLIT_DYNAMIC);
        printf("      f=0, M6-shaped speeds: static %.3f  dynamic %.3f  (Amdahl identical cores: 12)\n", st, dy);
        CHECK(near(st, 12 * 0.4, 1e-12));                      /* 정적 분할: 가장 느린 코어 × p */
        CHECK(near(dy, 2 + 3.2 + 2.4, 1e-12));                 /* 동적: 속도의 합 = 7.6 */
        CHECK(hetero_speedup(0.0, m6, 2, SPLIT_STATIC) == hetero_speedup(0.0, m6, 2, SPLIT_DYNAMIC));   /* 같은 코어끼리면 같다 */
        CHECK(near(hetero_speedup(0.0, m6, 6, SPLIT_STATIC), 6 * 0.8, 1e-12));
        CHECK(hetero_speedup(0.0, m6, 6, SPLIT_STATIC) > hetero_speedup(0.0, m6, 7, SPLIT_STATIC)); /* 7번째(E) 코어가 오히려 느리게 */
        CHECK(near(hetero_speedup(0.0, m6, 12, SPLIT_STATIC), hetero_speedup(0.0, m6, 6, SPLIT_STATIC), 1e-12)); /* E 6개 추가 = 이득 0 */
        CHECK(near(karp_flatt(st, 12), (1.0 / 4.8 - 1.0 / 12) / (11.0 / 12), 1e-12));   /* f=0 인데 e > 0 */
        CHECK(karp_flatt(st, 12) > 0.1);
    }
    {
        long lo, hi, tot = 0;
        bool contig = true;
        long prev = 0;
        for (int t = 0; t < 7; t++) {
            static_range(100, 7, t, &lo, &hi);
            if (lo != prev) contig = false;
            prev = hi;
            tot += hi - lo;
        }
        CHECK(contig && prev == 100 && tot == 100);
        static_range(100, 7, 0, &lo, &hi);
        CHECK(lo == 0 && hi == 15);                             /* 100 = 7·14 + 2 → 앞 2 조각 15 개 */
        static_range(100, 7, 6, &lo, &hi);
        CHECK(lo == 86 && hi == 100);
        static_range(3, 8, 5, &lo, &hi);
        CHECK(lo == 3 && hi == 3);                              /* 일보다 스레드가 많으면 빈 조각 */
    }
    {
        const long units = 20000, ser = 1000;
        const int spin = 50;
        uint64_t ref = 0;
        for (long u = 0; u < units; u++) ref += unit_work((uint64_t)u, spin);
        bool all = true, cover = true;
        for (int p = 1; p <= 12; p++)
            for (int m = 0; m < 2; m++) {
                tstat_t st[MAXT + 1];
                uint64_t r = par_kernel(units, ser, spin, p, (split_t)m, 37, st);
                long done = st[p].units;
                for (int t = 0; t < p; t++) done += st[t].units;
                if (r != ref) all = false;
                if (done != units) cover = false;
            }
        CHECK(all);                                             /* 1..12 스레드 × 정적/동적 → 같은 합 */
        CHECK(cover);                                           /* 단위 작업을 빠짐없이, 한 번씩 */
        CHECK(par_kernel(units, 0, spin, 3, SPLIT_DYNAMIC, units * 2, NULL) == ref);   /* chunk > n */
        CHECK(par_kernel(units, units, spin, 4, SPLIT_STATIC, 1, NULL) == ref);        /* 전부 직렬 */
        CHECK(par_kernel(units, ser, spin, 0, SPLIT_STATIC, 1, NULL) == 0);            /* 잘못된 p */
    }

    printf("-- Part B: roofline, FMA FLOP counting, STREAM byte rules --\n");
    CHECK(roofline(100.0, 50.0, 0.5) == 25.0);
    CHECK(roofline(100.0, 50.0, 2.0) == 100.0);
    CHECK(roofline(100.0, 50.0, 10.0) == 100.0);
    CHECK(ridge_point(100.0, 50.0) == 2.0);
    CHECK(roofline(100.0, 50.0, ridge_point(100.0, 50.0)) == 100.0);
    CHECK(fma_flops(1.0, 4) == 8.0);                            /* fmla.4s 하나 = 8 FLOP */
    CHECK(fma_flops(1e9 * 16, 4) == 1.28e11);                   /* 16 누산기 × 10^9 반복 */
    CHECK(fma_flops(1.0, 1) == 2.0);                            /* 스칼라 fmadd = 2 FLOP */
    CHECK(stream_bytes(ST_COPY, 1000, 8, false) == 16000.0);
    CHECK(stream_bytes(ST_SCALE, 1000, 8, false) == 16000.0);
    CHECK(stream_bytes(ST_ADD, 1000, 8, false) == 24000.0);
    CHECK(stream_bytes(ST_TRIAD, 1000, 8, false) == 24000.0);
    CHECK(stream_bytes(ST_TRIAD, 1000, 8, true) == 32000.0);    /* RFO 면 원소당 32 B */
    CHECK(stream_flops(ST_COPY, 10) == 0.0 && stream_flops(ST_SCALE, 10) == 10.0 && stream_flops(ST_TRIAD, 10) == 20.0);
    CHECK(near(stream_flops(ST_TRIAD, 1000) / stream_bytes(ST_TRIAD, 1000, 8, false), 1.0 / 12, 1e-15));  /* triad AI */
    {
        enum { N = 1003 };
        static double a[N], b[N], c[N];
        for (int i = 0; i < N; i++) { a[i] = i; b[i] = 2.0 * i; c[i] = -1; }
        stream_kernel(ST_COPY, a, b, c, 3.0, 0, N);             /* c = a */
        stream_kernel(ST_SCALE, a, b, c, 3.0, 0, N);            /* b = 3c = 3i */
        stream_kernel(ST_ADD, a, b, c, 3.0, 0, N);              /* c = a + b = 4i */
        stream_kernel(ST_TRIAD, a, b, c, 3.0, 0, 500);          /* a = b + 3c = 15i (앞 절반만) */
        bool ok = true;
        for (int i = 0; i < N; i++) {
            if (b[i] != 3.0 * i || c[i] != 4.0 * i) ok = false;
            if (a[i] != (i < 500 ? 15.0 * i : (double)i)) ok = false;
        }
        CHECK(ok);
    }
#if defined(__aarch64__)
    {
        /* 피크 커널이 실제로 그 계산을 한다 (FLOP 을 세려면 먼저 옳아야) — 스칼라 fmaf 와 비트 단위로 같다 */
        float a = 1.0f / 3.0f, b = 0.001f;
        CHECK(peak_fma_1(1000, a, b) == peak_ref(1, 1000, a, b));
        CHECK(peak_fma_8(1000, a, b) == peak_ref(8, 1000, a, b));
        CHECK(peak_fma_24(777, a, b) == peak_ref(24, 777, a, b));
        (void)peak_fma_2; (void)peak_fma_4; (void)peak_fma_12; (void)peak_fma_16;
    }
#endif

    printf("-- Part C: kernels (fp32) + arithmetic intensity + bound verdict --\n");
    {
        float a[37], b[37], c[37];
        for (int i = 0; i < 37; i++) { a[i] = (float)i; b[i] = 0.5f * i; }
        vadd(37, a, b, c);
        CHECK(c[0] == 0.0f && c[36] == 54.0f);
    }
    CHECK(mm_check(1, 1));
    CHECK(mm_check(7, 3));
    CHECK(mm_check(16, 16));
    CHECK(mm_check(33, 8));
    CHECK(mm_check(64, 24));                                    /* T 가 n 의 약수가 아님 */
    CHECK(mm_check(50, 128));                                   /* T > n */
    CHECK(conv_check(1, 1, true));
    CHECK(conv_check(15, 3, true));                             /* n < 16: 나머지 경로만 */
    CHECK(conv_check(16, 3, true));
    CHECK(conv_check(1000, 7, true));
    CHECK(conv_check(4099, 64, true));
    CHECK(conv_check(4099, 64, false));                         /* 실수 입력: 순서는 같지만 fused/unfused 차이 → 오차 ≤ 1e-5·K */
    {
        cost_t v = cost_vadd(1000, false), vr = cost_vadd(1000, true);
        CHECK(v.flops == 1000.0 && v.bytes == 12000.0 && vr.bytes == 16000.0);
        CHECK(near(ai_of(v), 1.0 / 12, 1e-15) && ai_of(vr) == 1.0 / 16);
        cost_t m = cost_matmul(1024, false);
        CHECK(m.flops == 2.0 * 1024 * 1024 * 1024 && m.bytes == 12.0 * 1024 * 1024);
        CHECK(near(ai_of(m), 1024.0 / 6, 1e-12));               /* n/6 */
        CHECK(near(ai_of(cost_matmul(1024, true)), 1024.0 / 8, 1e-12));
        cost_t c3 = cost_conv1d(1u << 24, 3, false), c64 = cost_conv1d(1u << 24, 64, false);
        printf("      AI: vadd %.4f  matmul(1024) %.1f  conv1d K=3 %.4f  K=64 %.3f  FLOP/B\n",
               ai_of(v), ai_of(m), ai_of(c3), ai_of(c64));
        CHECK(near(ai_of(c3), 0.75, 1e-6));                      /* ≈ 2K/8 = K/4 */
        CHECK(near(ai_of(c64), 16.0, 1e-4));
        CHECK(near(ai_of(cost_conv1d(1u << 24, 64, true)), 64.0 / 6, 1e-4));
        CHECK(ai_of((cost_t){ 1, 0 }) == 0.0);
        /* 바운드 판정: peak 100 GFLOP/s, 50 GB/s → 릿지 2 FLOP/B */
        CHECK(bound_of(ai_of(v), 100, 50) == BOUND_MEMORY);
        CHECK(bound_of(ai_of(m), 100, 50) == BOUND_COMPUTE);
        CHECK(bound_of(0.75, 100, 50) == BOUND_MEMORY);
        CHECK(bound_of(16.0, 100, 50) == BOUND_COMPUTE);
        CHECK(bound_of(2.0, 100, 50) == BOUND_COMPUTE);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
