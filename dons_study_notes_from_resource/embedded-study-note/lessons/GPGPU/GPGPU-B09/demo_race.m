/* GPGPU-B09 demo_race — lost update 와 "barrier 를 뺐을 때" 를 실제 GPU 에서 세어 본다 (CHECK 없음)
 *
 *   A  전역 카운터 c[0]++ : plain / volatile / coherent(device) / atomic, n = 2^8 .. 2^20, 5 회씩
 *   B  threadgroup 뒤집기 : barrier 없음 / simdgroup_barrier / threadgroup_barrier(mem_none) / threadgroup_barrier,
 *      T = 32, 64, 256, 1024, skew(홀수 simdgroup 지연) 0 과 2000, n = 2^20, 5 회씩 틀린 원소 수
 *   C  plain c[0]++ 를 한 encoder 에 10 번 dispatch (n = 1, 32, 2^20) → dispatch 사이에는 쓰기가 보이나?
 *   ./demo_race oob   : threadgroup 메모리 범위 밖 쓰기 커널 (MTL_SHADER_VALIDATION=1 과 함께 실행해 볼 것)
 *
 * build (레슨 폴더에서): clang -fobjc-arc -O2 -Wall -Wextra -Werror demo_race.m -framework Metal -framework Foundation -o /tmp/GPGPU-B09_work/demo_race
 */
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <stdint.h>
#include <stdio.h>
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

static id<MTLBuffer> buf(size_t bytes) {
    id<MTLBuffer> b = [g_dev newBufferWithLength:bytes options:MTLResourceStorageModeShared];
    memset(b.contents, 0, bytes);
    return b;
}

static int run(const char *kernel, NSArray<id<MTLBuffer>> *bufs, const uint32_t *consts, int nconst,
               NSUInteger groups, NSUInteger T, NSUInteger tg_bytes) {
    NSError *err = nil;
    id<MTLFunction> f = [g_lib newFunctionWithName:[NSString stringWithUTF8String:kernel]];
    id<MTLComputePipelineState> p = [g_dev newComputePipelineStateWithFunction:f error:&err];
    if (!p) { printf("pipeline %s: %s\n", kernel, err.localizedDescription.UTF8String); return 0; }
    id<MTLCommandBuffer> cb = [g_q commandBuffer];
    id<MTLComputeCommandEncoder> e = [cb computeCommandEncoder];
    [e setComputePipelineState:p];
    NSUInteger i = 0;
    for (id<MTLBuffer> b in bufs) [e setBuffer:b offset:0 atIndex:i++];
    for (int k = 0; k < nconst; k++) [e setBytes:&consts[k] length:4 atIndex:i++];
    if (tg_bytes) [e setThreadgroupMemoryLength:tg_bytes atIndex:0];
    [e dispatchThreadgroups:MTLSizeMake(groups, 1, 1) threadsPerThreadgroup:MTLSizeMake(T, 1, 1)];
    [e endEncoding];
    [cb commit];
    [cb waitUntilCompleted];
    if (cb.status != MTLCommandBufferStatusCompleted) {
        printf("  command buffer status=%ld error=%s\n", (long)cb.status, cb.error.localizedDescription.UTF8String);
        return 0;
    }
    return 1;
}

static void part_a(void) {
    const char *names[4] = { "cnt_plain", "cnt_volatile", "cnt_coherent", "cnt_atomic" };
    uint32_t ns[5] = { 1u << 8, 1u << 10, 1u << 14, 1u << 17, 1u << 20 };
    printf("A) 전역 카운터: 결과 / n  (5회, T=256)\n");
    for (int v = 0; v < 4; v++) {
        for (int j = 0; j < 5; j++) {
            uint32_t n = ns[j];
            printf("  %-13s n=%-8u :", names[v], n);
            for (int r = 0; r < 5; r++) {
                id<MTLBuffer> c = buf(4);
                uint32_t k[1] = { n };
                run(names[v], @[c], k, 1, (n + 255) / 256, 256, 0);
                printf(" %u", ((uint32_t *)c.contents)[0]);
            }
            printf("\n");
        }
    }
}

static uint32_t rev_wrong(const char *kernel, uint32_t n, uint32_t T, uint32_t skew, uint32_t salt) {
    id<MTLBuffer> bin = buf(n * 4u), bout = buf(n * 4u), sink = buf(4);
    uint32_t *in = bin.contents, *out = bout.contents;
    for (uint32_t i = 0; i < n; i++) { in[i] = (i * 2654435761u) ^ salt; out[i] = 0xFFFFFFFFu; }
    uint32_t k[1] = { skew };
    if (!run(kernel, @[bin, bout, sink], k, 1, n / T, T, T * 4u)) return UINT32_MAX;
    uint32_t bad = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t base = i / T * T, l = i - base;
        bad += out[i] != in[base + T - 1 - l];
    }
    return bad;
}

static void part_b(void) {
    const char *names[4] = { "rev_nobar", "rev_simdbar", "rev_tgbar_none", "rev_tgbar" };
    uint32_t Ts[4] = { 32, 64, 256, 1024 }, skews[2] = { 0, 2000 };
    uint32_t n = 1u << 20;
    printf("B) threadgroup 뒤집기: 틀린 원소 수 / %u  (5회)\n", n);
    for (int s = 0; s < 2; s++)
        for (int v = 0; v < 4; v++)
            for (int t = 0; t < 4; t++) {
                printf("  skew=%-4u %-15s T=%-4u :", skews[s], names[v], Ts[t]);
                for (int r = 0; r < 5; r++)
                    printf(" %u", rev_wrong(names[v], n, Ts[t], skews[s], 0x9e3779b9u * (uint32_t)(r + 1)));
                printf("\n");
            }
}

/* C: plain c[0]++ 커널을 한 encoder 에 10 번 dispatch → dispatch 경계에서는 앞의 쓰기가 보인다 */
static void part_c(void) {
    NSError *err = nil;
    id<MTLComputePipelineState> p =
        [g_dev newComputePipelineStateWithFunction:[g_lib newFunctionWithName:@"cnt_plain"] error:&err];
    uint32_t ns[3] = { 1, 32, 1u << 20 };
    printf("C) plain c[0]++ 를 10 dispatch (한 command buffer, serial encoder)\n");
    for (int j = 0; j < 3; j++) {
        id<MTLBuffer> c = buf(4);
        id<MTLCommandBuffer> cb = [g_q commandBuffer];
        id<MTLComputeCommandEncoder> e = [cb computeCommandEncoder];
        [e setComputePipelineState:p];
        [e setBuffer:c offset:0 atIndex:0];
        [e setBytes:&ns[j] length:4 atIndex:1];
        NSUInteger T = ns[j] < 256 ? ns[j] : 256;
        for (int k = 0; k < 10; k++)
            [e dispatchThreadgroups:MTLSizeMake(ns[j] / T, 1, 1) threadsPerThreadgroup:MTLSizeMake(T, 1, 1)];
        [e endEncoding];
        [cb commit];
        [cb waitUntilCompleted];
        printf("  10 x %-8u threads → c = %u\n", ns[j], ((uint32_t *)c.contents)[0]);
    }
}

int main(int argc, char **argv) {
    @autoreleasepool {
        if (!gpu_init()) return 1;
        printf("device: %s, MTL_SHADER_VALIDATION=%s\n", g_dev.name.UTF8String,
               getenv("MTL_SHADER_VALIDATION") ? getenv("MTL_SHADER_VALIDATION") : "(unset)");
        if (argc > 1 && strcmp(argv[1], "oob") == 0) {
            printf("oob: rev_oob T=256 n=4096 → wrong = %u\n", rev_wrong("rev_oob", 4096, 256, 0, 1));
            return 0;
        }
        part_a();
        part_b();
        part_c();
    }
    return 0;
}
