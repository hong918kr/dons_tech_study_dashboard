/* ARCH-A11 demo_roofline — Apple M6 실측: 피크 FLOP/s, STREAM 대역폭, 커널 GFLOP/s → 루프라인 (CHECK 없음)
 *
 *   freq            의존 add 체인 (1 사이클 지연 가정) 으로 클럭 어림 → FMA 지연·파이프 수 추정에 쓴다
 *   peak            1 스레드, NEON fmla.4s 누산기 수 1..24 스윕 (fp32)
 *   peakmt          p = 1,2,4,6,8,10,12 스레드 × peak_fma_16 — 합계 GFLOP/s, 스레드별 속도·코어 번호
 *   stream [N]      copy/scale/add/triad (double), 배열 3개 × N (기본 2^26 → 3 × 512 MB = 1.5 GB), p = 1,2,4,8,12
 *   hier            triad 1 스레드, 작업 집합 48 KB / 1.5 MB / 6 MB / 24 MB / 384 MB (캐시 계층별 대역폭)
 *   kernels         fp32 vadd / matmul naive·tiled / conv1d naive·blocked (K=3, 64), 1 스레드
 *   kernelsmt [P]   vadd, conv1d blocked (K=3, 64) 를 P 스레드 (기본 12) 로
 *   (peakmt / stream / kernelsmt 뒤에 dyn 을 붙이면 정적 등분 대신 동적 분할: chunk 를 atomic 카운터로 나눠 가짐)
 *   qos             1 스레드를 QoS USER_INTERACTIVE 와 BACKGROUND (macOS 가 E 코어로 보내는 클래스) 로 각각 띄워
 *                   add 체인 속도 · peak_fma_16 · triad 384 MB 를 잰다 → 코어 종류별 속도 (고정이 아니라 스케줄러 힌트)
 *   percpu [P]      P 스레드 (기본 12) 가 동시에 add 체인 (2 ms 조각 × 300). 조각 앞뒤 코어 번호가 같은 조각만 모아
 *                   코어 번호별 "add/ns" (≈ 그 코어의 클럭) 와 peak_fma_16 GFLOP/s 중앙값 → 코어 종류별 차이를 직접 본다
 *   blas            Accelerate cblas_sgemm (USE_ACCELERATE 로 빌드했을 때만) — 라벨 붙인 "천장"
 *
 * 규칙: 5회 중앙값 (min–max 함께), 실행 직후 pthread_cpu_number_np 코어 번호 (0–5 E, 6–7 Super, 8–11 P 는
 * ARCH-M08 의 추정 매핑, Apple 공식 아님). QoS USER_INTERACTIVE 요청 = 힌트일 뿐 고정 아님. 매 모드 시작/끝에 load average.
 *
 * build: cc -std=c11 -O2 -pthread demo_roofline.c -o roof
 *        cc -std=c11 -O2 -pthread -DUSE_ACCELERATE -DACCELERATE_NEW_LAPACK demo_roofline.c -framework Accelerate -o roof_blas
 *        cc -std=c11 -O2 -ffast-math -pthread demo_roofline.c -o roof_fm      (conv1d naive 가 벡터화되는지 비교)
 */
#define main solution_main
#include "solution.c"
#undef main
#include <pthread/qos.h>
#ifdef USE_ACCELERATE
#include <Accelerate/Accelerate.h>
#endif

static void loadavg(const char *tag) {
    double l[3] = { 0 };
    getloadavg(l, 3);
    printf("[%s] load average %.2f %.2f %.2f\n", tag, l[0], l[1], l[2]);
    fflush(stdout);
}
static int cmpd(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return (x > y) - (x < y); }
typedef struct { double med, lo, hi; } stat3_t;
static stat3_t stat5(const double *v, int n) {
    double s[16];
    memcpy(s, v, sizeof(double) * n);
    qsort(s, n, sizeof(double), cmpd);
    return (stat3_t){ s[n / 2], s[0], s[n - 1] };
}
static int cur_cpu(void) { size_t c = 0; pthread_cpu_number_np(&c); return (int)c; }
static void *xalloc(size_t bytes) {
    void *p = aligned_alloc(16384, (bytes + 16383) & ~(size_t)16383);
    if (!p) { fprintf(stderr, "alloc failed\n"); exit(2); }
    return p;
}

/* ---- 스레드 풀: 스레드 0 = 호출 스레드. 세대 번호로 출발, done 카운터로 끝 ---- */
typedef void (*task_fn)(int t, int p, void *arg);
static struct {
    int p;
    task_fn fn;
    void *arg;
    _Atomic unsigned gen;
    _Atomic int done, quit;
    unsigned start_gen;            /* 새 스레드는 이 세대부터 기다린다 (지난 세대의 일을 다시 하지 않게) */
    pthread_t th[MAXT];
    int cpu[MAXT];
} pool;
static void *pool_thread(void *v) {
    int t = (int)(intptr_t)v;
    unsigned seen = pool.start_gen;
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
    for (;;) {
        unsigned g;
        while ((g = atomic_load_explicit(&pool.gen, memory_order_acquire)) == seen) {
            if (atomic_load_explicit(&pool.quit, memory_order_relaxed)) return NULL;
            __asm__ volatile("yield");
        }
        seen = g;
        pool.fn(t, pool.p, pool.arg);
        pool.cpu[t] = cur_cpu();
        atomic_fetch_add_explicit(&pool.done, 1, memory_order_release);
    }
}
static void pool_start(int p) {
    pool.p = p;
    atomic_store(&pool.quit, 0);
    pool.start_gen = atomic_load(&pool.gen);
    for (int t = 1; t < p; t++) pthread_create(&pool.th[t], NULL, pool_thread, (void *)(intptr_t)t);
}
static double pool_run(task_fn fn, void *arg) {     /* 반환: 출발 → 마지막 스레드 끝 (초) */
    pool.fn = fn;
    pool.arg = arg;
    atomic_store_explicit(&pool.done, 0, memory_order_relaxed);
    uint64_t t0 = now_ns();
    atomic_fetch_add_explicit(&pool.gen, 1, memory_order_release);
    fn(0, pool.p, arg);
    pool.cpu[0] = cur_cpu();
    while (atomic_load_explicit(&pool.done, memory_order_acquire) != pool.p - 1) __asm__ volatile("yield");
    return (double)(now_ns() - t0) * 1e-9;
}
static void pool_stop(void) {
    atomic_store(&pool.quit, 1);
    for (int t = 1; t < pool.p; t++) pthread_join(pool.th[t], NULL);
}

/* 정적 등분 (g_dyn = 0) 또는 동적 분할 (g_dyn = 1, g_chunk 개씩) 으로 [0, n) 을 돈다. g_items[t] = 스레드 t 가 한 양 */
static int g_dyn;
static size_t g_chunk = (size_t)1 << 16;
static _Atomic size_t g_next;
static size_t g_items[MAXT];
typedef struct { int t, p; size_t n; bool used; } range_it;
static bool range_next(range_it *it, size_t *lo, size_t *hi) {
    if (!g_dyn) {
        if (it->used) return false;
        it->used = true;
        long l, h;
        static_range((long)it->n, it->p, it->t, &l, &h);
        *lo = (size_t)l; *hi = (size_t)h;
    } else {
        size_t b = atomic_fetch_add_explicit(&g_next, g_chunk, memory_order_relaxed);
        if (b >= it->n) return false;
        *lo = b; *hi = b + g_chunk < it->n ? b + g_chunk : it->n;
    }
    g_items[it->t] += *hi - *lo;
    return true;
}
static double pool_run_r(task_fn fn, void *arg) {    /* 분할 상태를 비우고 pool_run */
    atomic_store(&g_next, 0);
    memset(g_items, 0, sizeof g_items);
    return pool_run(fn, arg);
}
static void print_share(void) {                     /* 마지막 실행의 스레드별 몫 (%), 코어 번호 */
    size_t tot = 0;
    for (int t = 0; t < pool.p; t++) tot += g_items[t];
    printf("    share%% by cpu:");
    for (int t = 0; t < pool.p; t++) printf(" %d:%.1f", pool.cpu[t], 100.0 * (double)g_items[t] / (double)tot);
    printf("\n");
}

/* ---- freq: 의존 add 100 개 × iters ---- */
static double est_freq_ghz(void) {
    double v[5];
    for (int r = 0; r < 5; r++) {
        uint64_t x = 0, t0 = now_ns();
        const long iters = 20000000;
        for (long i = 0; i < iters; i++)
            __asm__ volatile(".rept 100\n\tadd %0, %0, #1\n\t.endr" : "+r"(x));
        v[r] = 100.0 * iters / (double)(now_ns() - t0);
        if (x != 100ull * iters) printf("?? add chain\n");
    }
    stat3_t s = stat5(v, 5);
    printf("dependent-add rate (≈ clock if add latency = 1 cycle): median %.2f G/s (min %.2f max %.2f) cpu %d\n", s.med, s.lo, s.hi, cur_cpu());
    return s.med;
}

typedef float (*peak_fn)(long, float, float);
static const int nacc_list[] = { 1, 2, 4, 8, 12, 16, 24 };
static const peak_fn peak_list[] = { peak_fma_1, peak_fma_2, peak_fma_4, peak_fma_8, peak_fma_12, peak_fma_16, peak_fma_24 };
static volatile float g_a = 0.999999f, g_b = 1e-7f;
static volatile float g_sink;

static void mode_peak(void) {
    double ghz = est_freq_ghz();
    const double total_fma = 4e8;
    for (int k = 0; k < 7; k++) {
        int nacc = nacc_list[k];
        long iters = (long)(total_fma / nacc);
        double v[5];
        int cpus[5];
        for (int r = 0; r < 5; r++) {
            uint64_t t0 = now_ns();
            g_sink = peak_list[k](iters, g_a, g_b);
            double sec = (double)(now_ns() - t0) * 1e-9;
            cpus[r] = cur_cpu();
            v[r] = fma_flops((double)iters * nacc, 4) / sec * 1e-9;
        }
        stat3_t s = stat5(v, 5);
        double fma_per_ns = s.med / 8.0;
        printf("NACC=%2d  GFLOP/s median %7.2f (min %7.2f max %7.2f)  fmla/cycle %.2f  cpu %d %d %d %d %d\n", nacc, s.med, s.lo, s.hi,
               fma_per_ns / ghz, cpus[0], cpus[1], cpus[2], cpus[3], cpus[4]);
    }
}

typedef struct { peak_fn fn; long iters; double rate[MAXT]; } peakarg_t;
/* 일 = p × 25 조각, 조각 = peak_fma_16(10^6 반복) = 1.28e8 FLOP. 정적이면 스레드당 25 조각 */
static void task_peak(int t, int p, void *v) {
    peakarg_t *a = v;
    uint64_t t0 = now_ns();
    range_it it = { t, p, (size_t)p * 25, false };
    size_t lo, hi, done = 0;
    while (range_next(&it, &lo, &hi))
        for (size_t c = lo; c < hi; c++) {
            float r = a->fn(a->iters, g_a, g_b);
            if (r == 12345.0f) g_sink = r;
            done++;
        }
    a->rate[t] = 1e-9 * 16.0 * 8.0 * (double)a->iters * (double)done / ((double)(now_ns() - t0) * 1e-9);
}
static void mode_peakmt(void) {
    static const int ps[] = { 1, 2, 4, 6, 8, 10, 12 };
    for (int q = 0; q < 7; q++) {
        int p = ps[q];
        pool_start(p);
        peakarg_t a = { .fn = peak_fma_16, .iters = 1000000 };    /* 조각 하나 = 1.6e7 fmla */
        g_chunk = 1;
        double v[5];
        double rates[MAXT] = { 0 };
        int cpus[MAXT] = { 0 };
        for (int r = 0; r < 5; r++) {
            double sec = pool_run_r(task_peak, &a);
            v[r] = 16.0 * 8.0 * (double)a.iters * 25.0 * p / sec * 1e-9;
            if (r == 4) { memcpy(rates, a.rate, sizeof rates); memcpy(cpus, pool.cpu, sizeof cpus); }
        }
        pool_stop();
        stat3_t s = stat5(v, 5);
        printf("p=%2d %s total GFLOP/s median %7.1f (min %7.1f max %7.1f)  run5 cpu:GFLOP/s(chunks)", p, g_dyn ? "dyn" : "static", s.med, s.lo, s.hi);
        for (int t = 0; t < p; t++) printf(" %d:%.0f(%zu)", cpus[t], rates[t], g_items[t]);
        printf("\n");
        fflush(stdout);
    }
}

/* ---- STREAM ---- */
typedef struct { stream_k k; double *a, *b, *c; size_t n; int reps; } starg_t;
static void task_stream(int t, int p, void *v) {
    starg_t *s = v;
    range_it it = { t, p, s->n, false };
    size_t lo, hi;
    while (range_next(&it, &lo, &hi))
        for (int r = 0; r < s->reps; r++) stream_kernel(s->k, s->a, s->b, s->c, 3.0, lo, hi);
}
static void stream_init(double *a, double *b, double *c, size_t n) {
    for (size_t i = 0; i < n; i++) { a[i] = 1.0; b[i] = 2.0; c[i] = 0.0; }
}
static void mode_stream(size_t n) {
    double *a = xalloc(n * 8), *b = xalloc(n * 8), *c = xalloc(n * 8);
    stream_init(a, b, c, n);
    printf("STREAM-like, double, n = %zu per array, total %.0f MB, split %s; GB/s = STREAM bytes (no RFO) / time\n", n,
           3.0 * n * 8 / 1048576, g_dyn ? "dynamic (chunk 65536 elem = 512 KB)" : "static");
    static const int ps[] = { 1, 2, 4, 8, 12 };
    for (int q = 0; q < 5; q++) {
        int p = ps[q];
        pool_start(p);
        printf("p=%2d", p);
        for (int k = 0; k < 4; k++) {
            starg_t s = { (stream_k)k, a, b, c, n, 1 };             /* reps 1: 조각을 반복하면 L2 에서 다시 읽게 된다 */
            pool_run_r(task_stream, &s);                            /* 워밍업 1회 */
            double v[5];
            for (int r = 0; r < 5; r++) v[r] = stream_bytes((stream_k)k, n, 8, false) / pool_run_r(task_stream, &s) * 1e-9;
            stat3_t st = stat5(v, 5);
            printf("  %s %6.1f (%5.1f-%5.1f)", (const char *[]){ "copy", "scale", "add", "triad" }[k], st.med, st.lo, st.hi);
        }
        printf("\n");
        if (p > 1) print_share();
        fflush(stdout);
        pool_stop();
    }
    free(a); free(b); free(c);
}
static void mode_hier(void) {
    static const size_t ws[] = { 48u << 10, 1536u << 10, 6u << 20, 24u << 20, 384u << 20 };   /* 3 배열 합 */
    pool_start(1);
    for (int q = 0; q < 5; q++) {
        size_t n = ws[q] / 24;
        double *a = xalloc(n * 8), *b = xalloc(n * 8), *c = xalloc(n * 8);
        stream_init(a, b, c, n);
        int reps = (int)((1u << 30) / ws[q]);                       /* 한 번 측정에 약 1 GB 이상 */
        if (reps < 1) reps = 1;
        starg_t s = { ST_TRIAD, a, b, c, n, reps };
        pool_run_r(task_stream, &s);
        double v[5];
        for (int r = 0; r < 5; r++) v[r] = stream_bytes(ST_TRIAD, n, 8, false) * reps / pool_run_r(task_stream, &s) * 1e-9;
        stat3_t st = stat5(v, 5);
        printf("triad 1 thread, working set %8.2f MB: %6.1f GB/s (min %5.1f max %5.1f)  %.2f GFLOP/s  cpu %d\n",
               ws[q] / 1048576.0, st.med, st.lo, st.hi, st.med / 12.0, pool.cpu[0]);
        free(a); free(b); free(c);
    }
    pool_stop();
}

/* ---- 커널 ---- */
typedef struct { int kind; size_t n; int K, T; float *x, *y, *z, *w; } karg_t;   /* kind 0 vadd, 1 naive mm, 2 tiled, 3 conv naive, 4 conv blocked */
static void task_kernel(int t, int p, void *v) {
    karg_t *a = v;
    if (a->kind == 1) { mm_naive((int)a->n, a->x, a->y, a->z); return; }
    if (a->kind == 2) { mm_tiled((int)a->n, a->T, a->x, a->y, a->z); return; }
    range_it it = { t, p, a->n, false };
    size_t lo, hi;
    while (range_next(&it, &lo, &hi)) {
        switch (a->kind) {
        case 0: vadd(hi - lo, a->x + lo, a->y + lo, a->z + lo); break;
        case 3: conv1d_naive(hi - lo, a->K, a->x + lo, a->w, a->y + lo); break;
        case 4: conv1d_blocked(hi - lo, a->K, a->x + lo, a->w, a->y + lo); break;
        }
    }
}
static void run_kernel(const char *name, karg_t *a, cost_t c) {
    double v[5];
    pool_run_r(task_kernel, a);                                      /* 워밍업 */
    for (int r = 0; r < 5; r++) v[r] = c.flops / pool_run_r(task_kernel, a) * 1e-9;
    stat3_t s = stat5(v, 5);
    printf("%-22s AI(compulsory) %8.3f  GFLOP/s median %7.2f (min %7.2f max %7.2f)  %s", name, ai_of(c), s.med, s.lo, s.hi,
           pool.p == 1 ? "cpu" : (g_dyn ? "dyn" : "static"));
    if (pool.p == 1) printf(" %d", pool.cpu[0]);
    printf("\n");
    if (pool.p > 1) print_share();
    fflush(stdout);
}
static void fill(float *p, size_t n, uint32_t seed) {
    for (size_t i = 0; i < n; i++) { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; p[i] = (float)(seed % 2001u) / 1000.0f - 1.0f; }
}
static void mode_kernels(int p) {
    pool_start(p);
    {
        size_t n = (size_t)1 << 25;                                  /* 3 × 128 MB */
        float *a = xalloc(n * 4), *b = xalloc(n * 4), *c = xalloc(n * 4);
        fill(a, n, 1); fill(b, n, 2); memset(c, 0, n * 4);
        karg_t k = { .kind = 0, .n = n, .x = a, .y = b, .z = c };
        run_kernel("vadd n=2^25", &k, cost_vadd(n, false));
        free(a); free(b); free(c);
    }
    if (p == 1) {
        int n = 1024;
        float *A = xalloc((size_t)n * n * 4), *B = xalloc((size_t)n * n * 4), *C = xalloc((size_t)n * n * 4);
        fill(A, (size_t)n * n, 3); fill(B, (size_t)n * n, 4);
        karg_t k = { .kind = 1, .n = (size_t)n, .x = A, .y = B, .z = C };
        run_kernel("matmul naive n=1024", &k, cost_matmul(n, false));
        static const int Ts[] = { 32, 64, 128, 256 };
        for (int q = 0; q < 4; q++) {
            char nm[64];
            snprintf(nm, sizeof nm, "matmul tiled T=%d", Ts[q]);
            k.kind = 2; k.T = Ts[q];
            run_kernel(nm, &k, cost_matmul(n, false));
        }
        free(A); free(B); free(C);
    }
    {
        size_t n = (size_t)1 << 24;
        float *x = xalloc((n + 64) * 4), *y = xalloc(n * 4), w[64];
        fill(x, n + 64, 5); fill(w, 64, 6);
        static const int Ks[] = { 3, 64 };
        for (int q = 0; q < 2; q++) {
            char nm[64];
            karg_t k = { .kind = 3, .n = n, .K = Ks[q], .x = x, .y = y, .w = w };
            if (p == 1) {
                snprintf(nm, sizeof nm, "conv1d naive K=%d", Ks[q]);
                run_kernel(nm, &k, cost_conv1d(n, Ks[q], false));
            }
            k.kind = 4;
            snprintf(nm, sizeof nm, "conv1d blocked K=%d", Ks[q]);
            run_kernel(nm, &k, cost_conv1d(n, Ks[q], false));
        }
        free(x); free(y);
    }
    pool_stop();
}

/* ---- percpu: 코어 번호별 의존 add 속도 ---- */
#define PC_SLICES 300
typedef struct { double rate[PC_SLICES], gf[PC_SLICES], gf24[PC_SLICES]; int cpu[PC_SLICES], cpuf[PC_SLICES], cpu24[PC_SLICES]; } pcres_t;
static pcres_t g_pc[MAXT];
static void task_percpu(int t, int p, void *v) {
    (void)p; (void)v;
    for (int k = 0; k < PC_SLICES; k++) {
        int c0 = cur_cpu();
        uint64_t x = 0, t0 = now_ns();
        for (long i = 0; i < 80000; i++) __asm__ volatile(".rept 100\n\tadd %0, %0, #1\n\t.endr" : "+r"(x));
        double ns = (double)(now_ns() - t0);
        int c1 = cur_cpu();
        g_pc[t].cpu[k] = c0 == c1 ? c0 : -1;
        g_pc[t].rate[k] = 8e6 / ns;
        t0 = now_ns();                                   /* 같은 조각 길이로 FMA 피크: 10^5 × 16 fmla.4s = 1.28e7 FLOP */
        g_sink = peak_fma_16(100000, g_a, g_b);
        ns = (double)(now_ns() - t0);
        c0 = cur_cpu();
        g_pc[t].cpuf[k] = c0 == c1 ? c0 : -1;
        g_pc[t].gf[k] = 1.28e7 / ns;
        t0 = now_ns();                                   /* 누산기 24 개: 10^5 × 24 fmla.4s = 1.92e7 FLOP */
        g_sink = peak_fma_24(100000, g_a, g_b);
        ns = (double)(now_ns() - t0);
        c1 = cur_cpu();
        g_pc[t].cpu24[k] = c0 == c1 ? c0 : -1;
        g_pc[t].gf24[k] = 1.92e7 / ns;
    }
}
static void mode_percpu(int P) {
    if (P < 1 || P > 12) P = 12;
    pool_start(P);
    pool_run_r(task_percpu, NULL);
    pool_stop();
    static double v[MAXT][MAXT * PC_SLICES], g[MAXT][MAXT * PC_SLICES], g24[MAXT][MAXT * PC_SLICES];
    int cnt[MAXT] = { 0 }, cg[MAXT] = { 0 }, cg24[MAXT] = { 0 }, migr = 0;
    for (int t = 0; t < P; t++)
        for (int k = 0; k < PC_SLICES; k++) {
            int c = g_pc[t].cpu[k], cf = g_pc[t].cpuf[k];
            if (cf >= 0 && cf < MAXT) g[cf][cg[cf]++] = g_pc[t].gf[k];
            int c24 = g_pc[t].cpu24[k];
            if (c24 >= 0 && c24 < MAXT) g24[c24][cg24[c24]++] = g_pc[t].gf24[k];
            if (c < 0 || c >= MAXT) { migr++; continue; }
            v[c][cnt[c]++] = g_pc[t].rate[k];
        }
    printf("%d threads x %d slices; slices with migration (dropped): %d\n", P, PC_SLICES, migr);
    for (int c = 0; c < 12; c++) {
        if (!cnt[c]) { printf("  cpu %2d: no clean slice\n", c); continue; }
        qsort(v[c], cnt[c], sizeof(double), cmpd);
        printf("  cpu %2d: add/ns median %.2f  p90 %.2f  (slices %d)", c, v[c][cnt[c] / 2], v[c][cnt[c] * 9 / 10], cnt[c]);
        if (cg[c]) { qsort(g[c], cg[c], sizeof(double), cmpd); printf("   fma16 GFLOP/s median %.1f (slices %d)", g[c][cg[c] / 2], cg[c]); }
        if (cg24[c]) { qsort(g24[c], cg24[c], sizeof(double), cmpd); printf("  fma24 %.1f (%d)", g24[c][cg24[c] / 2], cg24[c]); }
        printf("\n");
    }
}

/* ---- qos: 코어 종류별 1 스레드 속도 ---- */
typedef struct { qos_class_t q; double add_g, peak, triad; int cpu0, cpu1; } qosres_t;
static void *qos_body(void *v) {
    qosres_t *r = v;
    double va[5], vp[5], vt[5];
    size_t n = (size_t)1 << 24;
    double *a = xalloc(n * 8), *b = xalloc(n * 8), *c = xalloc(n * 8);
    stream_init(a, b, c, n);
    r->cpu0 = cur_cpu();
    for (int k = 0; k < 5; k++) {
        uint64_t x = 0, t0 = now_ns();
        for (long i = 0; i < 5000000; i++) __asm__ volatile(".rept 100\n\tadd %0, %0, #1\n\t.endr" : "+r"(x));
        va[k] = 100.0 * 5000000 / (double)(now_ns() - t0);
        t0 = now_ns();
        g_sink = peak_fma_16(10000000, g_a, g_b);
        vp[k] = 16.0 * 8.0 * 1e7 / (double)(now_ns() - t0);
        t0 = now_ns();
        stream_kernel(ST_TRIAD, a, b, c, 3.0, 0, n);
        vt[k] = stream_bytes(ST_TRIAD, n, 8, false) / (double)(now_ns() - t0);
    }
    r->cpu1 = cur_cpu();
    g_sink = (float)(a[n / 2] + a[n - 1]);         /* 결과를 읽어야 triad 가 죽은 코드로 지워지지 않는다 */
    r->add_g = stat5(va, 5).med; r->peak = stat5(vp, 5).med; r->triad = stat5(vt, 5).med;
    free(a); free(b); free(c);
    return NULL;
}
static void mode_qos(void) {
    static const qos_class_t qs[] = { QOS_CLASS_USER_INTERACTIVE, QOS_CLASS_BACKGROUND, QOS_CLASS_USER_INTERACTIVE, QOS_CLASS_BACKGROUND };
    static const char *const qn[] = { "USER_INTERACTIVE", "BACKGROUND", "USER_INTERACTIVE", "BACKGROUND" };
    for (int i = 0; i < 4; i++) {
        pthread_attr_t at;
        pthread_attr_init(&at);
        pthread_attr_set_qos_class_np(&at, qs[i], 0);
        qosres_t r = { .q = qs[i] };
        pthread_t th;
        pthread_create(&th, &at, qos_body, &r);
        pthread_join(th, NULL);
        printf("QoS %-16s  add-chain %.2f G/s  peak_fma_16 %6.1f GFLOP/s  triad(384 MB) %6.1f GB/s  cpu start %d end %d\n",
               qn[i], r.add_g, r.peak, r.triad, r.cpu0, r.cpu1);
        fflush(stdout);
    }
}

static void mode_blas(void) {
#ifdef USE_ACCELERATE
    static const int ns[] = { 1024, 2048 };
    for (int q = 0; q < 2; q++) {
        int n = ns[q];
        float *A = xalloc((size_t)n * n * 4), *B = xalloc((size_t)n * n * 4), *C = xalloc((size_t)n * n * 4);
        fill(A, (size_t)n * n, 3); fill(B, (size_t)n * n, 4);
        double v[5];
        cblas_sgemm(CblasRowMajor, CblasNoTrans, CblasNoTrans, n, n, n, 1.0f, A, n, B, n, 0.0f, C, n);
        for (int r = 0; r < 5; r++) {
            uint64_t t0 = now_ns();
            cblas_sgemm(CblasRowMajor, CblasNoTrans, CblasNoTrans, n, n, n, 1.0f, A, n, B, n, 0.0f, C, n);
            v[r] = 2.0 * n * (double)n * n / ((double)(now_ns() - t0) * 1e-9) * 1e-9;
        }
        stat3_t s = stat5(v, 5);
        printf("Accelerate cblas_sgemm n=%d  GFLOP/s median %8.1f (min %8.1f max %8.1f)\n", n, s.med, s.lo, s.hi);
        free(A); free(B); free(C);
    }
#else
    printf("build with -DUSE_ACCELERATE -DACCELERATE_NEW_LAPACK -framework Accelerate\n");
#endif
}

int main(int argc, char **argv) {
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
    const char *m = argc > 1 ? argv[1] : "peak";
    g_dyn = argc > 2 && !strcmp(argv[argc - 1], "dyn");
    printf("%s\n", __VERSION__);
    loadavg("start");
    if (!strcmp(m, "freq")) est_freq_ghz();
    else if (!strcmp(m, "peak")) mode_peak();
    else if (!strcmp(m, "peakmt")) mode_peakmt();
    else if (!strcmp(m, "stream")) mode_stream((argc > 2 && atol(argv[2]) > 0) ? (size_t)atol(argv[2]) : ((size_t)1 << 26));
    else if (!strcmp(m, "hier")) mode_hier();
    else if (!strcmp(m, "kernels")) mode_kernels(1);
    else if (!strcmp(m, "kernelsmt")) mode_kernels((argc > 2 && atoi(argv[2]) > 0) ? atoi(argv[2]) : 12);
    else if (!strcmp(m, "blas")) mode_blas();
    else if (!strcmp(m, "qos")) mode_qos();
    else if (!strcmp(m, "percpu")) mode_percpu(argc > 2 ? atoi(argv[2]) : 12);
    else { printf("usage: %s freq|peak|peakmt [dyn]|stream [N] [dyn]|hier|kernels|kernelsmt [P] [dyn]|qos|percpu [P]|blas\n", argv[0]); return 2; }
    loadavg("end");
    return 0;
}
