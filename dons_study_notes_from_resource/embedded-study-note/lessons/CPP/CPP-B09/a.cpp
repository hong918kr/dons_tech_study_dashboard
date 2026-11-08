// a.cpp — UART 드라이버 TU. 전역 g_uart 의 초기화가 다른 TU 의 g_clock 을 읽는다.
#include "board.h"

#if FIX >= 2
constinit Uart g_uart{&g_clock, 115200};
#else
Uart::Uart(uint32_t baud) : brr_(g_clock.apb1_hz() / baud) {
    trace("a: Uart ctor sees g_clock.sysclk_hz =", g_clock.sysclk_hz);
}
Uart g_uart{115200};                         // 동적 초기화 (dynamic initialization)
#endif
