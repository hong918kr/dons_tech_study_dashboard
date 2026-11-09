// GPGPU-M03 Histogram: Privatization과 Atomic 경합 — Metal 커널 (호스트 .m 이 실행 중에 컴파일한다)
//
//   hist_v1_global  Part A  원소(바이트)마다 device atomic 1번 — 256개 주소에 전 GPU 가 몰린다
//   hist_v2_tg      Part B  threadgroup 지역 히스토그램(privatization):
//                           0 으로 초기화 → barrier → 지역 누적 → barrier → bin 마다 device atomic 1번으로 병합
//   hist_v3_simd    Part C  v2 + SIMD 집계: simdgroup 전체가 같은 값(같은 bin 4개)을 읽었으면
//                           lane 32개의 +1 x 4 를 lane 하나의 +cnt 로 합친다 (편향 입력 / 포화된 하늘)
//
// 입력은 바이트 배열이지만 uint(4바이트) 단위로 읽는다. 뒤에 남는 0~3 바이트(tail)는 gid < tail 인 스레드가 처리.
// 히스토그램은 순서와 무관하므로 4바이트 중 어느 것이 몇 번째 바이트인지(엔디언)는 상관없다.
#include <metal_stdlib>
using namespace metal;

struct HistParams {
    uint n_bytes;   // 전체 바이트 수
    uint n_words;   // n_bytes / 4 (내림)
};

constant uint NBIN = 256u;

static inline uint tail_byte(device const uint *words, uint idx) {
    return uint(((device const uchar *)words)[idx]);
}

// ---------------- Part A — 전역 atomic 버전 ----------------
kernel void hist_v1_global(device const uint *words [[buffer(0)]],
                           device atomic_uint *hist [[buffer(1)]],
                           constant HistParams &P [[buffer(2)]],
                           uint gid [[thread_position_in_grid]],
                           uint gsize [[threads_per_grid]],
                           uint lid [[thread_position_in_threadgroup]],
                           uint tgsz [[threads_per_threadgroup]])
{
    (void)lid; (void)tgsz;
    for (uint i = gid; i < P.n_words; i += gsize) {          // grid-stride: 스레드 하나가 여러 word
        uint w = words[i];
        for (uint k = 0; k < 4u; ++k)
            atomic_fetch_add_explicit(&hist[(w >> (8u * k)) & 0xFFu], 1u, memory_order_relaxed);
    }
    uint tail = P.n_bytes - 4u * P.n_words;                    // 0..3
    if (gid < tail)
        atomic_fetch_add_explicit(&hist[tail_byte(words, 4u * P.n_words + gid)], 1u, memory_order_relaxed);
}

// ---------------- Part B — threadgroup 지역 히스토그램 + 병합 ----------------
kernel void hist_v2_tg(device const uint *words [[buffer(0)]],
                       device atomic_uint *hist [[buffer(1)]],
                       constant HistParams &P [[buffer(2)]],
                       uint gid [[thread_position_in_grid]],
                       uint gsize [[threads_per_grid]],
                       uint lid [[thread_position_in_threadgroup]],
                       uint tgsz [[threads_per_threadgroup]])
{
    threadgroup atomic_uint local[NBIN];                       // 1 KB — threadgroup 마다 따로 있다

    // (1) 0 으로 초기화 — threadgroup memory 는 0 이 보장되지 않는다. tgsz 가 256 보다 작거나 커도 맞게.
    for (uint b = lid; b < NBIN; b += tgsz)
        atomic_store_explicit(&local[b], 0u, memory_order_relaxed);
    // (2) 모두 0 이 된 다음에야 누적을 시작한다
    threadgroup_barrier(mem_flags::mem_threadgroup);

    // (3) 지역 누적 — 경합은 이 threadgroup 의 스레드끼리만
    for (uint i = gid; i < P.n_words; i += gsize) {
        uint w = words[i];
        for (uint k = 0; k < 4u; ++k)
            atomic_fetch_add_explicit(&local[(w >> (8u * k)) & 0xFFu], 1u, memory_order_relaxed);
    }
    uint tail = P.n_bytes - 4u * P.n_words;
    if (gid < tail)
        atomic_fetch_add_explicit(&local[tail_byte(words, 4u * P.n_words + gid)], 1u, memory_order_relaxed);

    // (4) 모든 스레드의 누적이 끝난 다음에야 읽는다
    threadgroup_barrier(mem_flags::mem_threadgroup);
    // (5) 병합 — bin 마다 device atomic 1번 (0 이면 건너뜀)
    for (uint b = lid; b < NBIN; b += tgsz) {
        uint c = atomic_load_explicit(&local[b], memory_order_relaxed);
        if (c != 0u)
            atomic_fetch_add_explicit(&hist[b], c, memory_order_relaxed);
    }
}

// ---------------- Part C — v2 + SIMD 집계 (편향 입력용 빠른 길) ----------------
kernel void hist_v3_simd(device const uint *words [[buffer(0)]],
                         device atomic_uint *hist [[buffer(1)]],
                         constant HistParams &P [[buffer(2)]],
                         uint gid [[thread_position_in_grid]],
                         uint gsize [[threads_per_grid]],
                         uint lid [[thread_position_in_threadgroup]],
                         uint tgsz [[threads_per_threadgroup]])
{
    threadgroup atomic_uint local[NBIN];
    for (uint b = lid; b < NBIN; b += tgsz)
        atomic_store_explicit(&local[b], 0u, memory_order_relaxed);
    threadgroup_barrier(mem_flags::mem_threadgroup);

    for (uint i = gid; i < P.n_words; i += gsize) {
        uint w = words[i];
        uint first = simd_broadcast_first(w);                  // 활성 lane 중 첫 lane 의 word
        bool uniform = simd_all(w == first) && (first == (first & 0xFFu) * 0x01010101u);
        if (uniform) {                                         // simdgroup 전체가 같은 bin x 4
            uint cnt = simd_sum(4u);                           // 4 x (활성 lane 수) — 모든 활성 lane 이 호출
            if (simd_is_first())
                atomic_fetch_add_explicit(&local[first & 0xFFu], cnt, memory_order_relaxed);
        } else {
            for (uint k = 0; k < 4u; ++k)
                atomic_fetch_add_explicit(&local[(w >> (8u * k)) & 0xFFu], 1u, memory_order_relaxed);
        }
    }
    uint tail = P.n_bytes - 4u * P.n_words;
    if (gid < tail)
        atomic_fetch_add_explicit(&local[tail_byte(words, 4u * P.n_words + gid)], 1u, memory_order_relaxed);

    threadgroup_barrier(mem_flags::mem_threadgroup);
    for (uint b = lid; b < NBIN; b += tgsz) {
        uint c = atomic_load_explicit(&local[b], memory_order_relaxed);
        if (c != 0u)
            atomic_fetch_add_explicit(&hist[b], c, memory_order_relaxed);
    }
}
