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
    // TODO(Part A): grid-stride 로 word i = gid, gid+gsize, ... 를 읽고 바이트 4개마다
    //               atomic_fetch_add_explicit(&hist[b], 1u, memory_order_relaxed).
    //               뒤에 남는 0~3 바이트(tail)는 gid < tail 인 스레드가 tail_byte() 로.
    (void)words; (void)hist; (void)P; (void)gid; (void)gsize;
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

    // (1) TODO(Part B): 0 으로 초기화 — threadgroup memory 는 0 이 보장되지 않는다. tgsz 가 256 보다 작거나 커도 맞게.
    // (2) TODO(Part B): 모두 0 이 된 다음에야 누적을 시작한다 (barrier)
    (void)lid; (void)tgsz;

    // (3) 지역 누적 — 경합은 이 threadgroup 의 스레드끼리만
    for (uint i = gid; i < P.n_words; i += gsize) {
        uint w = words[i];
        for (uint k = 0; k < 4u; ++k)
            atomic_fetch_add_explicit(&local[(w >> (8u * k)) & 0xFFu], 1u, memory_order_relaxed);
    }
    uint tail = P.n_bytes - 4u * P.n_words;
    if (gid < tail)
        atomic_fetch_add_explicit(&local[tail_byte(words, 4u * P.n_words + gid)], 1u, memory_order_relaxed);

    // (4) TODO(Part B): 모든 스레드의 누적이 끝난 다음에야 읽는다 (barrier)
    // (5) TODO(Part B): 병합 — bin 마다 device atomic 1번 (0 이면 건너뜀)
    (void)hist;
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
    // TODO(Part C): 0 초기화 + barrier (Part B 와 같음)
    (void)lid; (void)tgsz;

    for (uint i = gid; i < P.n_words; i += gsize) {
        uint w = words[i];
        // TODO(Part C): simdgroup 의 활성 lane 이 모두 같은 word 이고 그 word 의 4바이트가 같으면
        //               (simd_broadcast_first / simd_all) lane 하나가 local[bin] += 4 x 활성 lane 수 (simd_sum).
        //               아니면 아래의 느린 길.
        {
            for (uint k = 0; k < 4u; ++k)
                atomic_fetch_add_explicit(&local[(w >> (8u * k)) & 0xFFu], 1u, memory_order_relaxed);
        }
    }
    uint tail = P.n_bytes - 4u * P.n_words;
    if (gid < tail)
        atomic_fetch_add_explicit(&local[tail_byte(words, 4u * P.n_words + gid)], 1u, memory_order_relaxed);

    // TODO(Part C): barrier + 병합 (Part B 와 같음)
    (void)hist;
}
