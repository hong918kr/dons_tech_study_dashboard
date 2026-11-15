// CPP-A02 demo_strong_uart4.cpp — 다른 TU 의 "강한" 정의가 weak alias 를 덮는지 (asm_targets.sh vm)
// -DFORGET_EXTERN_C : extern "C" 를 빼먹은 판 — 이름이 맹글링되어 weak 기본 핸들러가 그대로 남는다 (링크 에러 없음!)
volatile unsigned g_uart4_hits;
#ifdef FORGET_EXTERN_C
void UART4_IRQHandler(void) { g_uart4_hits = g_uart4_hits + 1; }
#else
extern "C" void UART4_IRQHandler(void) { g_uart4_hits = g_uart4_hits + 1; }
#endif
