/* CPP-B03 demo: enum 혼용 — C enum 은 조용히 컴파일, enum class 는 컴파일 에러
 *   c++ -std=c++20 -Wall -Wextra -c demo_mix_error.cpp               # C 스타일: 경고 1개뿐
 *   c++ -std=c++20 -Wall -Wextra -c demo_mix_error.cpp -DSHOW_ERROR  # enum class: 에러 7개
 */
#include <cstdint>
#include <utility>

/* ---- C 스타일 (unscoped) ---- */
enum c_parity { PARITY_NONE = 0, PARITY_EVEN = 2, PARITY_ODD = 3 };
enum c_stop   { STOP_1 = 0, STOP_0_5 = 1, STOP_2 = 2, STOP_1_5 = 3 };
struct c_cfg { c_parity parity; c_stop stop; };

std::uint32_t c_style(c_cfg cfg) {
    std::uint32_t raw = PARITY_ODD;            /* OK: 암시적 정수 변환            */
    cfg.parity = static_cast<c_parity>(STOP_2); /* C 에선 cast 없이도 됨 (C++ 은 cast 필요) */
    if (cfg.parity == PARITY_EVEN) raw++;
    raw += (cfg.stop == STOP_2);               /* 다른 enum 끼리 섞어도 아무 말 없음 */
    return raw | (PARITY_EVEN | STOP_2);       /* 다른 enum 끼리 | → deprecated 경고만 */
}

/* ---- enum class ---- */
enum class Parity   : std::uint32_t { None = 0b00, Even = 0b10, Odd = 0b11 };
enum class StopBits : std::uint32_t { One = 0, Half = 1, Two = 2, OneAndHalf = 3 };
enum class UartMode : std::uint32_t { Async, Sync };
struct UartConfig { UartMode mode; Parity parity; StopBits stop; };

std::uint32_t cpp_style(UartConfig cfg) {
    std::uint32_t raw = static_cast<std::uint32_t>(cfg.parity);   /* 명시적 변환만 허용 */
    UartMode m{1u};   /* C++17: 고정 기저 타입 enum 은 정수로 direct-list-init 가능 */
#ifdef SHOW_ERROR
    std::uint32_t e1 = Parity::Odd;            /* (1) enum → 정수 암시적 변환 */
    cfg.parity = StopBits::Two;                /* (2) 다른 enum 대입 */
    if (cfg.mode == Parity::Odd) raw++;        /* (3) 다른 enum 비교 */
    UartMode e4 = 2;                           /* (4) 정수 → enum */
    UartMode e5{-1};                           /* (5) narrowing: -1 → uint32_t */
    auto e6 = Parity::Even | Parity::Odd;      /* (6) operator| 정의 안 함 */
    auto e7 = std::to_underlying(Parity::Odd); /* (7) C++23 함수 — -std=c++20 의 libc++ 엔 없음 */
    (void)e1; (void)e4; (void)e5; (void)e6; (void)e7;
#endif
    return raw + static_cast<std::uint32_t>(m);
}
