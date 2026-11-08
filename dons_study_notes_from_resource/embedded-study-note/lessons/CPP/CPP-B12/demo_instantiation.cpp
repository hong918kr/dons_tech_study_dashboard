// CPP-B12 demo: 템플릿 하나 → T 마다 함수 하나 (진짜 MMIO 스타일, volatile 캐스트)
// 이 파일은 실행하지 않는다 (0x4000'0000 같은 주소를 진짜로 읽으면 macOS 에선 segfault). 오브젝트만 만들어 관찰한다.
//   c++ -std=c++20 -O0 -fno-exceptions -fno-rtti -c demo_instantiation.cpp -o /tmp/inst_O0.o
//   nm /tmp/inst_O0.o | c++filt          # 인스턴스 4개가 각각 심볼로
//   nm /tmp/inst_O0.o                    # 맹글링된 원래 이름
//   c++ -std=c++20 -O2 -fno-exceptions -fno-rtti -c demo_instantiation.cpp -o /tmp/inst_O2.o
//   nm /tmp/inst_O2.o | c++filt          # 전부 인라인 → 템플릿 심볼이 사라진다
//   c++ -std=c++20 -O2 -fno-exceptions -fno-rtti -S -o - demo_instantiation.cpp   # ldrb / ldrh / ldr w / ldr x
#include <concepts>
#include <cstdint>

template <typename T>
concept RegWord = std::unsigned_integral<T> && !std::same_as<T, bool> && (sizeof(T) <= 8);

template <RegWord T>
T reg_read(std::uintptr_t addr) {
    return *reinterpret_cast<volatile const T*>(addr);   // 실제 MMIO 읽기: 정확히 sizeof(T) 폭으로 1회
}

template <RegWord T>
void reg_write(std::uintptr_t addr, T value) {
    *reinterpret_cast<volatile T*>(addr) = value;        // 실제 MMIO 쓰기
}

constexpr std::uintptr_t PERIPH = 0x4000'0000u;

std::uint64_t poll_all() {
    std::uint64_t s = 0;
    s += reg_read<std::uint8_t>(PERIPH + 0x08);
    s += reg_read<std::uint16_t>(PERIPH + 0x04);
    s += reg_read<std::uint32_t>(PERIPH + 0x00);
    s += reg_read<std::uint64_t>(PERIPH + 0x10);
    reg_write<std::uint8_t>(PERIPH + 0x08, 0x5A);
    reg_write(PERIPH + 0x00, 0x1u);                       // 추론: unsigned int → reg_write<unsigned int>
    return s;
}
