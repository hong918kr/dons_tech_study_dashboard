// CPP-B09 demo — static initialization order fiasco (SIOF) across two TUs.
//   FIX=0 (default): ClockConfig / Uart 모두 동적 초기화 → 링크 순서가 결과를 바꾼다
//   FIX=1          : ClockConfig 를 constexpr 생성자 + constinit → 상수 초기화(.data)
//   FIX=2          : Uart 도 constexpr 생성자(설정만 저장) + constinit → __mod_init_func 0개
#pragma once
#if __has_include(<cstdint>)
#include <cstdint>
#else   // build.sh 의 ELF(armv7em-none-eabi) 컴파일은 sysroot 가 없어 헤더가 없다
typedef unsigned int uint32_t;
#endif

#ifndef FIX
#define FIX 0
#endif

void trace(const char* who, uint32_t v);   // main.cpp 에 정의 (부트 로그)

struct ClockConfig {
    uint32_t sysclk_hz;
    uint32_t apb1_shift;                    // APB1 분주 = 2^shift
#if FIX >= 1
    constexpr ClockConfig(uint32_t hz, uint32_t shift) : sysclk_hz(hz), apb1_shift(shift) {
        if (!__builtin_is_constant_evaluated())   // = std::is_constant_evaluated()
            trace("b: ClockConfig ctor (runtime)", hz);
    }
#else
    ClockConfig(uint32_t hz, uint32_t shift);           // b.cpp 에 정의 (평범한 C++ 생성자)
#endif
    constexpr uint32_t apb1_hz() const { return sysclk_hz >> apb1_shift; }
};
extern ClockConfig g_clock;                 // b.cpp

struct Uart {
#if FIX >= 2
    const ClockConfig* clk;                 // 주소는 상수식 → constinit 가능
    uint32_t baud;
    constexpr Uart(const ClockConfig* c, uint32_t b) : clk(c), baud(b) {}
    uint32_t brr() const { return clk->apb1_hz() / baud; }   // 값은 쓸 때(init 시점) 계산
#else
    uint32_t brr_;
    explicit Uart(uint32_t baud);           // a.cpp: g_clock 을 읽어 BRR 계산
    uint32_t brr() const { return brr_; }
#endif
};
extern Uart g_uart;                         // a.cpp
