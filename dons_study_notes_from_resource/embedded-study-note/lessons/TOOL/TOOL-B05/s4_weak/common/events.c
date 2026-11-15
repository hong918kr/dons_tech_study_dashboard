/* events.c — ISR 가 남긴 이벤트 카운터 (항상 링크됨) */
#include "events.h"

static volatile unsigned g_ev[EV_COUNT];

void event_post(int ev) { g_ev[ev]++; }
unsigned event_count(int ev) { return g_ev[ev]; }
