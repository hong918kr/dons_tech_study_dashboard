// CPP-M04 demo: ODR RMW toggle 사이에 ISR 이 끼어들 때 mock 버스에서 일어나는 일을 로그로 출력한다
// build: c++ -std=c++20 -Wall -Wextra -O2 -fno-exceptions -fno-rtti demo_isr_race.cpp -o demo_isr_race && ./demo_isr_race
#define main solution_main
#include "solution.cpp"
#undef main

static const char* reg_name(Reg r) {
    switch (r) {
    case Reg::IDR:   return "IDR ";
    case Reg::ODR:   return "ODR ";
    case Reg::BSRR:  return "BSRR";
    default:         return "?   ";
    }
}
static void dump(const char* title) {
    std::printf("--- %s\n", title);
    for (unsigned i = 0; i < mock::log_n; ++i) {
        const auto& e = mock::log[i];
        std::printf("  [%u] %s %s 0x%08x\n", i, e.is_write ? "write" : "read ", reg_name(e.reg), e.value);
    }
    std::printf("  최종 ODR = 0x%04x  (PA5=%u, PA3=%u)\n", PortA::peek(Reg::ODR),
                (PortA::peek(Reg::ODR) >> 5) & 1u, (PortA::peek(Reg::ODR) >> 3) & 1u);
}

int main() {
    reset_all();
    mock::arm_isr(isr_set_other);
    Led::toggle();
    dump("toggle() = ODR RMW, ISR 이 그 사이 PA3 set");

    reset_all();
    mock::arm_isr(isr_set_other);
    Led::toggle_bsrr();
    dump("toggle_bsrr(), 같은 끼어들기");

    reset_all();
    mock::arm_isr(isr_toggle_led);
    Led::toggle_bsrr();
    dump("toggle_bsrr(), ISR 도 같은 핀 PA5 를 toggle");

    reset_all();
    mock::arm_isr(isr_toggle_led);
    Led::toggle_critical<mock::IrqGuard>();
    dump("toggle_critical(), ISR 은 unmask 때까지 pending");

    unsigned fired = 0;
    const unsigned lost_rmw = count_lost_updates<Led::toggle>(0x1234'5678u, 2001, fired);
    std::printf("\n고정 시드 2001회: ISR 발생 %u회, RMW 유실 %u회", fired, lost_rmw);
    const unsigned lost_bsrr = count_lost_updates<Led::toggle_bsrr>(0x1234'5678u, 2001, fired);
    std::printf(", BSRR 유실 %u회\n", lost_bsrr);
    return 0;
}
