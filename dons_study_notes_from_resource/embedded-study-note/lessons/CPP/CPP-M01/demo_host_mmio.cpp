// CPP-M01 demo: "진짜 MMIO 경로" (reinterpret_cast + volatile) 를 호스트에서 실제로 실행해 본다.
//   mmap 으로 고정 주소에 페이지를 받아 주변장치인 척 시키고, UartMap<MmioBus, HOST_BASE> 를 돌린다.
//   같은 드라이버를 MockBus 로도 돌려서, 레지스터 블록 바이트가 똑같이 나오는지 비교한다.
// build: c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti demo_host_mmio.cpp -o demo_host_mmio && ./demo_host_mmio
#include <cstdio>
#include <cstring>
#include <sys/mman.h>
#include "reg.hpp"

constexpr std::uintptr_t HOST_BASE = 0x7'0000'0000u;   // Cortex-M 주소 대신 맥에서 비어 있는 고정 주소

struct MockBus {
    static constexpr std::uintptr_t BASE = 0x4000'4400u;
    alignas(8) static inline std::uint8_t mem[0x40] = {};
    static inline unsigned accesses = 0;
    template <RegWord T> static T read(std::uintptr_t a) { T v; std::memcpy(&v, &mem[a - BASE], sizeof v); ++accesses; return v; }
    template <RegWord T> static void write(std::uintptr_t a, T v) { std::memcpy(&mem[a - BASE], &v, sizeof v); ++accesses; }
};

template <typename Map>
static std::size_t bring_up() {
    const std::uint8_t msg[] = {0x10, 0x20, 0x30};
    if (!uart_init<Map>(0x0683u)) return 0;
    return uart_send<Map>(msg, 8);
}

int main() {
    void* want = reinterpret_cast<void*>(HOST_BASE);
    void* got = mmap(want, 0x4000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON | MAP_FIXED, -1, 0);
    if (got != want) { std::printf("mmap gave %p instead of %p, skipped\n", got, want); return 0; }

    using HostMap = UartMap<MmioBus, HOST_BASE>;
    using MockMap = UartMap<MockBus>;

    // "하드웨어" 초기값: ID, SR.TXE 를 양쪽에 똑같이 넣는다
    auto* page = static_cast<std::uint8_t*>(got);
    std::uint32_t id = uart::CHIP_ID, sr = uart::SR_TXE;
    std::memcpy(page + 0x18, &id, 4);          std::memcpy(page + 0x00, &sr, 4);
    std::memcpy(MockBus::mem + 0x18, &id, 4);  std::memcpy(MockBus::mem + 0x00, &sr, 4);

    std::size_t a = bring_up<HostMap>();
    std::size_t b = bring_up<MockMap>();
    std::printf("sent over MmioBus(mmap page at %#lx): %zu\n", static_cast<unsigned long>(HOST_BASE), a);
    std::printf("sent over MockBus(array):             %zu  (mock accesses=%u)\n", b, MockBus::accesses);
    std::printf("register block bytes identical: %s\n", std::memcmp(page, MockBus::mem, 0x40) == 0 ? "yes" : "NO");
    std::printf("CR=%#x BRR=%#x TDR=%#x\n", HostMap::CR::read(), HostMap::BRR::read(),
                static_cast<unsigned>(page[0x08]));
    return (a == 3 && b == 3 && std::memcmp(page, MockBus::mem, 0x40) == 0) ? 0 : 1;
}
