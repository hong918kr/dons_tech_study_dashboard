/* CPP-A07 demo_target.cpp — solution.cpp 의 프레임워크 + 시나리오를 Cortex-M4 용으로 (하네스·트레이스 제외).
 * 확인할 것: ① 전역 AO 가 constinit (= .bss, 초기화 코드·.init_array 0) ② 정적 객체별 RAM (nm -S)
 *            ③ operator new / malloc / __cxa_* 참조 0 ④ 디스패치 asm.   bash target.sh 가 빌드·측정한다. */
#define AO_NO_HARNESS
#define AO_TRACE_CAP 0
#include "solution.cpp"

constinit Board g_board;                                     // 동적 초기화가 필요하면 여기서 컴파일 에러

extern "C" void EXTI0_IRQHandler() { g_board.exti_isr(); }   // 버튼 핀 양쪽 엣지
extern "C" void SysTick_Handler() { ++g_now; g_board.systick_isr(); }

[[noreturn]] void app_main() {
    g_board.start();
    for (;;) {
        if (!g_board.fw.run_one()) __asm volatile("wfi");     // idle 훅: 다음 인터럽트까지 잠 (경쟁 조건은 5. 풀이 참고)
    }
}
int main() { app_main(); }
#ifdef AO_LINK                                               // link.sh: -nostartfiles 링크용 진입점 (.data 복사 등은 생략)
extern "C" [[noreturn]] void _start() { app_main(); }
#endif

#ifdef AO_SHOW_SIZES   /* 크기를 컴파일러에게 묻기: 정의 없는 템플릿에 넣으면 에러 메시지에 값이 찍힌다 */
template <std::size_t...> struct Show;
Show<sizeof(Event), sizeof(PressEvt), sizeof(TimeEvent), sizeof(Button::State), sizeof(EventQueue), sizeof(ActiveObject),
     sizeof(Button), sizeof(Led), sizeof(Stats), sizeof(Framework), sizeof(StaticPool<kBlock, 8>), sizeof(Board)> show_sizes;
#endif
