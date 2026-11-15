/* ENV: qemu */
/* ARM-B14 harness.c — starter.S / solution.S 공용 펌웨어 테스트 (Cortex-M3, QEMU mps2-an385).
 * 빌드: run_qemu.sh [starter|solution]  →  harness.c + shared_startup.c + <variant>.S
 * 출력: PASS/FAIL 줄, "N/M passed", SYS_EXIT 코드 (0 = 전부 통과, 1 = 실패 있음).
 */
#include <stdint.h>

/* ---- starter.S / solution.S 가 구현하는 함수 ---- */
uint32_t get_sp(void);                 /* 주어짐 */
void     svc_call(uint32_t cmd);       /* 주어짐 */
uint32_t get_msp(void);
uint32_t get_psp(void);
void     set_psp(uint32_t top);
uint32_t get_control(void);
void     set_control(uint32_t v);
uint32_t get_xpsr(void);
uint32_t get_ipsr(void);
uint32_t get_primask(void);
uint32_t run_on_psp(uint32_t *psp_top, uint32_t (*fn)(uint32_t), uint32_t arg);

/* ---- shared_startup.c ---- */
void sh_puts(const char *s);
void sh_hex(uint32_t v);
void sh_dec(uint32_t v);
void check(int ok, const char *expr, int line);
extern unsigned g_pass, g_total;
extern uint32_t _ebss[], _estack[];
typedef void (*vector_t)(void);
extern const vector_t g_vectors[16];
#define CHECK(e) check((e) ? 1 : 0, #e, __LINE__)

#define REG32(a)   (*(volatile uint32_t *)(uintptr_t)(a))
#define SCB_ICSR   REG32(0xE000ED04u)          /* bit28 PENDSVSET */
#define CTRL_NPRIV 1u
#define CTRL_SPSEL 2u
#define SVC_CMD_NOP  0u
#define SVC_CMD_PRIV 1u                        /* "Thread 를 다시 특권으로" 시스템 콜 */

/* ---- 핸들러가 채우는 관찰 기록 ---- */
volatile uint32_t g_svc_cmd, g_sp_before_svc;
struct exc_obs { uint32_t ipsr, xpsr, control, exc_return, sp, msp, psp, n; };
volatile struct exc_obs g_obs;

/* SVC_Handler / PendSV_Handler 트램펄린(.S)이 b 로 넘어온다. 여기 return = 예외 복귀. */
void exc_observe(uint32_t exc_return, uint32_t sp, uint32_t msp, uint32_t psp) {
    g_obs.ipsr = get_ipsr();
    g_obs.xpsr = get_xpsr();
    g_obs.control = get_control();
    g_obs.exc_return = exc_return;
    g_obs.sp = sp;
    g_obs.msp = msp;
    g_obs.psp = psp;
    g_obs.n++;
    if (g_obs.ipsr == 11u && g_svc_cmd == SVC_CMD_PRIV)
        set_control(get_control() & ~CTRL_NPRIV);  /* Handler 는 항상 특권 → Thread 의 nPRIV 를 지운다 */
}

static void pend_pendsv(void) {
    SCB_ICSR = 1u << 28;                           /* PENDSVSET */
    __asm__ volatile("dsb\n isb" ::: "memory");    /* 쓰기 완료 + 다음 명령 전에 예외가 들어오게 */
}

static void dump(const char *name, uint32_t v) { sh_puts(name); sh_hex(v); sh_puts("\n"); }

/* ---- Part B/C 에서 PSP 위에서 돌릴 "태스크" ---- */
static uint32_t g_task_stack[256] __attribute__((aligned(8)));   /* 1 KB */
#define TASK_TOP (&g_task_stack[256])

struct task_obs { uint32_t sp, psp, msp, control, ipsr, ctrl_after_svc, sp_after_svc; };
static volatile struct task_obs t;

static uint32_t task_probe(uint32_t arg) {         /* 특권 Thread, PSP */
    t.sp = get_sp();
    t.psp = get_psp();
    t.msp = get_msp();
    t.control = get_control();
    t.ipsr = get_ipsr();
    svc_call(SVC_CMD_NOP);                         /* Part C: PSP 를 쓰는 Thread 에서 예외 */
    t.ctrl_after_svc = get_control();
    t.sp_after_svc = get_sp();
    return arg + 1u;
}

struct unpriv_obs { uint32_t ctrl_drop, ctrl_try, ctrl_back; };
static volatile struct unpriv_obs u;

static uint32_t task_unpriv(uint32_t arg) {        /* RTOS 태스크처럼: 비특권 + PSP */
    set_control(get_control() | CTRL_NPRIV);       /* 특권 → 비특권 (한 방향은 MSR 로 가능) */
    u.ctrl_drop = get_control();
    set_control(0u);                               /* 비특권에서 MSR CONTROL → 무시돼야 한다 */
    u.ctrl_try = get_control();
    svc_call(SVC_CMD_PRIV);                        /* 돌아가는 길은 예외(SVC)뿐 */
    u.ctrl_back = get_control();
    return arg * 2u;
}

int main(void) {
    sh_puts("ARM-B14 Cortex-M register model (QEMU mps2-an385)\n");

    /* ---------- Part A: 리셋 직후 Thread 모드 (특권, MSP) ---------- */
    uint32_t sp0 = get_sp(), msp0 = get_msp(), psp0 = get_psp();
    dump("  reset: sp      = ", sp0);
    dump("  reset: msp     = ", msp0);
    dump("  reset: psp     = ", psp0);
    dump("  reset: control = ", get_control());
    dump("  reset: xpsr    = ", get_xpsr());
    CHECK(get_ipsr() == 0u);                                   /* Thread 모드 */
    CHECK(get_control() == 0u);                                /* nPRIV=0, SPSEL=0 */
    CHECK(msp0 == sp0);                                        /* Thread 가 MSP 사용 중 */
    CHECK(msp0 > (uint32_t)(uintptr_t)_ebss && msp0 <= (uint32_t)(uintptr_t)_estack);
    CHECK((uint32_t)(uintptr_t)g_vectors[0] == (uint32_t)(uintptr_t)_estack);
    CHECK((get_xpsr() & 0x1FFu) == 0u);                        /* IPSR 필드 */
    CHECK((get_xpsr() & (1u << 24)) == 0u);                    /* EPSR.T 는 MRS 로 0 */
    CHECK(get_primask() == 0u);
    set_psp((uint32_t)(uintptr_t)TASK_TOP);
    CHECK(get_psp() == (uint32_t)(uintptr_t)TASK_TOP);
    set_psp(0x20004000u);                                      /* 아무 값이나 — 아직 아무도 PSP 를 안 쓴다 */
    CHECK(get_psp() == 0x20004000u);
    CHECK(get_sp() == sp0 && get_msp() == msp0);               /* PSP 를 바꿔도 지금 sp 는 그대로 */

    /* ---------- Part B: Thread 를 PSP 로 전환 ---------- */
    uint32_t msp_before = get_msp();
    uint32_t r = run_on_psp(TASK_TOP, task_probe, 41u);
    dump("  task : sp      = ", t.sp);
    dump("  task : psp     = ", t.psp);
    dump("  task : msp     = ", t.msp);
    dump("  task : control = ", t.control);
    CHECK(r == 42u);                                           /* fn 의 반환값이 그대로 */
    CHECK(t.sp == t.psp);                                      /* 핵심: sp == psp */
    CHECK(t.control == CTRL_SPSEL);                            /* 특권 + PSP */
    CHECK(t.sp < (uint32_t)(uintptr_t)TASK_TOP && t.sp >= (uint32_t)(uintptr_t)g_task_stack);
    CHECK(t.msp == msp_before - 8u);                           /* MSP 에는 push {r4, lr} 만 */
    CHECK(t.ipsr == 0u);                                       /* PSP 여도 여전히 Thread 모드 */
    CHECK(get_control() == 0u && get_sp() == get_msp());       /* 돌아온 뒤 MSP */

    /* ---------- Part C: 예외 안에서 본 레지스터 ---------- */
    /* C1. PSP 를 쓰던 Thread 에서 SVC (task_probe 안의 svc_call) */
    struct exc_obs svc = g_obs;
    dump("  SVC  : ipsr       = ", svc.ipsr);
    dump("  SVC  : control    = ", svc.control);
    dump("  SVC  : EXC_RETURN = ", svc.exc_return);
    dump("  SVC  : sp         = ", svc.sp);
    dump("  SVC  : msp        = ", svc.msp);
    dump("  SVC  : psp        = ", svc.psp);
    dump("  SVC  : thread sp before svc = ", g_sp_before_svc);
    CHECK(svc.n == 1u && svc.ipsr == 11u);                     /* IPSR = SVCall 예외 번호 */
    CHECK((svc.xpsr & 0x1FFu) == 11u);
    CHECK(svc.msp != 0u && svc.sp == svc.msp && svc.msp == t.msp);  /* Handler 는 MSP, 프레임은 PSP 로 → MSP 그대로 */
    CHECK(svc.ipsr == 11u && (svc.control & CTRL_SPSEL) == 0u); /* 핸들러 안에서 SPSEL 은 0 으로 읽힌다 */
    CHECK(svc.exc_return == 0xFFFFFFFDu);                      /* 값만 기록 (해석은 ARM-M09) */
    CHECK(svc.psp == g_sp_before_svc - 32u);                   /* 8 워드가 PSP 쪽에 쌓였다 */
    CHECK(t.ctrl_after_svc == CTRL_SPSEL && t.sp_after_svc == t.sp);  /* 복귀하면 다시 PSP */

    /* C2. MSP 를 쓰던 Thread(main) 에서 PendSV */
    uint32_t sp_main = get_sp();
    pend_pendsv();
    struct exc_obs pv = g_obs;
    dump("  PendSV: ipsr       = ", pv.ipsr);
    dump("  PendSV: EXC_RETURN = ", pv.exc_return);
    dump("  PendSV: sp         = ", pv.sp);
    CHECK(pv.n == 2u && pv.ipsr == 14u);
    CHECK(pv.exc_return == 0xFFFFFFF9u);
    CHECK(pv.sp == pv.msp && pv.sp == sp_main - 32u);          /* 이번엔 프레임이 MSP 에 쌓였다 */

    /* C3. 비특권 태스크: MSR CONTROL 무시 → SVC 로 특권 회복 */
    r = run_on_psp(TASK_TOP, task_unpriv, 21u);
    dump("  unpriv: control after drop = ", u.ctrl_drop);
    dump("  unpriv: control after msr  = ", u.ctrl_try);
    dump("  unpriv: control after svc  = ", u.ctrl_back);
    CHECK(u.ctrl_drop == (CTRL_NPRIV | CTRL_SPSEL));
    CHECK(u.ctrl_try == (CTRL_NPRIV | CTRL_SPSEL));            /* 무시됨 */
    CHECK(u.ctrl_back == CTRL_SPSEL);                          /* SVC 핸들러가 nPRIV 를 지웠다 */
    CHECK(r == 42u && get_control() == 0u && get_sp() == get_msp());

    sh_dec(g_pass); sh_puts("/"); sh_dec(g_total); sh_puts(" passed\n");
    return g_pass == g_total ? 0 : 1;
}
