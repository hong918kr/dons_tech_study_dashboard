/* GPGPU-M03 demo_bench — 히스토그램 버전별 처리량 (CHECK 없음, 결과 일치만 확인해서 표시)
 *
 *   ./bench main   버전(v0 읽기만 · v1 · v2 · v3 · v3b · v3c · CPU 1스레드) x 입력(uniform · zipf · same · lane32)
 *                  N = 2^24, 2^26 — GPU 시간 = commandBuffer.GPUEndTime - GPUStartTime, 11회 중앙값
 *   ./bench size   N = 2^10 .. 2^24 에서 v1 vs v2 (작은 입력에서는 privatization 이 손해)
 *   ./bench ntg    threadgroup 수 16 .. 16384 에서 v1 vs v2 (병합 비용이 커지는 지점)
 *   ./bench place  히스토그램 출력 버퍼(1 KB)를 12번 새로 만들어 v1 / v2 — 버퍼마다 다른가, 실행마다 다른가
 *   ./bench state  v0 / v1 을 번갈아 15번 (2초 쉰 뒤, dispatch 사이 CPU 8 MB memset) + 3초 연속 dispatch 뒤 15번.
 *                  이 맥에서 v1 은 0.378 ms("느린 상태") 또는 0.11 ms 대("빠른 상태")로 나왔고 v0(읽기만)은
 *                  거의 그대로였다 → atomic 처리량만 바뀐다. GPU 클럭/전력 상태로 추정 (확인 못 함).
 *   main/size/ntg/place 는 시작할 때 GPU 를 2초 돌려 "빠른 상태" 에서 잰다.
 *
 * 다른 세션이 같은 맥에서 동시에 벤치마크를 돌린다 → 절대값보다 배수를 볼 것. load average 를 같이 찍는다.
 * build: clang -fobjc-arc -O2 -Wall -Wextra -Werror demo_bench.m -framework Metal -framework Foundation -o bench
 *        ./bench main   (레슨 폴더에서 — demo_bench.metal 을 읽는다)
 */
#define _DARWIN_C_SOURCE
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define NBIN 256u
#define REPS 11

static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

enum { IN_UNIFORM, IN_ZIPF, IN_SAME, IN_LANE32, IN_COUNT };
static const char *const IN_NAME[] = { "uniform", "zipf1.2", "same", "lane32" };

/* solution.m 과 같은 생성기 + 진단용 lane32: word i 의 4바이트가 모두 (i & 31) →
 * simdgroup 의 lane 32개가 서로 다른 bin 32개, 모든 simdgroup 이 같은 bin 32개 */
static void gen_input(uint8_t *p, size_t n, int kind, uint32_t seed) {
    static uint32_t thr[NBIN];
    static bool ready;
    if (!ready) {
        double w[NBIN], z = 0, acc = 0;
        for (unsigned k = 0; k < NBIN; k++) { w[k] = 1.0 / pow((double)(k + 1), 1.2); z += w[k]; }
        for (unsigned k = 0; k < NBIN; k++) {
            acc += w[k];
            double t = acc / z * 4294967296.0;
            thr[k] = (k == NBIN - 1 || t >= 4294967295.0) ? UINT32_MAX : (uint32_t)t;
        }
        ready = true;
    }
    uint32_t s = seed;
    for (size_t i = 0; i < n; i++) {
        uint32_t r = xs32(&s);
        if (kind == IN_UNIFORM) p[i] = (uint8_t)(r >> 24);
        else if (kind == IN_SAME) p[i] = 42;
        else if (kind == IN_LANE32) p[i] = (uint8_t)((i >> 2) & 31u);
        else {
            unsigned lo = 0, hi = NBIN - 1;
            while (lo < hi) { unsigned mid = (lo + hi) / 2; if (r <= thr[mid]) hi = mid; else lo = mid + 1; }
            p[i] = (uint8_t)lo;
        }
    }
}

typedef struct { uint32_t n_bytes, n_words; } HistParams;
static id<MTLDevice> g_dev;
static id<MTLCommandQueue> g_q;
static id<MTLLibrary> g_lib;

static id<MTLComputePipelineState> pipe_of(const char *name) {
    NSError *err = nil;
    id<MTLComputePipelineState> ps = [g_dev newComputePipelineStateWithFunction:
        [g_lib newFunctionWithName:[NSString stringWithUTF8String:name]] error:&err];
    if (!ps) { printf("pipeline %s: %s\n", name, err.localizedDescription.UTF8String); exit(1); }
    return ps;
}

/* 한 번 실행 → GPU ms. 출력 앞 1 KB(히스토그램)만 0 으로 지운다 — 큰 memset 을 dispatch 사이에 끼우면
 * 측정과 상관없는 CPU 일이 GPU 를 쉬게 만들지 않도록. */
static double run_once(id<MTLComputePipelineState> ps, id<MTLBuffer> in, id<MTLBuffer> out, size_t n,
                       uint32_t tg, uint32_t ntg) {
    memset(out.contents, 0, out.length < 1024 ? out.length : 1024);
    HistParams P = { (uint32_t)n, (uint32_t)(n / 4) };
    id<MTLCommandBuffer> cb = [g_q commandBuffer];
    id<MTLComputeCommandEncoder> e = [cb computeCommandEncoder];
    [e setComputePipelineState:ps];
    [e setBuffer:in offset:0 atIndex:0];
    [e setBuffer:out offset:0 atIndex:1];
    [e setBytes:&P length:sizeof P atIndex:2];
    [e dispatchThreadgroups:MTLSizeMake(ntg, 1, 1) threadsPerThreadgroup:MTLSizeMake(tg, 1, 1)];
    [e endEncoding];
    [cb commit];
    [cb waitUntilCompleted];
    return (cb.GPUEndTime - cb.GPUStartTime) * 1e3;
}

static int cmp_d(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return (x > y) - (x < y); }
static double median(double *v, int n) { qsort(v, (size_t)n, sizeof v[0], cmp_d); return v[n / 2]; }

static uint32_t pick_ntg(size_t n, uint32_t tg) {
    size_t words = n / 4, per_tg = (size_t)tg * 16, g = (words + per_tg - 1) / per_tg;
    return g < 1 ? 1u : g > 4096 ? 4096u : (uint32_t)g;
}

static void loadavg(const char *tag) {
    double l[3];
    if (getloadavg(l, 3) == 3) printf("[%s] load average %.2f %.2f %.2f\n", tag, l[0], l[1], l[2]);
}

/* GPU 를 ms 밀리초 동안 쉬지 않고 돌린다. 이 맥에서는 GPU 가 한동안(수백 ms ~ 수 초) 바빠야
 * atomic 커널이 "빠른 상태" 로 나왔다 (./bench state). 클럭/전력 상태로 추정 — 확인 못 함. */
static void warm_up_ms(id<MTLBuffer> in, id<MTLBuffer> out, size_t n, unsigned ms) {
    static id<MTLComputePipelineState> ps;
    if (!ps) ps = pipe_of("hist_v2_tg");
    uint64_t t0 = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
    while (clock_gettime_nsec_np(CLOCK_UPTIME_RAW) - t0 < (uint64_t)ms * 1000000ull) run_once(ps, in, out, n, 256, pick_ntg(n, 256));
}
static void warm_up(id<MTLBuffer> in, id<MTLBuffer> out, size_t n) { warm_up_ms(in, out, n, 300); }

static double cpu_ms(const uint8_t *p, size_t n, uint32_t h[NBIN]) {
    double v[5];
    for (int r = 0; r < 5; r++) {
        uint64_t t0 = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
        memset(h, 0, NBIN * 4);
        for (size_t i = 0; i < n; i++) h[p[i]]++;
        v[r] = (double)(clock_gettime_nsec_np(CLOCK_UPTIME_RAW) - t0) / 1e6;
    }
    return median(v, 5);
}

static const char *const KN[] = { "hist_v0_read", "hist_v1_global", "hist_v2_tg", "hist_v3_simd", "hist_v3b_subsg", "hist_v3c_match" };
static const char *const KS[] = { "v0 read", "v1 global", "v2 tg", "v3 simd", "v3b subsg", "v3c match" };
#define NK 6

static void bench_main(size_t N) {
    uint8_t *p = malloc(N);
    id<MTLBuffer> in = [g_dev newBufferWithLength:N options:MTLResourceStorageModeShared];
    uint32_t tg = 256, ntg = pick_ntg(N, tg);
    id<MTLBuffer> out = [g_dev newBufferWithLength:(size_t)tg * ntg * 4 options:MTLResourceStorageModeShared];
    id<MTLComputePipelineState> ps[NK];
    for (int k = 0; k < NK; k++) ps[k] = pipe_of(KN[k]);
    printf("\nN = %zu bytes (%.0f MiB), tg %u x ntg %u, median of %d GPU runs (ms | GB/s), CPU = 1 thread median of 5\n",
           N, (double)N / 1048576.0, tg, ntg, REPS);
    printf("%-8s", "input");
    for (int k = 0; k < NK; k++) printf(" | %-15s", KS[k]);
    printf(" | %-15s\n", "CPU 1T");
    for (int inp = 0; inp < IN_COUNT; inp++) {
        gen_input(p, N, inp, 7);
        memcpy(in.contents, p, N);
        uint32_t ref[NBIN];
        double cms = cpu_ms(p, N, ref);
        warm_up(in, out, N);
        double t[NK][REPS];
        bool ok[NK];
        for (int k = 0; k < NK; k++) ok[k] = true;
        for (int r = 0; r < REPS; r++)                     /* 버전을 번갈아 돌려 클럭 변화가 고르게 퍼지게 */
            for (int k = 0; k < NK; k++) {
                t[k][r] = run_once(ps[k], in, out, N, tg, ntg);
                if (k > 0 && memcmp(out.contents, ref, NBIN * 4) != 0) ok[k] = false;
            }
        printf("%-8s", IN_NAME[inp]);
        for (int k = 0; k < NK; k++) {
            double m = median(t[k], REPS);
            printf(" | %6.3f %6.1f%s", m, (double)N / m / 1e6, ok[k] ? "  " : " X");
        }
        printf(" | %6.2f %6.2f\n", cms, (double)N / cms / 1e6);
    }
    free(p);
}

static void bench_size(void) {
    printf("\nsize sweep (uniform), tg 256, ntg = pick_ntg(N), median of %d (ms)\n", REPS);
    printf("%10s | %9s | %9s | %9s | v2/v1\n", "N", "v0 read", "v1 global", "v2 tg");
    size_t NMAX = (size_t)1 << 24;
    uint8_t *p = malloc(NMAX);
    gen_input(p, NMAX, IN_UNIFORM, 7);
    id<MTLBuffer> in = [g_dev newBufferWithBytes:p length:NMAX options:MTLResourceStorageModeShared];
    id<MTLBuffer> out = [g_dev newBufferWithLength:4096 * 256 * 4 options:MTLResourceStorageModeShared];
    id<MTLComputePipelineState> ps[3] = { pipe_of(KN[0]), pipe_of(KN[1]), pipe_of(KN[2]) };
    warm_up(in, out, NMAX);
    for (int e = 10; e <= 24; e += 2) {
        size_t N = (size_t)1 << e;
        double t[3][REPS];
        warm_up_ms(in, out, NMAX, 1500);                   /* 작은 N 줄은 GPU 를 거의 쉬게 하므로 줄마다 다시 예열 */
        for (int r = 0; r < REPS; r++)
            for (int k = 0; k < 3; k++) t[k][r] = run_once(ps[k], in, out, N, 256, pick_ntg(N, 256));
        double m0 = median(t[0], REPS), m1 = median(t[1], REPS), m2 = median(t[2], REPS);
        printf("%10zu | %9.4f | %9.4f | %9.4f | %.2f\n", N, m0, m1, m2, m2 / m1);
    }
    free(p);
}

static void bench_ntg(void) {
    size_t N = (size_t)1 << 24;
    printf("\nthreadgroup-count sweep, N = 2^24, tg 256, median of %d (ms); merge atomics = ntg x (nonzero bins)\n", REPS);
    printf("%6s | %-21s | %-21s\n", "ntg", "uniform v1 / v2", "same v1 / v2");
    uint8_t *p = malloc(N);
    id<MTLBuffer> in[2];
    gen_input(p, N, IN_UNIFORM, 7);
    in[0] = [g_dev newBufferWithBytes:p length:N options:MTLResourceStorageModeShared];
    gen_input(p, N, IN_SAME, 7);
    in[1] = [g_dev newBufferWithBytes:p length:N options:MTLResourceStorageModeShared];
    id<MTLBuffer> out = [g_dev newBufferWithLength:NBIN * 4 options:MTLResourceStorageModeShared];
    id<MTLComputePipelineState> v1 = pipe_of(KN[1]), v2 = pipe_of(KN[2]);
    warm_up(in[0], out, N);
    const uint32_t ntgs[] = { 16, 64, 256, 1024, 4096, 16384 };
    for (size_t j = 0; j < sizeof ntgs / sizeof ntgs[0]; j++) {
        double t[4][REPS];
        warm_up(in[0], out, N);
        for (int r = 0; r < REPS; r++) {
            t[0][r] = run_once(v1, in[0], out, N, 256, ntgs[j]);
            t[1][r] = run_once(v2, in[0], out, N, 256, ntgs[j]);
            t[2][r] = run_once(v1, in[1], out, N, 256, ntgs[j]);
            t[3][r] = run_once(v2, in[1], out, N, 256, ntgs[j]);
        }
        printf("%6u | %8.3f / %8.3f   | %8.3f / %8.3f\n", ntgs[j], median(t[0], REPS), median(t[1], REPS),
               median(t[2], REPS), median(t[3], REPS));
    }
    free(p);
}

static void bench_state(void) {
    size_t N = (size_t)1 << 24;
    uint8_t *p = malloc(N);
    gen_input(p, N, IN_UNIFORM, 7);
    id<MTLBuffer> in = [g_dev newBufferWithBytes:p length:N options:MTLResourceStorageModeShared];
    id<MTLBuffer> out = [g_dev newBufferWithLength:4096 * 256 * 4 options:MTLResourceStorageModeShared];
    id<MTLComputePipelineState> v0 = pipe_of(KN[0]), v1 = pipe_of(KN[1]);
    struct timespec ts = { 2, 0 };
    nanosleep(&ts, NULL);
    char *gap = malloc((size_t)8 << 20);
    for (int phase = 0; phase < 2; phase++) {
        if (phase == 0) printf("\nafter 2 s idle, 8 MB CPU memset between dispatches: run# : v0 read ms / v1 global ms (uniform, N = 2^24, ntg 1024)\n");
        else { warm_up_ms(in, out, N, 3000); printf("after 3 s of back-to-back dispatches, no CPU work between dispatches:\n"); }
        for (int r = 0; r < 15; r++) {
            if (phase == 0) memset(gap, r, (size_t)8 << 20);
            double x = run_once(v0, in, out, N, 256, 1024);
            if (phase == 0) memset(gap, r + 1, (size_t)8 << 20);
            double y = run_once(v1, in, out, N, 256, 1024);
            printf("%2d: %.3f / %.3f%s", r, x, y, (r % 5 == 4) ? "\n" : "   ");
        }
    }
    free(gap);
    free(p);
}

static void bench_place(void) {
    size_t N = (size_t)1 << 24;
    uint8_t *p = malloc(N);
    gen_input(p, N, IN_UNIFORM, 7);
    id<MTLBuffer> in = [g_dev newBufferWithBytes:p length:N options:MTLResourceStorageModeShared];
    gen_input(p, N, IN_ZIPF, 7);
    id<MTLBuffer> inz = [g_dev newBufferWithBytes:p length:N options:MTLResourceStorageModeShared];
    id<MTLComputePipelineState> v1 = pipe_of(KN[1]), v2 = pipe_of(KN[2]);
    NSMutableArray *keep = [NSMutableArray array];        /* 이전 버퍼를 살려 둬야 다른 위치가 나온다 */
    printf("\nfresh 1 KB hist buffer each row, N = 2^24, ntg 1024, median of %d (ms): uniform v1 / v2 | zipf v1 / v2\n", REPS);
    warm_up(in, [g_dev newBufferWithLength:1024 options:MTLResourceStorageModeShared], N);
    for (int b = 0; b < 12; b++) {
        id<MTLBuffer> out = [g_dev newBufferWithLength:NBIN * 4 options:MTLResourceStorageModeShared];
        [keep addObject:out];
        double t[4][REPS];
        for (int r = 0; r < REPS; r++) {
            t[0][r] = run_once(v1, in, out, N, 256, 1024);
            t[1][r] = run_once(v2, in, out, N, 256, 1024);
            t[2][r] = run_once(v1, inz, out, N, 256, 1024);
            t[3][r] = run_once(v2, inz, out, N, 256, 1024);
        }
        printf("buf %2d gpuAddress %#llx: %.3f / %.3f | %.3f / %.3f\n", b, (unsigned long long)out.gpuAddress,
               median(t[0], REPS), median(t[1], REPS), median(t[2], REPS), median(t[3], REPS));
    }
    free(p);
}

int main(int argc, char **argv) {
    @autoreleasepool {
        g_dev = MTLCreateSystemDefaultDevice();
        NSError *err = nil;
        NSString *src = [NSString stringWithContentsOfFile:@"demo_bench.metal" encoding:NSUTF8StringEncoding error:&err];
        if (!g_dev || !src) { printf("need Metal + demo_bench.metal in cwd\n"); return 1; }
        g_lib = [g_dev newLibraryWithSource:src options:nil error:&err];
        if (!g_lib) { printf("compile: %s\n", err.localizedDescription.UTF8String); return 1; }
        g_q = [g_dev newCommandQueue];
        printf("GPU %s, maxThreadgroupMemoryLength %lu B, maxThreadsPerThreadgroup %lu\n", g_dev.name.UTF8String,
               (unsigned long)g_dev.maxThreadgroupMemoryLength, (unsigned long)g_dev.maxThreadsPerThreadgroup.width);
        for (int k = 0; k < NK; k++) {
            id<MTLComputePipelineState> ps = pipe_of(KN[k]);
            printf("  %-15s threadExecutionWidth %lu, staticThreadgroupMemoryLength %lu B, maxTotalThreadsPerThreadgroup %lu\n",
                   KN[k], (unsigned long)ps.threadExecutionWidth, (unsigned long)ps.staticThreadgroupMemoryLength,
                   (unsigned long)ps.maxTotalThreadsPerThreadgroup);
        }
        const char *mode = argc > 1 ? argv[1] : "main";
        loadavg("start");
        if (strcmp(mode, "state") != 0) {                  /* 2초 예열 (state 모드는 일부러 안 함) */
            size_t n = (size_t)1 << 24;
            id<MTLBuffer> a = [g_dev newBufferWithLength:n options:MTLResourceStorageModeShared];
            id<MTLBuffer> b = [g_dev newBufferWithLength:1024 options:MTLResourceStorageModeShared];
            warm_up_ms(a, b, n, 2000);
        }
        if (!strcmp(mode, "main")) { bench_main((size_t)1 << 24); bench_main((size_t)1 << 26); }
        else if (!strcmp(mode, "size")) bench_size();
        else if (!strcmp(mode, "ntg")) bench_ntg();
        else if (!strcmp(mode, "state")) bench_state();
        else if (!strcmp(mode, "place")) bench_place();
        else { printf("usage: %s main|size|ntg|state|place\n", argv[0]); return 1; }
        loadavg("end");
        return 0;
    }
}
