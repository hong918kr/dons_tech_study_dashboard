/* GPGPU-M03 추가 연습 — 한 번에 검증 (Objective-C + Metal)
 *
 *   연습 1  12비트 RAW 4096-bin 히스토그램: threadgroup 16 KB, 파이프라인의 정적 threadgroup 메모리 확인,
 *           14비트(16384-bin, 64 KB)는 32 KB 한도를 넘어 파이프라인 생성이 실패한다
 *   연습 2  RGBA 4채널 히스토그램 (AWB 통계 블록 흉내): 채널별 합 == 픽셀 수, CPU 와 일치
 *   연습 3  device atomic 0번인 2-pass (부분 히스토그램 저장 → 열 합): 같은 command buffer 의 두 dispatch
 *   연습 4  병합 비용 모델: v2 의 device atomic 횟수 = Σ_threadgroup (0 아닌 bin 수) — CPU 예측 == GPU 카운터
 *
 * 커널 소스 = practice.metal (현재 디렉터리에서 읽음 → 레슨 폴더에서 실행).
 * build: clang -fobjc-arc -O2 -Wall -Wextra -Werror practice.m -framework Metal -framework Foundation -o pr && ./pr
 */
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; printf("PASS %s\n", #expr); } \
    else { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } } while (0)

static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

/* ---------------- GPU 공통 ---------------- */
static id<MTLDevice> g_dev;
static id<MTLCommandQueue> g_q;
static id<MTLLibrary> g_lib;

static bool gpu_init(void) {
    g_dev = MTLCreateSystemDefaultDevice();
    if (!g_dev) return false;
    NSString *me = [[NSString stringWithUTF8String:__FILE__] lastPathComponent];
    NSString *path = [[me stringByDeletingPathExtension] stringByAppendingPathExtension:@"metal"];
    NSError *err = nil;
    NSString *src = [NSString stringWithContentsOfFile:path encoding:NSUTF8StringEncoding error:&err];
    if (!src) { printf("cannot read %s (run from the lesson dir)\n", path.UTF8String); return false; }
    g_lib = [g_dev newLibraryWithSource:src options:nil error:&err];
    if (!g_lib) { printf("Metal compile error: %s\n", err.localizedDescription.UTF8String); return false; }
    g_q = [g_dev newCommandQueue];
    return g_q != nil;
}

/* 파이프라인을 만든다. 실패하면 nil, *why 에 이유 (NULL 가능) */
static id<MTLComputePipelineState> make_pipe(const char *name, NSString **why) {
    id<MTLFunction> f = [g_lib newFunctionWithName:[NSString stringWithUTF8String:name]];
    if (!f) { if (why) *why = @"no such function"; return nil; }
    NSError *err = nil;
    id<MTLComputePipelineState> ps = [g_dev newComputePipelineStateWithFunction:f error:&err];
    if (!ps && why) *why = err.localizedDescription;
    return ps;
}

static id<MTLBuffer> gbuf(const void *src, size_t len) {
    id<MTLBuffer> b = [g_dev newBufferWithLength:(len ? len : 4) options:MTLResourceStorageModeShared];
    if (b) { if (src) memcpy(b.contents, src, len); else memset(b.contents, 0, b.length); }
    return b;
}

typedef struct { id<MTLComputePipelineState> ps; id<MTLBuffer> b0, b1; uint32_t n, tg, ntg; } disp_t;

/* dispatch 들을 한 command buffer / 한 compute encoder 에 차례로 넣는다.
 * 기본(serial) compute encoder 에서는 앞 dispatch 의 쓰기가 다음 dispatch 에 보인다. */
static bool run_seq(const disp_t *d, int cnt) {
    id<MTLCommandBuffer> cb = [g_q commandBuffer];
    id<MTLComputeCommandEncoder> enc = [cb computeCommandEncoder];
    for (int i = 0; i < cnt; i++) {
        if (!d[i].ps || d[i].tg > d[i].ps.maxTotalThreadsPerThreadgroup) { [enc endEncoding]; return false; }
        [enc setComputePipelineState:d[i].ps];
        [enc setBuffer:d[i].b0 offset:0 atIndex:0];
        [enc setBuffer:d[i].b1 offset:0 atIndex:1];
        uint32_t n = d[i].n;
        [enc setBytes:&n length:sizeof n atIndex:2];
        [enc dispatchThreadgroups:MTLSizeMake(d[i].ntg, 1, 1) threadsPerThreadgroup:MTLSizeMake(d[i].tg, 1, 1)];
    }
    [enc endEncoding];
    [cb commit];
    [cb waitUntilCompleted];
    return cb.status == MTLCommandBufferStatusCompleted;
}

static bool eq_u32(const uint32_t *a, const uint32_t *b, size_t n) {
    for (size_t i = 0; i < n; i++) if (a[i] != b[i]) { printf("  differ at %zu: %u vs %u\n", i, a[i], b[i]); return false; }
    return true;
}

/* ---------------- 연습 4: CPU 예측 ---------------- */
/* 원소 i 는 gid = i mod (tg*ntg) 인 스레드, 즉 threadgroup gid / tg 가 처리한다 (grid-stride).
 * v2 의 병합 device atomic 수 = threadgroup 마다 0 이 아닌 bin 의 수의 합. */
static uint64_t predict_merges(const uint8_t *p, size_t n, uint32_t tg, uint32_t ntg) {
    size_t gsize = (size_t)tg * ntg;
    uint8_t *seen = calloc((size_t)ntg * 256, 1);
    if (!seen) return 0;
    uint64_t m = 0;
    for (size_t i = 0; i < n; i++) {
        size_t t = (i % gsize) / tg;
        uint8_t *s = &seen[t * 256 + p[i]];
        if (!*s) { *s = 1; m++; }
    }
    free(seen);
    return m;
}

int main(void) {
    @autoreleasepool {
        if (!gpu_init()) return 1;

        printf("-- 연습 1: 12-bit RAW, 4096 bins\n");
        {
            const uint32_t n = 1u << 22;                                  /* 4M 픽셀 */
            uint16_t *raw = malloc((size_t)n * 2);
            uint32_t *cpu = calloc(4096, 4);
            uint32_t s = 5;
            for (uint32_t i = 0; i < n; i++) {
                uint32_t x = xs32(&s);
                raw[i] = (i % 1000 == 0) ? (uint16_t)0xFFFF : (uint16_t)(64 + (x & 0x0FFF) / 2); /* 블랙 레벨 64 + 잡음 */
                cpu[raw[i] > 4095 ? 4095 : raw[i]]++;
            }
            id<MTLComputePipelineState> ps = make_pipe("hist12_tg", NULL);
            CHECK(ps != nil && ps.staticThreadgroupMemoryLength == 4096 * 4
                  && ps.staticThreadgroupMemoryLength <= g_dev.maxThreadgroupMemoryLength);
            id<MTLBuffer> bi = gbuf(raw, (size_t)n * 2), bh = gbuf(NULL, 4096 * 4);
            disp_t d = { ps, bi, bh, n, 256, 256 };
            CHECK(run_seq(&d, 1) && eq_u32(bh.contents, cpu, 4096));
            NSString *why = nil;
            id<MTLComputePipelineState> big = make_pipe("hist14_too_big", &why);
            printf("  hist14_too_big: %s\n", big ? "pipeline OK?!" : why.UTF8String);
            CHECK(big == nil && [why containsString:@"exceeds"]);
            free(raw); free(cpu);
        }

        printf("-- 연습 2: RGBA 4-channel histograms\n");
        {
            const uint32_t npx = 1280u * 720u;
            uint8_t *px = malloc((size_t)npx * 4);
            uint32_t cpu[1024] = { 0 };
            uint32_t s = 9;
            for (uint32_t i = 0; i < npx; i++) {
                uint32_t x = xs32(&s);
                px[4 * i + 0] = (uint8_t)(x & 0xFF);                       /* R: 고르게 */
                px[4 * i + 1] = (uint8_t)(128 + ((x >> 8) & 0x3F));        /* G: 가운데 몰림 */
                px[4 * i + 2] = (uint8_t)((x >> 16) & 0x1F);               /* B: 어두움 */
                px[4 * i + 3] = 255;                                       /* A: 한 bin (최악의 경합) */
                for (int ch = 0; ch < 4; ch++) cpu[256 * ch + px[4 * i + ch]]++;
            }
            id<MTLBuffer> bi = gbuf(px, (size_t)npx * 4), bh = gbuf(NULL, 1024 * 4);
            disp_t d = { make_pipe("hist_rgba_tg", NULL), bi, bh, npx, 256, 128 };
            CHECK(run_seq(&d, 1) && eq_u32(bh.contents, cpu, 1024));
            const uint32_t *h = bh.contents;
            bool sums = true;
            for (int ch = 0; ch < 4; ch++) {
                uint64_t t = 0;
                for (int b = 0; b < 256; b++) t += h[256 * ch + b];
                sums = sums && t == npx;
            }
            CHECK(sums && h[768 + 255] == npx);
            free(px);
        }

        printf("-- 연습 3: atomic-free two-pass (partial + column reduce)\n");
        {
            const uint32_t n = (1u << 24) + 5, tg = 256, ntg = 512;
            uint8_t *p = malloc(n);
            uint32_t cpu[256] = { 0 };
            uint32_t s = 21;
            for (uint32_t i = 0; i < n; i++) { p[i] = (uint8_t)(xs32(&s) % 7 == 0 ? 3 : xs32(&s) >> 24); cpu[p[i]]++; }
            id<MTLBuffer> bi = gbuf(p, n), part = gbuf(NULL, (size_t)ntg * 256 * 4), bh = gbuf(NULL, 256 * 4);
            disp_t d[2] = { { make_pipe("hist_partial", NULL), bi, part, n, tg, ntg },
                            { make_pipe("hist_reduce", NULL), part, bh, ntg, 256, 1 } };
            CHECK(run_seq(d, 2) && eq_u32(bh.contents, cpu, 256));
            uint32_t first[256];
            memcpy(first, bh.contents, sizeof first);
            bool same = true;
            for (int r = 0; r < 3; r++) { memset(bh.contents, 0xAB, 256 * 4); same = run_seq(d, 2) && eq_u32(bh.contents, first, 256) && same; }
            CHECK(same);                                                   /* 출력 버퍼를 0 으로 안 지워도 된다 (덮어씀) */
            free(p);
        }

        printf("-- 연습 4: merge-cost model\n");
        {
            const uint32_t n = 1u << 22, tg = 256;
            uint8_t *p = malloc(n);
            id<MTLComputePipelineState> ps = make_pipe("hist_v2_count", NULL);
            const char *const nm[] = { "uniform", "same", "16 bins" };
            bool all_ok = true;
            for (int kind = 0; kind < 3; kind++) {
                uint32_t s = 33;
                for (uint32_t i = 0; i < n; i++) {
                    uint32_t x = xs32(&s);
                    p[i] = kind == 0 ? (uint8_t)(x >> 24) : kind == 1 ? (uint8_t)42 : (uint8_t)((x >> 24) & 0xF0);
                }
                const uint32_t ntgs[] = { 1, 64, 1024 };
                for (int j = 0; j < 3; j++) {
                    id<MTLBuffer> bi = gbuf(p, n), bh = gbuf(NULL, 257 * 4);
                    disp_t d = { ps, bi, bh, n, tg, ntgs[j] };
                    bool ran = run_seq(&d, 1);
                    uint64_t got = ((const uint32_t *)bh.contents)[256], want = predict_merges(p, n, tg, ntgs[j]);
                    printf("  %-8s ntg %4u: device atomics v1 = %u, v2 merge = %llu (pred %llu), ratio %.0fx\n",
                           nm[kind], ntgs[j], n, (unsigned long long)got, (unsigned long long)want,
                           got ? (double)n / (double)got : 0.0);
                    all_ok = all_ok && ran && got == want;
                }
            }
            CHECK(all_ok);
            /* 공식으로도: uniform 이면 (데이터가 충분할 때) ntg x 256, same 이면 ntg x 1 */
            uint32_t s = 33;
            for (uint32_t i = 0; i < n; i++) p[i] = (uint8_t)(xs32(&s) >> 24);
            CHECK(predict_merges(p, n, tg, 1024) == 1024u * 256u);
            memset(p, 42, n);
            CHECK(predict_merges(p, n, tg, 1024) == 1024u && predict_merges(p, 100, tg, 1024) == 1u);
            free(p);
        }

        printf("%d/%d passed\n", g_pass, g_total);
        return g_pass == g_total ? 0 : 1;
    }
}
