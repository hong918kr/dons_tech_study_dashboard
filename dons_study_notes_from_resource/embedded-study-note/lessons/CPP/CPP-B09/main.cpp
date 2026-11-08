// main.cpp — 부트 로그 + 결과 출력
#include <cstdio>
#include "board.h"

void trace(const char* who, uint32_t v) { std::printf("  [pre-main] %s %u\n", who, (unsigned)v); }

int main() {
    std::printf("  main: g_clock.sysclk_hz=%u  g_uart.brr()=%u  (기대값 %u)\n",
                (unsigned)g_clock.sysclk_hz, (unsigned)g_uart.brr(), 16'000'000u / 115200u);
    return g_uart.brr() == 16'000'000u / 115200u ? 0 : 1;
}
