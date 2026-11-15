/* ENV: board — Cortex-M3/M4/M7 참고 코드 (DWT CYCCNT 로 WCET·지터 재기). 맥에서 실행하지 않는다.
 * 컴파일만 확인:  clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_dwt_cyccnt.c -o -
 * QEMU 탐침(선택): -DQEMU_PROBE 로 mps2-an385(Cortex-M3) 펌웨어를 만들어 CYCCNT 가 QEMU 에서 움직이는지 본다
 *                  (Lima VM elinux, 명령은 md 5. 풀이 "보드에서는" 절).
 * 레지스터 주소·비트는 ARMv7-M ARM (DDI0403) C1.6 DWT, C1.6.x DEMCR 기준.
 */
#include <stdint.h>

#define DEMCR          (*(volatile uint32_t *)0xE000EDFCu)   /* Debug Exception and Monitor Control */
#define DEMCR_TRCENA   (1u << 24)                            /* DWT/ITM 전원·클록 켜기 */
#define DWT_CTRL       (*(volatile uint32_t *)0xE0001000u)
#define DWT_CYCCNT     (*(volatile uint32_t *)0xE0001004u)
#define DWT_LAR        (*(volatile uint32_t *)0xE0001FB0u)   /* 일부 M7 에서 잠금 해제 필요 (아래 주석) */
#define DWT_CTRL_CYCCNTENA (1u << 0)
#define DWT_CTRL_NOCYCCNT  (1u << 25)                        /* 1 = 사이클 카운터 없음 */

/* 0 = OK, -1 = 이 코어에 CYCCNT 없음 (ARMv6-M M0/M0+ 는 DWT 에 CYCCNT 가 없다),
 * -2 = 켰는데 안 움직임 (QEMU mps2-an385 가 이렇다: DWT 가 전부 0 으로 읽힘 — 실측) */
int cyc_init(void) {
    DEMCR |= DEMCR_TRCENA;
    DWT_LAR = 0xC5ACCE55u;           /* CoreSight 잠금 해제 키. M7 일부 부품에서 이게 없으면 CYCCNTENA 가 안 먹는다는
                                        보고가 있다 (문서·커뮤니티 기준, 미검증). 잠금이 없는 코어에선 무시된다 */
    if (DWT_CTRL & DWT_CTRL_NOCYCCNT) return -1;
    DWT_CYCCNT = 0;
    DWT_CTRL |= DWT_CTRL_CYCCNTENA;
    uint32_t a = DWT_CYCCNT;
    for (volatile int i = 0; i < 16; i++) {}
    return DWT_CYCCNT != a ? 0 : -2;          /* "켰다" 가 아니라 "움직인다" 를 확인 */
}
static inline uint32_t cyc_now(void) { return DWT_CYCCNT; }

/* ---- 보드 위 측정 루프: 표본을 다 저장하지 않는다 (RAM 이 없다) — 히스토그램 + 최대 + "최대를 만든 입력" ---- */
#define NB 64u
#define BIN_CYC 16u
typedef struct {
    uint32_t bins[NB], over, n, min, max, max_at;     /* max_at = 몇 번째 호출이 최대였나 */
    int32_t max_sp, max_meas;                         /* 최대를 만든 입력 → 호스트에서 재현 */
    uint32_t overhead;                                /* 빈 구간 최솟값 (빼 준다) */
} cyc_stats_t;

extern int32_t ctrl_step_board(int32_t sp, int32_t meas);   /* 보드 프로젝트의 측정 대상 */

uint32_t cyc_calibrate(void) {                        /* 빈 구간 최소 = 읽기 두 번의 비용 */
    uint32_t best = 0xFFFFFFFFu;
    for (int i = 0; i < 64; i++) {
        uint32_t t0 = cyc_now();
        __asm__ volatile("" ::: "memory");
        uint32_t d = cyc_now() - t0;
        if (d < best) best = d;
    }
    return best;
}
void cyc_measure_one(cyc_stats_t *s, int32_t sp, int32_t meas, int mask_irq) {
    uint32_t primask = 0;
    if (mask_irq) { __asm__ volatile("mrs %0, primask\n cpsid i" : "=r"(primask) :: "memory"); }
    uint32_t t0 = cyc_now();
    __asm__ volatile("" ::: "memory");
    (void)ctrl_step_board(sp, meas);
    __asm__ volatile("" ::: "memory");
    uint32_t d = cyc_now() - t0 - s->overhead;        /* uint32 뺄셈: 2^32 랩 한 번까지 안전 */
    if (mask_irq) { __asm__ volatile("msr primask, %0" :: "r"(primask) : "memory"); }
    uint32_t b = d / BIN_CYC;
    if (b < NB) s->bins[b]++; else s->over++;
    if (s->n == 0 || d < s->min) s->min = d;
    if (s->n == 0 || d > s->max) { s->max = d; s->max_at = s->n; s->max_sp = sp; s->max_meas = meas; }
    s->n++;
}

#ifdef QEMU_PROBE
/* ---- QEMU mps2-an385 탐침: CYCCNT 가 모델링되는가? ---- */
static void semi(uint32_t op, const void *arg) {
    register uint32_t r0 __asm__("r0") = op;
    register const void *r1 __asm__("r1") = arg;
    __asm__ volatile("bkpt 0xab" : "+r"(r0) : "r"(r1) : "memory");
}
static void puts_(const char *s) { semi(0x04, s); }
static void puthex(const char *tag, uint32_t v) {
    char b[32]; int k = 0;
    while (*tag) b[k++] = *tag++;
    b[k++] = '0'; b[k++] = 'x';
    for (int i = 7; i >= 0; i--) b[k++] = "0123456789abcdef"[(v >> (4 * i)) & 15];
    b[k++] = '\n'; b[k] = 0;
    puts_(b);
}
static void exit_(uint32_t code) {
    uint32_t blk[2] = {0x20026u, code};
    semi(0x20, blk);
    for (;;) {}
}
int32_t ctrl_step_board(int32_t sp, int32_t meas) { return sp - meas; }
void HardFault_Handler(void) { puts_("HardFault (DWT access faulted)\n"); exit_(1); }
void Reset_Handler(void) {
    puthex("DEMCR before   = ", DEMCR);
    int rc = cyc_init();
    puthex("DEMCR after    = ", DEMCR);
    puthex("DWT_CTRL       = ", DWT_CTRL);
    puts_(rc == 0 ? "cyc_init: ok\n" : rc == -1 ? "cyc_init: NOCYCCNT=1 (no counter)\n"
                                                : "cyc_init: counter does not move\n");
    uint32_t a = cyc_now();
    for (volatile uint32_t i = 0; i < 100000u; i++) {}
    uint32_t b = cyc_now();
    puthex("CYCCNT t0      = ", a);
    puthex("CYCCNT t1      = ", b);
    puthex("delta (100k)   = ", b - a);
    cyc_stats_t s = {0};
    s.overhead = cyc_calibrate();
    puthex("overhead       = ", s.overhead);
    exit_(rc == 0 ? 0 : 2);
}
extern uint32_t _estack;
__attribute__((section(".isr_vector"), used)) static void (*const vectors[16])(void) = {
    (void (*)(void))&_estack, Reset_Handler, HardFault_Handler, HardFault_Handler,
    HardFault_Handler, HardFault_Handler, HardFault_Handler,
};
#endif
