// CPP-M06 측정용: 호출부 TU — 호출 지점마다 noinline 함수 하나씩, objdump 로 간접/직접을 본다
#include "demo_bus.hpp"
#define PROBE extern "C" [[gnu::noinline]]

PROBE u32 via_base(ByteBus& b)  { return b.status(); }   // 기반 참조: 타입 정보 없음
PROBE u32 via_ref(UartDrv& u)   { return u.status(); }   // 구체 타입 참조: 파생이 있을 수 있나?
PROBE u32 via_object()          { return g_uart.status(); } // 객체 자체: 동적 타입이 확정
PROBE u32 via_console()         { return console().status(); } // 다른 TU 함수가 돌려준 참조
#ifndef ONLY_UART
PROBE u32 fanout() {                                       // 서로 다른 타입 3개를 도는 표
    u32 sum = 0;
    for (ByteBus* b : g_buses) sum += b->status();
    return sum;
}
#endif

static volatile u32 sink;
extern "C" void _start() {
    static const u8 msg[2] = {0x41, 0x42};
    console().init(115200);
    console().write(msg, 2);
    sink = via_base(console());
    sink = via_ref(uart_ref());
    sink = via_object();
    sink = via_console();
#ifndef ONLY_UART
    sink = fanout();
#endif
    for (;;) {}
}
