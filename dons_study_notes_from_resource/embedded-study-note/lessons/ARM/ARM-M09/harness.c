/* ENV: qemu */
/* ARM-M09 harness.c — 펌웨어 테스트 (Cortex-M3 mps2-an385 / -DHAS_FPU 면 Cortex-M4F mps2-an386).
 * starter.c / solution.c 가 맨 위에서 #include 한다 (TODO 함수만 각 파일에 있다).
 * 빌드: run_qemu.sh [starter|solution]  →  <v>.c(+이 파일) + <v>.S + shared_startup.c
 * 출력: PASS/FAIL 줄, "N/M passed", SYS_EXIT 코드 (0 = 전부 통과, 1 = 실패 있음).
 */
#include <stdint.h>

/* ---- TODO (starter.c / solution.c) ---- */
struct exc_info {
    uint8_t handler_mode;   /* 1 = Handler 모드로 복귀 (중첩), 0 = Thread 로 */
    uint8_t use_psp;        /* 1 = 프레임이 PSP 에, 0 = MSP 에 */
    uint8_t fp_frame;       /* 1 = 확장(FP) 프레임 (EXC_RETURN bit4 == 0) */
    uint8_t frame_words;    /* 8 또는 26 (정렬 패딩 제외) */
};
int      decode_exc_return(uint32_t exc_return, int has_fpu, struct exc_info *out);
uint32_t pre_exception_sp(const uint32_t *frame, uint32_t exc_return);
uint32_t svc_dispatch(uint32_t *frame);

/* ---- starter.S / solution.S (주어진 헬퍼) ---- */
uint32_t sys_add(uint32_t a, uint32_t b);                      /* svc #0 */
uint32_t sys_xor4(uint32_t a, uint32_t b, uint32_t c, uint32_t d); /* svc #1 */
uint32_t sys_ticks(void);                                      /* svc #2 */
uint32_t sys_wait_tick(uint32_t spin_limit);                   /* svc #3 */
uint32_t sys_bad(void);                                        /* svc #0x42 */
uint32_t sys_priv(void);                                       /* svc #0x7F (테스트 전용) */
uint32_t svc_sentinel(uint32_t misalign);
uint32_t svc_under_primask(uint32_t a, uint32_t b);
void     spin_until_change(volatile uint32_t *p, uint32_t old, uint32_t limit);
uint32_t get_msp(void), get_psp(void), get_control(void), get_ipsr(void);
void     set_control(uint32_t v);
uint32_t run_on_psp(uint32_t *psp_top, uint32_t (*fn)(uint32_t), uint32_t arg);
extern char svc_sentinel_svc[], svc_sentinel_ret[], svc_primask_ret[], spin_loop_begin[], spin_loop_end[];
#ifdef HAS_FPU
uint32_t fp_svc_probe(uint32_t mode);
void     touch_fpu(void);
#endif

/* ---- shared_startup.c ---- */
void sh_puts(const char *s);
void sh_hex(uint32_t v);
void sh_dec(uint32_t v);
__attribute__((noreturn)) void sh_exit(int code);
void check(int ok, const char *expr, int line);
extern unsigned g_pass, g_total;
#define CHECK(e) check((e) ? 1 : 0, #e, __LINE__)

#define REG32(a)    (*(volatile uint32_t *)(uintptr_t)(a))
#define SCB_ICSR    REG32(0xE000ED04u)      /* bit25 PENDSTCLR */
#define SCB_CCR     REG32(0xE000ED14u)      /* bit9 STKALIGN */
#define SCB_SHPR2   REG32(0xE000ED1Cu)      /* [31:24] SVCall 우선순위 */
#define SCB_SHPR3   REG32(0xE000ED20u)      /* [31:24] SysTick, [23:16] PendSV */
#define SCB_HFSR    REG32(0xE000ED2Cu)      /* bit30 FORCED (W1C) */
#define SYST_CSR    REG32(0xE000E010u)
#define SYST_RVR    REG32(0xE000E014u)
#define SYST_CVR    REG32(0xE000E018u)
#define FPCCR       REG32(0xE000EF34u)      /* bit31 ASPEN, bit30 LSPEN, bit0 LSPACT */
#define FPCAR       REG32(0xE000EF38u)

#define SVC_ADD 0u
#define SVC_XOR4 1u
#define SVC_TICKS 2u
#define SVC_WAIT 3u
#define SVC_PRIV 0x7Fu
#define SVC_ENOSYS 0xFFFFFFFFu
#define WAIT_TIMEOUT 0xFFFFFFFEu
#define XPSR_T (1u << 24)
#define XPSR_ALIGN (1u << 9)                /* "진입 때 4 바이트 패딩을 넣었다" */
#define SPIN 3000000u

/* ---- 핸들러가 채우는 관찰 기록 ---- */
struct obs { uint32_t n, exc_return, frame_addr, sp_before, ipsr, psp, num, f[26]; };
volatile struct obs g_svc, g_tick, g_hf;
volatile uint32_t g_sp_before_svc, g_sp_spin, g_ticks;
volatile uint32_t g_tick_arm, g_tick_do_svc, g_tick_svc_result, g_hf_expect, g_hfsr, g_svc_touch_fpu;
volatile uint32_t g_fp_after[16], g_fpccr_in_svc, g_fpcar_in_svc, g_fp_slots_after_touch[18];

static void record(volatile struct obs *o, const uint32_t *frame, uint32_t exc_return) {
    unsigned words = (exc_return & 0x10u) ? 8u : 26u;           /* 기록만 — 판정은 decode_exc_return */
    for (unsigned i = 0; i < 26u; i++) o->f[i] = i < words ? frame[i] : 0u;
    o->exc_return = exc_return;
    o->frame_addr = (uint32_t)(uintptr_t)frame;
    o->sp_before = pre_exception_sp(frame, exc_return);        /* 프레임이 살아 있는 지금 계산 (나중엔 덮어써진다) */
    o->ipsr = get_ipsr();
    o->psp = get_psp();
    o->n++;
}

/* SVC 스텁이 b 로 넘어온다: 여기 return = 예외 복귀 */
void svc_handler_c(uint32_t *frame, uint32_t exc_return) {
#ifdef HAS_FPU
    g_fpccr_in_svc = FPCCR;                                     /* FP 명령 전에 읽는다 */
    g_fpcar_in_svc = FPCAR;
#endif
    record(&g_svc, frame, exc_return);
#ifdef HAS_FPU
    if (g_svc_touch_fpu) {
        touch_fpu();                                            /* lazy 저장 발동 + s0 망가뜨림 */
        for (unsigned i = 0; i < 18u; i++) g_fp_slots_after_touch[i] = frame[8 + i];
    }
#endif
    g_svc.num = svc_dispatch(frame);
    if (g_svc.num == SVC_PRIV) set_control(get_control() & ~1u); /* Handler 는 특권 → nPRIV 지우기 */
}

void systick_c(uint32_t *frame, uint32_t exc_return) {
    g_ticks++;
    if (g_tick_arm) { g_tick_arm = 0; record(&g_tick, frame, exc_return); }
    if (g_tick_do_svc) { g_tick_do_svc = 0; g_tick_svc_result = sys_add(1u, 2u); }  /* 같은 우선순위에서 svc */
}

void hardfault_c(uint32_t *frame, uint32_t exc_return) {
    if (!g_hf_expect) {
        sh_puts("unexpected HardFault, HFSR="); sh_hex(SCB_HFSR);
        sh_puts(" CFSR="); sh_hex(REG32(0xE000ED28u));
        sh_puts(" stacked PC="); sh_hex(frame[6]); sh_puts("\n");
        sh_exit(3);
    }
    g_hf_expect = 0;
    record(&g_hf, frame, exc_return);
    g_hfsr = SCB_HFSR;
    SCB_HFSR = g_hfsr;                                          /* W1C: 다음 실험을 위해 지운다 */
    /* 스택된 PC 는 이미 svc 다음 명령 → 그냥 돌아가면 svc 를 "건너뛴" 셈 */
}

/* 핸들러 안 SVC #3 에서 쓰는 주어진 헬퍼: 틱이 바뀔 때까지 (최대 limit 번) 돈다 */
uint32_t wait_tick_change(uint32_t limit) {
    uint32_t t0 = g_ticks;
    while (limit--) if (g_ticks != t0) return g_ticks;
    return WAIT_TIMEOUT;
}

static void systick_start(uint32_t reload) { SYST_RVR = reload; SYST_CVR = 0u; SYST_CSR = 7u; } /* CPU 클럭, 인터럽트, 켜기 */
static void systick_stop(void) { SYST_CSR = 0u; SCB_ICSR = 1u << 25; }                        /* 끄고 pending 도 지움 */
static void prio_set(uint32_t svc, uint32_t tick) {
    SCB_SHPR2 = svc << 24;
    SCB_SHPR3 = tick << 24;
}
static void dump(const char *name, uint32_t v) { sh_puts(name); sh_hex(v); sh_puts("\n"); }
static void dump_frame(const char *tag, volatile struct obs *o) {
    static const char *nm[8] = { "r0", "r1", "r2", "r3", "r12", "lr", "pc", "xpsr" };
    sh_puts(tag); sh_puts(" EXC_RETURN="); sh_hex(o->exc_return); sh_puts(" frame@"); sh_hex(o->frame_addr);
    sh_puts(" sp_before="); sh_hex(o->sp_before); sh_puts("\n   ");
    for (int i = 0; i < 8; i++) { sh_puts(" "); sh_puts(nm[i]); sh_puts("="); sh_hex(o->f[i]); if (i == 3) sh_puts("\n   "); }
    sh_puts("\n");
}
static int info_is(uint32_t er, int fpu, int hm, int psp, int fp) {
    struct exc_info x = { 9, 9, 9, 0 };
    return decode_exc_return(er, fpu, &x) == 1 && x.handler_mode == hm && x.use_psp == psp && x.fp_frame == fp &&
           x.frame_words == (fp ? 26u : 8u);
}

/* ---- Part B: 디코더 기준 모델 = 표 (비트 논리와 독립) ---- */
static int ref_valid(uint32_t v, int fpu) {
    static const uint32_t ok[6] = { 0xFFFFFFF1u, 0xFFFFFFF9u, 0xFFFFFFFDu, 0xFFFFFFE1u, 0xFFFFFFE9u, 0xFFFFFFEDu };
    for (int i = 0; i < (fpu ? 6 : 3); i++) if (v == ok[i]) return 1;
    return 0;
}
static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

/* ---- Part B/C: PSP 위의 태스크 ---- */
static uint32_t g_task_stack[256] __attribute__((aligned(8)));
#define TASK_TOP (&g_task_stack[256])
static int on_task_stack(uint32_t a) {
    return a >= (uint32_t)(uintptr_t)g_task_stack && a < (uint32_t)(uintptr_t)TASK_TOP;
}
static volatile uint32_t t_sum, t_er, t_frame, t_ctrl, t_ticks, t_ticks_k, t_bad, t_ctrl_back;

static uint32_t task_priv(uint32_t arg) {          /* 특권 Thread + PSP */
    t_sum = sys_add(arg, 100u);
    t_er = g_svc.exc_return;
    t_frame = g_svc.frame_addr;
    return 0u;
}
static uint32_t task_user(uint32_t arg) {          /* 비특권 Thread + PSP = RTOS 사용자 태스크 */
    set_control(get_control() | 1u);               /* nPRIV = 1 */
    t_ctrl = get_control();
    t_sum = sys_xor4(arg, 0x0F0F0000u, 0x00F0u, 0x1u);
    t_ticks = sys_ticks();
    t_ticks_k = g_ticks;
    t_bad = sys_bad();
    t_er = g_svc.exc_return;
    t_frame = g_svc.frame_addr;
    sys_priv();                                    /* 돌아가는 길 (run_on_psp 의 SPSEL=0 은 특권이어야 먹힌다) */
    t_ctrl_back = get_control();
    return 0u;
}
#ifdef HAS_FPU
static volatile uint32_t t_fp_s0;
static uint32_t task_fp(uint32_t arg) { t_fp_s0 = fp_svc_probe(arg); t_er = g_svc.exc_return; t_frame = g_svc.frame_addr; return 0u; }
#endif

int main(void) {
#ifdef HAS_FPU
    sh_puts("ARM-M09 exception entry/return (QEMU mps2-an386, Cortex-M4F)\n");
    const int fpu = 1;
#else
    sh_puts("ARM-M09 exception entry/return (QEMU mps2-an385, Cortex-M3)\n");
    const int fpu = 0;
#endif
    dump("  CCR = ", SCB_CCR);

    /* ================= Part A: 스택 프레임과 SVC 디스패처 ================= */
    /* A1. 센티널 레지스터로 svc #1 (Thread, MSP, sp 8 정렬) */
    uint32_t r = svc_sentinel(0u);
    dump_frame("A1 svc #1 (MSP, aligned):", &g_svc);
    CHECK(g_svc.n == 1u && g_svc.ipsr == 11u);                          /* SVCall = 예외 11 */
    CHECK(g_svc.exc_return == 0xFFFFFFF9u);                             /* Thread, MSP, 기본 프레임 */
    CHECK(g_svc.f[0] == 0xA0A0A0A0u && g_svc.f[1] == 0xA1A1A1A1u && g_svc.f[2] == 0xA2A2A2A2u && g_svc.f[3] == 0xA3A3A3A3u);
    CHECK(g_svc.f[4] == 0xC12C12C1u && g_svc.f[5] == 0x1EE71EE7u);      /* r12, lr */
    CHECK(g_svc.f[6] == (uint32_t)(uintptr_t)svc_sentinel_ret);         /* 스택된 PC = svc 다음 명령 */
    CHECK(svc_sentinel_ret - svc_sentinel_svc == 2);                     /* SVC 는 16비트 명령 */
    CHECK(g_svc.n && *(const uint16_t *)(uintptr_t)(g_svc.f[6] - 2u) == 0xDF01u);  /* [pc-2] = svc #1 인코딩 */
    CHECK((g_svc.f[7] & XPSR_T) && (g_svc.f[7] & 0x1FFu) == 0u && !(g_svc.f[7] & XPSR_ALIGN));
    CHECK(g_svc.frame_addr == g_sp_before_svc - 32u);                   /* 8 워드 = 32 B 아래 */
    CHECK(g_svc.num == SVC_XOR4 && r == (0xA0A0A0A0u ^ 0xA1A1A1A1u ^ 0xA2A2A2A2u ^ 0xA3A3A3A3u));

    /* A2. 시스템 콜: 반환값 = 핸들러가 고친 스택된 r0 */
    CHECK(sys_add(40u, 2u) == 42u && g_svc.num == SVC_ADD);
    CHECK(sys_xor4(1u, 2u, 4u, 8u) == 15u);
    CHECK(sys_bad() == SVC_ENOSYS && g_svc.num == 0x42u);
    g_ticks = 1234u;
    CHECK(sys_ticks() == 1234u && g_svc.num == SVC_TICKS);

    /* A3. 비동기 예외: SysTick 이 Thread(MSP) 의 루프를 끊는다 */
    uint32_t svc_n0 = g_svc.n;
    g_ticks = 0u; g_tick_arm = 1u;
    systick_start(20000u);
    spin_until_change(&g_ticks, 0u, SPIN);
    systick_stop();
    dump_frame("A3 SysTick (thread, MSP):", &g_tick);
    CHECK(g_ticks >= 1u && g_tick.n == 1u && g_tick.ipsr == 15u);
    CHECK(g_tick.exc_return == 0xFFFFFFF9u);
    CHECK(g_tick.f[6] >= (uint32_t)(uintptr_t)spin_loop_begin && g_tick.f[6] <= (uint32_t)(uintptr_t)spin_loop_end);
    CHECK((g_tick.f[7] & XPSR_T) && (g_tick.f[7] & 0x1FFu) == 0u);    /* 끊긴 쪽은 Thread (IPSR 0) */
    CHECK(g_svc.n == svc_n0);

    /* ================= Part B: EXC_RETURN 디코딩과 가장자리 ================= */
    /* B1. 디코더: 상위 24 비트 = 1 인 256 값 전수 + 랜덤 10만 + 이름 붙은 값 */
    unsigned bad = 0, nvalid[2] = { 0, 0 };
    for (int h = 0; h < 2; h++)
        for (uint32_t lo = 0; lo < 256u; lo++) {
            uint32_t v = 0xFFFFFF00u | lo;
            struct exc_info x = { 0, 0, 0, 0 };
            int got = decode_exc_return(v, h, &x);
            if (got != ref_valid(v, h)) bad++;
            if (got) {
                nvalid[h]++;
                if (x.handler_mode != !(v & 8u) || x.use_psp != !!(v & 4u) || x.fp_frame != !(v & 0x10u) ||
                    x.frame_words != ((v & 0x10u) ? 8u : 26u)) bad++;
            }
        }
    uint32_t seed = 0x2545F491u;
    for (int i = 0; i < 100000; i++) {
        uint32_t v = xs32(&seed);
        struct exc_info x;
        if (decode_exc_return(v, 1, &x) != ref_valid(v, 1)) bad++;
    }
    CHECK(bad == 0u && nvalid[0] == 3u && nvalid[1] == 6u);
    CHECK(info_is(0xFFFFFFF1u, 0, 1, 0, 0) && info_is(0xFFFFFFF9u, 0, 0, 0, 0) && info_is(0xFFFFFFFDu, 0, 0, 1, 0));
    CHECK(info_is(0xFFFFFFE1u, 1, 1, 0, 1) && info_is(0xFFFFFFE9u, 1, 0, 0, 1) && info_is(0xFFFFFFEDu, 1, 0, 1, 1));
    {
        struct exc_info x;
        CHECK(!decode_exc_return(0xFFFFFFF5u, 1, &x));       /* Handler + PSP: 있을 수 없는 조합 */
        CHECK(!decode_exc_return(0xFFFFFFE9u, 0, &x));       /* FPU 없는 코어에 FP 프레임 */
        CHECK(!decode_exc_return(0xFFFFFFF8u, 1, &x) && !decode_exc_return(0xEFFFFFF9u, 1, &x) &&
              !decode_exc_return(0x00000000u, 1, &x) && !decode_exc_return(0xFFFFFFFFu, 1, &x));
    }

    /* B2. 진입 전 sp 복원: 정렬된 경우 / 4 mod 8 경우 (STKALIGN 패딩) */
    CHECK(g_tick.sp_before == g_sp_spin);                               /* A3 의 SysTick 프레임 */
    svc_sentinel(0u);
    CHECK(g_svc.sp_before == g_sp_before_svc);
    svc_sentinel(1u);
    dump_frame("B2 svc #1 (sp = 4 mod 8):", &g_svc);
    dump("   sp before svc = ", g_sp_before_svc);
    CHECK((g_sp_before_svc & 7u) == 4u);
    CHECK(g_svc.f[7] & XPSR_ALIGN);                                     /* xPSR bit9 = 1 */
    CHECK(g_svc.frame_addr == g_sp_before_svc - 4u - 32u);              /* 4 B 패딩 + 32 B */
    CHECK((g_svc.frame_addr & 7u) == 0u);                               /* 프레임은 8 정렬 */
    CHECK(g_svc.sp_before == g_sp_before_svc);                          /* 패딩까지 되돌린다 */

    /* B3. Thread 가 PSP 를 쓰면 프레임은 PSP 에, EXC_RETURN = 0xFFFFFFFD */
    run_on_psp(TASK_TOP, task_priv, 7u);
    dump("B3 PSP task: EXC_RETURN = ", t_er);
    CHECK(t_sum == 107u && t_er == 0xFFFFFFFDu);
    CHECK(on_task_stack(t_frame) && g_svc.psp == t_frame);              /* 핸들러 안 PSP = 프레임 주소 */

    /* B4. 같은 우선순위: SVC 핸들러 안에서 기다려도 SysTick 은 끼어들지 못한다 */
    prio_set(0u, 0u);
    g_ticks = 0u;
    systick_start(2000u);
    r = sys_wait_tick(SPIN);
    uint32_t after = g_ticks;                                           /* svc 가 끝나자마자 pending 틱이 들어온다 */
    systick_stop();
    dump("B4 equal prio: wait result = ", r);
    CHECK(r == WAIT_TIMEOUT && after >= 1u);

    /* B5. SysTick(0x40) 이 SVCall(0x80) 보다 급하면 중첩 → EXC_RETURN 0xFFFFFFF1 */
    prio_set(0x80u, 0x40u);
    g_ticks = 0u; g_tick_arm = 1u; g_tick.n = 0u;
    systick_start(2000u);
    r = sys_wait_tick(SPIN);
    systick_stop();
    prio_set(0u, 0u);
    dump_frame("B5 SysTick nested in SVC:", &g_tick);
    CHECK(r != WAIT_TIMEOUT && g_tick.n == 1u);
    CHECK(g_tick.exc_return == 0xFFFFFFF1u);                            /* Handler 로 복귀, MSP */
    CHECK((g_tick.f[7] & 0x1FFu) == 11u);                               /* 끊긴 쪽 IPSR = SVCall */
    CHECK(g_tick.sp_before != 0u && g_tick.sp_before <= g_svc.frame_addr);  /* SVC 핸들러 스택 위에서 끊김 */

    /* B6. SysTick 핸들러(우선순위 0) 안에서 svc(우선순위 0) → HardFault 로 승격 */
    svc_n0 = g_svc.n;
    g_hf.n = 0u; g_hf_expect = 1u; g_tick_do_svc = 1u; g_tick_svc_result = 0u;
    g_ticks = 0u;
    systick_start(20000u);
    spin_until_change(&g_ticks, 0u, SPIN);
    systick_stop();
    dump_frame("B6 svc inside SysTick -> HardFault:", &g_hf);
    dump("   HFSR = ", g_hfsr);
    CHECK(g_hf.n == 1u && g_hf.ipsr == 3u);
    CHECK(g_hf.exc_return == 0xFFFFFFF1u && (g_hf.f[7] & 0x1FFu) == 15u);  /* SysTick 을 끊었다 */
    CHECK(g_hfsr & (1u << 30));                                         /* FORCED */
    CHECK(g_svc.n == svc_n0 && g_tick_svc_result == 1u);                /* svc 는 실행 안 됨 → r0 = a 그대로 */

    /* B7. PRIMASK=1 인 Thread 에서 svc → HardFault (흔한 실수) */
    g_hf.n = 0u; g_hf_expect = 1u;
    r = svc_under_primask(5u, 6u);
    CHECK(g_hf.n == 1u && g_hf.exc_return == 0xFFFFFFF9u && (g_hf.f[7] & 0x1FFu) == 0u);
    CHECK(g_hf.f[6] == (uint32_t)(uintptr_t)svc_primask_ret && r == 5u && g_svc.n == svc_n0);

    /* ================= Part C: 비특권 태스크의 시스템 콜 (+ FPU 프레임) ================= */
    if (sys_add(1u, 1u) != 2u || g_svc.num != SVC_ADD) {                 /* 디스패처가 안 되면 sys_priv 도 안 된다 */
        CHECK(0 && "Part C1 skipped: SVC path not working (run_on_psp would not return)");
    } else {
        g_ticks = 77u;
        run_on_psp(TASK_TOP, task_user, 0x12345678u);
        dump("C1 user task: control = ", t_ctrl);
        dump("   xor4 = ", t_sum);
        CHECK(t_ctrl == 3u);                                            /* nPRIV | SPSEL */
        CHECK(t_sum == (0x12345678u ^ 0x0F0F0000u ^ 0x00F0u ^ 0x1u));
        CHECK(t_ticks == 77u && t_ticks_k == 77u && t_bad == SVC_ENOSYS);
        CHECK(t_er == 0xFFFFFFFDu && on_task_stack(t_frame));
        CHECK(t_ctrl_back == 2u && get_control() == 0u);
    }
    CHECK(info_is(g_tick.exc_return, fpu, 1, 0, 0) && info_is(g_hf.exc_return, fpu, 0, 0, 0));

#ifdef HAS_FPU
    /* C2. FPU 프레임 (Cortex-M4F) */
    const uint32_t one = 0x3F800000u;
    dump("C2 FPCCR at reset = ", FPCCR);
    CHECK((FPCCR & 0xC0000000u) == 0xC0000000u);                        /* ASPEN, LSPEN 리셋값 1 */
    fp_svc_probe(0u);                                                   /* FPCA = 0 → 기본 프레임 */
    CHECK(g_svc.exc_return == 0xFFFFFFF9u && g_svc.frame_addr == g_sp_before_svc - 32u);

    g_svc_touch_fpu = 0u;                                               /* lazy: 자리만 잡고 안 쓴다 */
    uint32_t s0 = fp_svc_probe(1u);
    dump_frame("C2 lazy (LSPEN=1), no FP in handler:", &g_svc);
    dump("   FPCCR in handler = ", g_fpccr_in_svc);
    dump("   FPCAR in handler = ", g_fpcar_in_svc);
    dump("   frame[8] (S0 slot) = ", g_svc.f[8]);
    CHECK(g_svc.exc_return == 0xFFFFFFE9u && info_is(g_svc.exc_return, 1, 0, 0, 1));
    CHECK(g_svc.frame_addr == g_sp_before_svc - 104u);                  /* 26 워드 */
    CHECK(g_svc.sp_before == g_sp_before_svc);                          /* 26 워드 + 패딩 규칙 그대로 */
    CHECK((g_fpccr_in_svc & 1u) && g_fpcar_in_svc == g_svc.frame_addr + 32u);   /* LSPACT=1, FPCAR = S0 자리 */
    CHECK(g_svc.f[8] == 0x5A5A5A5Au && g_svc.f[23] == 0x5A5A5A5Au);      /* 아직 안 쓰였다 */
    CHECK(s0 == one);

    g_svc_touch_fpu = 1u;                                               /* 핸들러가 FP 를 쓰면 그때 저장 */
    s0 = fp_svc_probe(1u);
    dump("C2 lazy, handler touched FP: frame[8] = ", g_fp_slots_after_touch[0]);
    dump("   FPSCR slot = ", g_fp_slots_after_touch[16]);
    CHECK(g_svc.f[8] == 0x5A5A5A5Au && g_fp_slots_after_touch[0] == one && g_fp_slots_after_touch[15] == 0x41800000u);
    CHECK((g_fp_slots_after_touch[16] & 0x0FC00000u) == 0x02C00000u);   /* FPSCR 의 DN/RMode */
    CHECK(s0 == one && g_fp_after[15] == 0x41800000u);                  /* s0 = 0xDEADBEEF 였지만 복원 */

    FPCCR &= ~(1u << 30);                                               /* LSPEN = 0: 진입 때 바로 저장 */
    g_svc_touch_fpu = 0u;
    fp_svc_probe(1u);
    dump("C2 LSPEN=0: frame[8] = ", g_svc.f[8]);
    CHECK(g_svc.f[8] == one && g_svc.f[23] == 0x41800000u && !(g_fpccr_in_svc & 1u));
    FPCCR |= 1u << 30;

    run_on_psp(TASK_TOP, task_fp, 1u);                                  /* PSP + FP → 0xFFFFFFED */
    dump("C2 PSP task with FP: EXC_RETURN = ", t_er);
    CHECK(t_er == 0xFFFFFFEDu && on_task_stack(t_frame) && t_fp_s0 == one);

    prio_set(0x80u, 0x40u);                                             /* 핸들러가 FP 를 쓴 뒤 중첩 → 0xFFFFFFE1 */
    g_svc_touch_fpu = 1u; g_tick_arm = 1u; g_tick.n = 0u;
    systick_start(2000u);
    r = sys_wait_tick(SPIN);
    systick_stop();
    prio_set(0u, 0u);
    g_svc_touch_fpu = 0u;
    dump("C2 SysTick nested in FP-using SVC: EXC_RETURN = ", g_tick.exc_return);
    CHECK(r != WAIT_TIMEOUT && g_tick.exc_return == 0xFFFFFFE1u && info_is(g_tick.exc_return, 1, 1, 0, 1));
#endif

    sh_dec(g_pass); sh_puts("/"); sh_dec(g_total); sh_puts(" passed\n");
    return g_pass == g_total ? 0 : 1;
}
