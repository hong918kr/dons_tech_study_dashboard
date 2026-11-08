// CPP-B09 demo — constinit 은 "상수 초기화가 안 되면 컴파일 에러"로 만들어 준다.
//   c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti -c demo_constinit_error.cpp               # OK
//   c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti -c -DSHOW_ERROR demo_constinit_error.cpp  # 에러 2개
#include <cstdint>

struct ClockConfig {                       // constexpr 생성자 → constinit OK
    uint32_t hz;
    constexpr explicit ClockConfig(uint32_t h) : hz(h) {}
};
constinit ClockConfig g_ok{16'000'000};

volatile uint32_t g_otp_word = 37;         // mock OTP 레지스터

struct LegacyClock {                       // C 스타일 습관: 생성자에 constexpr 없음
    uint32_t hz;
    explicit LegacyClock(uint32_t h) : hz(h) {}
};

#ifdef SHOW_ERROR
constinit LegacyClock g_bad1{16'000'000};          // ① constexpr 아닌 생성자 호출
constinit uint32_t    g_bad2 = g_otp_word;         // ② volatile(런타임) 값 읽기
#endif
