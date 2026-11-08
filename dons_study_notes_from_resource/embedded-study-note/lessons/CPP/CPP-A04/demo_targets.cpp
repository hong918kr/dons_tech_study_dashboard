// CPP-A04 demo_targets.cpp — 같은 std::atomic 코드가 M0+ / M3 / M4 / arm64 에서 무엇이 되나 (asm_targets.sh)
// extern "C" 라 함수 이름이 asm 에 그대로 보인다. 링크하지 않는다 (-S 만).
#include <atomic>
#include <cstdint>

std::atomic<uint32_t> a32;
std::atomic<uint64_t> a64;
volatile uint32_t v32;
volatile uint64_t v64;

extern "C" {
uint32_t ld32_relaxed() { return a32.load(std::memory_order_relaxed); }
uint32_t ld32_acquire() { return a32.load(std::memory_order_acquire); }
uint32_t ld32_seqcst()  { return a32.load(); }
void     st32_relaxed(uint32_t x) { a32.store(x, std::memory_order_relaxed); }
void     st32_release(uint32_t x) { a32.store(x, std::memory_order_release); }
void     st32_seqcst(uint32_t x)  { a32.store(x); }
uint32_t add32_relaxed() { return a32.fetch_add(1, std::memory_order_relaxed); }
uint32_t add32_seqcst()  { return a32.fetch_add(1); }
bool     cas32(uint32_t e, uint32_t d) { return a32.compare_exchange_strong(e, d); }
uint64_t ld64()  { return a64.load(); }
void     st64(uint64_t x) { a64.store(x); }
uint64_t add64() { return a64.fetch_add(1); }
bool     cas64(uint64_t e, uint64_t d) { return a64.compare_exchange_strong(e, d); }
void     vinc32() { v32 = v32 + 1; }        // volatile "++": 원자적이지 않다
uint64_t vld64() { return v64; }            // volatile 64비트 읽기: 32비트 코어에선 2워드
}
