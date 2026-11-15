/* irq.h — 벡터 번호, 핸들러 이름(= 링커가 찾는 심볼 이름), 시뮬레이션 NVIC */
#ifndef IRQ_H
#define IRQ_H
#ifdef __cplusplus
extern "C" {
#endif

enum { VEC_RESET, VEC_NMI, VEC_HARDFAULT, VEC_SYSTICK, VEC_USART1, VEC_TIM2, VEC_COUNT };

void Reset_Handler(void);
void Default_Handler(void);
void NMI_Handler(void);
void HardFault_Handler(void);
void SysTick_Handler(void);
void USART1_IRQHandler(void);
void TIM2_IRQHandler(void);

extern void (*const g_vectors[VEC_COUNT])(void);
extern volatile unsigned g_default_hits;   /* Default_Handler 가 불린 횟수 */
extern volatile int g_unhandled_vec;       /* 마지막으로 기본 핸들러로 간 벡터 */

void nvic_fire(int vec);                   /* 호스트용: 인터럽트 발생 흉내 */

#ifdef __cplusplus
}
#endif
#endif
