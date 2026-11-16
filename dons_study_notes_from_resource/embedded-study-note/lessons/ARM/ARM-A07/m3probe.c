/* ENV: qemu */
/* ARM-A07 m3probe — SSAT 은 ARMv7-M 기본 명령 (Cortex-M3 에도 있다), QADD/SMLAD/SMULBB 는 DSP 확장 (ARMv7E-M: M4/M7).
 * run_qemu.sh m3probe: -mcpu=cortex-m3 로 빌드해 mps2-an385 에서 실행.
 *   1) ssat 은 그냥 돈다 (어셈블러도 M3 에서 받아 준다)
 *   2) qadd 는 M3 어셈블러가 거부하므로 인코딩을 .inst.w 로 직접 넣는다 → UNDEFINSTR UsageFault → (UsageFault 꺼짐) HardFault 로 승격
 *   HardFault 에서 CFSR.UNDEFINSTR(bit16) 와 HFSR.FORCED(bit30) 가 서 있으면 exit 0.
 */
#include <stdint.h>

void sh_puts(const char *s);
void sh_hex(uint32_t v);
void sh_dec(int32_t v);
__attribute__((noreturn)) void sh_exit(int code);
#define REG32(a) (*(volatile uint32_t *)(uintptr_t)(a))

static volatile int g_stage;

void HardFault_Handler(void) {
    uint32_t cfsr = REG32(0xE000ED28u), hfsr = REG32(0xE000ED2Cu);
    sh_puts("HardFault: CFSR="); sh_hex(cfsr); sh_puts(" HFSR="); sh_hex(hfsr); sh_puts("\n");
    int ok = g_stage == 2 && (cfsr & (1u << 16)) && (hfsr & (1u << 30));
    sh_puts(ok ? "PASS qadd on Cortex-M3 -> UNDEFINSTR (escalated: FORCED)\n" : "FAIL unexpected fault\n");
    sh_exit(ok ? 0 : 1);
}

int main(void) {
    volatile int32_t v = 100000;
    int32_t r;
    __asm__ volatile("ssat %0, #16, %1" : "=r"(r) : "r"(v));          /* ARMv7-M 기본 → M3 OK */
    sh_puts("ssat #16 of 100000 on Cortex-M3 = "); sh_dec(r); sh_puts("\n");
    if (r != 32767) { sh_puts("FAIL ssat\n"); return 1; }
    sh_puts("PASS ssat runs on Cortex-M3\n");
    g_stage = 2;
    __asm__ volatile("mov r1, %0\n\t.inst.w 0xfa81f081" : : "r"(v) : "r0", "r1");   /* qadd r0, r1, r1 */
    sh_puts("FAIL qadd executed on Cortex-M3 without fault\n");
    return 1;
}
