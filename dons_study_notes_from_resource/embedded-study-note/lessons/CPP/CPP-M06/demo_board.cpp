// CPP-M06 측정용: 객체 정의 TU — 앱 TU 는 console() 뒤의 실제 타입을 모른다 (LTO 없으면)
#include "demo_bus.hpp"
UartDrv g_uart{0x40004400u};
ByteBus& console()  { return g_uart; }
UartDrv& uart_ref() { return g_uart; }
#ifndef ONLY_UART
SpiDrv  g_spi{0x40013000u};
I2cDrv  g_i2c{0x40005400u, 0x50};
ByteBus* const g_buses[3] = {&g_uart, &g_spi, &g_i2c};
#endif
#if defined(VIRTUAL_DTOR) && defined(OUTLINE_DTOR)
ByteBus::~ByteBus() {}
#endif
