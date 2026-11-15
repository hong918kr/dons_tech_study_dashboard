/* CPP-A03 fw_start.cpp — 링크 실험용 최소 보드/스타트업 (실제 프로젝트의 startup.s + HAL 자리).
 * _start: .init_array 를 돌리고 app_main() 으로. 벡터 테이블·.data 복사·.bss 0 채우기는 생략 (BM 트랙 참고). */
#include <cstdint>
extern "C" {
void app_main();
extern void (*__init_array_start[])();
extern void (*__init_array_end[])();
volatile uint32_t g_uart_tx_count;
void hal_uart_write(const uint8_t*, uint32_t n) { g_uart_tx_count = g_uart_tx_count + n; }
int32_t hal_otp_offset() { return 3; }
uint64_t hal_cycles64() { return 168000ull * 5000u; }
[[noreturn]] void _start() {
    for (void (**p)() = __init_array_start; p != __init_array_end; ++p) (*p)();
    app_main();
    for (;;) { }
}
}
