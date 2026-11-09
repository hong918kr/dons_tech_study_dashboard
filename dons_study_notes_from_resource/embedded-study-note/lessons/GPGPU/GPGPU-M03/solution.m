/* GPGPU-M03 Histogram: Privatization과 Atomic 경합 — 호스트 (Objective-C + Metal)
 *
 *   Part A  CPU 기준 히스토그램 hist_cpu + 전역 atomic 커널 hist_v1_global (.metal)
 *   Part B  threadgroup 지역 히스토그램 커널 hist_v2_tg (.metal: 0 초기화 → barrier → 누적 → barrier → 병합)
 *   Part C  SIMD 집계 커널 hist_v3_simd (.metal) + ISP 자동 노출(AE) 통계 ae_stats / ae_step (이 파일)
 *
 * 체크는 전부 결정적이다: GPU 결과 == CPU 히스토그램 (bin 256개 모두 정확히), 합 == N, AE 결정값.
 * 시간은 CHECK 하지 않는다 → demo_bench.m.
 * 커널 소스는 이 파일 이름의 .m 을 .metal 로 바꾼 파일(solution.m → solution.metal)을 현재 디렉터리에서 읽어
 * 실행 중에 컴파일한다 (newLibraryWithSource — 이 맥엔 Xcode 의 metal 컴파일러가 없다). 레슨 폴더에서 실행할 것.
 *
 * build: clang -fobjc-arc -O2 -Wall -Wextra -Werror solution.m -framework Metal -framework Foundation -o so && ./so
 */
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NBIN 256u

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; printf("PASS %s\n", #expr); } \
    else { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } } while (0)

/* ======================= 결정적 입력 (xorshift32) ======================= */
static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

enum { IN_UNIFORM, IN_ZIPF, IN_SAME };
static const char *const IN_NAME[] = { "uniform", "zipf1.2", "same" };
#define SAME_BIN 42u

/* Zipf(s = 1.2): rank k (0..255) 의 확률 ∝ 1/(k+1)^1.2 → bin 0 이 약 25 %. bin = rank (단순하게).
 * 누적 확률을 2^32 눈금 정수 표로 만들어 두고 이진 탐색 → 같은 seed 면 언제나 같은 바이트열. */
static void gen_input(uint8_t *p, size_t n, int kind, uint32_t seed) {
    static uint32_t thr[NBIN];
    static bool thr_ready;
    if (!thr_ready) {
        double w[NBIN], z = 0, acc = 0;
        for (unsigned k = 0; k < NBIN; k++) { w[k] = 1.0 / pow((double)(k + 1), 1.2); z += w[k]; }
        for (unsigned k = 0; k < NBIN; k++) {
            acc += w[k];
            double t = acc / z * 4294967296.0;
            thr[k] = (k == NBIN - 1 || t >= 4294967295.0) ? UINT32_MAX : (uint32_t)t;
        }
        thr_ready = true;
    }
    uint32_t s = seed ? seed : 1u;
    for (size_t i = 0; i < n; i++) {
        uint32_t r = xs32(&s);
        if (kind == IN_UNIFORM) p[i] = (uint8_t)(r >> 24);
        else if (kind == IN_SAME) p[i] = (uint8_t)SAME_BIN;
        else {
            unsigned lo = 0, hi = NBIN - 1;                 /* 첫 k: r <= thr[k] */
            while (lo < hi) { unsigned mid = (lo + hi) / 2; if (r <= thr[mid]) hi = mid; else lo = mid + 1; }
            p[i] = (uint8_t)lo;
        }
    }
}

/* ======================= Part A — CPU 기준 ======================= */
/* h[b] = p[0..n) 중 값이 b 인 바이트 수. h 는 이 함수가 0 으로 채운 뒤 센다. */
static void hist_cpu(const uint8_t *p, size_t n, uint32_t h[NBIN]) {
    memset(h, 0, NBIN * sizeof h[0]);
    for (size_t i = 0; i < n; i++) h[p[i]]++;
}

static uint64_t hist_sum(const uint32_t h[NBIN]) {
    uint64_t s = 0;
    for (unsigned b = 0; b < NBIN; b++) s += h[b];
    return s;
}

/* ======================= GPU 공통 (하네스 — 고칠 필요 없음) ======================= */
typedef struct { uint32_t n_bytes, n_words; } HistParams;          /* .metal 의 HistParams 와 같은 배치 */

static id<MTLDevice> g_dev;
static id<MTLCommandQueue> g_q;
static id<MTLLibrary> g_lib;
static NSMutableDictionary<NSString *, id<MTLComputePipelineState>> *g_pipes;

static bool gpu_init(void) {
    g_dev = MTLCreateSystemDefaultDevice();
    if (!g_dev) { printf("no Metal device\n"); return false; }
    NSString *me = [[NSString stringWithUTF8String:__FILE__] lastPathComponent];   /* "solution.m" */
    NSString *path = [[me stringByDeletingPathExtension] stringByAppendingPathExtension:@"metal"];
    NSError *err = nil;
    NSString *src = [NSString stringWithContentsOfFile:path encoding:NSUTF8StringEncoding error:&err];
    if (!src) { printf("cannot read %s (run from the lesson dir)\n", path.UTF8String); return false; }
    g_lib = [g_dev newLibraryWithSource:src options:nil error:&err];
    if (!g_lib) { printf("Metal compile error: %s\n", err.localizedDescription.UTF8String); return false; }
    g_q = [g_dev newCommandQueue];
    g_pipes = [NSMutableDictionary dictionary];
    return g_q != nil;
}

static id<MTLComputePipelineState> gpu_pipe(const char *name) {
    NSString *key = [NSString stringWithUTF8String:name];
    id<MTLComputePipelineState> ps = g_pipes[key];
    if (ps) return ps;
    id<MTLFunction> f = [g_lib newFunctionWithName:key];
    if (!f) { printf("no kernel %s\n", name); return nil; }
    NSError *err = nil;
    ps = [g_dev newComputePipelineStateWithFunction:f error:&err];
    if (!ps) { printf("pipeline %s: %s\n", name, err.localizedDescription.UTF8String); return nil; }
    g_pipes[key] = ps;
    return ps;
}

/* 커널 `name` 을 ntg 개 threadgroup x tg 스레드로 돌려 out[256] 에 히스토그램을 받는다.
 * n == 0 이면 GPU 를 쓰지 않고 0 을 돌려준다 (길이 0 버퍼는 만들 수 없다). gpu_ms 는 NULL 가능. */
static bool gpu_hist(const char *name, const uint8_t *data, size_t n, uint32_t tg, uint32_t ntg,
                     uint32_t out[NBIN], double *gpu_ms) {
    memset(out, 0, NBIN * sizeof out[0]);
    if (gpu_ms) *gpu_ms = 0;
    if (n == 0) return true;
    if (n > UINT32_MAX || tg == 0 || ntg == 0) return false;
    id<MTLComputePipelineState> ps = gpu_pipe(name);
    if (!ps || tg > ps.maxTotalThreadsPerThreadgroup) return false;
    size_t padded = (n + 3) & ~(size_t)3;
    id<MTLBuffer> bin = [g_dev newBufferWithLength:padded options:MTLResourceStorageModeShared];
    id<MTLBuffer> bh = [g_dev newBufferWithLength:NBIN * sizeof(uint32_t) options:MTLResourceStorageModeShared];
    if (!bin || !bh) return false;
    memcpy(bin.contents, data, n);
    memset(bh.contents, 0, NBIN * sizeof(uint32_t));
    HistParams P = { (uint32_t)n, (uint32_t)(n / 4) };
    id<MTLCommandBuffer> cb = [g_q commandBuffer];
    id<MTLComputeCommandEncoder> enc = [cb computeCommandEncoder];
    [enc setComputePipelineState:ps];
    [enc setBuffer:bin offset:0 atIndex:0];
    [enc setBuffer:bh offset:0 atIndex:1];
    [enc setBytes:&P length:sizeof P atIndex:2];
    [enc dispatchThreadgroups:MTLSizeMake(ntg, 1, 1) threadsPerThreadgroup:MTLSizeMake(tg, 1, 1)];
    [enc endEncoding];
    [cb commit];
    [cb waitUntilCompleted];
    if (cb.status != MTLCommandBufferStatusCompleted) {
        printf("command buffer error: %s\n", cb.error.localizedDescription.UTF8String);
        return false;
    }
    if (gpu_ms) *gpu_ms = (cb.GPUEndTime - cb.GPUStartTime) * 1e3;
    memcpy(out, bh.contents, NBIN * sizeof(uint32_t));
    return true;
}

/* 스레드 하나가 word 16개쯤 처리하도록 threadgroup 수를 고른다 (최소 1, 최대 4096). */
static uint32_t pick_ntg(size_t n, uint32_t tg) {
    size_t words = n / 4, per_tg = (size_t)tg * 16;
    size_t g = (words + per_tg - 1) / per_tg;
    return g < 1 ? 1u : g > 4096 ? 4096u : (uint32_t)g;
}

/* GPU 결과가 CPU 와 bin 256개 모두 같은가 (다르면 첫 차이를 찍는다) */
static bool hist_eq(const char *tag, const uint32_t g[NBIN], const uint32_t c[NBIN]) {
    for (unsigned b = 0; b < NBIN; b++)
        if (g[b] != c[b]) { printf("  %s: bin %u gpu %u cpu %u\n", tag, b, g[b], c[b]); return false; }
    return true;
}

static bool gpu_matches(const char *kernel, const uint8_t *p, size_t n, uint32_t tg, uint32_t ntg) {
    uint32_t g[NBIN], c[NBIN];
    hist_cpu(p, n, c);
    if (!gpu_hist(kernel, p, n, tg, ntg, g, NULL)) { printf("  %s: dispatch failed\n", kernel); return false; }
    return hist_eq(kernel, g, c) && hist_sum(g) == n;
}

/* ======================= Part C — ISP 자동 노출(AE) 통계 ======================= */
/* 8비트 휘도(Y) 히스토그램 → 노출 판단. 실제 ISP 는 히스토그램을 하드웨어 통계 블록이 만들고
 * 펌웨어(AE 알고리즘)는 이런 요약만 계산한다. 정수 연산만 쓴다 (결정적).
 *   n       = 전체 픽셀 수
 *   mean_q8 = floor(256 * sum(b * h[b]) / n)          (평균 밝기, 1/256 단위)
 *   p50/p99 = cum(b) * 100 >= q * n 을 만족하는 가장 작은 b (q = 50, 99)
 *   clip_hi = bin >= 250 픽셀 수, clip_lo = bin <= 5 픽셀 수
 * n == 0 이면 false (st 는 0). */
typedef struct { uint64_t n; uint32_t mean_q8; uint8_t p50, p99; uint64_t clip_hi, clip_lo; } ae_stats_t;

static bool ae_stats(const uint32_t h[NBIN], ae_stats_t *st) {
    memset(st, 0, sizeof *st);
    uint64_t n = 0, wsum = 0;
    for (unsigned b = 0; b < NBIN; b++) { n += h[b]; wsum += (uint64_t)b * h[b]; }
    if (n == 0) return false;
    st->n = n;
    st->mean_q8 = (uint32_t)(wsum * 256 / n);
    uint64_t cum = 0;
    bool got50 = false, got99 = false;
    for (unsigned b = 0; b < NBIN; b++) {
        cum += h[b];
        if (!got50 && cum * 100 >= 50 * n) { st->p50 = (uint8_t)b; got50 = true; }
        if (!got99 && cum * 100 >= 99 * n) { st->p99 = (uint8_t)b; got99 = true; }
        if (b >= 250) st->clip_hi += h[b];
        if (b <= 5) st->clip_lo += h[b];
    }
    return true;
}

/* 노출 한 단계 결정: 하이라이트 포화가 2 % 초과면 -1 (어둡게), 아니고 p99 < 200 이면 +1 (밝게), 아니면 0. */
static int ae_step(const ae_stats_t *st) {
    if (st->n == 0) return 0;
    if (st->clip_hi * 50 > st->n) return -1;
    if (st->p99 < 200) return +1;
    return 0;
}

/* 합성 Y 프레임 (1920x1080). dark: 0..63 잡음 / sky: 위 40 % 행이 255 로 포화, 아래는 60..187 /
 * normal: 20..235 고르게 + 하이라이트 1 % 정도 */
enum { FR_DARK, FR_SKY, FR_NORMAL };
#define FW 1920u
#define FH 1080u
static void make_frame(uint8_t *y, int kind, uint32_t seed) {
    uint32_t s = seed;
    for (uint32_t r = 0; r < FH; r++)
        for (uint32_t c = 0; c < FW; c++) {
            uint32_t x = xs32(&s);
            uint8_t v;
            if (kind == FR_DARK) v = (uint8_t)(x & 63u);
            else if (kind == FR_SKY) v = (r < FH * 2 / 5) ? 255u : (uint8_t)(60u + (x & 127u));
            else v = (x % 100u == 0) ? (uint8_t)(240u + (x >> 28)) : (uint8_t)(20u + (x >> 8) % 216u);
            y[(size_t)r * FW + c] = v;
        }
}

/* ======================= 테스트 하네스 ======================= */
int main(void) {
    @autoreleasepool {
        if (!gpu_init()) return 1;
        const size_t N = (size_t)1 << 24;                               /* 16 MiB */
        uint8_t *buf = malloc(N + 8);
        if (!buf) return 1;
        uint32_t c[NBIN], g[NBIN];

        printf("-- Part A: CPU reference + global atomics (hist_v1_global)\n");
        const uint8_t hello[] = "hello, histogram";
        hist_cpu(hello, 16, c);
        CHECK(c['l'] == 2 && c['o'] == 2 && c['h'] == 2 && c[','] == 1 && c[' '] == 1 && hist_sum(c) == 16);
        gen_input(buf, N, IN_UNIFORM, 7);
        hist_cpu(buf, N, c);
        CHECK(hist_sum(c) == N);
        gen_input(buf, N, IN_SAME, 7);
        hist_cpu(buf, N, c);
        CHECK(c[SAME_BIN] == N && hist_sum(c) == N);
        gen_input(buf, N, IN_ZIPF, 7);
        hist_cpu(buf, N, c);
        CHECK(c[0] > N / 5 && c[0] < N / 3 && c[255] > 0);             /* Zipf 1.2: bin 0 ≈ 25 % */
        for (int k = IN_UNIFORM; k <= IN_SAME; k++) {
            gen_input(buf, N, k, 11);
            bool v1_match = gpu_matches("hist_v1_global", buf, N, 256, pick_ntg(N, 256));
            printf("  v1 %-8s %s\n", IN_NAME[k], v1_match ? "match" : "MISMATCH");
            CHECK(v1_match);
        }
        {
            const size_t odd[] = { 1, 3, 4, 5, 1000003 };
            bool v1_tail_ok = true;
            for (size_t i = 0; i < sizeof odd / sizeof odd[0]; i++) {
                gen_input(buf, odd[i], IN_UNIFORM, 3);
                v1_tail_ok = gpu_matches("hist_v1_global", buf, odd[i], 256, pick_ntg(odd[i], 256)) && v1_tail_ok;
            }
            CHECK(v1_tail_ok);                                          /* tail 바이트 0~3 */
            CHECK(gpu_hist("hist_v1_global", buf, 0, 256, 1, g, NULL) && hist_sum(g) == 0);
        }

        printf("-- Part B: threadgroup privatization (hist_v2_tg)\n");
        for (int k = IN_UNIFORM; k <= IN_SAME; k++) {
            gen_input(buf, N, k, 13);
            bool v2_match = gpu_matches("hist_v2_tg", buf, N, 256, pick_ntg(N, 256));
            printf("  v2 %-8s %s\n", IN_NAME[k], v2_match ? "match" : "MISMATCH");
            CHECK(v2_match);
        }
        gen_input(buf, N, IN_ZIPF, 17);
        {
            const uint32_t tgs[] = { 32, 64, 96, 128, 1024 };          /* 256 보다 작게 · 크게 · 2의 거듭제곱 아님 */
            bool v2_tgsize_ok = true;
            for (size_t i = 0; i < sizeof tgs / sizeof tgs[0]; i++)
                v2_tgsize_ok = gpu_matches("hist_v2_tg", buf, N, tgs[i], pick_ntg(N, tgs[i])) && v2_tgsize_ok;
            CHECK(v2_tgsize_ok);
        }
        CHECK(gpu_matches("hist_v2_tg", buf, N, 256, 1));               /* threadgroup 1개가 전부 */
        CHECK(gpu_matches("hist_v2_tg", buf, N, 256, 4096));            /* 병합 4096 x 256 */
        {
            const size_t odd[] = { 1, 2, 7, 255, 1000003 };
            bool v2_tail_ok = true;
            for (size_t i = 0; i < sizeof odd / sizeof odd[0]; i++) {
                gen_input(buf, odd[i], IN_ZIPF, 5);
                v2_tail_ok = gpu_matches("hist_v2_tg", buf, odd[i], 256, pick_ntg(odd[i], 256)) && v2_tail_ok;
            }
            CHECK(v2_tail_ok);
        }
        {   /* 반복 실행해도 같은 답 — 지역 히스토그램은 dispatch 마다 새로 0 이어야 한다 */
            gen_input(buf, N, IN_UNIFORM, 19);
            bool v2_repeat_ok = true;
            for (int r = 0; r < 5; r++) v2_repeat_ok = gpu_matches("hist_v2_tg", buf, N, 256, 64) && v2_repeat_ok;
            CHECK(v2_repeat_ok);
        }

        printf("-- Part C: SIMD aggregation (hist_v3_simd) + ISP auto-exposure stats\n");
        for (int k = IN_UNIFORM; k <= IN_SAME; k++) {
            gen_input(buf, N, k, 23);
            bool v3_match = gpu_matches("hist_v3_simd", buf, N, 256, pick_ntg(N, 256));
            printf("  v3 %-8s %s\n", IN_NAME[k], v3_match ? "match" : "MISMATCH");
            CHECK(v3_match);
        }
        {   /* 부분 활성 simdgroup (마지막 반복), tg 크기, tail */
            gen_input(buf, N, IN_SAME, 1);
            buf[12345] = 7;                                             /* 한 word 만 다름 → 그 simdgroup 은 느린 길 */
            bool v3_partial_ok = gpu_matches("hist_v3_simd", buf, N, 96, 333);
            v3_partial_ok = gpu_matches("hist_v3_simd", buf, 4 * 1000 + 3, 64, 3) && v3_partial_ok;
            v3_partial_ok = gpu_matches("hist_v3_simd", buf, 1000003, 1024, 2) && v3_partial_ok;
            CHECK(v3_partial_ok);
        }
        {   /* 손으로 계산한 히스토그램 → AE 통계 */
            uint32_t h[NBIN] = { 0 };
            h[10] = 50; h[100] = 49; h[255] = 1;
            ae_stats_t st;
            CHECK(ae_stats(h, &st) && st.n == 100 && st.p50 == 10 && st.p99 == 100);
            CHECK(st.mean_q8 == 14476 && st.clip_hi == 1 && st.clip_lo == 0 && ae_step(&st) == +1);
            memset(h, 0, sizeof h);
            CHECK(!ae_stats(h, &st) && st.n == 0 && ae_step(&st) == 0);
        }
        {   /* 합성 프레임 3장: GPU(v3) 히스토그램 == CPU, AE 결정 */
            const size_t FN = (size_t)FW * FH;
            const int want[] = { +1, -1, 0 };
            const char *const fname[] = { "dark", "sky", "normal" };
            bool all_eq = true, all_step = true;
            for (int k = FR_DARK; k <= FR_NORMAL; k++) {
                make_frame(buf, k, 99u + (uint32_t)k);
                hist_cpu(buf, FN, c);
                bool eq = gpu_hist("hist_v3_simd", buf, FN, 256, pick_ntg(FN, 256), g, NULL) && hist_eq("frame", g, c)
                          && hist_sum(g) == FN;
                ae_stats_t st;
                bool ok = ae_stats(g, &st);
                int step = ae_step(&st);
                printf("  frame %-6s mean %.1f p50 %u p99 %u clip_hi %.2f%% -> step %+d\n", fname[k],
                       st.mean_q8 / 256.0, st.p50, st.p99, ok ? 100.0 * (double)st.clip_hi / (double)st.n : 0.0, step);
                all_eq = all_eq && eq;
                all_step = all_step && ok && step == want[k];
            }
            CHECK(all_eq);
            CHECK(all_step);
        }

        free(buf);
        printf("%d/%d passed\n", g_pass, g_total);
        return g_pass == g_total ? 0 : 1;
    }
}
