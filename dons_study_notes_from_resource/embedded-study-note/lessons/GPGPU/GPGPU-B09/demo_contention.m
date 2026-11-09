/* GPGPU-B09 demo_contention — 2^22 개 atomic add 를 1, 2, 4, ..., 1024 개 주소에 나눠 쳤을 때 처리량 (CHECK 없음)
 *
 *   interleaved/packed : 주소 = gid % naddr, 주소끼리 4 B 간격 (한 128 B 줄에 32 개)
 *   interleaved/128B   : 같은 주소 배치, 주소끼리 128 B 간격 (줄마다 하나)
 *   blocked/packed     : 주소 = gid / (n/naddr) — simdgroup 32 lane 이 모두 같은 주소
 *   simd_agg (naddr=1) : simd_sum 후 lane 0 만 atomic (n/32 번)
 *   nop / store_only   : 바닥선 (스레드만 띄우기 / atomic 없이 자기 칸에 쓰기)
 * 시간 = commandBuffer GPUEndTime - GPUStartTime ÷ 16 (dispatch 16 번을 한 command buffer 에), 워밍업 후 7 회 중앙값. 각 실행 후 합계가 n 인지 확인한다.
 *
 * build (레슨 폴더에서): clang -fobjc-arc -O2 -Wall -Wextra -Werror demo_contention.m -framework Metal -framework Foundation -o /tmp/GPGPU-B09_work/demo_contention
 */
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static id<MTLDevice> g_dev;
static id<MTLCommandQueue> g_q;
static id<MTLLibrary> g_lib;

static int gpu_init(void) {
    g_dev = MTLCreateSystemDefaultDevice();
    NSString *path = [[[NSString stringWithUTF8String:__FILE__] stringByDeletingPathExtension]
                      stringByAppendingPathExtension:@"metal"];
    NSError *err = nil;
    NSString *src = [NSString stringWithContentsOfFile:path encoding:NSUTF8StringEncoding error:&err];
    if (!src) { printf("cannot read %s\n", path.UTF8String); return 0; }
    g_lib = [g_dev newLibraryWithSource:src options:[MTLCompileOptions new] error:&err];
    if (!g_lib) { printf("compile: %s\n", err.localizedDescription.UTF8String); return 0; }
    g_q = [g_dev newCommandQueue];
    return 1;
}

#define REPS 16   /* dispatch 16 번을 한 command buffer 로 → 시간 / 16 */

static int cmp_d(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

/* 7 회 실행, 중앙값 ms. *ok = 모든 실행에서 합계 == n (nop·store_only 는 검사 안 함) */
static double bench(const char *kernel, uint32_t n, uint32_t naddr, uint32_t stride, int *ok) {
    NSError *err = nil;
    id<MTLComputePipelineState> p =
        [g_dev newComputePipelineStateWithFunction:[g_lib newFunctionWithName:[NSString stringWithUTF8String:kernel]]
                                             error:&err];
    if (!p) { printf("pipeline: %s\n", err.localizedDescription.UTF8String); exit(1); }
    int is_store = strcmp(kernel, "store_only") == 0 || strcmp(kernel, "nop") == 0;
    size_t words = is_store ? n : (size_t)naddr * stride;
    id<MTLBuffer> b = [g_dev newBufferWithLength:words * 4 options:MTLResourceStorageModeShared];
    double ms[7];
    *ok = 1;
    for (int r = 0; r < 7; r++) {
        memset(b.contents, 0, words * 4);
        id<MTLCommandBuffer> cb = [g_q commandBuffer];
        id<MTLComputeCommandEncoder> e = [cb computeCommandEncoder];
        [e setComputePipelineState:p];
        [e setBuffer:b offset:0 atIndex:0];
        [e setBytes:&n length:4 atIndex:1];
        [e setBytes:&naddr length:4 atIndex:2];
        [e setBytes:&stride length:4 atIndex:3];
        for (int k = 0; k < REPS; k++)   /* 같은 encoder 에 REPS 번: 한 번(수십 µs)만 재면 클럭·타이머에 휘둘린다 */
            [e dispatchThreadgroups:MTLSizeMake((n + 255) / 256, 1, 1) threadsPerThreadgroup:MTLSizeMake(256, 1, 1)];
        [e endEncoding];
        [cb commit];
        [cb waitUntilCompleted];
        ms[r] = (cb.GPUEndTime - cb.GPUStartTime) * 1e3 / REPS;
        if (!is_store) {
            uint64_t sum = 0;
            const uint32_t *v = b.contents;
            for (size_t i = 0; i < words; i++) sum += v[i];
            if (sum != (uint64_t)n * REPS) *ok = 0;
        }
    }
    qsort(ms, 7, sizeof ms[0], cmp_d);
    return ms[3];
}

int main(void) {
    @autoreleasepool {
        if (!gpu_init()) return 1;
        const uint32_t n = 1u << 22;
        int ok;
        for (int w = 0; w < 3; w++) bench("add_interleaved", n, 64, 1, &ok);   /* 워밍업: GPU 클럭을 올린다 */
        printf("device: %s, n = %u atomic adds per dispatch, T = 256, 16 dispatches per command buffer, median of 7\n", g_dev.name.UTF8String, n);
        double nop = bench("nop", n, 1, 1, &ok);
        printf("nop (launch only)        : %.3f ms  %.2f G threads/s\n", nop, n / nop / 1e6);
        double base = bench("store_only", n, 1, 1, &ok);
        printf("store_only (no atomic)   : %.3f ms  %.2f G stores/s\n", base, n / base / 1e6);
        double agg = bench("add_simd_agg", n, 1, 1, &ok);
        printf("simd_agg naddr=1         : %.3f ms  %.2f G adds/s (logical)  sum_ok=%d\n", agg, n / agg / 1e6, ok);
        printf("%7s | %22s | %22s | %22s\n", "naddr", "interleaved/packed", "interleaved/128B", "blocked/packed");
        for (uint32_t naddr = 1; naddr <= 1024; naddr *= 2) {
            int ok1, ok2, ok3;
            double a = bench("add_interleaved", n, naddr, 1, &ok1);
            double s = bench("add_interleaved", n, naddr, 32, &ok2);
            double bl = bench("add_blocked", n, naddr, 1, &ok3);
            printf("%7u | %7.3f ms %6.2f Gop/s | %7.3f ms %6.2f Gop/s | %7.3f ms %6.2f Gop/s  ok=%d%d%d\n", naddr,
                   a, n / a / 1e6, s, n / s / 1e6, bl, n / bl / 1e6, ok1, ok2, ok3);
        }
    }
    return 0;
}
