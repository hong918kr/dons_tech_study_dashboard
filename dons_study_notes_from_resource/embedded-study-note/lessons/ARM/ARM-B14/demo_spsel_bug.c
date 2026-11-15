/* ENV: qemu */
/* ARM-B14 demo_spsel_bug.c — 7. 자주 하는 실수 1 재현: PSP 를 준비하지 않고 C 에서 SPSEL=1.
 * 빌드/실행: run_qemu.sh spselbug   (= demo_spsel_bug.c + solution.S + shared_startup.c)
 */
#include <stdint.h>
uint32_t get_sp(void);
uint32_t get_psp(void);
uint32_t get_control(void);
void set_control(uint32_t v);
void sh_puts(const char *s);
void sh_hex(uint32_t v);
volatile uint32_t g_svc_cmd, g_sp_before_svc;               /* solution.S 의 svc_call 이 참조 */
void exc_observe(uint32_t a, uint32_t b, uint32_t c, uint32_t d) { (void)a; (void)b; (void)c; (void)d; }

__attribute__((noinline)) static uint32_t use_stack(uint32_t x) {   /* push/pop 이 있는 평범한 함수 */
    volatile uint32_t buf[4] = { x, x + 1u, x + 2u, x + 3u };
    return buf[0] + buf[3];
}

int main(void) {
    sh_puts("before: sp="); sh_hex(get_sp()); sh_puts(" psp="); sh_hex(get_psp());
    sh_puts(" control="); sh_hex(get_control()); sh_puts("\n");
    set_control(2u);                     /* 버그: PSP 는 리셋 후 그대로(QEMU 에서 0) */
    uint32_t r = use_stack(5u);          /* sp = PSP 위에서 push → ? */
    sh_puts("survived?! r="); sh_hex(r); sh_puts("\n");
    return 0;
}
