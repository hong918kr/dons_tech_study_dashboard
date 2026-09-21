#include <cstdint>
#include <iostream>

// 1. 하드웨어 주소 정의 (constexpr 사용)
// GPIO A 포트의 기본 주소
constexpr uintptr_t GPIOA_BASE_ADDR = 0x40020000;

// 출력 데이터 레지스터(ODR)의 오프셋
constexpr uintptr_t ODR_OFFSET = 0x14;

// 최종 레지스터 주소
constexpr uintptr_t GPIOA_ODR_ADDR = GPIOA_BASE_ADDR + ODR_OFFSET;

// 레지스터 데이터 타입 (일반적으로 uint32_t)
using RegType = uint32_t;

// 2. 레지스터 접근을 위한 템플릿 클래스 정의
// 레지스터 주소(Address)를 Non-Type Template Parameter로 받습니다.
template <uintptr_t Address>
class Register
{
private:
    // volatile 키워드를 사용하여 컴파일러 최적화를 방지하고
    // 실제 메모리 맵 레지스터에 대한 접근을 강제합니다.
    static volatile RegType& get_ref() noexcept
    {
        // 런타임 오버헤드 없이 상수 주소를 volatile 포인터로 변환하여 참조를 반환합니다.
        return *reinterpret_cast<volatile RegType*>(Address);
    }

public:
    // 읽기 작업 (Read): 타입 안전성을 제공하며 volatile 접근 보장
    static RegType read() noexcept
    {
        return get_ref();
    }

    // 쓰기 작업 (Write): 레지스터에 전체 값을 기록
    static void write(RegType value) noexcept
    {
        get_ref() = value;
    }

    // 수정 작업 (Read-Modify-Write, RMW): 특정 비트만 설정
    static void set_bits(RegType bits_to_set) noexcept
    {
        get_ref() |= bits_to_set;
    }
    
    // 수정 작업 (RMW): 특정 비트만 해제 (Clear)
    static void clear_bits(RegType bits_to_clear) noexcept
    {
        get_ref() &= ~bits_to_clear;
    }
};

// 3. 구체적인 레지스터 타입 별칭 생성 (가독성 향상)
// GPIOA의 출력 데이터 레지스터 (ODR)
using GpioA_ODR = Register<GPIOA_ODR_ADDR>;

// 4. 메모리 맵 레지스터를 시뮬레이션하기 위한 가상 메모리 공간
volatile RegType fake_memory[0x10000] = {0};

// 5. 메모리 맵 레지스터 주소를 가상 메모리로 매핑
template <>
volatile RegType& Register<GPIOA_ODR_ADDR>::get_ref() noexcept
{
    return fake_memory[ODR_OFFSET / sizeof(RegType)];
}

// 6. GPIO 핀 초기화 함수
void initialize_gpio_pin()
{
    // Pin 0와 Pin 1을 High로 설정하고 나머지 핀은 0으로 클리어 (전체 쓰기)
    GpioA_ODR::write(0b11); 
    
    // Pin 5만 High로 설정 (RMW: 다른 비트의 상태는 유지)
    GpioA_ODR::set_bits(1 << 5);

    // Pin 1을 Low로 클리어 (RMW: 다른 비트의 상태는 유지)
    GpioA_ODR::clear_bits(1 << 1);

    // 현재 레지스터 값 읽기
    RegType current_state = GpioA_ODR::read();
    std::cout << "Current GPIOA_ODR state: 0x" << std::hex << current_state << std::endl;
}

// 7. Main 함수
int main()
{
    std::cout << "Initializing GPIO pins..." << std::endl;
    initialize_gpio_pin();

    // GPIOA_ODR의 최종 상태 출력
    std::cout << "Final GPIOA_ODR state: 0x" << std::hex << fake_memory[ODR_OFFSET / sizeof(RegType)] << std::endl;

    return 0;
}