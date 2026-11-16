/* ENV: qemu */
/* BM-A03 부트로더 → 앱 점프: VTOR, MSP, 주변장치 정리 — 부트로더 쪽 (0x00000000, boot.ld)
 *
 * 한 QEMU 실행 = 부팅 10 번. 매 부팅마다 부트로더가 "더러운 상태"(SysTick 동작·pending, IRQ7 enable+pending,
 * UART0 켜짐, PRIMASK=1)를 만든 뒤 앱 벡터를 검사하고, 정리하고, 점프한다. 단계 k 마다 한 가지 실수를 주입:
 *   0 NORMAL  1 NO_VTOR  2 SYSTICK_RUNNING  3 PENDST_LEFT  4 NVIC_LEFT  5 PRIMASK_SET  6 THUMB_CLEAR
 *   7 ERASED_APP (벡터 0xFFFFFFFF → 거부, 부트로더에 머묾)  8 NORMAL (리셋 뒤 이미지가 원래대로인가)
 * 앱(app.c)은 진입 스냅샷 + SysTick 시험 결과를 메일박스(0x2000F800)에 쓰고 리셋 → 다음 부팅이 모아서,
 * 마지막 부팅이 표 + CHECK → SYS_EXIT 0/1.
 * 빌드/실행 (Lima VM elinux 안): bash run_qemu.sh solution|starter|probe|loader|layout|practice
 */
#include <stddef.h>
#include "boot_app.h"

extern uint32_t _sidata[], _sdata[], _edata[], _sbss[], _ebss[], _estack[];

/* ---------------- 시험 계획 (부트로더 전용, 0x2000F900 — 리셋을 건너 남는다) ---------------- */
#define PLAN_MAGIC 0x504C414Eu   /* "PLAN" */
#define NSTEPS     9u
#define MAX_BOOTS  14u
enum { INJ_NONE, INJ_NO_VTOR, INJ_SYSTICK_RUNNING, INJ_PENDST_LEFT, INJ_NVIC_LEFT, INJ_PRIMASK_SET,
       INJ_THUMB_CLEAR, INJ_ERASED_APP, INJ_NONE_AGAIN };
static const char *const k_step_name[NSTEPS] = {
    "NORMAL         ", "NO_VTOR        ", "SYSTICK_RUNNING", "PENDST_LEFT    ", "NVIC_LEFT      ",
    "PRIMASK_SET    ", "THUMB_CLEAR    ", "ERASED_APP     ", "NORMAL_AGAIN   " };
enum { OUT_NONE, OUT_PENDING, OUT_RAN, OUT_FAULT, OUT_REFUSED, OUT_RETURNED, OUT_LOST };
static const char *const k_out_name[7] = { "none    ", "pending ", "ran     ", "FAULT   ", "REFUSED ",
                                           "RETURNED", "lost    " };

typedef struct {
    uint32_t outcome, refuse, app_sp, app_pc;
    uint32_t stage, early_ticks, early_irqs, early_irq_num, early_stage, app_ticks, boot_ticks_in_app;
    uint32_t e_msp, e_primask, e_control, e_vtor, e_syst_csr, e_icsr, e_iser0, e_ispr0, e_uart_ctrl;
    uint32_t f_exc, f_cfsr, f_hfsr, f_pc, f_stage;
} result_t;
typedef struct {
    uint32_t magic, boots, next;
    result_t res[NSTEPS];
    uint32_t check;
} plan_t;
#define PLAN ((plan_t *)PLAN_ADDR)
_Static_assert(sizeof(plan_t) <= 0x700u, "plan must fit 0x2000F900..0x20010000");
_Static_assert(sizeof(mailbox_t) <= 0x100u, "mailbox must fit 256 B");

static uint32_t fnv1a_words(const uint32_t *w, uint32_t n) {
    uint32_t h = 0x811C9DC5u;
    for (uint32_t i = 0; i < n; ++i)
        for (uint32_t s = 0; s < 32u; s += 8u) { h ^= (w[i] >> s) & 0xFFu; h *= 0x01000193u; }
    return h;
}
#define PLAN_WORDS ((uint32_t)((offsetof(plan_t, check) - offsetof(plan_t, boots)) / 4u))
static int plan_valid(void) {
    return PLAN->magic == PLAN_MAGIC && PLAN->check == fnv1a_words(&PLAN->boots, PLAN_WORDS) &&
           PLAN->next <= NSTEPS;
}
static void plan_seal(void) { PLAN->check = fnv1a_words(&PLAN->boots, PLAN_WORDS); dsb(); }

/* ---------------- 부트로더 자신의 핸들러: 메일박스 카운터만 (점프 뒤에 불려도 앱 RAM 을 안 망가뜨리게) ---- */
void SysTick_Handler(void) { MB->boot_ticks++; }
void Irq7_Handler(void) { MB->boot_irq_hits++; }

/* ================= TODO 1: 앱 벡터 테이블 sanity check =================
 * vt[0] = 초기 MSP, vt[1] = 리셋 벡터. 아래 순서로 검사해 첫 실패 코드를 돌려준다 (전부 통과 = VEC_OK).
 *   VEC_ERASED   : vt[0] 와 vt[1] 이 둘 다 0xFFFFFFFF (지워진 플래시 = 앱 없음)
 *   VEC_SP_RANGE : SP 가 (RAM_LO, APP_RAM_HI] 밖  (full-descending 스택: 꼭대기 == HI 는 허용, == LO 는 0 바이트)
 *   VEC_SP_ALIGN : SP 가 8 의 배수가 아님 (AAPCS)
 *   VEC_PC_THUMB : 리셋 벡터 bit0 == 0 (Thumb 비트 없음)
 *   VEC_PC_RANGE : (PC & ~1) 이 [APP_BASE + 4·APP_NVEC, APP_LIMIT) 밖 — 벡터 테이블 안·슬롯 밖·부트로더 안 금지 */
enum { VEC_OK = 0, VEC_ERASED, VEC_SP_RANGE, VEC_SP_ALIGN, VEC_PC_THUMB, VEC_PC_RANGE, VEC_TODO };
static const char *const k_vec_name[7] = { "OK", "ERASED", "SP_RANGE", "SP_ALIGN", "PC_THUMB", "PC_RANGE",
                                           "TODO" };
static uint32_t app_vector_ok(const uint32_t *vt) {
    (void)vt;
    return VEC_TODO;                                       /* TODO: 안전한 쪽 = 전부 거부 */
}

/* ================= TODO 2: SysTick 정지 =================
 * 카운터를 끄고(CTRL=0), CVR 에 아무 값이나 써서 카운터와 COUNTFLAG 를 지우고,
 * "그 다음에" 이미 걸려 있는 SysTick pending 을 ICSR.PENDSTCLR 로 지운다.
 * (CTRL=0 은 pending 을 지우지 않는다 — demo_probe P2 실측) */
static void systick_stop(void) {
    /* TODO */
}

/* ================= TODO 3: 인터럽트 원천 정리 =================
 * (a) 부트로더가 쓴 장치(UART0)를 먼저 끈다: CTRL=0, INTSTATUS 의 원인 비트 W1C (신호선을 내린다)
 * (b) 그 다음 NVIC 의 모든 줄을 disable(ICER) + pending clear(ICPR). 줄 수는 ICTR 로 (워드 = INTLINESNUM+1).
 * (c) DSB + ISB. */
static void nvic_quiesce(void) {
    /* TODO */
}

/* ================= TODO 4: 점프 =================
 * VTOR = vtor → DSB/ISB → CONTROL = 0 (MSP·특권) → MSP = sp → PRIMASK = primask → ISB → BX pc.
 * MSP 를 바꾼 뒤에는 C 코드가 스택을 쓰면 안 되므로 마지막 부분은 한 덩어리의 인라인 asm 으로.
 * 계약: 앱은 PRIMASK = 0 (리셋 직후와 같은 상태)으로 시작한다 — 호출자가 primask 로 넘긴다. */
__attribute__((noinline)) static void boot_jump(uint32_t vtor, uint32_t sp, uint32_t pc, uint32_t primask) {
    (void)vtor; (void)sp; (void)pc; (void)primask;      /* TODO: 돌아오면 하네스가 RETURNED 로 기록 */
}

/* ---------------- 일부러 틀린 정리 (결함 주입용, 시험 하네스) ---------------- */
static void systick_stop_ctrl_only(void) { SYST_CSR = 0u; dsb(); }   /* 흔한 버그: PENDSTCLR 를 잊음 */

static void handoff(uint32_t k, const uint32_t *vt) {
    __asm__ volatile("cpsid i" ::: "memory");             /* 0) 여기부터 아무것도 끼어들 수 없다 */
    if (k == INJ_PENDST_LEFT) systick_stop_ctrl_only();
    else if (k != INJ_SYSTICK_RUNNING) systick_stop();
    if (k != INJ_NVIC_LEFT) nvic_quiesce();
    uint32_t vtor = (k == INJ_NO_VTOR) ? SCB_VTOR : APP_BASE;   /* 잊으면 VTOR 는 부트로더 테이블(0) 그대로 */
    uint32_t pc = (k == INJ_THUMB_CLEAR) ? (vt[1] & ~1u) : vt[1];
    uint32_t primask = (k == INJ_PRIMASK_SET) ? 1u : 0u;
    boot_jump(vtor, vt[0], pc, primask);
}

/* ---------------- 부트로더가 "일을 한" 흔적: 점프 직전의 더러운 상태를 결정적으로 만든다 ---------------- */
static void wait_countflag(void) {
    (void)SYST_CSR;                                        /* 읽기 = COUNTFLAG 지움 → "새" wrap 을 기다린다 */
    for (uint32_t i = 0; i < 1000000u; ++i)
        if (SYST_CSR & SYST_COUNTFLAG) return;
}
static void boot_activity(void) {
    MB->boot_ticks = 0u;
    MB->boot_irq_hits = 0u;
    SYST_RVR = TICK_RELOAD;                                /* 타임아웃용 100 µs tick */
    SYST_CVR = 0u;
    SYST_CSR = SYST_CLKSOURCE | SYST_TICKINT | SYST_ENABLE;
    NVIC_ISER(0) = 1u << BOOT_IRQ;                         /* "플래시 컨트롤러" IRQ */
    UART0_BAUDDIV = 16u;                                   /* 콘솔: TX + TX 인터럽트 */
    UART0_CTRL = (1u << 0) | (1u << 2);
    UART0_DATA = 'B';
    __asm__ volatile("cpsie i" ::: "memory");
    for (uint32_t i = 0; i < 3000000u && MB->boot_ticks < 2u; ++i) { __asm__ volatile("nop"); }
    NVIC_ISPR(0) = 1u << BOOT_IRQ;                         /* 한 번 처리됨 */
    dsb(); isb();
    /* 마지막 작업(예: 플래시 검증)은 인터럽트를 막고 했다 — 그 사이 IRQ7 과 tick 이 또 왔다 */
    __asm__ volatile("cpsid i" ::: "memory");
    NVIC_ISPR(0) = 1u << BOOT_IRQ;
    wait_countflag();
    uint32_t primask;
    __asm__ volatile("mrs %0, primask" : "=r"(primask));
    sh_puts("  [boot] state before handoff: PRIMASK="); sh_dec(primask); sh_puts(" SYST_CSR=");
    sh_hex(SYST_CSR); sh_puts(" PENDST="); sh_dec((SCB_ICSR >> 26) & 1u); sh_puts(" ISER0=");
    sh_hex(NVIC_ISER(0)); sh_puts(" ISPR0="); sh_hex(NVIC_ISPR(0)); sh_puts(" UART0.CTRL=");
    sh_hex(UART0_CTRL); sh_puts(" ticks="); sh_dec(MB->boot_ticks); sh_puts(" irq7="); sh_dec(MB->boot_irq_hits);
    sh_puts("\n");
}

/* ---------------- 지난 부팅의 결과 모으기 ---------------- */
static void collect(uint32_t k) {
    result_t *r = &PLAN->res[k];
    if (r->outcome != OUT_PENDING) return;                 /* REFUSED/RETURNED 는 부트로더가 이미 적었다 */
    if (MB->magic != MB_MAGIC || MB->step != k) { r->outcome = OUT_LOST; r->stage = MB->stage; return; }
    r->outcome = (MB->stage == STAGE_FAULT) ? OUT_FAULT : OUT_RAN;
    r->stage = MB->stage; r->early_ticks = MB->early_ticks; r->early_irqs = MB->early_irqs;
    r->early_irq_num = MB->early_irq_num; r->early_stage = MB->early_stage; r->app_ticks = MB->app_ticks;
    r->boot_ticks_in_app = MB->boot_ticks_in_app;
    r->e_msp = MB->e_msp; r->e_primask = MB->e_primask; r->e_control = MB->e_control; r->e_vtor = MB->e_vtor;
    r->e_syst_csr = MB->e_syst_csr; r->e_icsr = MB->e_icsr; r->e_iser0 = MB->e_iser0; r->e_ispr0 = MB->e_ispr0;
    r->e_uart_ctrl = MB->e_uart_ctrl;
    r->f_exc = MB->f_exc; r->f_cfsr = MB->f_cfsr; r->f_hfsr = MB->f_hfsr; r->f_pc = MB->f_pc;
    r->f_stage = MB->f_stage;
    MB->magic = 0u;                                        /* 소비했으면 무효화 */
}
static void mailbox_arm(uint32_t k) {
    volatile uint32_t *w = (volatile uint32_t *)MB_ADDR;
    for (uint32_t i = 0; i < sizeof(mailbox_t) / 4u; ++i) w[i] = 0u;
    MB->step = k;
    MB->stage = STAGE_NOT_STARTED;
}

/* ---------------- 시험 하네스 ---------------- */
#define CHECK(expr) do { total++; if (expr) { pass++; } else { \
    sh_puts("FAIL " __FILE__ ":"); sh_dec(__LINE__); sh_puts("  " #expr "  [step "); sh_dec(k); sh_puts("]\n"); } } while (0)

static int check_contract_ok(const result_t *r) {          /* NORMAL 단계: 앱이 본 상태 = 리셋 직후와 같다 */
    return r->e_vtor == APP_BASE && r->e_msp == r->app_sp && r->e_msp == APP_RAM_HI && r->e_primask == 0u &&
           r->e_control == 0u && r->e_syst_csr == 0u && (r->e_icsr & ICSR_PENDSTSET) == 0u &&
           r->e_iser0 == 0u && r->e_ispr0 == 0u && r->e_uart_ctrl == 0u && r->early_ticks == 0u &&
           r->early_irqs == 0u;
}

static int final_report(void) {
    int pass = 0, total = 0;
    uint32_t k = 0;
    sh_puts("== handoff summary (QEMU mps2-an385, bootloader 0x0 -> app 0x4000) ==\n");
    sh_puts("step inject          outcome  vec      e_VTOR     e_MSP      PM SYST_CSR   ISPR0      early t/i apptk boottk fault\n");
    for (k = 0; k < NSTEPS; ++k) {
        const result_t *r = &PLAN->res[k];
        sh_dec(k); sh_puts("    "); sh_puts(k_step_name[k]); sh_puts(" "); sh_puts(k_out_name[r->outcome < 7u ? r->outcome : 0u]);
        sh_puts(" "); sh_puts(k_vec_name[r->refuse < 7u ? r->refuse : 6u]); sh_puts(r->refuse == VEC_OK ? "       " : " ");
        sh_hex(r->e_vtor); sh_puts(" "); sh_hex(r->e_msp); sh_puts(" "); sh_dec(r->e_primask); sh_puts("  ");
        sh_hex(r->e_syst_csr); sh_puts(" "); sh_hex(r->e_ispr0); sh_puts(" "); sh_dec(r->early_ticks); sh_puts("/");
        sh_dec(r->early_irqs); sh_puts("       "); sh_dec(r->app_ticks); sh_puts("     "); sh_dec(r->boot_ticks_in_app);
        if (r->outcome == OUT_FAULT) { sh_puts("    exc"); sh_dec(r->f_exc); sh_puts(" CFSR="); sh_hex(r->f_cfsr);
                                       sh_puts(" pc="); sh_hex(r->f_pc); }
        sh_puts("\n");
    }
    const result_t *R = PLAN->res;
    /* 0, 8: 정상 — 앱이 리셋과 같은 상태를 보고, 자기 SysTick 이 자기 핸들러로 */
    for (k = 0; k < NSTEPS; k += INJ_NONE_AGAIN) {
        CHECK(R[k].outcome == OUT_RAN && R[k].refuse == VEC_OK);
        CHECK(check_contract_ok(&R[k]));
        CHECK(R[k].app_ticks >= 3u && R[k].boot_ticks_in_app == 0u);
    }
    k = INJ_NO_VTOR;           /* VTOR 를 잊음: 앱 진입 시 VTOR=0, 앱의 SysTick 이 부트로더 핸들러로 */
    CHECK(R[k].outcome == OUT_RAN && R[k].e_vtor == 0u);
    CHECK(R[k].app_ticks == 0u && R[k].boot_ticks_in_app > 0u);
    k = INJ_SYSTICK_RUNNING;   /* SysTick 안 끔: 진입 시 ENABLE=1, 앱이 준비되기 전에 tick */
    CHECK(R[k].outcome == OUT_RAN && (R[k].e_syst_csr & SYST_ENABLE) != 0u);
    CHECK(R[k].early_ticks >= 1u);
    k = INJ_PENDST_LEFT;       /* CTRL=0 만: SysTick 은 꺼졌는데 남은 pending 이 앱 첫 명령보다 먼저 */
    CHECK(R[k].outcome == OUT_RAN && (R[k].e_syst_csr & SYST_ENABLE) == 0u);
    CHECK(R[k].early_ticks == 1u && R[k].early_stage == STAGE_NOT_STARTED);
    CHECK(R[k].app_ticks >= 3u);
    k = INJ_NVIC_LEFT;         /* NVIC/UART 정리 안 함: IRQ7 이 앱 진입 전에, UART 는 켜진 채 */
    CHECK(R[k].outcome == OUT_RAN && R[k].early_irqs == 1u && R[k].early_irq_num == BOOT_IRQ);
    CHECK(R[k].early_stage == STAGE_NOT_STARTED && R[k].e_uart_ctrl != 0u);
    k = INJ_PRIMASK_SET;       /* PRIMASK=1 로 점프: 리셋 상태를 믿는 앱은 인터럽트를 영영 못 받는다 */
    CHECK(R[k].outcome == OUT_RAN && R[k].e_primask == 1u && R[k].app_ticks == 0u);
    k = INJ_THUMB_CLEAR;       /* bit0 = 0 으로 BX: 앱 첫 명령에서 INVSTATE → (UsageFault 꺼짐) HardFault FORCED */
    CHECK(R[k].outcome == OUT_FAULT && R[k].f_exc == 3u && R[k].f_stage == STAGE_NOT_STARTED);
    CHECK(R[k].f_cfsr == CFSR_INVSTATE && R[k].f_hfsr == HFSR_FORCED && R[k].f_pc == (R[k].app_pc & ~1u));
    k = INJ_ERASED_APP;        /* 지워진 벡터: 거부하고 부트로더에 머문다 (앱은 한 명령도 안 돈다) */
    CHECK(R[k].outcome == OUT_REFUSED && R[k].refuse == VEC_ERASED && R[k].app_sp == 0xFFFFFFFFu);
    /* 같은 이미지가 리셋마다 다시 로드됐나: 8 단계의 벡터 = 0 단계의 벡터 */
    CHECK(R[INJ_NONE_AGAIN].app_sp == R[INJ_NONE].app_sp && R[INJ_NONE_AGAIN].app_pc == R[INJ_NONE].app_pc);

    /* TODO 1 단위 시험: 손으로 만든 벡터들 */
    static const struct { uint32_t sp, pc, want; } tv[] = {
        { 0xFFFFFFFFu, 0xFFFFFFFFu, VEC_ERASED },  { 0x2000F800u, 0x00004101u, VEC_OK },
        { 0x2000F808u, 0x00004101u, VEC_SP_RANGE }, { 0x20000000u, 0x00004101u, VEC_SP_RANGE },
        { 0x00000000u, 0x00000000u, VEC_SP_RANGE }, { 0x2000F7FCu, 0x00004101u, VEC_SP_ALIGN },
        { 0x2000F800u, 0x00004100u, VEC_PC_THUMB }, { 0x2000F800u, 0x00004001u, VEC_PC_RANGE },
        { 0x2000F800u, 0x000040C1u, VEC_OK },       { 0x2000F800u, 0x000040BFu, VEC_PC_RANGE },
        { 0x2000F800u, 0x00040001u, VEC_PC_RANGE }, { 0x2000F800u, 0x0003FFFFu, VEC_OK },
        { 0x2000F800u, 0x00000101u, VEC_PC_RANGE }, { 0x20000008u, 0x00004101u, VEC_OK },
    };
    k = 100u;
    for (uint32_t i = 0; i < sizeof tv / sizeof tv[0]; ++i) {
        uint32_t v[2] = { tv[i].sp, tv[i].pc };
        CHECK(app_vector_ok(v) == tv[i].want);
    }
    k = 200u;
    CHECK(PLAN->boots == NSTEPS + 1u);
    CHECK(SCB_VTOR == 0u);                                 /* 리셋은 VTOR 를 0 으로 되돌렸다 */
    sh_dec((uint32_t)pass); sh_puts("/"); sh_dec((uint32_t)total); sh_puts(" passed\n");
    return pass == total ? 0 : 1;
}

int main(void) {
    if (!plan_valid()) {
        sh_puts("cold boot: no valid plan @"); sh_hex(PLAN_ADDR); sh_puts("\n");
        volatile uint32_t *w = (volatile uint32_t *)PLAN_ADDR;
        for (uint32_t i = 0; i < sizeof(plan_t) / 4u; ++i) w[i] = 0u;
        PLAN->magic = PLAN_MAGIC;
    }
    PLAN->boots++;
    sh_puts("== boot #"); sh_dec(PLAN->boots); sh_puts(" (bootloader @0x0, VTOR="); sh_hex(SCB_VTOR); sh_puts(") ==\n");
    if (PLAN->boots > MAX_BOOTS) { plan_seal(); sh_puts("boot-loop guard: too many resets\n"); return 2; }
    if (PLAN->next > 0u) {
        uint32_t p = PLAN->next - 1u;
        collect(p);
        sh_puts("  [boot] step "); sh_dec(p); sh_puts(" result: "); sh_puts(k_out_name[PLAN->res[p].outcome]);
        sh_puts("\n");
    }
    plan_seal();
    if (PLAN->next < NSTEPS) {
        uint32_t k = PLAN->next++;
        result_t *r = &PLAN->res[k];
        r->outcome = OUT_PENDING;
        plan_seal();                                       /* 진행을 먼저 기록 — 이 단계가 다시 돌지 않게 */
        sh_puts("  [boot] step "); sh_dec(k); sh_puts(": "); sh_puts(k_step_name[k]); sh_puts("\n");
        boot_activity();
        volatile uint32_t *slot = (volatile uint32_t *)APP_BASE;
        if (k == INJ_ERASED_APP) { slot[0] = 0xFFFFFFFFu; slot[1] = 0xFFFFFFFFu; }   /* 지워진(빈) 슬롯 흉내 */
        const uint32_t vt[2] = { slot[0], slot[1] };
        r->app_sp = vt[0];
        r->app_pc = vt[1];
        r->refuse = app_vector_ok(vt);
        sh_puts("  [boot] app vector @"); sh_hex(APP_BASE); sh_puts(": SP="); sh_hex(vt[0]); sh_puts(" PC=");
        sh_hex(vt[1]); sh_puts(" -> "); sh_puts(k_vec_name[r->refuse < 7u ? r->refuse : 6u]); sh_puts("\n");
        if (r->refuse != VEC_OK) {
            r->outcome = OUT_REFUSED;
            plan_seal();
            sh_puts("  [boot] no valid app -> staying in bootloader (recovery mode would wait for an image)\n");
            system_reset();
        }
        mailbox_arm(k);
        plan_seal();
        handoff(k, vt);
        r->outcome = OUT_RETURNED;                         /* 점프가 돌아왔다 = TODO 미완성 */
        plan_seal();
        sh_puts("  [boot] jump returned?! -> reset\n");
        system_reset();
    }
    return final_report();
}

/* ---------------- startup (BM-B08 축약판) ---------------- */
void Reset_Handler(void);
void Default_Handler(void) {
    uint32_t ipsr;
    __asm__ volatile("mrs %0, ipsr" : "=r"(ipsr));
    sh_puts("[boot] Default_Handler: unexpected exception "); sh_dec(ipsr); sh_puts(" CFSR="); sh_hex(SCB_CFSR);
    sh_puts("\n");
    sh_exit(3);
}
typedef void (*vector_t)(void);
#define D Default_Handler
__attribute__((section(".isr_vector"), used))
const vector_t g_vectors[16 + 32] = {
    (vector_t)_estack, Reset_Handler, D, D, D, D, D, 0, 0, 0, 0, D, D, 0, D, SysTick_Handler,
    D, D, D, D, D, D, D, Irq7_Handler, D, D, D, D, D, D, D, D,
    D, D, D, D, D, D, D, D, D, D, D, D, D, D, D, D,
};
__attribute__((noreturn)) void Reset_Handler(void) {
    const uint32_t *src = _sidata;
    for (uint32_t *dst = _sdata; dst < _edata;) *dst++ = *src++;
    for (uint32_t *dst = _sbss; dst < _ebss;) *dst++ = 0u;
    sh_exit(main());
}
