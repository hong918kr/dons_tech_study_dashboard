// CPP-M09 demo_host_asm — 호스트(arm64 libc++)에서 vector 이동과 array 멤버 이동의 기계어 비교
// build: c++ -std=c++20 -O2 -fno-exceptions -fno-rtti -S demo_host_asm.cpp -o -
#include <array>
#include <cstdint>
#include <utility>
#include <vector>

struct Vec   { std::vector<std::uint32_t> v; };
struct Arr64 { std::array<std::uint32_t, 64> a; };

void move_vec(Vec* dst, Vec& src)     { new (dst) Vec(std::move(src)); }
void move_arr(Arr64* dst, Arr64& src) { new (dst) Arr64(std::move(src)); }
void copy_arr(Arr64* dst, const Arr64& src) { new (dst) Arr64(src); }
