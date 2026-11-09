/* demo_m4_hal.h — solution.c의 FW_BEGIN~FW_END 구간을 Cortex-M4용으로 컴파일할 때 쓰는 HAL 선언.
 * 시뮬 대신 실제 보드가 구현한다고 가정 (Part C). 레지스터 접근은 보드 파일 몫이고 여기엔 없다.
 *
 * sed -n '/FW_BEGIN/,/FW_END/p' solution.c > /tmp/TEST-A05_work/fw_only.c
 * clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -ffreestanding -std=c11 \
 *       -Wall -Wextra -Werror -include demo_m4_hal.h -c /tmp/TEST-A05_work/fw_only.c -o /tmp/TEST-A05_work/fw_m4.o
 */
#include <stdbool.h>
#include <stdint.h>

uint32_t hal_now_us(void);
void     hal_timer_start(uint32_t delay_us);
void     hal_timer_stop(void);
void     hal_uart_send(uint32_t req_id);

static inline uint32_t hal_irq_save(void)
{
    uint32_t primask;
    __asm volatile("mrs %0, primask\n\tcpsid i" : "=r"(primask));
    return primask;
}

static inline void hal_irq_restore(uint32_t primask)
{
    __asm volatile("msr primask, %0" : : "r"(primask));
}

/* 실제 CPU에서는 명령 실행 시간이 흐르므로 window는 빈 함수, 시험 표식도 빈 함수 */
static inline void hal_window(void) {}
static inline void hal_mark(uint32_t tag, uint32_t id) { (void)tag; (void)id; }

enum { MARK_RX_CHECK, MARK_RX_COMMIT, MARK_TMR_CHECK, MARK_TMR_COMMIT };
