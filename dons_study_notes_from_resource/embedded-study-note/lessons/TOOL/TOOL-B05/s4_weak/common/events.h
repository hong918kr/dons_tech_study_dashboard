/* events.h — 핸들러가 남기는 흔적 (C/C++ 양쪽에서 include) */
#ifndef EVENTS_H
#define EVENTS_H
#ifdef __cplusplus
extern "C" {
#endif

enum { EV_UART_RX, EV_TIM2_TICK, EV_COUNT };
void event_post(int ev);
unsigned event_count(int ev);

#ifdef __cplusplus
}
#endif
#endif
