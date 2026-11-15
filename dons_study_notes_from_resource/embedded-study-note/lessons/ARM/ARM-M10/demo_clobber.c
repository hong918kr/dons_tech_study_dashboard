/* ENV: qemu */
/* ARM-M10 demo_clobber.c — inline asm 크리티컬 섹션에서 "memory" clobber 를 빼면?
 * run_qemu.sh clobber 가 -O2 로 컴파일해 두 함수의 실제 코드를 보여 준다 (실행은 안 함).
 */
#include <stdint.h>

uint32_t g_count;                       /* ISR 도 올리는 공유 카운터 (일부러 volatile 아님) */

static inline uint32_t enter_noclob(void) {
    uint32_t pm;
    __asm__ volatile("mrs %0, primask\n\tcpsid i" : "=r"(pm));          /* clobber 없음 */
    return pm;
}
static inline void exit_noclob(uint32_t pm) {
    __asm__ volatile("msr primask, %0" :: "r"(pm));                     /* clobber 없음 */
}
static inline uint32_t enter_ok(void) {
    uint32_t pm;
    __asm__ volatile("mrs %0, primask\n\tcpsid i" : "=r"(pm) :: "memory");
    return pm;
}
static inline void exit_ok(uint32_t pm) {
    __asm__ volatile("msr primask, %0" :: "r"(pm) : "memory");
}

void add_n_noclob(uint32_t n) {         /* n 번 "보호된" 증가 */
    for (uint32_t i = 0; i < n; i++) {
        uint32_t k = enter_noclob();
        g_count++;
        exit_noclob(k);
    }
}
void add_n_ok(uint32_t n) {
    for (uint32_t i = 0; i < n; i++) {
        uint32_t k = enter_ok();
        g_count++;
        exit_ok(k);
    }
}
