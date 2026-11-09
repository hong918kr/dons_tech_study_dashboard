// GPGPU-B09 Atomic과 Barrier — GPU 커널 (같은 이름의 호스트 .m 이 실행 중에 이 파일을 읽어 컴파일한다)
//
//   Part A  count_plain  : c[0]++ (레이스. 데모용으로만 실행, CHECK 안 함)
//           count_atomic : atomic_fetch_add_explicit(relaxed) → 정확히 n
//   Part B  tg_reverse   : threadgroup 메모리에 쓰고 → barrier → "다른 simdgroup" 의 칸을 읽기
//           (그룹 안에서 순서 뒤집기. 마지막 그룹은 덜 찰 수 있다 → m 개만 뒤집는다)
//   Part C  spread_add   : n 번의 atomic add 를 naddr 개 주소에 고르게 (주소 = gid % naddr)
//
// MSL 4.0 (이 맥 기본 languageVersion): device atomic 의 order 인자는 memory_order_relaxed 만 컴파일된다.
#include <metal_stdlib>
using namespace metal;

// ---------------- Part A ----------------
kernel void count_plain(device uint *c        [[buffer(0)]],
                        constant uint &n      [[buffer(1)]],
                        uint gid              [[thread_position_in_grid]])
{
    if (gid < n) c[0]++;                    // load → add → store : 다른 스레드와 겹치면 증가가 사라진다
}

kernel void count_atomic(device atomic_uint *c [[buffer(0)]],
                         constant uint &n      [[buffer(1)]],
                         uint gid              [[thread_position_in_grid]])
{
    if (gid < n) atomic_fetch_add_explicit(c, 1u, memory_order_relaxed);   // RMW 한 번 = 사라지는 증가 없음
}

// ---------------- Part B ----------------
// 그룹 g 의 시작 base = g * T, 유효 개수 m = min(T, n - base).
// 결과: out[base + l] = in[base + (m - 1 - l)]   (l < m)
// T ≥ 64 이면 l 과 m-1-l 은 대부분 서로 다른 simdgroup(32 lane) 에 있다 → 같은 simdgroup lockstep 에 기대면 안 된다.
kernel void tg_reverse(device const uint *in  [[buffer(0)]],
                       device uint *out       [[buffer(1)]],
                       constant uint &n       [[buffer(2)]],
                       threadgroup uint *tg   [[threadgroup(0)]],      // 크기는 호스트가 T*4 바이트로 정한다
                       uint gid               [[thread_position_in_grid]],
                       uint lid               [[thread_position_in_threadgroup]],
                       uint T                 [[threads_per_threadgroup]],
                       uint g                 [[threadgroup_position_in_grid]])
{
    uint base = g * T;
    uint m = min(T, n - base);
    tg[lid] = (gid < n) ? in[gid] : 0u;     // 모든 스레드가 쓴다 (범위 밖 스레드도 return 하지 않는다)
    threadgroup_barrier(mem_flags::mem_threadgroup);   // 그룹 전원이 여기 올 때까지 대기 + tg 쓰기를 보이게
    if (lid < m) out[gid] = tg[m - 1 - lid];            // barrier 는 if 밖 (모든 스레드가 만나야 한다)
}

// ---------------- Part C ----------------
kernel void spread_add(device atomic_uint *bins [[buffer(0)]],
                       constant uint &n         [[buffer(1)]],
                       constant uint &naddr     [[buffer(2)]],
                       uint gid                 [[thread_position_in_grid]])
{
    if (gid < n) atomic_fetch_add_explicit(&bins[gid % naddr], 1u, memory_order_relaxed);
}
