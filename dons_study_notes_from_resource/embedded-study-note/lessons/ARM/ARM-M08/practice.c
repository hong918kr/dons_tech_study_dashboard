/* ENV: qemu */
/* ARM-M08 practice.c — 추가 연습 1~4 의 답과 테스트 (Cortex-M3, QEMU mps2-an385).
 * 빌드: run_qemu.sh practice  →  practice.c + practice.S + shared_startup.c
 *  연습 1  vtor_align_bytes / vtor_ok      — VTOR 정렬 규칙 계산 (순수 C)
 *  연습 2  vec_audit                       — 벡터 테이블 린트: 짝수·범위 밖 엔트리 찾기 (순수 C)
 *  연습 3  irq_install (.S) + NMI 패치      — 런타임 설치, NMI 는 PRIMASK 로 못 막는다
 *  연습 4  pop_pc_to / mov_pc_to (.S)      — 짝수 주소로 가는 세 가지 길: blx·pop{pc} = INVSTATE, mov pc = 무사
 */
#include <stdint.h>

/* ---- practice.S ---- */
extern const uint32_t g_vectors[48];
void vtor_relocate(uint32_t *dst, const uint32_t *src, uint32_t nwords);
void vector_patch(uint32_t *table, uint32_t exc_num, uint32_t handler);
void irq_install(uint32_t irqn, uint32_t handler);
void pop_pc_to(uint32_t target);
extern const char pop_pc_ret[];
void mov_pc_to(uint32_t target);
void bx_target(void);

/* ---- shared_startup.c ---- */
void sh_puts(const char *s);
void sh_kv(const char *k, uint32_t v);
void sh_dec(uint32_t v);
__attribute__((noreturn)) void sh_exit(int code);
void check(int ok, const char *expr, int line);
extern unsigned g_pass, g_total;
extern uint32_t _sidata[];
#define CHECK(e) check((e) ? 1 : 0, #e, __LINE__)

#define REG32(a)    (*(volatile uint32_t *)(uintptr_t)(a))
#define SCB_ICSR    REG32(0xE000ED04u)      /* bit31 NMIPENDSET */
#define SCB_VTOR    REG32(0xE000ED08u)
#define SCB_SHCSR   REG32(0xE000ED24u)
#define SCB_CFSR    REG32(0xE000ED28u)
#define SCB_HFSR    REG32(0xE000ED2Cu)
#define NVIC_ISER0  REG32(0xE000E100u)
#define NVIC_ISPR0  REG32(0xE000E200u)
#define FN(f)       ((uint32_t)(uintptr_t)(f))
#define NVEC        48u

/* ---- practice.S 의 테이블이 참조하는 핸들러 ---- */
volatile uint32_t g_svc_n, g_tick1, g_irq6_n, g_irq7_n, g_nmi_n, g_bx_ran;
#define HANDLER __attribute__((aligned(4)))
HANDLER void SVC_Handler(void)     { g_svc_n++; }
HANDLER void SysTick_Handler(void) { g_tick1++; }
HANDLER void Irq6_Handler(void)    { g_irq6_n++; }
HANDLER void Irq7_Handler(void)    { g_irq7_n++; }       /* 연습 3: 런타임 설치 */
HANDLER void Nmi_Handler2(void)    { g_nmi_n++; }        /* 연습 3: NMI 엔트리 교체 */
uint32_t g_ram_vectors[NVEC] __attribute__((aligned(256)));

/* ---- fault 기록 (harness.c 와 같은 것) ---- */
struct fault_obs { uint32_t ipsr, cfsr, hfsr, pc, lr, xpsr, exc_return, n; };
volatile struct fault_obs g_f;
volatile uint32_t g_expect_fault;
void fault_record(uint32_t *frame, uint32_t exc_return) {
    uint32_t ipsr;
    __asm__ volatile("mrs %0, ipsr" : "=r"(ipsr));
    g_f.ipsr = ipsr; g_f.cfsr = SCB_CFSR; g_f.hfsr = SCB_HFSR;
    g_f.lr = frame[5]; g_f.pc = frame[6]; g_f.xpsr = frame[7];
    g_f.exc_return = exc_return; g_f.n++;
    if (!g_expect_fault) {
        sh_puts("unexpected fault"); sh_kv("cfsr", g_f.cfsr); sh_kv("stacked pc", g_f.pc);
        sh_exit(3);
    }
    g_expect_fault = 0;
    frame[7] |= 1u << 24;                   /* T = 1 로 고쳐서 복귀 */
    SCB_CFSR = g_f.cfsr;
    SCB_HFSR = g_f.hfsr;
}

/* ===== 연습 1: VTOR 정렬 ===== */
/* 테이블 크기 (16 + n_irq) 워드 → 바이트 → 2 의 거듭제곱으로 올림, 최소 128 B (ARMv7-M ARM B3.2.5 기준) */
uint32_t vtor_align_bytes(uint32_t n_irq) {
    uint32_t bytes = (16u + n_irq) * 4u, a = 128u;
    while (a < bytes) a <<= 1;
    return a;
}
/* Cortex-M3 r2p0 (DUI0552A): TBLOFF = VTOR[29:7], [31:30] 예약 → 테이블은 0x00000000–0x3FFFFF80 안 */
int vtor_ok(uint32_t addr, uint32_t n_irq) {
    return (addr % vtor_align_bytes(n_irq)) == 0u && (addr >> 30) == 0u;
}

/* ===== 연습 2: 벡터 테이블 린트 ===== */
/* 핸들러 칸(1..n-1, reserved 7..10·13 제외)이 0 이거나, 짝수이거나, 코드 범위 [lo, hi) 밖이면 bad.
 * 반환 = bad 개수, *first = 첫 bad 번호 (없으면 0). */
uint32_t vec_audit(const uint32_t *t, uint32_t n, uint32_t lo, uint32_t hi, uint32_t *first) {
    uint32_t bad = 0;
    *first = 0;
    for (uint32_t i = 1; i < n; ++i) {
        if ((i >= 7u && i <= 10u) || i == 13u) continue;
        uint32_t v = t[i], a = v & ~1u;
        int ok = v != 0u && (v & 1u) && a >= lo && a < hi;
        if (!ok && bad++ == 0u) *first = i;
    }
    return bad;
}

int main(void) {
    sh_puts("ARM-M08 practice\n");

    /* 연습 1 */
    CHECK(vtor_align_bytes(0) == 128u);       /* 16 워드 = 64 B → 최소 128 */
    CHECK(vtor_align_bytes(16) == 128u);      /* 32 워드 = 128 B */
    CHECK(vtor_align_bytes(17) == 256u);      /* 33 워드 = 132 B → 256 */
    CHECK(vtor_align_bytes(32) == 256u);      /* 이 레슨 (QEMU mps2-an385): 48 워드 = 192 B */
    CHECK(vtor_align_bytes(82) == 512u);      /* STM32F407 (82 IRQ 가정): 98 워드 = 392 B */
    CHECK(vtor_align_bytes(240) == 1024u);    /* Cortex-M3 최대 240 IRQ: 256 워드 */
    CHECK(vtor_ok(0x20000200u, 32) && !vtor_ok(0x20000280u, 32) && vtor_ok(0x20000280u, 16));
    CHECK(!vtor_ok(0x40000000u, 32));         /* [31:30] 은 TBLOFF 밖 */

    /* 연습 2 */
    uint32_t first, lo = NVEC * 4u, hi = FN(_sidata);   /* 코드 = 테이블 뒤 ~ .data 초기값 앞 */
    CHECK(vec_audit(g_vectors, NVEC, lo, hi, &first) == 0u && first == 0u);
    uint32_t t[NVEC];
    for (uint32_t i = 0; i < NVEC; ++i) t[i] = g_vectors[i];
    t[15] &= ~1u;                                   /* SysTick bit0 지움 */
    t[30] = 0x20000001u;                            /* RAM 주소 (코드 범위 밖) */
    t[8] = 0x12345678u;                             /* reserved 칸은 검사 안 함 */
    CHECK(vec_audit(t, NVEC, lo, hi, &first) == 2u && first == 15u);
    t[2] = 0u;
    CHECK(vec_audit(t, NVEC, lo, hi, &first) == 3u && first == 2u);

    /* 연습 3: RAM 테이블 + 런타임 설치 */
    vtor_relocate(g_ram_vectors, g_vectors, NVEC);
    CHECK(SCB_VTOR == FN(g_ram_vectors));
    irq_install(7u, FN(Irq7_Handler));
    CHECK(g_ram_vectors[16 + 7] == FN(Irq7_Handler) && g_vectors[16 + 7] != FN(Irq7_Handler));
    NVIC_ISER0 = 1u << 7;
    NVIC_ISPR0 = 1u << 7;
    __asm__ volatile("dsb\n isb" ::: "memory");
    CHECK(g_irq7_n == 1u);
    vector_patch(g_ram_vectors, 2u, FN(Nmi_Handler2));
    __asm__ volatile("cpsid i" ::: "memory");       /* PRIMASK = 1: 설정 가능한 우선순위는 전부 막힘 */
    SCB_ICSR = 1u << 31;                            /* NMIPENDSET */
    __asm__ volatile("dsb\n isb" ::: "memory");
    uint32_t nmi_in_primask = g_nmi_n;
    __asm__ volatile("cpsie i" ::: "memory");
    CHECK(nmi_in_primask == 1u);                    /* NMI(-2) 는 PRIMASK 로 못 막는다 */

    /* 연습 4: 짝수 주소로 가는 세 가지 길 */
    uint32_t even = FN(bx_target) & ~1u;
    SCB_SHCSR |= 1u << 18;                          /* USGFAULTENA */
    g_expect_fault = 1;
    pop_pc_to(even);                                /* 4a: pop {pc} 짝수 → INVSTATE */
    sh_kv("4a pop{pc} even: cfsr", g_f.cfsr); sh_kv("4a stacked pc", g_f.pc); sh_kv("4a stacked lr", g_f.lr);
    CHECK(g_f.ipsr == 6u && g_f.cfsr == (1u << 17) && g_f.pc == even);
    CHECK(g_f.lr == (FN(pop_pc_ret) | 1u) && g_bx_ran == 1u);
    uint32_t nfault = g_f.n;
    g_expect_fault = 0;                             /* 4b 는 fault 가 나면 안 된다 (나면 exit 3) */
    mov_pc_to(even);                                /* 4b: mov pc, 짝수 → T 그대로, 정상 실행 */
    mov_pc_to(FN(bx_target));                       /*     mov pc, 홀수 → bit0 무시, 역시 정상 */
    CHECK(g_f.n == nfault && g_bx_ran == 3u);

    sh_dec(g_pass); sh_puts("/"); sh_dec(g_total); sh_puts(" passed\n");
    return g_pass == g_total ? 0 : 1;
}
