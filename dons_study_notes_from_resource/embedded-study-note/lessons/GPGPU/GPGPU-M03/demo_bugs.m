/* GPGPU-M03 demo_bugs — 틀린 v2 다섯 가지를 각각 RUNS 번 돌려 "몇 번 틀렸나 / 얼마나 틀렸나" 를 센다 (CHECK 없음)
 *   ./bugs [runs] [ntg] [tg]     기본 20회, ntg 1024, tg 256, 입력 = uniform 2^24 바이트
 *   DIRTY=1 ./bugs   각 실행 전에 같은 인코더에서 threadgroup 메모리를 0xDEAD 로 채우는 커널을 먼저 돌린다 (bug_no_zero 확인)
 *   OOB=1 ./bugs     bin+1 off-by-one 커널을 한 번 돌린다. 셰이더 검증 보고 확인용:
 *                    MTL_SHADER_VALIDATION=1 MTL_SHADER_VALIDATION_REPORT_TO_STDERR=1 OOB=1 ./bugs
 *   틀린 결과가 0번 나와도 "맞다" 는 뜻이 아니다 — 그 실행에서 그 인터리빙이 안 일어났을 뿐.
 * build: clang -fobjc-arc -O2 -Wall -Wextra -Werror demo_bugs.m -framework Metal -framework Foundation -o bugs
 *        ./bugs        (레슨 폴더에서 — demo_bugs.metal 을 읽는다)
 */
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }
typedef struct { uint32_t n_bytes, n_words; } HistParams;

int main(int argc, char **argv) {
    @autoreleasepool {
        int runs = argc > 1 ? atoi(argv[1]) : 20;
        uint32_t ntg = argc > 2 ? (uint32_t)atoi(argv[2]) : 1024, tg = argc > 3 ? (uint32_t)atoi(argv[3]) : 256;
        id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
        NSError *err = nil;
        NSString *src = [NSString stringWithContentsOfFile:@"demo_bugs.metal" encoding:NSUTF8StringEncoding error:&err];
        id<MTLLibrary> lib = src ? [dev newLibraryWithSource:src options:nil error:&err] : nil;
        if (!lib) { printf("need demo_bugs.metal in cwd: %s\n", err.localizedDescription.UTF8String); return 1; }
        id<MTLCommandQueue> q = [dev newCommandQueue];
        const size_t N = (size_t)1 << 24;
        uint8_t *p = malloc(N);
        uint32_t ref[256] = { 0 }, s = 7;
        for (size_t i = 0; i < N; i++) { p[i] = (uint8_t)(xs32(&s) >> 24); ref[p[i]]++; }
        id<MTLBuffer> in = [dev newBufferWithBytes:p length:N options:MTLResourceStorageModeShared];
        id<MTLBuffer> out = [dev newBufferWithLength:1024 options:MTLResourceStorageModeShared];
        const char *const names[] = { "ok_v2", "bug_no_zero", "bug_no_barrier1", "bug_no_barrier2", "bug_plain_merge", "bug_rmw_merge" };
        if (getenv("OOB")) {
            id<MTLComputePipelineState> ps = [dev newComputePipelineStateWithFunction:[lib newFunctionWithName:@"bug_oob"] error:&err];
            id<MTLCommandBuffer> cb = [q commandBuffer];
            id<MTLComputeCommandEncoder> e = [cb computeCommandEncoder];
            [e setComputePipelineState:ps];
            [e setBuffer:in offset:0 atIndex:0];
            [e setBuffer:out offset:0 atIndex:1];
            [e dispatchThreadgroups:MTLSizeMake(4, 1, 1) threadsPerThreadgroup:MTLSizeMake(256, 1, 1)];
            [e endEncoding];
            [cb commit];
            [cb waitUntilCompleted];
            printf("bug_oob: status %ld, error %s\n", (long)cb.status, cb.error ? cb.error.localizedDescription.UTF8String : "none");
            free(p);
            return 0;
        }
        id<MTLComputePipelineState> dirty = [dev newComputePipelineStateWithFunction:[lib newFunctionWithName:@"dirty"] error:&err];
        bool use_dirty = getenv("DIRTY") != NULL;
        printf("uniform N = 2^24, tg %u x ntg %u, %d runs each%s\n", tg, ntg, runs, use_dirty ? " (threadgroup memory dirtied first)" : "");
        printf("%-16s | wrong runs | sum(|gpu-cpu|) min .. max over runs | total count min .. max (want %zu)\n", "kernel", N);
        for (int k = 0; k < 6; k++) {
            id<MTLComputePipelineState> ps = [dev newComputePipelineStateWithFunction:
                [lib newFunctionWithName:[NSString stringWithUTF8String:names[k]]] error:&err];
            if (!ps) { printf("%s: %s\n", names[k], err.localizedDescription.UTF8String); return 1; }
            int wrong = 0;
            uint64_t emin = UINT64_MAX, emax = 0, tmin = UINT64_MAX, tmax = 0;
            for (int r = 0; r < runs; r++) {
                memset(out.contents, 0, 1024);
                HistParams P = { (uint32_t)N, (uint32_t)(N / 4) };
                id<MTLCommandBuffer> cb = [q commandBuffer];
                id<MTLComputeCommandEncoder> e = [cb computeCommandEncoder];
                if (use_dirty) {
                    [e setComputePipelineState:dirty];
                    [e setBuffer:out offset:0 atIndex:1];
                    [e dispatchThreadgroups:MTLSizeMake(4096, 1, 1) threadsPerThreadgroup:MTLSizeMake(256, 1, 1)];
                }
                [e setComputePipelineState:ps];
                [e setBuffer:in offset:0 atIndex:0];
                [e setBuffer:out offset:0 atIndex:1];
                [e setBytes:&P length:sizeof P atIndex:2];
                [e dispatchThreadgroups:MTLSizeMake(ntg, 1, 1) threadsPerThreadgroup:MTLSizeMake(tg, 1, 1)];
                [e endEncoding];
                [cb commit];
                [cb waitUntilCompleted];
                const uint32_t *h = out.contents;
                uint64_t err_sum = 0, tot = 0;
                for (int b = 0; b < 256; b++) { err_sum += h[b] > ref[b] ? h[b] - ref[b] : ref[b] - h[b]; tot += h[b]; }
                if (err_sum) wrong++;
                if (err_sum < emin) emin = err_sum;
                if (err_sum > emax) emax = err_sum;
                if (tot < tmin) tmin = tot;
                if (tot > tmax) tmax = tot;
            }
            printf("%-16s | %4d/%-4d  | %10llu .. %-10llu | %10llu .. %llu\n", names[k], wrong, runs,
                   (unsigned long long)emin, (unsigned long long)emax, (unsigned long long)tmin, (unsigned long long)tmax);
        }
        free(p);
        return 0;
    }
}
