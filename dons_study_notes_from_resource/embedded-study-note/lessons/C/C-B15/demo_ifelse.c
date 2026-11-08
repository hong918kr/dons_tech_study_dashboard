/* C-B15 demo: 중괄호 { } 매크로와 문장 나열 매크로는 if/else 를 깨뜨린다.
 *   cc -std=c11 -c demo_ifelse.c                     → 컴파일 에러 (BRACE)
 *   cc -std=c11 -c -DUNBRACED demo_ifelse.c          → 컴파일 에러 (UNBRACED)
 *   cc -std=c11 -c -DFIXED demo_ifelse.c             → OK (do-while(0))
 */
void led_on(int n);
int g_led_events;

#if defined(FIXED)
#define LED_ON_COUNT(n)  do { led_on(n); g_led_events++; } while (0)
#elif defined(UNBRACED)
#define LED_ON_COUNT(n)  led_on(n); g_led_events++
#else
#define LED_ON_COUNT(n)  { led_on(n); g_led_events++; }
#endif

void demo(int go) {
    if (go)
        LED_ON_COUNT(1);
    else
        led_on(0);
}
