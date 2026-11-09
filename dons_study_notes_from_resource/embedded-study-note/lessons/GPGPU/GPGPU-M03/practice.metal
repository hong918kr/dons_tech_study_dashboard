// GPGPU-M03 추가 연습 — 커널 (practice.m 이 실행 중에 컴파일한다)
//   연습 1  12비트 RAW 4096-bin 히스토그램 (threadgroup 16 KB) + 16384-bin 은 32 KB 한도 초과
//   연습 2  RGBA 4채널 히스토그램 (AWB 통계) — threadgroup [4][256]
//   연습 3  atomic 없는 2-pass: threadgroup 별 부분 히스토그램을 그냥 저장 → 열(column) 합
//   연습 4  병합 비용 세기: v2 + "device atomic 몇 번 했나" 카운터
#include <metal_stdlib>
using namespace metal;

struct Cnt { uint n; };

// ---------------- 연습 1 ----------------
constant uint NB12 = 4096u;
kernel void hist12_tg(device const ushort *v [[buffer(0)]],
                      device atomic_uint *hist [[buffer(1)]],
                      constant Cnt &P [[buffer(2)]],
                      uint gid [[thread_position_in_grid]], uint gsize [[threads_per_grid]],
                      uint lid [[thread_position_in_threadgroup]], uint tgsz [[threads_per_threadgroup]])
{
    threadgroup atomic_uint local[NB12];                         // 16 KB
    for (uint b = lid; b < NB12; b += tgsz) atomic_store_explicit(&local[b], 0u, memory_order_relaxed);
    threadgroup_barrier(mem_flags::mem_threadgroup);
    for (uint i = gid; i < P.n; i += gsize) {
        uint b = min(uint(v[i]), NB12 - 1u);                     // 12비트 밖의 값은 마지막 bin 으로
        atomic_fetch_add_explicit(&local[b], 1u, memory_order_relaxed);
    }
    threadgroup_barrier(mem_flags::mem_threadgroup);
    for (uint b = lid; b < NB12; b += tgsz) {
        uint c = atomic_load_explicit(&local[b], memory_order_relaxed);
        if (c != 0u) atomic_fetch_add_explicit(&hist[b], c, memory_order_relaxed);
    }
}

// 14비트 = 16384 bin x 4 B = 64 KB → threadgroup memory 한도(32 KB)를 넘는다. 라이브러리는 컴파일되지만
// 파이프라인 생성이 실패해야 한다 (호스트가 확인).
kernel void hist14_too_big(device const ushort *v [[buffer(0)]],
                           device atomic_uint *hist [[buffer(1)]],
                           constant Cnt &P [[buffer(2)]],
                           uint gid [[thread_position_in_grid]], uint gsize [[threads_per_grid]],
                           uint lid [[thread_position_in_threadgroup]], uint tgsz [[threads_per_threadgroup]])
{
    threadgroup atomic_uint local[16384];
    for (uint b = lid; b < 16384u; b += tgsz) atomic_store_explicit(&local[b], 0u, memory_order_relaxed);
    threadgroup_barrier(mem_flags::mem_threadgroup);
    for (uint i = gid; i < P.n; i += gsize)
        atomic_fetch_add_explicit(&local[min(uint(v[i]), 16383u)], 1u, memory_order_relaxed);
    threadgroup_barrier(mem_flags::mem_threadgroup);
    for (uint b = lid; b < 16384u; b += tgsz) {
        uint c = atomic_load_explicit(&local[b], memory_order_relaxed);
        if (c != 0u) atomic_fetch_add_explicit(&hist[b], c, memory_order_relaxed);
    }
}

// ---------------- 연습 2 ----------------
kernel void hist_rgba_tg(device const uchar4 *px [[buffer(0)]],
                         device atomic_uint *hist [[buffer(1)]],          // [4][256]: R, G, B, A
                         constant Cnt &P [[buffer(2)]],
                         uint gid [[thread_position_in_grid]], uint gsize [[threads_per_grid]],
                         uint lid [[thread_position_in_threadgroup]], uint tgsz [[threads_per_threadgroup]])
{
    threadgroup atomic_uint local[4 * 256];                      // 4 KB
    for (uint b = lid; b < 1024u; b += tgsz) atomic_store_explicit(&local[b], 0u, memory_order_relaxed);
    threadgroup_barrier(mem_flags::mem_threadgroup);
    for (uint i = gid; i < P.n; i += gsize) {
        uchar4 p = px[i];
        atomic_fetch_add_explicit(&local[0u   + p.r], 1u, memory_order_relaxed);
        atomic_fetch_add_explicit(&local[256u + p.g], 1u, memory_order_relaxed);
        atomic_fetch_add_explicit(&local[512u + p.b], 1u, memory_order_relaxed);
        atomic_fetch_add_explicit(&local[768u + p.a], 1u, memory_order_relaxed);
    }
    threadgroup_barrier(mem_flags::mem_threadgroup);
    for (uint b = lid; b < 1024u; b += tgsz) {
        uint c = atomic_load_explicit(&local[b], memory_order_relaxed);
        if (c != 0u) atomic_fetch_add_explicit(&hist[b], c, memory_order_relaxed);
    }
}

// ---------------- 연습 3 ----------------
// pass 1: threadgroup tg 의 지역 히스토그램을 partial[tg * 256 + b] 에 "그냥" 저장 (device atomic 0번)
kernel void hist_partial(device const uchar *bytes [[buffer(0)]],
                         device uint *partial [[buffer(1)]],
                         constant Cnt &P [[buffer(2)]],
                         uint gid [[thread_position_in_grid]], uint gsize [[threads_per_grid]],
                         uint lid [[thread_position_in_threadgroup]], uint tgsz [[threads_per_threadgroup]],
                         uint tg [[threadgroup_position_in_grid]])
{
    threadgroup atomic_uint local[256];
    for (uint b = lid; b < 256u; b += tgsz) atomic_store_explicit(&local[b], 0u, memory_order_relaxed);
    threadgroup_barrier(mem_flags::mem_threadgroup);
    for (uint i = gid; i < P.n; i += gsize)
        atomic_fetch_add_explicit(&local[bytes[i]], 1u, memory_order_relaxed);
    threadgroup_barrier(mem_flags::mem_threadgroup);
    for (uint b = lid; b < 256u; b += tgsz)
        partial[tg * 256u + b] = atomic_load_explicit(&local[b], memory_order_relaxed);   // 0 도 저장
}

// pass 2: bin b 를 맡은 스레드가 partial[0..ntg)[b] 를 순서대로 더한다 (항상 같은 순서 → 결정적)
kernel void hist_reduce(device const uint *partial [[buffer(0)]],
                        device uint *hist [[buffer(1)]],
                        constant Cnt &P [[buffer(2)]],                    // P.n = threadgroup 수 (pass 1)
                        uint b [[thread_position_in_grid]])
{
    if (b >= 256u) return;
    uint s = 0;
    for (uint t = 0; t < P.n; ++t) s += partial[t * 256u + b];
    hist[b] = s;
}

// ---------------- 연습 4 ----------------
kernel void hist_v2_count(device const uchar *bytes [[buffer(0)]],
                          device atomic_uint *hist [[buffer(1)]],         // [256] + [1] 병합 카운터
                          constant Cnt &P [[buffer(2)]],
                          uint gid [[thread_position_in_grid]], uint gsize [[threads_per_grid]],
                          uint lid [[thread_position_in_threadgroup]], uint tgsz [[threads_per_threadgroup]])
{
    threadgroup atomic_uint local[256];
    for (uint b = lid; b < 256u; b += tgsz) atomic_store_explicit(&local[b], 0u, memory_order_relaxed);
    threadgroup_barrier(mem_flags::mem_threadgroup);
    for (uint i = gid; i < P.n; i += gsize)
        atomic_fetch_add_explicit(&local[bytes[i]], 1u, memory_order_relaxed);
    threadgroup_barrier(mem_flags::mem_threadgroup);
    for (uint b = lid; b < 256u; b += tgsz) {
        uint c = atomic_load_explicit(&local[b], memory_order_relaxed);
        if (c != 0u) {
            atomic_fetch_add_explicit(&hist[b], c, memory_order_relaxed);
            atomic_fetch_add_explicit(&hist[256], 1u, memory_order_relaxed);   // 병합 atomic 1번 셈
        }
    }
}
