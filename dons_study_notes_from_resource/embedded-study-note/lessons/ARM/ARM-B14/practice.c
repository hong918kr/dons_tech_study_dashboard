/* ENV: qemu */
/* ARM-B14 practice.c — 추가 연습 1~4 를 한 번에 검증 (Cortex-M3, QEMU mps2-an385).
 * 빌드: run_qemu.sh practice  →  practice.c + practice.S(= solution.S + 연습 함수) + shared_startup.c
 */
#include <stdint.h>

uint32_t get_sp(void);
void     svc_call(uint32_t cmd);
uint32_t get_msp(void);
uint32_t get_psp(void);
void     set_psp(uint32_t top);
uint32_t get_control(void);
void     set_control(uint32_t v);
uint32_t get_xpsr(void);
uint32_t get_ipsr(void);
uint32_t get_primask(void);
uint32_t run_on_psp(uint32_t *psp_top, uint32_t (*fn)(uint32_t), uint32_t arg);
uint32_t nzcv_after_cmp(uint32_t a, uint32_t b);       /* 연습 1 */
uint32_t q_after_usat8(int32_t x);                     /* 연습 1 */

void sh_puts(const char *s);
void sh_hex(uint32_t v);
void sh_dec(uint32_t v);
void check(int ok, const char *expr, int line);
extern unsigned g_pass, g_total;
#define CHECK(e) check((e) ? 1 : 0, #e, __LINE__)

#define REG32(a)   (*(volatile uint32_t *)(uintptr_t)(a))
#define SYST_CSR   REG32(0xE000E010u)
#define SYST_RVR   REG32(0xE000E014u)
#define SYST_CVR   REG32(0xE000E018u)
#define CTRL_NPRIV 1u
#define CTRL_SPSEL 2u
#define SVC_CMD_NOP       0u
#define SVC_CMD_PRIV      1u
#define SVC_CMD_TRY_SPSEL 2u                   /* 연습 2: Handler 에서 SPSEL 쓰기 시도 */

volatile uint32_t g_svc_cmd, g_sp_before_svc;
struct exc_obs { uint32_t ipsr, control, ctrl_after_write, exc_return, sp, msp, psp, n; };
volatile struct exc_obs g_obs;

void exc_observe(uint32_t exc_return, uint32_t sp, uint32_t msp, uint32_t psp) {
    g_obs.ipsr = get_ipsr();
    g_obs.control = get_control();
    g_obs.exc_return = exc_return;
    g_obs.sp = sp;
    g_obs.msp = msp;
    g_obs.psp = psp;
    g_obs.n++;
    if (g_obs.ipsr == 15u) SYST_CSR = 0u;                     /* SysTick 은 한 번만 */
    if (g_obs.ipsr == 11u && g_svc_cmd == SVC_CMD_PRIV)
        set_control(get_control() & ~CTRL_NPRIV);
    if (g_obs.ipsr == 11u && g_svc_cmd == SVC_CMD_TRY_SPSEL) {
        set_control(get_control() | CTRL_SPSEL);              /* Handler 모드: SPSEL 쓰기는 무시 */
        g_obs.ctrl_after_write = get_control();
    }
}

static uint32_t g_task_stack[256] __attribute__((aligned(8)));
#define TASK_TOP (&g_task_stack[256])
#define TOP_U32  ((uint32_t)(uintptr_t)TASK_TOP)

/* ---------- 연습 3: 스택 칠하기(painting)와 최고 수위(high-water mark) ---------- */
#define PAINT 0xDEADBEEFu
static void stack_paint(void) {
    for (unsigned i = 0; i < 256u; i++) ((volatile uint32_t *)g_task_stack)[i] = PAINT;
}
static uint32_t stack_high_water(void) {         /* 꼭대기에서부터 쓰인 바이트 수 */
    unsigned i = 0;
    while (i < 256u && ((volatile uint32_t *)g_task_stack)[i] == PAINT) i++;
    return (256u - i) * 4u;
}
static volatile uint32_t g_deepest_sp;
__attribute__((noinline)) static uint32_t burn(uint32_t depth) {
    volatile uint32_t local[8];
    for (unsigned i = 0; i < 8u; i++) local[i] = depth + i;
    if (depth == 0u) { g_deepest_sp = get_sp(); return local[7]; }
    return burn(depth - 1u) + local[0];                       /* 꼬리 호출 아님 → 프레임이 쌓인다 */
}
static uint32_t task_burn(uint32_t depth) { return burn(depth); }

/* ---------- 연습 4: PSP 태스크를 SysTick 이 끊는다 ---------- */
static uint32_t task_wait_tick(uint32_t arg) {
    uint32_t n0 = g_obs.n;
    SYST_RVR = 2000u; SYST_CVR = 0u; SYST_CSR = 7u;          /* enable | TICKINT | CPU clock */
    while (g_obs.n == n0) { }                                 /* 틱이 올 때까지 PSP 위에서 대기 */
    return arg + (get_control() == CTRL_SPSEL ? 1u : 100u);
}

int main(void) {
    sh_puts("ARM-B14 practice\n");

    /* ---------- 연습 1: APSR 보기 — CMP 뒤 NZCV, USAT 뒤 Q ---------- */
    CHECK(nzcv_after_cmp(1u, 2u) == 0x8u);                    /* N           */
    CHECK(nzcv_after_cmp(2u, 2u) == 0x6u);                    /*   Z C       */
    CHECK(nzcv_after_cmp(3u, 2u) == 0x2u);                    /*     C       */
    CHECK(nzcv_after_cmp(0x80000000u, 1u) == 0x3u);           /*     C V     */
    CHECK(nzcv_after_cmp(0x7FFFFFFFu, 0xFFFFFFFFu) == 0x9u);  /* N       V   */
    CHECK(nzcv_after_cmp(0xFFFFFFFFu, 1u) == 0xAu);           /* N   C       */
    CHECK(q_after_usat8(300) == 1u && q_after_usat8(100) == 0u);
    CHECK(q_after_usat8(-5) == 1u && q_after_usat8(255) == 0u);

    /* ---------- 연습 2: 무시되는 쓰기 / 0 으로 읽히는 읽기 ---------- */
    uint32_t msp_priv = get_msp();
    set_psp(0x20002000u);
    set_control(CTRL_NPRIV);                                  /* main 을 비특권으로 (MSP 그대로) */
    uint32_t un_ctrl = get_control();
    uint32_t un_msp = get_msp();
    uint32_t un_psp = get_psp();
    set_psp(0x20003000u);                                     /* 비특권 MSR PSP → 무시? */
    uint32_t un_ipsr = get_ipsr();
    svc_call(SVC_CMD_PRIV);                                   /* 특권 회복 */
    sh_puts("  unpriv reads: msp="); sh_hex(un_msp); sh_puts(" psp="); sh_hex(un_psp);
    sh_puts(" control="); sh_hex(un_ctrl); sh_puts(" (priv msp="); sh_hex(msp_priv); sh_puts(")\n");
    CHECK(un_ctrl == CTRL_NPRIV && un_ipsr == 0u);
    CHECK(un_msp == 0u && un_psp == 0u);                      /* QEMU: 비특권 MRS MSP/PSP → 0 */
    CHECK(get_psp() == 0x20002000u);                          /* 비특권 MSR PSP 는 무시됐다 */
    CHECK(get_control() == 0u);
    svc_call(SVC_CMD_TRY_SPSEL);
    sh_puts("  handler: control before="); sh_hex(g_obs.control);
    sh_puts(" after msr(|SPSEL)="); sh_hex(g_obs.ctrl_after_write); sh_puts("\n");
    CHECK((g_obs.ctrl_after_write & CTRL_SPSEL) == 0u);       /* Handler 에서 SPSEL 쓰기는 무시 */
    CHECK(get_control() == 0u && get_sp() == get_msp());      /* 복귀 후에도 MSP */

    /* ---------- 연습 3: high-water mark ---------- */
    stack_paint();
    (void)run_on_psp(TASK_TOP, task_burn, 2u);
    uint32_t hw2 = stack_high_water(), deep2 = TOP_U32 - g_deepest_sp;
    stack_paint();
    (void)run_on_psp(TASK_TOP, task_burn, 8u);
    uint32_t hw8 = stack_high_water(), deep8 = TOP_U32 - g_deepest_sp;
    sh_puts("  depth 2: high-water="); sh_dec(hw2); sh_puts(" B, top-deepest_sp="); sh_dec(deep2);
    sh_puts(" B\n  depth 8: high-water="); sh_dec(hw8); sh_puts(" B, top-deepest_sp="); sh_dec(deep8);
    sh_puts(" B\n");
    CHECK(hw2 >= deep2 && hw8 >= deep8);                      /* 칠한 흔적 ≥ 실제로 내려간 깊이 */
    CHECK(hw8 > hw2 && (hw8 - hw2) % 8u == 0u);               /* 깊이 6 만큼 더, 프레임은 8 바이트 단위 */
    CHECK(((volatile uint32_t *)g_task_stack)[0] == PAINT);   /* 바닥 워드 무사 = 오버플로 없음 */

    /* ---------- 연습 4: SysTick 이 PSP 태스크를 끊는다 ---------- */
    uint32_t r = run_on_psp(TASK_TOP, task_wait_tick, 10u);
    sh_puts("  SysTick: ipsr="); sh_dec(g_obs.ipsr); sh_puts(" EXC_RETURN="); sh_hex(g_obs.exc_return);
    sh_puts(" sp="); sh_hex(g_obs.sp); sh_puts(" psp="); sh_hex(g_obs.psp); sh_puts("\n");
    CHECK(r == 11u);
    CHECK(g_obs.ipsr == 15u && g_obs.exc_return == 0xFFFFFFFDu);
    CHECK(g_obs.sp == g_obs.msp && g_obs.psp < TOP_U32 && g_obs.psp >= (uint32_t)(uintptr_t)g_task_stack);

    sh_dec(g_pass); sh_puts("/"); sh_dec(g_total); sh_puts(" passed\n");
    return g_pass == g_total ? 0 : 1;
}
