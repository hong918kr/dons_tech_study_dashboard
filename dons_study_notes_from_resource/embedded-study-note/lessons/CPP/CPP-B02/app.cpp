// CPP-B02 demo — C++ 애플리케이션: C HAL 호출 + ISR 정의 + C 콜백 등록
//   -DHAL_FORGET_EXTERN_C : hal.h 의 extern "C" 가드를 끈다 → 링크 에러 (시나리오 2)
//   -DISR_FORGET_EXTERN_C : USART1 ISR 에 extern "C" 를 빼먹는다 → 링크 성공, 조용한 버그 (시나리오 3)
#include "hal.h"
#include <cstdio>

namespace app {
class LineReader {
public:
    void attach() { hal_uart_set_rx_callback(&LineReader::rx_trampoline, this); }
    const char *line() const { return ready_ ? buf_ : nullptr; }
private:
    // C HAL 은 멤버 함수를 부를 수 없다 → static 트램펄린 + ctx(this)
    static void rx_trampoline(void *ctx, uint8_t b) { static_cast<LineReader *>(ctx)->on_byte(b); }
    void on_byte(uint8_t b) {
        if (b == '\n') { buf_[len_] = '\0'; ready_ = true; len_ = 0; }
        else if (b != '\r' && len_ + 1 < sizeof buf_) buf_[len_++] = static_cast<char>(b);
    }
    char buf_[32] = {};
    unsigned len_ = 0;
    bool ready_ = false;
};
}  // namespace app

static volatile unsigned g_usart1_hits;   // C++20: volatile 에 ++ 는 deprecated (CPP-B05 참고)

#ifdef ISR_FORGET_EXTERN_C
void USART1_IRQHandler() { g_usart1_hits = g_usart1_hits + 1; }             // BUG: C++ 링키지 → _Z17USART1_IRQHandlerv
#else
extern "C" void USART1_IRQHandler() { g_usart1_hits = g_usart1_hits + 1; }  // OK: 심볼 USART1_IRQHandler → weak 를 덮어씀
#endif
extern "C" void SysTick_Handler() {}

int main() {
    hal_gpio_write(5, true);
    std::printf("[app] ODR = 0x%08x\n", (unsigned)hal_gpio_read_odr());

    static const uint8_t msg[] = "hello from C++\n";
    hal_uart_write(msg, sizeof msg - 1);

    app::LineReader rd;
    rd.attach();
    for (const char *p = "AT+RST\r\n"; *p; ++p) hal_sim_rx_byte(static_cast<uint8_t>(*p));
    std::printf("[app] rx line = \"%s\"\n", rd.line() ? rd.line() : "(none)");

    hal_sim_fire_irq(USART1_IRQn);
    std::printf("[app] usart1 ISR hits = %u  %s\n", g_usart1_hits,
                g_usart1_hits ? "(OK)" : "(BUG: Default_Handler ran instead)");
    return g_usart1_hits ? 0 : 1;
}
