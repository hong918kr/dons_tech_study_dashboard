// CPP-M04 demo: 잘못된 핀/버스 조합이 어떤 컴파일 에러가 되는가
// 정상 빌드: c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti demo_errors.cpp -o demo_errors && ./demo_errors
// 에러 보기: c++ -std=c++20 -fno-exceptions -fno-rtti -fsyntax-only -DSHOW_ERROR=1 demo_errors.cpp  (1..5)
#define main solution_main
#include "solution.cpp"
#undef main

static void spi_select(Pin<PortA, 4>) {}       // 값 없는 타입도 "인자 타입"으로 핀을 고정할 수 있다

int main() {
    spi_select(Pin<PortA, 4>{});
#if defined(SHOW_ERROR) && SHOW_ERROR == 1
    Pin<PortA, 16>::set();                     // E1: 16번 핀은 없다
#elif defined(SHOW_ERROR) && SHOW_ERROR == 2
    PinBus<Pin<PortA, 0>, Pin<PortB, 1>>::write(1u);   // E2: 포트가 섞인 버스
#elif defined(SHOW_ERROR) && SHOW_ERROR == 3
    PinBus<Pin<PortA, 0>, Pin<PortA, 0>>::write(1u);   // E3: 같은 핀 두 번
#elif defined(SHOW_ERROR) && SHOW_ERROR == 4
    spi_select(Pin<PortB, 4>{});               // E4: 다른 포트의 같은 번호
#elif defined(SHOW_ERROR) && SHOW_ERROR == 5
    Pin<PortA, -1>::set();                     // E5: 음수 → unsigned NTTP 는 축소 변환
#endif
    std::printf("ok\n");
    return 0;
}
