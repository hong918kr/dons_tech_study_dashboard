// CPP-M01 demo: Cortex-M4 로 컴파일해 Reg 와 손으로 쓴 C 스타일을 나란히 비교한다 (main 없음, asm 보기용)
// 빌드는 asm_target.sh 참고
#include "reg.hpp"

using Uart1 = UartMap<MmioBus>;
#define U1_BASE 0x40004400u
#define C_REG(off) (*(volatile std::uint32_t*)(U1_BASE + (off)))
#define C_REG8(off) (*(volatile std::uint8_t*)(U1_BASE + (off)))

extern "C" {
void cpp_write_cr(std::uint32_t v) { Uart1::CR::write(v); }
void c_write_cr(std::uint32_t v)   { C_REG(0x0C) = v; }

std::uint32_t cpp_read_sr() { return Uart1::SR::read(); }
std::uint32_t c_read_sr()   { return C_REG(0x00); }

void cpp_set_en() { Uart1::CR::set_bits(uart::CR_EN); }
void c_set_en()   { C_REG(0x0C) |= uart::CR_EN; }

void cpp_modify_cr() { Uart1::CR::modify(0xF0u, 0x30u); }
void c_modify_cr()   { C_REG(0x0C) = (C_REG(0x0C) & ~0xF0u) | 0x30u; }

void cpp_putc(std::uint8_t b) { while ((Uart1::SR::read() & uart::SR_TXE) == 0) {} Uart1::TDR::write(b); }
void c_putc(std::uint8_t b)   { while ((C_REG(0x00) & uart::SR_TXE) == 0) {} C_REG8(0x08) = b; }

// volatile: 같은 레지스터를 두 번 읽으면 load 도 2개여야 한다
std::uint32_t cpp_read_twice() { return Uart1::SR::read() + Uart1::SR::read(); }

bool cpp_init(std::uint16_t brr) { return uart_init<Uart1>(brr); }
}
