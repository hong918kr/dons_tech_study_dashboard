// GPGPU-B09 추가 연습 — 커널 (practice.m 이 실행 중에 읽어 컴파일한다)
//
//   연습 1  tg_reduce      : threadgroup 트리 합 (루프 안 barrier — 모든 스레드가 매 반복 지나간다)
//   연습 2  sum_partials   : 두 번째 dispatch 로 그룹 부분합을 합친다 (커널 안 grid barrier 대신 dispatch 경계)
//   연습 3  compact        : atomic 으로 출력 칸 예약 (스트림 압축) — 순서는 비결정적, 집합은 결정적
//   연습 4  count_simd_agg : simd_sum 으로 모은 뒤 simdgroup 당 atomic 한 번 (+ lane 배치 확인 커널)
//   연습 5  reduce_last    : 한 번의 dispatch 로 전체 합 — 마지막에 끝난 threadgroup 이 합친다 (fence + ticket)
#include <metal_stdlib>
using namespace metal;

// T(2의 거듭제곱) 개의 tg[] 를 tg[0] 으로 접는다. 모든 스레드가 호출해야 한다 (안에 barrier).
static void tree_sum(threadgroup uint *tg, uint lid, uint T) {
    for (uint s = T / 2; s > 0; s >>= 1) {
        if (lid < s) tg[lid] += tg[lid + s];
        threadgroup_barrier(mem_flags::mem_threadgroup);   // if 밖: 매 반복 전원이 만난다
    }
}

// ---------- 연습 1 ----------
kernel void tg_reduce(device const uint *in   [[buffer(0)]],
                      device uint *partial    [[buffer(1)]],
                      constant uint &n        [[buffer(2)]],
                      threadgroup uint *tg    [[threadgroup(0)]],
                      uint gid [[thread_position_in_grid]], uint lid [[thread_position_in_threadgroup]],
                      uint T [[threads_per_threadgroup]], uint g [[threadgroup_position_in_grid]])
{
    tg[lid] = (gid < n) ? in[gid] : 0u;
    threadgroup_barrier(mem_flags::mem_threadgroup);
    tree_sum(tg, lid, T);
    if (lid == 0) partial[g] = tg[0];
}

// ---------- 연습 2 ----------  (threadgroup 하나로 실행)
kernel void sum_partials(device const uint *partial [[buffer(0)]],
                         device uint *result        [[buffer(1)]],
                         constant uint &ngroups     [[buffer(2)]],
                         threadgroup uint *tg       [[threadgroup(0)]],
                         uint lid [[thread_position_in_threadgroup]], uint T [[threads_per_threadgroup]])
{
    uint acc = 0;
    for (uint i = lid; i < ngroups; i += T) acc += partial[i];
    tg[lid] = acc;
    threadgroup_barrier(mem_flags::mem_threadgroup);
    tree_sum(tg, lid, T);
    if (lid == 0) result[0] = tg[0];
}

// ---------- 연습 3 ----------
kernel void compact(device const uint *in   [[buffer(0)]],
                    device uint *out        [[buffer(1)]],
                    device atomic_uint *cnt [[buffer(2)]],
                    constant uint &n        [[buffer(3)]],
                    uint gid [[thread_position_in_grid]])
{
    if (gid < n && in[gid] % 3u == 0u) {
        uint slot = atomic_fetch_add_explicit(cnt, 1u, memory_order_relaxed);   // 내 칸 번호 = 옛 값
        out[slot] = in[gid];
    }
}

// ---------- 연습 4 ----------
kernel void count_simd_agg(device const uint *in   [[buffer(0)]],
                           device atomic_uint *cnt [[buffer(1)]],      // cnt[0] = 개수, cnt[1] = atomic 횟수
                           constant uint &n        [[buffer(2)]],
                           uint gid [[thread_position_in_grid]])
{
    uint mine = (gid < n && (in[gid] & 7u) == 0u) ? 1u : 0u;
    uint s = simd_sum(mine);                                    // 같은 simdgroup 32 lane 의 합 (전원이 받는다)
    if (simd_is_first() && s != 0u) {                           // 활성 lane 중 첫 번째만
        atomic_fetch_add_explicit(&cnt[0], s, memory_order_relaxed);
        atomic_fetch_add_explicit(&cnt[1], 1u, memory_order_relaxed);
    }
}

// lane 배치 확인: thread_position_in_threadgroup == simdgroup_index * 32 + thread_index_in_simdgroup ?
kernel void lane_map(device uint *bad [[buffer(0)]],
                     uint lid [[thread_position_in_threadgroup]],
                     uint sg [[simdgroup_index_in_threadgroup]], uint lane [[thread_index_in_simdgroup]],
                     uint w [[threads_per_simdgroup]])
{
    if (lid != sg * w + lane || w != 32u) bad[0] = 1u;
}

// ---------- 연습 5 ----------
kernel void reduce_last(device const uint *in                [[buffer(0)]],
                        coherent(device) device uint *partial [[buffer(1)]],   // 다른 threadgroup 이 읽는다
                        device atomic_uint *ticket            [[buffer(2)]],
                        device uint *result                   [[buffer(3)]],
                        constant uint &n                      [[buffer(4)]],
                        threadgroup uint *tg                  [[threadgroup(0)]],
                        uint gid [[thread_position_in_grid]], uint lid [[thread_position_in_threadgroup]],
                        uint T [[threads_per_threadgroup]], uint g [[threadgroup_position_in_grid]],
                        uint ngroups [[threadgroups_per_grid]])
{
    threadgroup uint is_last;
    tg[lid] = (gid < n) ? in[gid] : 0u;
    threadgroup_barrier(mem_flags::mem_threadgroup);
    tree_sum(tg, lid, T);
    if (lid == 0) {
        partial[g] = tg[0];
        // release 쪽: partial 쓰기가 ticket 증가보다 먼저 device 전체에 보이게
        atomic_thread_fence(mem_flags::mem_device, memory_order_seq_cst, thread_scope_device);
        uint t = atomic_fetch_add_explicit(ticket, 1u, memory_order_relaxed);
        is_last = (t == ngroups - 1u) ? 1u : 0u;
        // acquire 쪽: 마지막 그룹은 ticket 을 본 뒤에 partial 을 읽는다
        if (is_last) atomic_thread_fence(mem_flags::mem_device, memory_order_seq_cst, thread_scope_device);
    }
    threadgroup_barrier(mem_flags::mem_threadgroup | mem_flags::mem_device);
    if (is_last) {                                   // 그룹 전체가 같은 값을 본다 → barrier 를 써도 균일
        uint acc = 0;
        for (uint i = lid; i < ngroups; i += T) acc += partial[i];
        tg[lid] = acc;
        threadgroup_barrier(mem_flags::mem_threadgroup);
        tree_sum(tg, lid, T);
        if (lid == 0) result[0] = tg[0];
    }
}
