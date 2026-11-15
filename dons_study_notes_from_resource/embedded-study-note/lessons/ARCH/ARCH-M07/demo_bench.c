/* ARCH-M07 demo_bench — Apple M6 실측: 행렬 곱 (double) GFLOP/s = 2 n^3 / 시간
 *
 *   main [T]     n = 512, 1024, 2048 × {ijk, ikj, jki, tiled(T, 기본 64)}
 *   tile N       n = N 에서 타일 크기 8..512 스윕
 *   pad N        ld = N (2의 거듭제곱) vs ld = N+16 (행 간격 = 홀수 개 128 B 라인) — ijk / jki / tiled
 *   blas         Accelerate cblas_dgemm (USE_ACCELERATE 로 빌드했을 때만) — "천장" 참고용, AMX/SME 사용
 *
 * 측정 규칙: 실행마다 C 를 새로 계산, 5회 중앙값. 한 번이 4 초를 넘으면 그 설정은 1회만 (runs 열에 표시).
 * QoS USER_INTERACTIVE 를 요청 (힌트일 뿐, macOS 는 코어 고정 불가). 실행 직후 pthread_cpu_number_np 로
 * 코어 번호를 찍는다 — 0–5 E / 6–7 Super / 8–11 P (LLAT-M05 · ARCH-M08 의 추정 매핑, Apple 공식 아님).
 *
 * build: cc -std=c11 -O2 demo_bench.c -o bench
 *        cc -std=c11 -O2 -fno-vectorize -fno-slp-vectorize demo_bench.c -o bench_novec
 *        cc -std=c11 -O2 -DUSE_ACCELERATE -DACCELERATE_NEW_LAPACK demo_bench.c -framework Accelerate -o bench_blas
 */
#define _DARWIN_C_SOURCE
#include <pthread.h>
#include <pthread/qos.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef USE_ACCELERATE
#include <Accelerate/Accelerate.h>
#endif

static int imin(int a, int b) { return a < b ? a : b; }

__attribute__((noinline)) static void mm_ijk(int n, int ld, const double *restrict A, const double *restrict B, double *restrict C) {
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++) {
            double s = 0.0;
            for (int k = 0; k < n; k++) s += A[i * ld + k] * B[k * ld + j];
            C[i * ld + j] = s;
        }
}
__attribute__((noinline)) static void mm_ikj(int n, int ld, const double *restrict A, const double *restrict B, double *restrict C) {
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) C[i * ld + j] = 0.0;
        for (int k = 0; k < n; k++) {
            double r = A[i * ld + k];
            for (int j = 0; j < n; j++) C[i * ld + j] += r * B[k * ld + j];
        }
    }
}
__attribute__((noinline)) static void mm_jki(int n, int ld, const double *restrict A, const double *restrict B, double *restrict C) {
    for (int j = 0; j < n; j++) {
        for (int i = 0; i < n; i++) C[i * ld + j] = 0.0;
        for (int k = 0; k < n; k++) {
            double r = B[k * ld + j];
            for (int i = 0; i < n; i++) C[i * ld + j] += A[i * ld + k] * r;
        }
    }
}
__attribute__((noinline)) static void mm_tiled(int n, int ld, int T, const double *restrict A, const double *restrict B, double *restrict C) {
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++) C[i * ld + j] = 0.0;
    for (int ii = 0; ii < n; ii += T)
        for (int jj = 0; jj < n; jj += T)
            for (int kk = 0; kk < n; kk += T) {
                int ie = imin(ii + T, n), je = imin(jj + T, n), ke = imin(kk + T, n);
                for (int i = ii; i < ie; i++)
                    for (int k = kk; k < ke; k++) {
                        double r = A[i * ld + k];
                        for (int j = jj; j < je; j++) C[i * ld + j] += r * B[k * ld + j];
                    }
            }
}

enum { V_IJK, V_IKJ, V_JKI, V_TILED, V_BLAS };
static const char *const vname[] = { "ijk", "ikj", "jki", "tiled", "blas" };

static uint64_t now_ns(void) { return clock_gettime_nsec_np(CLOCK_UPTIME_RAW); }

static void *xalloc(size_t bytes) {
    size_t sz = (bytes + 16383) & ~(size_t)16383;            /* 16 KB 페이지 정렬 */
    void *p = aligned_alloc(16384, sz);
    if (!p) { fprintf(stderr, "alloc failed\n"); exit(2); }
    return p;
}

static double *gA, *gB, *gC, *gRef;
static int g_refn = -1, g_refld = -1;

static void run_variant(int v, int n, int ld, int T) {
    switch (v) {
    case V_IJK: mm_ijk(n, ld, gA, gB, gC); break;
    case V_IKJ: mm_ikj(n, ld, gA, gB, gC); break;
    case V_JKI: mm_jki(n, ld, gA, gB, gC); break;
    case V_TILED: mm_tiled(n, ld, T, gA, gB, gC); break;
#ifdef USE_ACCELERATE
    case V_BLAS:
        cblas_dgemm(CblasRowMajor, CblasNoTrans, CblasNoTrans, n, n, n, 1.0, gA, ld, gB, ld, 0.0, gC, ld);
        break;
#endif
    default: break;
    }
}

static void setup(int n, int ld) {
    size_t bytes = (size_t)n * (size_t)ld * sizeof(double);
    free(gA); free(gB); free(gC); free(gRef);
    gA = xalloc(bytes); gB = xalloc(bytes); gC = xalloc(bytes); gRef = xalloc(bytes);
    uint32_t s = 12345u;
    for (size_t i = 0; i < (size_t)n * ld; i++) {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5; gA[i] = (double)(s % 2001) / 1000.0 - 1.0;
        s ^= s << 13; s ^= s >> 17; s ^= s << 5; gB[i] = (double)(s % 2001) / 1000.0 - 1.0;
    }
    /* 기준 = ikj (빠르고, ijk 와 같은 k 순서로 더한다) */
    mm_ikj(n, ld, gA, gB, gRef);
    g_refn = n; g_refld = ld;
}

static double max_diff(int n, int ld) {
    double m = 0;
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++) {
            double d = gC[i * ld + j] - gRef[i * ld + j];
            if (d < 0) d = -d;
            if (d > m) m = d;
        }
    return m;
}

static int cmp_d(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

/* 5회 중앙값 (첫 실행이 4 초 넘으면 1회). GFLOP/s 와 실행 횟수, 본 코어 번호를 찍는다. */
static void measure(int v, int n, int ld, int T) {
    double gf[5];
    int runs = 0, cpus[5];
    for (int r = 0; r < 5; r++) {
        memset(gC, 0, (size_t)n * ld * sizeof(double));
        uint64_t t0 = now_ns();
        run_variant(v, n, ld, T);
        uint64_t t1 = now_ns();
        size_t cpu = 0;
        pthread_cpu_number_np(&cpu);
        cpus[runs] = (int)cpu;
        double sec = (double)(t1 - t0) * 1e-9;
        gf[runs++] = 2.0 * n * (double)n * n / sec * 1e-9;
        if (r == 0 && sec > 4.0) break;
    }
    double sorted[5];
    memcpy(sorted, gf, sizeof(double) * runs);
    qsort(sorted, runs, sizeof(double), cmp_d);
    double med = sorted[runs / 2];
    printf("n=%4d ld=%4d %-5s", n, ld, vname[v]);
    if (v == V_TILED) printf(" T=%-3d", T); else printf("      ");
    printf(" GFLOP/s median %7.3f (min %7.3f max %7.3f) runs %d  time/run %8.3f s  maxdiff %.1e  cpu",
           med, sorted[0], sorted[runs - 1], runs, 2.0 * n * (double)n * n / (med * 1e9),
           max_diff(n, ld));
    for (int r = 0; r < runs; r++) printf(" %d", cpus[r]);
    printf("\n");
    fflush(stdout);
}

int main(int argc, char **argv) {
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
    const char *mode = argc > 1 ? argv[1] : "main";
    if (strcmp(mode, "main") == 0) {
        int T = argc > 2 ? atoi(argv[2]) : 64;
        static const int ns[] = { 512, 1024, 2048 };
        for (int t = 0; t < 3; t++) {
            setup(ns[t], ns[t]);
            for (int v = V_IJK; v <= V_TILED; v++) measure(v, ns[t], ns[t], T);
        }
    } else if (strcmp(mode, "tile") == 0) {
        int n = argc > 2 ? atoi(argv[2]) : 1024;
        static const int Ts[] = { 8, 16, 32, 48, 64, 96, 128, 256, 512 };
        setup(n, n);
        for (int t = 0; t < 9; t++) measure(V_TILED, n, n, Ts[t]);
    } else if (strcmp(mode, "pad") == 0) {
        int n = argc > 2 ? atoi(argv[2]) : 1024;
        for (int p = 0; p < 2; p++) {
            int ld = p ? n + 16 : n;
            setup(n, ld);
            measure(V_IJK, n, ld, 0);
            measure(V_JKI, n, ld, 0);
            measure(V_TILED, n, ld, 64);
        }
    } else if (strcmp(mode, "blas") == 0) {
#ifdef USE_ACCELERATE
        static const int ns[] = { 512, 1024, 2048 };
        for (int t = 0; t < 3; t++) { setup(ns[t], ns[t]); measure(V_BLAS, ns[t], ns[t], 0); }
#else
        printf("build with -DUSE_ACCELERATE -framework Accelerate\n");
#endif
    } else {
        printf("usage: %s main [T] | tile N | pad N | blas\n", argv[0]);
        return 2;
    }
    return 0;
}
