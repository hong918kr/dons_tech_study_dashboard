/* ENV: qemu */
/* ARM-M09 practice.c — 추가 연습 답 (Cortex-M3, QEMU mps2-an385).
 * 빌드: run_qemu.sh practice  →  practice.c + practice.S(#include "solution.S") + shared_startup.c
 * 연습 1 꼬리 물기 vs 중첩 (PendSV), 연습 2 64비트 시스템 콜 반환, 연습 3 PC 샘플링 프로파일러,
 * 연습 4 예외 복귀 주소 바꿔치기 (함수 끼워 넣기).
 */
#include <stdint.h>

void sh_puts(const char *s);
void sh_hex(uint32_t v);
void sh_dec(uint32_t v);
__attribute__((noreturn)) void sh_exit(int code);
void check(int ok, const char *expr, int line);
extern unsigned g_pass, g_total;
#define CHECK(e) check((e) ? 1 : 0, #e, __LINE__)
#define REG32(a)    (*(volatile uint32_t *)(uintptr_t)(a))
#define SCB_ICSR    REG32(0xE000ED04u)
#define SCB_SHPR2   REG32(0xE000ED1Cu)
#define SCB_SHPR3   REG32(0xE000ED20u)
#define SYST_CSR    REG32(0xE000E010u)
#define SYST_RVR    REG32(0xE000E014u)
#define SYST_CVR    REG32(0xE000E018u)

uint64_t sys_mul64(uint32_t a, uint32_t b);
void     sys_inject(void (*fn)(uint32_t), uint32_t arg);
void     sys_pend_pendsv(void);
void     busy_a(uint32_t n), busy_b(uint32_t n);
uint32_t get_msp(void);
extern char busy_a_end[], busy_b_end[], sys_inject_ret[];

volatile uint32_t g_sp_before_svc, g_sp_spin;          /* solution.S 가 참조 */

/* ---- 순서 기록 ---- */
static volatile uint32_t seq, svc_enter, svc_exit, pend_seq, pend_er, pend_frame, svc_frame, pend_pc, svc_pc;

void svc_handler_c(uint32_t *frame, uint32_t exc_return) {
    (void)exc_return;
    uint32_t num = ((const uint8_t *)(uintptr_t)frame[6])[-2];
    switch (num) {
    case 4: {                                              /* 연습 2: 64비트 결과 = r0:r1 */
        uint64_t p = (uint64_t)frame[0] * frame[1];
        frame[0] = (uint32_t)p;
        frame[1] = (uint32_t)(p >> 32);
        break;
    }
    case 5:                                                /* 연습 4: 복귀하면 fn(arg) 부터 */
        frame[5] = frame[6] | 1u;                          /* fn 의 lr = 원래 복귀 지점 (Thumb 비트!) */
        frame[6] = frame[0] & ~1u;                         /* 스택된 PC 는 bit0 = 0 */
        frame[0] = frame[1];                               /* fn 의 인자 */
        break;
    case 6:                                                /* 연습 1: PendSV pend */
        svc_enter = ++seq;
        svc_frame = (uint32_t)(uintptr_t)frame;
        svc_pc = frame[6];
        SCB_ICSR = 1u << 28;                               /* PENDSVSET */
        __asm__ volatile("dsb\n isb" ::: "memory");
        svc_exit = ++seq;
        break;
    default:
        frame[0] = 0xFFFFFFFFu;
    }
}

void pendsv_c(uint32_t *frame, uint32_t exc_return) {
    pend_seq = ++seq;
    pend_er = exc_return;
    pend_frame = (uint32_t)(uintptr_t)frame;
    pend_pc = frame[6];
}

/* ---- 연습 3: SysTick 이 스택된 PC 를 샘플링 ---- */
static volatile uint32_t samp_a, samp_b, samp_other;
void systick_c(uint32_t *frame, uint32_t exc_return) {
    (void)exc_return;
    uint32_t pc = frame[6];
    uint32_t a0 = (uint32_t)(uintptr_t)busy_a & ~1u, b0 = (uint32_t)(uintptr_t)busy_b & ~1u;
    if (pc >= a0 && pc < (uint32_t)(uintptr_t)busy_a_end) samp_a++;
    else if (pc >= b0 && pc < (uint32_t)(uintptr_t)busy_b_end) samp_b++;
    else samp_other++;
}

void hardfault_c(uint32_t *frame, uint32_t exc_return) {
    (void)exc_return;
    sh_puts("unexpected HardFault, stacked PC="); sh_hex(frame[6]); sh_puts("\n");
    sh_exit(3);
}

/* ---- 연습 4: 끼워 넣을 함수 ---- */
static volatile uint32_t inj_val, inj_sp, inj_lr;
static void injected(uint32_t arg) {
    uint32_t lr;
    __asm__ volatile("mov %0, lr" : "=r"(lr));
    inj_lr = lr;
    inj_sp = (uint32_t)(uintptr_t)__builtin_frame_address(0);
    inj_val = arg + 1u;
}

static void dump(const char *name, uint32_t v) { sh_puts(name); sh_hex(v); sh_puts("\n"); }

int main(void) {
    sh_puts("ARM-M09 practice\n");

    /* 연습 1a: 같은 우선순위 → 꼬리 물기 (tail-chaining) */
    SCB_SHPR2 = 0u; SCB_SHPR3 = 0u;
    seq = 0;
    sys_pend_pendsv();
    uint32_t after = ++seq;
    dump("  tail-chain: PendSV EXC_RETURN = ", pend_er);
    dump("  tail-chain: SVC frame = ", svc_frame);
    dump("  tail-chain: PendSV frame = ", pend_frame);
    CHECK(svc_enter == 1u && svc_exit == 2u && pend_seq == 3u && after == 4u);  /* SVC 끝 → PendSV → Thread */
    CHECK(pend_er == 0xFFFFFFF9u);                       /* Thread 로 돌아간다 (F1 아님) */
    CHECK(pend_frame == svc_frame && pend_pc == svc_pc); /* 같은 프레임을 그대로 쓴다 */

    /* 연습 1b: PendSV 가 더 급하면 → SVC 안에서 바로 중첩 */
    SCB_SHPR2 = 0x80u << 24;                             /* SVCall 0x80 */
    SCB_SHPR3 = 0x40u << 16;                             /* PendSV 0x40 */
    seq = 0;
    sys_pend_pendsv();
    after = ++seq;
    SCB_SHPR2 = 0u; SCB_SHPR3 = 0u;
    dump("  nested: PendSV EXC_RETURN = ", pend_er);
    dump("  nested: PendSV frame = ", pend_frame);
    CHECK(svc_enter == 1u && pend_seq == 2u && svc_exit == 3u && after == 4u);  /* SVC 도중에 PendSV */
    CHECK(pend_er == 0xFFFFFFF1u && pend_frame < svc_frame);

    /* 연습 2: 64비트 반환 */
    CHECK(sys_mul64(0xFFFFFFFFu, 0xFFFFFFFFu) == 0xFFFFFFFE00000001ull);
    CHECK(sys_mul64(0x10000u, 0x10000u) == 0x100000000ull);
    CHECK(sys_mul64(123456789u, 1000u) == 123456789000ull);

    /* 연습 3: PC 샘플링 */
    SYST_RVR = 3000u; SYST_CVR = 0u; SYST_CSR = 7u;
    busy_a(3000000u);
    uint32_t a1 = samp_a, b1 = samp_b, o1 = samp_other;
    busy_b(3000000u);
    SYST_CSR = 0u; SCB_ICSR = 1u << 25;
    uint32_t a2 = samp_a - a1, b2 = samp_b - b1, o2 = samp_other - o1;
    sh_puts("  profile busy_a: a="); sh_dec(a1); sh_puts(" b="); sh_dec(b1); sh_puts(" other="); sh_dec(o1); sh_puts("\n");
    sh_puts("  profile busy_b: a="); sh_dec(a2); sh_puts(" b="); sh_dec(b2); sh_puts(" other="); sh_dec(o2); sh_puts("\n");
    CHECK(a1 >= 3u && b1 == 0u && o1 <= 3u);             /* busy_a 동안의 샘플은 거의 다 busy_a 안 (개수는 QEMU 속도에 따라 변함) */
    CHECK(b2 >= 3u && a2 == 0u && o2 <= 3u);

    /* 연습 4: 복귀 주소 바꿔치기 */
    uint32_t msp0 = get_msp();
    sys_inject(injected, 41u);
    dump("  inject: lr seen by injected() = ", inj_lr);
    CHECK(inj_val == 42u);
    CHECK(inj_lr == ((uint32_t)(uintptr_t)sys_inject_ret | 1u));   /* fn 이 끝나면 svc 다음으로 */
    CHECK(get_msp() == msp0);                                       /* 스택 균형 */

    sh_dec(g_pass); sh_puts("/"); sh_dec(g_total); sh_puts(" passed\n");
    return g_pass == g_total ? 0 : 1;
}
