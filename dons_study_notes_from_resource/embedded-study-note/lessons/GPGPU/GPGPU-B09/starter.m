/* GPGPU-B09 Atomic과 Barrier: 레이스와 동기화 — 호스트 (Objective-C + Metal)
 *
 *   커널은 같은 이름의 .metal 파일(starter.m → starter.metal)을 실행 중에 읽어
 *   -[MTLDevice newLibraryWithSource:] 로 컴파일한다 (이 맥엔 `metal` 오프라인 컴파일러가 없다).
 *   __FILE__ 기준으로 찾으므로 레슨 폴더에서 `clang ... starter.m` 으로 빌드하고 레슨 폴더에서 실행한다.
 *
 *   Part A  전역 카운터: plain c[0]++ (관찰만) vs atomic_fetch_add (CHECK: 정확히 n)
 *   Part B  threadgroup 메모리 뒤집기 + barrier  (CHECK: CPU 골든 모델 ref_reverse 와 전부 같음)
 *   Part C  atomic 경합: n 번의 add 를 naddr = 1..1024 주소에 (CHECK: 주소마다 expect_at 과 같음)
 *
 * 체크는 CPU 쪽에서 정확한 개수만 본다. 시간·lost update 개수는 CHECK 하지 않는다
 * → demo_race.m (lost update, barrier 없을 때 틀린 원소 수) · demo_contention.m (Gops/s).
 *
 * build (레슨 폴더에서): clang -fobjc-arc -O2 -Wall -Wextra -Werror X.m -framework Metal -framework Foundation -o /tmp/GPGPU-B09_work/X   (X = starter | solution)
 */
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; printf("PASS  %s\n", #expr); } \
                         else printf("FAIL  %s  (line %d)\n", #expr, __LINE__); } while (0)

/* ======================= GPU 준비 (공통, TODO 없음) ======================= */
static id<MTLDevice> g_dev;
static id<MTLCommandQueue> g_q;
static id<MTLLibrary> g_lib;

static bool gpu_init(void) {
    g_dev = MTLCreateSystemDefaultDevice();
    if (!g_dev) { printf("no Metal device\n"); return false; }
    NSString *path = [[NSString stringWithUTF8String:__FILE__] stringByDeletingPathExtension];
    path = [path stringByAppendingPathExtension:@"metal"];               /* starter.m → starter.metal */
    NSError *err = nil;
    NSString *src = [NSString stringWithContentsOfFile:path encoding:NSUTF8StringEncoding error:&err];
    if (!src) { printf("cannot read %s (run from the lesson dir)\n", path.UTF8String); return false; }
    g_lib = [g_dev newLibraryWithSource:src options:[MTLCompileOptions new] error:&err];
    if (!g_lib) { printf("compile error: %s\n", err.localizedDescription.UTF8String); return false; }
    g_q = [g_dev newCommandQueue];
    return g_q != nil;
}

static id<MTLComputePipelineState> pso(const char *name) {
    id<MTLFunction> f = [g_lib newFunctionWithName:[NSString stringWithUTF8String:name]];
    if (!f) { printf("no kernel %s\n", name); return nil; }
    NSError *err = nil;
    id<MTLComputePipelineState> p = [g_dev newComputePipelineStateWithFunction:f error:&err];
    if (!p) printf("pipeline %s: %s\n", name, err.localizedDescription.UTF8String);
    return p;
}

static id<MTLBuffer> buf(size_t bytes) {   /* shared storage: CPU 와 GPU 가 같은 메모리를 본다 (통합 메모리) */
    id<MTLBuffer> b = [g_dev newBufferWithLength:bytes options:MTLResourceStorageModeShared];
    memset(b.contents, 0, bytes);
    return b;
}

/* 커널 하나를 groups × T 스레드로 실행하고 끝날 때까지 기다린다. 반환 = GPU 시간(ms), 실패 시 -1.
 * bufs 는 [[buffer(0..)]] 순서, consts 는 그 뒤 인덱스에 setBytes 로 넣는 uint32 상수들. */
static double run(const char *kernel, NSArray<id<MTLBuffer>> *bufs, const uint32_t *consts, int nconst,
                  NSUInteger groups, NSUInteger T, NSUInteger tg_bytes) {
    id<MTLComputePipelineState> p = pso(kernel);
    if (!p || T > p.maxTotalThreadsPerThreadgroup) return -1;
    id<MTLCommandBuffer> cb = [g_q commandBuffer];
    id<MTLComputeCommandEncoder> e = [cb computeCommandEncoder];
    [e setComputePipelineState:p];
    NSUInteger i = 0;
    for (id<MTLBuffer> b in bufs) [e setBuffer:b offset:0 atIndex:i++];
    for (int k = 0; k < nconst; k++) [e setBytes:&consts[k] length:sizeof(uint32_t) atIndex:i++];
    if (tg_bytes) [e setThreadgroupMemoryLength:tg_bytes atIndex:0];
    [e dispatchThreadgroups:MTLSizeMake(groups, 1, 1) threadsPerThreadgroup:MTLSizeMake(T, 1, 1)];
    [e endEncoding];
    [cb commit];
    [cb waitUntilCompleted];
    if (cb.status != MTLCommandBufferStatusCompleted) { printf("GPU error: %s\n", cb.error.description.UTF8String); return -1; }
    return (cb.GPUEndTime - cb.GPUStartTime) * 1e3;
}

static NSUInteger groups_for(uint32_t n, NSUInteger T) { return (n + T - 1) / T; }

/* ======================= Part A — 전역 카운터 ======================= */
static uint32_t count_on_gpu(const char *kernel, uint32_t n) {
    id<MTLBuffer> c = buf(sizeof(uint32_t));
    uint32_t k[1] = { n };
    if (run(kernel, @[c], k, 1, groups_for(n, 256), 256, 0) < 0) return 0;
    return ((uint32_t *)c.contents)[0];
}

/* ======================= Part B — CPU 골든 모델 ======================= */
/* 그룹(T 개씩) 안에서 뒤집기. 마지막 그룹은 m = min(T, n - base) 개만 뒤집는다.
 * out[base + l] = in[base + m - 1 - l]   (0 ≤ l < m) */
static void ref_reverse(const uint32_t *in, uint32_t *out, uint32_t n, uint32_t T) {
    (void)in; (void)out; (void)n; (void)T; /* TODO(B-host) */
}

static uint32_t tg_reverse_mismatches(uint32_t n, uint32_t T) {
    id<MTLBuffer> bin = buf(n * 4u), bout = buf(n * 4u);
    uint32_t *in = bin.contents, *out = bout.contents;
    for (uint32_t i = 0; i < n; i++) { in[i] = i * 2654435761u ^ 0x5a5a5a5au; out[i] = 0xFFFFFFFFu; }
    uint32_t k[1] = { n };
    if (run("tg_reverse", @[bin, bout], k, 1, groups_for(n, T), T, T * 4u) < 0) return n;
    uint32_t *want = malloc(n * 4u), bad = 0;
    memset(want, 0, n * 4u);
    ref_reverse(in, want, n, T);
    for (uint32_t i = 0; i < n; i++) bad += (out[i] != want[i]);
    free(want);
    return bad;
}

/* ======================= Part C — 경합 sweep 의 기대값 ======================= */
/* i = 0..n-1 이 주소 i % naddr 를 1 씩 올렸을 때 주소 a 의 최종 값 */
static uint32_t expect_at(uint32_t n, uint32_t naddr, uint32_t a) {
    (void)n; (void)naddr; (void)a;
    return 0; /* TODO(C-host) */
}

static bool spread_ok(uint32_t n, uint32_t naddr) {
    id<MTLBuffer> b = buf(naddr * 4u);
    uint32_t k[2] = { n, naddr };
    if (run("spread_add", @[b], k, 2, groups_for(n, 256), 256, 0) < 0) return false;
    const uint32_t *v = b.contents;
    uint64_t sum = 0;
    bool ok = true;
    for (uint32_t a = 0; a < naddr; a++) { sum += v[a]; ok = ok && v[a] == expect_at(n, naddr, a); }
    return ok && sum == n;
}

int main(void) {
    @autoreleasepool {
        if (!gpu_init()) return 1;
        printf("device: %s\n", g_dev.name.UTF8String);
        const char *san = getenv("B09_SMALL");          /* 새니타이저 빌드에서는 B09_SMALL=1 로 크기를 줄인다 */
        uint32_t N = san ? (1u << 16) : (1u << 20);

        /* Part A */
        uint32_t plain = count_on_gpu("count_plain", N);
        printf("INFO  plain c[0]++ : %u / %u  (lost %u — 관찰값, CHECK 아님)\n", plain, N, N - plain);
        CHECK(count_on_gpu("count_atomic", N) == N);
        CHECK(count_on_gpu("count_atomic", 1000) == 1000);

        /* Part B — 골든 모델 자체 (손으로 푼 예) */
        uint32_t in8[8] = { 10, 11, 12, 13, 14, 15, 16, 17 }, o8[8] = { 0 };
        ref_reverse(in8, o8, 8, 4);
        CHECK(o8[0] == 13 && o8[3] == 10 && o8[4] == 17 && o8[7] == 14);
        uint32_t o6[6] = { 0 };
        ref_reverse(in8, o6, 6, 4);                     /* 마지막 그룹 m = 2 */
        CHECK(o6[0] == 13 && o6[3] == 10 && o6[4] == 15 && o6[5] == 14);
        /* Part B — GPU vs 골든 모델 */
        CHECK(tg_reverse_mismatches(N, 32) == 0);       /* 한 simdgroup = 한 threadgroup */
        CHECK(tg_reverse_mismatches(N, 256) == 0);      /* 8 simdgroup 사이를 건너 읽는다 */
        CHECK(tg_reverse_mismatches(N, 1024) == 0);
        CHECK(tg_reverse_mismatches(1000, 256) == 0);   /* 마지막 그룹 232 개 */
        CHECK(tg_reverse_mismatches(77, 64) == 0);      /* 마지막 그룹 13 개 */

        /* Part C */
        CHECK(expect_at(10, 4, 0) == 3 && expect_at(10, 4, 1) == 3 && expect_at(10, 4, 2) == 2);
        CHECK(expect_at(4, 8, 3) == 1 && expect_at(4, 8, 4) == 0);
        uint32_t M = san ? (1u << 18) : (1u << 22);
        bool all = true;
        for (uint32_t naddr = 1; naddr <= 1024; naddr *= 2) all = all && spread_ok(M, naddr);
        CHECK(all);
        CHECK(spread_ok(M + 7, 3));                     /* 나누어떨어지지 않는 경우 */

        printf("%d/%d passed\n", g_pass, g_total);
        return g_pass == g_total ? 0 : 1;
    }
}
