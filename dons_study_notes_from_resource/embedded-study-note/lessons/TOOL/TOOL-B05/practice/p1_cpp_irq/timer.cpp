// timer.cpp — TIM2 핸들러를 C++ 드라이버 파일로 옮겼다 (연습 1)
// 버그 버전은 이 줄들만 달랐다:  #include "irq.h" 없음,  extern "C" 없음
//   → 정의가 __Z15TIM2_IRQHandlerv 로 맹글링 → weak 기본 핸들러가 그대로 남음 (링크는 성공)
#include "irq.h"      // 핸들러 원형을 extern "C" 블록 안에서 먼저 본다
#include "events.h"

namespace {
unsigned s_overflows;   // C++ 쪽 내부 상태 (맹글링돼도 상관없는 이름)
}

extern "C" void TIM2_IRQHandler(void)   // 벡터 테이블이 찾는 C 이름 그대로
{
    s_overflows++;
    event_post(EV_TIM2_TICK);
}
