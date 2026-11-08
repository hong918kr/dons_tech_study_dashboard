// LLAT-M02 demo_asm.cpp — seqlock reader/writer 가 arm64 에서 어떤 명령이 되는지 (-S 로 확인)
//
// build: c++ -std=c++20 -O2 -S demo_asm.cpp -o - | grep -v '^\s*\.' | less
//        c++ -std=c++20 -O2 -c demo_asm.cpp -o /tmp/a.o && xcrun llvm-objdump -d --no-show-raw-insn /tmp/a.o
// 데이터 워드는 4개로 줄였다 (asm 이 짧게). 함수는 extern "C" 라서 이름이 그대로 보인다.
#include <atomic>
#include <cstdint>

using std::memory_order_acquire, std::memory_order_relaxed, std::memory_order_release;
struct SL { std::atomic<std::uint64_t> seq; std::atomic<std::uint64_t> w[4]; };
struct Plain { std::uint64_t seq; std::uint64_t w[4]; };   // atomic_ref 용: 보통 구조체 (C 와 공유하는 레이아웃)

extern "C" void wr_correct(SL &s, const std::uint64_t *in) {      // 홀수 → release fence → 데이터 → release 짝수
    const std::uint64_t q = s.seq.load(memory_order_relaxed);
    s.seq.store(q + 1, memory_order_relaxed);
    std::atomic_thread_fence(memory_order_release);
    for (int k = 0; k < 4; k++) s.w[k].store(in[k], memory_order_relaxed);
    s.seq.store(q + 2, memory_order_release);
}
extern "C" bool rd_correct(const SL &s, std::uint64_t *out) {    // acquire → 데이터 → acquire fence → relaxed 재로드
    const std::uint64_t s1 = s.seq.load(memory_order_acquire);
    if (s1 & 1u) return false;
    std::uint64_t t[4];
    for (int k = 0; k < 4; k++) t[k] = s.w[k].load(memory_order_relaxed);
    std::atomic_thread_fence(memory_order_acquire);
    if (s.seq.load(memory_order_relaxed) != s1) return false;
    for (int k = 0; k < 4; k++) out[k] = t[k];
    return true;
}
extern "C" bool rd_no_fence(const SL &s, std::uint64_t *out) {   // 깨진 것: fence 없음
    const std::uint64_t s1 = s.seq.load(memory_order_acquire);
    if (s1 & 1u) return false;
    std::uint64_t t[4];
    for (int k = 0; k < 4; k++) t[k] = s.w[k].load(memory_order_relaxed);
    if (s.seq.load(memory_order_relaxed) != s1) return false;
    for (int k = 0; k < 4; k++) out[k] = t[k];
    return true;
}
extern "C" bool rd_acquire_data(const SL &s, std::uint64_t *out) {   // 대안: 데이터를 acquire 로 (fence 없이)
    const std::uint64_t s1 = s.seq.load(memory_order_acquire);
    if (s1 & 1u) return false;
    std::uint64_t t[4];
    for (int k = 0; k < 4; k++) t[k] = s.w[k].load(memory_order_acquire);
    if (s.seq.load(memory_order_relaxed) != s1) return false;
    for (int k = 0; k < 4; k++) out[k] = t[k];
    return true;
}
extern "C" bool rd_rmw(SL &s, std::uint64_t *out) {              // Boehm 의 "read-dont-modify-write": fetch_add(0, release)
    const std::uint64_t s1 = s.seq.load(memory_order_acquire);
    if (s1 & 1u) return false;
    std::uint64_t t[4];
    for (int k = 0; k < 4; k++) t[k] = s.w[k].load(memory_order_relaxed);
    if (s.seq.fetch_add(0, memory_order_release) != s1) return false;
    for (int k = 0; k < 4; k++) out[k] = t[k];
    return true;
}
extern "C" bool rd_atomic_ref(Plain &p, std::uint64_t *out) {    // 보통 구조체 + 워드마다 atomic_ref
    const std::uint64_t s1 = std::atomic_ref<std::uint64_t>(p.seq).load(memory_order_acquire);
    if (s1 & 1u) return false;
    std::uint64_t t[4];
    for (int k = 0; k < 4; k++) t[k] = std::atomic_ref<std::uint64_t>(p.w[k]).load(memory_order_relaxed);
    std::atomic_thread_fence(memory_order_acquire);
    if (std::atomic_ref<std::uint64_t>(p.seq).load(memory_order_relaxed) != s1) return false;
    for (int k = 0; k < 4; k++) out[k] = t[k];
    return true;
}
