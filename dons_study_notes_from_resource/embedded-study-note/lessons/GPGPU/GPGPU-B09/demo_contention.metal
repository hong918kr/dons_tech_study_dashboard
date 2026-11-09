// GPGPU-B09 demo_contention — atomic 경합 sweep 커널 (스레드 하나 = atomic add 하나)
#include <metal_stdlib>
using namespace metal;

// 주소 = (gid % naddr) * stride : 이웃 lane 이 서로 다른 주소 (simdgroup 하나가 min(32, naddr) 개 주소를 친다)
kernel void add_interleaved(device atomic_uint *bins [[buffer(0)]], constant uint &n [[buffer(1)]],
                            constant uint &naddr [[buffer(2)]], constant uint &stride [[buffer(3)]],
                            uint gid [[thread_position_in_grid]])
{
    if (gid < n) atomic_fetch_add_explicit(&bins[(gid % naddr) * stride], 1u, memory_order_relaxed);
}

// 주소 = (gid / (n / naddr)) * stride : 연속한 n/naddr 스레드가 같은 주소 (simdgroup 전체가 한 주소)
kernel void add_blocked(device atomic_uint *bins [[buffer(0)]], constant uint &n [[buffer(1)]],
                        constant uint &naddr [[buffer(2)]], constant uint &stride [[buffer(3)]],
                        uint gid [[thread_position_in_grid]])
{
    if (gid < n) atomic_fetch_add_explicit(&bins[(gid / (n / naddr)) * stride], 1u, memory_order_relaxed);
}

// 참고: simdgroup 안에서 먼저 더하고 (simd_sum) lane 0 만 atomic 한 번 → 주소 1 개에 n/32 번
kernel void add_simd_agg(device atomic_uint *bins [[buffer(0)]], constant uint &n [[buffer(1)]],
                         constant uint &naddr [[buffer(2)]], constant uint &stride [[buffer(3)]],
                         uint gid [[thread_position_in_grid]])
{
    (void)naddr; (void)stride;
    uint s = simd_sum(gid < n ? 1u : 0u);
    if (simd_is_first() && s) atomic_fetch_add_explicit(&bins[0], s, memory_order_relaxed);
}

// 바닥선: atomic 없이 스레드마다 자기 칸에 한 번 쓰기 (런치 + 스레드 스케줄 + 쓰기 비용)
kernel void store_only(device uint *out [[buffer(0)]], constant uint &n [[buffer(1)]],
                       constant uint &naddr [[buffer(2)]], constant uint &stride [[buffer(3)]],
                       uint gid [[thread_position_in_grid]])
{
    (void)naddr; (void)stride;
    if (gid < n) out[gid] = gid;
}

// 바닥선 2: 거의 아무것도 안 하는 커널 (스레드 2^22 개를 띄우는 비용만)
kernel void nop(device uint *out [[buffer(0)]], constant uint &n [[buffer(1)]],
                constant uint &naddr [[buffer(2)]], constant uint &stride [[buffer(3)]],
                uint gid [[thread_position_in_grid]])
{
    (void)naddr; (void)stride;
    if (gid == n) out[0] = gid;          // gid < n 이라 실제로는 쓰지 않는다
}
