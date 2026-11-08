/* gpio.c — uart.c 와 같은 이름의 helper(hw_reset)를 가진다. static 이라 충돌 없음. */
#include "gpio.h"

static unsigned s_odr;                  /* → b */

#ifdef DEMO_COLLISION
void hw_reset(void) {                   /* static 을 빠뜨림 → 외부 연결 (T) → uart.c 와 충돌 */
#else
static void hw_reset(void) {            /* → t (uart.c 의 hw_reset 과 별개) */
#endif
    s_odr = 0;
}

void gpio_init(void) { hw_reset(); }

void gpio_write(unsigned pin, int level) {
    if (level) s_odr |= 1u << pin;
    else       s_odr &= ~(1u << pin);
}

unsigned gpio_state(void) { return s_odr; }

#ifdef DEMO_TENTATIVE
int debug_flags;                        /* tentative definition — uart.c 에도 똑같이 있다 */
#endif
