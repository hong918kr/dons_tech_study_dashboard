// b.cpp — 클럭 설정 TU.
#include "board.h"

#if FIX >= 1
constinit ClockConfig g_clock{16'000'000, 0};   // 상수 초기화: 로더/스타트업이 .data 로 복사만 하면 끝
#else
ClockConfig::ClockConfig(uint32_t hz, uint32_t shift) : sysclk_hz(hz), apb1_shift(shift) {
    trace("b: ClockConfig ctor (runtime)", hz);
}
ClockConfig g_clock{16'000'000, 0};             // 동적 초기화 → __mod_init_func / .init_array 엔트리
#endif
