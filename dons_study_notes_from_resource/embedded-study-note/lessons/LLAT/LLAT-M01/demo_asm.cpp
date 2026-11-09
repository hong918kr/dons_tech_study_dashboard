// LLAT-M01 demo_asm — 각 단계의 hot path 가 arm64 에서 무슨 명령이 되나 (관찰용, CHECK 없음)
// build: c++ -std=c++20 -O2 -S demo_asm.cpp -o - | grep -v '^\s*[.;]'      (또는 -c 후 xcrun llvm-objdump -d)
#define LLAT_M01_NO_MAIN
#include "solution.cpp"

using Q1 = SpscV1<uint64_t, 1024>;
using Q2 = SpscV2<uint64_t, 1024>;
using Q4 = SpscSlot<uint64_t, 1024>;
extern "C" {
__attribute__((noinline)) bool v1_push(Q1 &q, uint64_t v) { return q.try_push(v); }
__attribute__((noinline)) bool v2_push(Q2 &q, uint64_t v) { return q.try_push(v); }
__attribute__((noinline)) bool v2_pop(Q2 &q, uint64_t &v) { return q.try_pop(v); }
__attribute__((noinline)) size_t v3_push_n(Q2 &q, const uint64_t *s, size_t n) { return q.push_n(s, n); }
__attribute__((noinline)) bool v4_push(Q4 &q, uint64_t v) { return q.try_push(v); }
__attribute__((noinline)) bool v4_pop(Q4 &q, uint64_t &v) { return q.try_pop(v); }
}
