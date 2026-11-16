/* ENV: qemu */
/* BM-A08 demo_probe — QEMU 10.2.1 mps2-an385 의 PMSAv7 MPU 가 "규칙 위반" 설정을 어떻게 다루나 실측.
 * 실기(Cortex-M3)에서는 대부분 UNPREDICTABLE 이라 이 결과를 HW 사실로 일반화하면 안 된다 (md 의 표 참고).
 * run_qemu.sh probe 는 -d guest_errors 로 실행 → QEMU 의 경고 줄도 함께 보인다.
 */
#include <stdint.h>

extern uint32_t _estack[];
static int semi(int op, const void *arg) {
    register int r0 __asm__("r0") = op;
    register const void *r1 __asm__("r1") = arg;
    __asm__ volatile("bkpt 0xab" : "+r"(r0) : "r"(r1) : "memory");
    return r0;
}
static void sh_puts(const char *s) { semi(0x04, s); }
static void sh_hex(uint32_t v) {
    char b[11];
    b[0] = '0'; b[1] = 'x'; b[10] = 0;
    for (int i = 9; i >= 2; --i, v >>= 4) b[i] = "0123456789abcdef"[v & 0xFu];
    sh_puts(b);
}
__attribute__((noreturn)) static void sh_exit(int code) {
    uint32_t blk[2] = { 0x20026u, (uint32_t)code };
    semi(0x20, blk);
    for (;;) { }
}

#define REG32(a)   (*(volatile uint32_t *)(uintptr_t)(a))
#define SCB_SHCSR  REG32(0xE000ED24u)
#define SCB_CFSR   REG32(0xE000ED28u)
#define SCB_MMFAR  REG32(0xE000ED34u)
#define MPU_TYPE   REG32(0xE000ED90u)
#define MPU_CTRL   REG32(0xE000ED94u)
#define MPU_RNR    REG32(0xE000ED98u)
#define MPU_RBAR   REG32(0xE000ED9Cu)
#define MPU_RASR   REG32(0xE000EDA0u)
#define dsb() __asm__ volatile("dsb" ::: "memory")
#define isb() __asm__ volatile("isb" ::: "memory")

static volatile uint32_t g_faults, g_cfsr, g_mmfar;
__attribute__((used)) void probe_fault(uint32_t *frame) {      /* 기록하고 그 ldr/str(16 비트)을 건너뛴다 */
    g_faults++;
    g_cfsr = SCB_CFSR;
    g_mmfar = SCB_MMFAR;
    SCB_CFSR = g_cfsr;
    frame[6] += 2u;
    if (g_faults > 20u) sh_exit(3);
}
__attribute__((naked)) void Fault_Entry(void) {
    __asm__ volatile("tst lr, #4\n ite eq\n mrseq r0, msp\n mrsne r0, psp\n push {r4, lr}\n bl probe_fault\n pop {r4, pc}\n");
}
void Reset_Handler(void);
typedef void (*vector_t)(void);
__attribute__((section(".isr_vector"), used))
const vector_t g_vectors[16] = { (vector_t)_estack, Reset_Handler, Fault_Entry, Fault_Entry, Fault_Entry,
                                 Fault_Entry, Fault_Entry, 0, 0, 0, 0, Fault_Entry, Fault_Entry, 0, Fault_Entry,
                                 Fault_Entry };

static uint32_t g_buf[256] __attribute__((aligned(1024)));
__attribute__((noinline)) static void touch(volatile uint32_t *p) {    /* 16 비트 ldr 하나 */
    uint32_t v;
    __asm__ volatile("ldr %0, [%1]" : "=l"(v) : "l"(p) : "memory");
    (void)v;
}
static void one(const char *what, uint32_t rbar, uint32_t rasr, volatile uint32_t *addr) {
    MPU_CTRL = 0u; dsb(); isb();
    for (uint32_t i = 0; i < 8u; ++i) { MPU_RNR = i; MPU_RASR = 0u; }
    MPU_RBAR = rbar; MPU_RASR = rasr;
    MPU_CTRL = 5u; dsb(); isb();                              /* PRIVDEFENA | ENABLE */
    g_faults = 0u; g_cfsr = 0u; g_mmfar = 0u;
    touch(addr);
    MPU_CTRL = 0u; dsb(); isb();
    sh_puts(what); sh_puts(" RASR(read)="); MPU_RNR = rbar & 7u; sh_hex(MPU_RASR);
    sh_puts(" access "); sh_hex((uint32_t)(uintptr_t)addr);
    sh_puts(g_faults ? " -> FAULT CFSR=" : " -> no fault  CFSR="); sh_hex(g_cfsr);
    sh_puts("\n");
}

int main(void) {
    const uint32_t b = (uint32_t)(uintptr_t)g_buf;
    SCB_SHCSR |= 1u << 16;
    sh_puts("P1 reset: MPU_TYPE="); sh_hex(MPU_TYPE); sh_puts(" CTRL="); sh_hex(MPU_CTRL);
    sh_puts(" RNR="); sh_hex(MPU_RNR); sh_puts(" RASR0="); sh_hex(MPU_RASR); sh_puts("\n");

    MPU_RBAR = b | (1u << 4) | 5u;
    sh_puts("P2 RBAR VALID|5 -> RNR="); sh_hex(MPU_RNR); sh_puts(" RBAR(read)="); sh_hex(MPU_RBAR); sh_puts("\n");
    MPU_RNR = 9u;
    sh_puts("P3 RNR=9 -> RNR="); sh_hex(MPU_RNR); sh_puts("\n");
    MPU_RNR = 1u; MPU_RASR = 0xFFFFFFFFu;
    sh_puts("P4 RASR=0xFFFFFFFF -> read "); sh_hex(MPU_RASR); sh_puts(" (구현된 비트만 남음)\n");
    MPU_RASR = 0u;

    /* P5: 1 KB 영역인데 base 가 256 B 만 정렬 → 규칙 위반 */
    one("P5 base misaligned (1KB @+256) ", (b + 256u) | 0x10u, (9u << 1) | 1u, &g_buf[64]);
    /* P6: 128 B 영역에 SRD=0xFF (모든 조각 끔) */
    one("P6 128B region, SRD=0xFF       ", b | 0x10u, (0xFFu << 8) | (6u << 1) | 1u, &g_buf[0]);
    /* P7: AP=100 (예약) */
    one("P7 AP=100 (reserved)           ", b | 0x10u, (4u << 24) | (9u << 1) | 1u, &g_buf[0]);
    /* P8: SIZE=3 (16 B, 규칙상 최소 4 = 32 B) — 16 B 안과 밖 */
    one("P8 SIZE=3 (16 B) inside        ", b | 0x10u, (3u << 1) | 1u, &g_buf[0]);
    one("P8 SIZE=3 (16 B) +16           ", b | 0x10u, (3u << 1) | 1u, &g_buf[4]);
    /* P9: 정상 대조군 — 1 KB, 정렬 OK, AP=000 */
    one("P9 control: 1KB aligned AP=000 ", b | 0x10u, (9u << 1) | 1u, &g_buf[64]);

    MPU_CTRL = 2u;                                            /* HFNMIENA 만 (ENABLE 없이) */
    sh_puts("P10 CTRL=HFNMIENA only -> read "); sh_hex(MPU_CTRL); sh_puts("\n");
    MPU_CTRL = 0u;
    return 0;
}
__attribute__((noreturn)) void Reset_Handler(void) { sh_exit(main()); }
