/* timer.c — TIM2 업데이트 인터럽트 핸들러 (새로 추가한 파일) */
#include "irq.h"
#include "events.h"

void TIM2_IRQHandler(void)
{
    event_post(EV_TIM2_TICK);     /* 실제: SR 의 UIF 클리어 → tick 처리 */
}
