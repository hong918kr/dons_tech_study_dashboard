// GPGPU-B09 demo_race — 레이스를 "보는" 커널들 (결과는 실행마다 다를 수 있다 → CHECK 없음)
#include <metal_stdlib>
using namespace metal;

// ---------- A: 전역 카운터 네 가지 ----------
kernel void cnt_plain(device uint *c [[buffer(0)]], constant uint &n [[buffer(1)]],
                      uint gid [[thread_position_in_grid]])
{ if (gid < n) c[0]++; }

kernel void cnt_volatile(volatile device uint *c [[buffer(0)]], constant uint &n [[buffer(1)]],
                         uint gid [[thread_position_in_grid]])
{ if (gid < n) c[0]++; }

kernel void cnt_coherent(coherent(device) device uint *c [[buffer(0)]], constant uint &n [[buffer(1)]],
                         uint gid [[thread_position_in_grid]])
{ if (gid < n) c[0]++; }

kernel void cnt_atomic(device atomic_uint *c [[buffer(0)]], constant uint &n [[buffer(1)]],
                       uint gid [[thread_position_in_grid]])
{ if (gid < n) atomic_fetch_add_explicit(c, 1u, memory_order_relaxed); }

// ---------- B: threadgroup 뒤집기, barrier 변형 ----------
// skew != 0 이면 홀수 simdgroup 이 쓰기 전에 잠깐 바쁜 일을 한다 → 다른 simdgroup 이 먼저 읽게 만든다.
static inline uint busy(uint x, uint iters) {
    for (uint i = 0; i < iters; i++) x = x * 1664525u + 1013904223u;
    return x;
}

#define TG_ARGS device const uint *in [[buffer(0)]], device uint *out [[buffer(1)]],          \
                device uint *sink [[buffer(2)]], constant uint &skew [[buffer(3)]],           \
                threadgroup uint *tg [[threadgroup(0)]],                                      \
                uint gid [[thread_position_in_grid]], uint lid [[thread_position_in_threadgroup]], \
                uint T [[threads_per_threadgroup]], uint sg [[simdgroup_index_in_threadgroup]]

#define DELAY_ODD_SIMDGROUPS()                                                                \
    if (skew && (sg & 1u)) { uint x = busy(gid, skew); if (x == 0x12345678u) sink[0] = x; }

kernel void rev_nobar(TG_ARGS) {
    DELAY_ODD_SIMDGROUPS();
    tg[lid] = in[gid];
    out[gid] = tg[T - 1 - lid];                              // barrier 없음
}

kernel void rev_simdbar(TG_ARGS) {
    DELAY_ODD_SIMDGROUPS();
    tg[lid] = in[gid];
    simdgroup_barrier(mem_flags::mem_threadgroup);           // 같은 simdgroup 끼리만 맞춘다
    out[gid] = tg[T - 1 - lid];
}

kernel void rev_tgbar(TG_ARGS) {
    DELAY_ODD_SIMDGROUPS();
    tg[lid] = in[gid];
    threadgroup_barrier(mem_flags::mem_threadgroup);         // 정답
    out[gid] = tg[T - 1 - lid];
}

kernel void rev_tgbar_none(TG_ARGS) {
    DELAY_ODD_SIMDGROUPS();
    tg[lid] = in[gid];
    threadgroup_barrier(mem_flags::mem_none);                // 실행 barrier 만, 메모리 순서 플래그 없음
    out[gid] = tg[T - 1 - lid];
}

// ---------- 셰이더 검증용: threadgroup 메모리 범위 밖 쓰기 (데모 인자 "oob" 일 때만 실행) ----------
kernel void rev_oob(TG_ARGS) {
    (void)skew; (void)sink; (void)sg;
    tg[lid + 1] = in[gid];                                   // lid == T-1 이면 tg[T] — 할당 크기 T*4 바이트 밖
    threadgroup_barrier(mem_flags::mem_threadgroup);
    out[gid] = tg[T - 1 - lid];
}
