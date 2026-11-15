// CPP-A05 demo_global_placement.cpp — "버퍼 포인터를 객체에 저장하는" thin 설계(ETL ivector / LLVM SmallVectorBase 류)의
// 전역 객체가 어디에 놓이는가. bloat.sh place 가 clang(thumbv7em) 과 arm-none-eabi-g++ 로 심볼 섹션을 찍는다.
//   g_fat  : fat::static_vector<uint32_t,64>      {size, 저장소}           → 기대: .bss
//   g_thin : thin::static_vector<uint32_t,64>     {size, 저장소} (이 레슨) → 기대: .bss
//   g_ptr  : PtrVec  — 생성자가 buf_ = storage_ 를 저장 (constexpr 아님)
//   g_ptrc : PtrVecC — 같은 것 + constexpr 생성자 + storage_{} (constinit 가능하게)
#include "demo_vec.hpp"

struct PtrCore {                                  // ETL/LLVM 스타일 헤더: 버퍼 포인터 + 크기 + 용량
    std::byte* buf_;
    std::uint16_t size_ = 0;
    std::uint16_t cap_;
    PtrCore(std::byte* b, std::uint16_t c) noexcept : buf_(b), cap_(c) {}
};
template <class T, std::size_t N>
struct PtrVec : PtrCore {
    PtrVec() noexcept : PtrCore(storage_, N) {}
    alignas(T) std::byte storage_[N * sizeof(T)];
};
struct PtrCoreC {
    std::byte* buf_;
    std::uint16_t size_ = 0;
    std::uint16_t cap_;
    constexpr PtrCoreC(std::byte* b, std::uint16_t c) noexcept : buf_(b), cap_(c) {}
};
template <class T, std::size_t N>
struct PtrVecC : PtrCoreC {
    constexpr PtrVecC() noexcept : PtrCoreC(storage_, N), storage_{} {}
    alignas(T) std::byte storage_[N * sizeof(T)];
};

fat::static_vector<std::uint32_t, 64> g_fat;
thin::static_vector<std::uint32_t, 64> g_thin;
PtrVec<std::uint32_t, 64> g_ptr;
constinit PtrVecC<std::uint32_t, 64> g_ptrc;

std::size_t touch() { return g_fat.size() + g_thin.size() + g_ptr.size_ + g_ptrc.size_; }

#ifdef SHOW_ERROR
// 저장소를 초기화하지 않은 채 constinit 을 요구하면? (CPP-M10 의 zero_init 태그와 같은 문제)
constinit PtrVec<std::uint32_t, 4> g_bad;        // 생성자가 constexpr 이 아님
template <class T, std::size_t N>
struct PtrVecC2 : PtrCoreC {
    constexpr PtrVecC2() noexcept : PtrCoreC(storage_, N) {}
    alignas(T) std::byte storage_[N * sizeof(T)];
};
constinit PtrVecC2<std::uint32_t, 4> g_bad2;     // constexpr 이지만 storage_ 가 초기화되지 않음
#endif
