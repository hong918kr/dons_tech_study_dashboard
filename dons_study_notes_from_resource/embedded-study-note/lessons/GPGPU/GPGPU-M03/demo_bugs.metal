// GPGPU-M03 demo_bugs — hist_v2_tg 를 일부러 틀리게 만든 5가지 (demo_bugs.m 이 실행 중에 컴파일한다)
//   bug_no_zero      지역 히스토그램을 0 으로 지우지 않음 (threadgroup memory 는 초기화되지 않는다)
//   bug_no_barrier1  0 초기화 뒤 barrier 없음 → 다른 simdgroup 의 0 쓰기가 이미 센 값을 지울 수 있다
//   bug_no_barrier2  병합 전 barrier 없음 → 다른 simdgroup 이 아직 세는 중인 bin 을 읽을 수 있다
//   bug_plain_merge  병합을 hist[b] = c (그냥 저장) → threadgroup 끼리 덮어쓴다 (마지막 것만 남음)
//   bug_rmw_merge    병합을 hist[b] = hist[b] + c (atomic 아닌 읽고-더하고-쓰기) → lost update
//   dirty            threadgroup 메모리에 쓰레기를 채우는 커널 (DIRTY=1 일 때 같은 인코더에서 먼저 실행)
//   bug_oob          bin + 1 off-by-one → hist[256] 쓰기 (OOB=1, 셰이더 검증이 잡는지 보는 용도)
#include <metal_stdlib>
using namespace metal;
struct HistParams { uint n_bytes; uint n_words; };

#define ZERO   for (uint b = lid; b < 256u; b += tgsz) atomic_store_explicit(&local[b], 0u, memory_order_relaxed);
#define COUNT  for (uint i = gid; i < P.n_words; i += gsize) { uint w = words[i]; \
                   for (uint k = 0; k < 4u; ++k) atomic_fetch_add_explicit(&local[(w >> (8u * k)) & 0xFFu], 1u, memory_order_relaxed); }
#define BAR    threadgroup_barrier(mem_flags::mem_threadgroup);
#define MERGE_ATOMIC for (uint b = lid; b < 256u; b += tgsz) { uint c = atomic_load_explicit(&local[b], memory_order_relaxed); \
                   if (c != 0u) atomic_fetch_add_explicit(&hist[b], c, memory_order_relaxed); }
#define ARGS(H) device const uint *words [[buffer(0)]], H [[buffer(1)]], constant HistParams &P [[buffer(2)]], \
                uint gid [[thread_position_in_grid]], uint gsize [[threads_per_grid]], \
                uint lid [[thread_position_in_threadgroup]], uint tgsz [[threads_per_threadgroup]]

kernel void ok_v2(ARGS(device atomic_uint *hist))           { threadgroup atomic_uint local[256]; ZERO BAR COUNT BAR MERGE_ATOMIC }
kernel void bug_no_zero(ARGS(device atomic_uint *hist))     { threadgroup atomic_uint local[256];      BAR COUNT BAR MERGE_ATOMIC }
kernel void bug_no_barrier1(ARGS(device atomic_uint *hist)) { threadgroup atomic_uint local[256]; ZERO     COUNT BAR MERGE_ATOMIC }
kernel void bug_no_barrier2(ARGS(device atomic_uint *hist)) { threadgroup atomic_uint local[256]; ZERO BAR COUNT     MERGE_ATOMIC }
kernel void bug_plain_merge(ARGS(device uint *hist)) {
    threadgroup atomic_uint local[256]; ZERO BAR COUNT BAR
    for (uint b = lid; b < 256u; b += tgsz) hist[b] = atomic_load_explicit(&local[b], memory_order_relaxed);
}
kernel void bug_rmw_merge(ARGS(device uint *hist)) {
    threadgroup atomic_uint local[256]; ZERO BAR COUNT BAR
    for (uint b = lid; b < 256u; b += tgsz) hist[b] = hist[b] + atomic_load_explicit(&local[b], memory_order_relaxed);
}

kernel void dirty(device atomic_uint *hist [[buffer(1)]], uint lid [[thread_position_in_threadgroup]], uint tgsz [[threads_per_threadgroup]]) {
    threadgroup atomic_uint junk[8192];                         // 32 KB 전부
    for (uint b = lid; b < 8192u; b += tgsz) atomic_store_explicit(&junk[b], 0xDEADu, memory_order_relaxed);
    threadgroup_barrier(mem_flags::mem_threadgroup);
    if (lid == 0) atomic_fetch_add_explicit(&hist[0], atomic_load_explicit(&junk[8191u - lid], memory_order_relaxed) & 0u, memory_order_relaxed);
}

kernel void bug_oob(device const uchar *in [[buffer(0)]], device atomic_uint *hist [[buffer(1)]], uint gid [[thread_position_in_grid]]) {
    uint b = uint(in[gid]) + 1u;                                // 255 → bin 256 (버퍼는 256칸)
    atomic_fetch_add_explicit(&hist[b], 1u, memory_order_relaxed);
}
