/* GPGPU-B09 추가 연습 — 호스트 (Objective-C + Metal). 커널 = practice.metal (실행 중 컴파일)
 *
 *   연습 1  threadgroup 트리 합 → 그룹 부분합이 CPU 와 같다
 *   연습 2  두 dispatch (부분합 → 합치기) 를 한 encoder 에 → 최종 합이 CPU 와 같다 (커널 안 grid barrier 없음)
 *   연습 3  atomic 칸 예약 스트림 압축 → 개수 같고, 정렬하면 CPU 필터 결과와 같다 (순서는 비결정적)
 *   연습 4  simd_sum + simdgroup 당 atomic 한 번 → 개수 같고, atomic 횟수 = "참이 하나라도 있는 32 묶음" 수
 *   연습 5  한 dispatch 단일 패스 합 (마지막 threadgroup 이 합침, fence + ticket) → 5 회 모두 CPU 와 같다
 *
 * build (레슨 폴더에서): clang -fobjc-arc -O2 -Wall -Wextra -Werror practice.m -framework Metal -framework Foundation -o /tmp/GPGPU-B09_work/practice
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

static id<MTLDevice> g_dev;
static id<MTLCommandQueue> g_q;
static id<MTLLibrary> g_lib;

static bool gpu_init(void) {
    g_dev = MTLCreateSystemDefaultDevice();
    if (!g_dev) return false;
    NSString *path = [[[NSString stringWithUTF8String:__FILE__] stringByDeletingPathExtension]
                      stringByAppendingPathExtension:@"metal"];
    NSError *err = nil;
    NSString *src = [NSString stringWithContentsOfFile:path encoding:NSUTF8StringEncoding error:&err];
    if (!src) { printf("cannot read %s (run from the lesson dir)\n", path.UTF8String); return false; }
    g_lib = [g_dev newLibraryWithSource:src options:[MTLCompileOptions new] error:&err];
    if (!g_lib) { printf("compile error: %s\n", err.localizedDescription.UTF8String); return false; }
    g_q = [g_dev newCommandQueue];
    return true;
}

static id<MTLComputePipelineState> pso(const char *name) {
    NSError *err = nil;
    id<MTLComputePipelineState> p =
        [g_dev newComputePipelineStateWithFunction:[g_lib newFunctionWithName:[NSString stringWithUTF8String:name]]
                                             error:&err];
    if (!p) printf("pipeline %s: %s\n", name, err.localizedDescription.UTF8String);
    return p;
}

static id<MTLBuffer> buf(size_t bytes) {
    id<MTLBuffer> b = [g_dev newBufferWithLength:bytes options:MTLResourceStorageModeShared];
    memset(b.contents, 0, bytes);
    return b;
}

/* encoder 에 dispatch 하나를 추가 (버퍼 0.., 그 뒤 uint32 상수들) */
static void enc(id<MTLComputeCommandEncoder> e, const char *kernel, NSArray<id<MTLBuffer>> *bufs,
                const uint32_t *consts, int nconst, NSUInteger groups, NSUInteger T, NSUInteger tg_bytes) {
    [e setComputePipelineState:pso(kernel)];
    NSUInteger i = 0;
    for (id<MTLBuffer> b in bufs) [e setBuffer:b offset:0 atIndex:i++];
    for (int k = 0; k < nconst; k++) [e setBytes:&consts[k] length:4 atIndex:i++];
    if (tg_bytes) [e setThreadgroupMemoryLength:tg_bytes atIndex:0];
    [e dispatchThreadgroups:MTLSizeMake(groups, 1, 1) threadsPerThreadgroup:MTLSizeMake(T, 1, 1)];
}

static bool finish(id<MTLCommandBuffer> cb) {
    [cb commit];
    [cb waitUntilCompleted];
    if (cb.status != MTLCommandBufferStatusCompleted) printf("GPU error: %s\n", cb.error.description.UTF8String);
    return cb.status == MTLCommandBufferStatusCompleted;
}

static bool run1(const char *kernel, NSArray<id<MTLBuffer>> *bufs, const uint32_t *consts, int nconst,
                 NSUInteger groups, NSUInteger T, NSUInteger tg_bytes) {
    id<MTLCommandBuffer> cb = [g_q commandBuffer];
    id<MTLComputeCommandEncoder> e = [cb computeCommandEncoder];
    enc(e, kernel, bufs, consts, nconst, groups, T, tg_bytes);
    [e endEncoding];
    return finish(cb);
}

static id<MTLBuffer> make_input(uint32_t n, uint32_t seed) {     /* xorshift32, 값 0..255 */
    id<MTLBuffer> b = buf(n * 4u);
    uint32_t *v = b.contents, x = seed;
    for (uint32_t i = 0; i < n; i++) { x ^= x << 13; x ^= x >> 17; x ^= x << 5; v[i] = x & 0xFFu; }
    return b;
}

static uint32_t cpu_sum(const uint32_t *v, uint32_t n) {
    uint32_t s = 0;
    for (uint32_t i = 0; i < n; i++) s += v[i];
    return s;
}

static int cmp_u32(const void *a, const void *b) {
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return (x > y) - (x < y);
}

/* ---------- 연습 1 ---------- */
static bool ex1(uint32_t n, uint32_t T) {
    id<MTLBuffer> in = make_input(n, 0x1234567u + n);
    uint32_t G = (n + T - 1) / T;
    id<MTLBuffer> part = buf(G * 4u);
    uint32_t k[1] = { n };
    if (!run1("tg_reduce", @[in, part], k, 1, G, T, T * 4u)) return false;
    const uint32_t *v = in.contents, *p = part.contents;
    for (uint32_t g = 0; g < G; g++) {
        uint32_t m = (n - g * T < T) ? n - g * T : T;
        if (p[g] != cpu_sum(v + g * T, m)) return false;
    }
    return true;
}

/* ---------- 연습 2 ---------- */
static bool ex2(uint32_t n, uint32_t T) {
    id<MTLBuffer> in = make_input(n, 0xBEEFu + n);
    uint32_t G = (n + T - 1) / T;
    id<MTLBuffer> part = buf(G * 4u), res = buf(4);
    uint32_t k1[1] = { n }, k2[1] = { G };
    id<MTLCommandBuffer> cb = [g_q commandBuffer];
    id<MTLComputeCommandEncoder> e = [cb computeCommandEncoder];   /* 기본 = serial dispatch: 앞 dispatch 가 끝나야 다음 */
    enc(e, "tg_reduce", @[in, part], k1, 1, G, T, T * 4u);
    enc(e, "sum_partials", @[part, res], k2, 1, 1, 1024, 1024 * 4u);
    [e endEncoding];
    if (!finish(cb)) return false;
    return ((uint32_t *)res.contents)[0] == cpu_sum(in.contents, n);
}

/* ---------- 연습 3 ---------- */
static bool ex3(uint32_t n) {
    id<MTLBuffer> in = buf(n * 4u), out = buf(n * 4u), cnt = buf(4);
    uint32_t *v = in.contents;
    for (uint32_t i = 0; i < n; i++) v[i] = i * 7u + 1u;           /* 서로 다른 값 → 정렬 비교가 정확 */
    uint32_t k[1] = { n };
    if (!run1("compact", @[in, out, cnt], k, 1, (n + 255) / 256, 256, 0)) return false;
    uint32_t c = ((uint32_t *)cnt.contents)[0];
    uint32_t *want = malloc(n * 4u), w = 0;
    for (uint32_t i = 0; i < n; i++) if (v[i] % 3u == 0u) want[w++] = v[i];
    bool ok = (c == w);
    if (ok) {
        uint32_t *got = out.contents;
        qsort(got, c, 4, cmp_u32);
        ok = memcmp(got, want, (size_t)c * 4u) == 0;               /* want 는 이미 오름차순 */
    }
    free(want);
    return ok;
}

/* ---------- 연습 4 ---------- */
static bool lane_map_ok(uint32_t T) {
    id<MTLBuffer> bad = buf(4);
    if (!run1("lane_map", @[bad], NULL, 0, 64, T, 0)) return false;
    return ((uint32_t *)bad.contents)[0] == 0;
}

static bool ex4(uint32_t n, uint32_t *count_out, uint32_t *ops_out) {
    id<MTLBuffer> in = make_input(n, 0xC0FFEEu);
    id<MTLBuffer> cnt = buf(8);
    uint32_t k[1] = { n };
    if (!run1("count_simd_agg", @[in, cnt], k, 1, (n + 255) / 256, 256, 0)) return false;
    const uint32_t *v = in.contents, *c = cnt.contents;
    uint32_t want = 0, chunks = 0;
    for (uint32_t base = 0; base < n; base += 32) {                /* 32 개 묶음 = simdgroup 하나 (lane_map 으로 확인) */
        uint32_t s = 0;
        for (uint32_t i = base; i < base + 32 && i < n; i++) s += (v[i] & 7u) == 0u;
        want += s;
        chunks += (s != 0);
    }
    *count_out = c[0];
    *ops_out = c[1];
    return c[0] == want && c[1] == chunks;
}

/* ---------- 연습 5 ---------- */
static bool ex5(uint32_t n, uint32_t T, int runs) {
    id<MTLBuffer> in = make_input(n, 0xFACEu + n);
    uint32_t G = (n + T - 1) / T, want = cpu_sum(in.contents, n);
    bool ok = true;
    for (int r = 0; r < runs; r++) {
        id<MTLBuffer> part = buf(G * 4u), ticket = buf(4), res = buf(4);
        uint32_t k[1] = { n };
        if (!run1("reduce_last", @[in, part, ticket, res], k, 1, G, T, T * 4u)) return false;
        ok = ok && ((uint32_t *)res.contents)[0] == want && ((uint32_t *)ticket.contents)[0] == G;
    }
    return ok;
}

int main(void) {
    @autoreleasepool {
        if (!gpu_init()) return 1;
        printf("device: %s\n", g_dev.name.UTF8String);
        const bool small = getenv("B09_SMALL") != NULL;
        uint32_t N = small ? (1u << 16) : (1u << 20);

        CHECK(ex1(N, 256));
        CHECK(ex1(N, 1024));
        CHECK(ex1(1000, 64));                 /* 마지막 그룹 40 개 */

        CHECK(ex2(N, 256));
        CHECK(ex2(12345, 128));

        CHECK(ex3(N));
        CHECK(ex3(1000));

        CHECK(lane_map_ok(256));
        CHECK(lane_map_ok(1024));
        uint32_t count = 0, ops = 0;
        CHECK(ex4(N, &count, &ops));
        printf("INFO  연습 4: 개수 %u, atomic %u 번 (스레드마다 하면 %u 번)\n", count, ops, count);

        CHECK(ex5(N, 256, 5));
        CHECK(ex5(77777, 1024, 5));

        printf("%d/%d passed\n", g_pass, g_total);
        return g_pass == g_total ? 0 : 1;
    }
}
