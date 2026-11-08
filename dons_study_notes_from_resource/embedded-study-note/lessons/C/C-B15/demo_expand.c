/* C-B15 demo: 순진한 매크로가 실제로 어떻게 펼쳐지는지 본다.
 *   cc -E -P demo_expand.c | grep -v '^$'      전처리 결과만 보기
 *   cc -std=c11 -Wall -c demo_expand.c          컴파일러 경고 보기
 */
#define SQUARE(x)        x * x
#define MAX(a, b)        a > b ? a : b
#define LED_ON_COUNT(n)  led_on(n); g_led_events++

void led_on(int n);
int g_led_events;

int demo(int a, int x, int go) {
    int r1 = SQUARE(a + 1);
    int r2 = 100 / SQUARE(5);
    int r3 = 10 + MAX(3, 2);
    int r4 = MAX(x & 0xF, 3);
    if (go) LED_ON_COUNT(1);
    return r1 + r2 + r3 + r4;
}
