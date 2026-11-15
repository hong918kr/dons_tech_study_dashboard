/* ENV: qemu */
/* ARM-M12 practice.c — 추가 연습 1~5 의 답 (Cortex-M3, QEMU mps2-an385).
 * 빌드: run_qemu.sh practice → practice.c + practice.S + shared_startup.c + shared_triggers.S
 * 디코더·명령 길이는 solution.c 와 같다 (자체 완결 복사본). 바뀐 것: fault_recover 가
 * (연습 3) 쌓인 xPSR.T 를 보고, (연습 4) IT 블록 안이면 ITSTATE 를 한 칸 진행시킨다.
 */
#include <stdint.h>
#include <stddef.h>

/* ---- shared_startup.c ---- */
void sh_puts(const char *s);
void sh_hex(uint32_t v);
void sh_dec(uint32_t v);
void check(int ok, const char *expr, int line);
extern const char *g_tag;
__attribute__((noreturn)) void finish(void);
#define CHECK(e) check((e) ? 1 : 0, #e, __LINE__)

/* ---- shared_triggers.S ---- */
typedef uint32_t (*trig_t)(uint32_t, uint32_t);
uint32_t run_trigger(trig_t fn, uint32_t a, uint32_t b);
uint32_t run_on_psp(uint32_t *psp_top, uint32_t (*fn)(uint32_t), uint32_t arg);
uint32_t fault_div(uint32_t, uint32_t), fault_ldr(uint32_t, uint32_t), fault_ldrd(uint32_t, uint32_t);
uint32_t fault_udf16(uint32_t, uint32_t), fault_udf32(uint32_t, uint32_t), fault_tbit(uint32_t, uint32_t);
extern const uint16_t fault_div_insn[], fault_ldr_insn[], fault_ldrd_insn[], fault_udf16_insn[],
                      fault_udf32_insn[], trigger_ret[];
uint32_t get_ipsr(void);
/* ---- practice.S ---- */
uint32_t do_svc(void), fault_nocp(uint32_t, uint32_t), fault_tbit2(uint32_t, uint32_t);
uint32_t fault_it_div(uint32_t a, uint32_t b, uint32_t flag), fault_str(uint32_t, uint32_t);
extern const uint16_t svc_udf_insn[], fault_nocp_insn[], fault_it_div_insn[], fault_str_insn[];
uint32_t get_control(void);
#define ADDR(sym) ((uint32_t)(uintptr_t)(sym))

/* ---- SCB fault 레지스터 (ARMv7-M ARM B3.2) ---- */
#define REG32(a)   (*(volatile uint32_t *)(uintptr_t)(a))
#define SCB_SHCSR  REG32(0xE000ED24u)   /* bit16 MEMFAULTENA, 17 BUSFAULTENA, 18 USGFAULTENA */
#define SCB_CCR    REG32(0xE000ED14u)   /* bit3 UNALIGN_TRP, bit4 DIV_0_TRP (켜는 정책 = BM-M13) */
#define SCB_CFSR   REG32(0xE000ED28u)   /* MMFSR[7:0] | BFSR[15:8] | UFSR[31:16], W1C */
#define SCB_HFSR   REG32(0xE000ED2Cu)   /* W1C */
#define SCB_MMFAR  REG32(0xE000ED34u)
#define SCB_BFAR   REG32(0xE000ED38u)
#define SCB_SHPR2  REG32(0xE000ED1Cu)   /* [31:24] = SVCall 우선순위 */
#define SCB_ACTLR  REG32(0xE000E008u)   /* bit1 DISDEFWBUF (M3/M4) */

#define CFSR_IACCVIOL   (1u << 0)
#define CFSR_DACCVIOL   (1u << 1)
#define CFSR_MUNSTKERR  (1u << 3)
#define CFSR_MSTKERR    (1u << 4)
#define CFSR_MMARVALID  (1u << 7)
#define CFSR_IBUSERR    (1u << 8)
#define CFSR_PRECISERR  (1u << 9)
#define CFSR_UNSTKERR   (1u << 11)
#define CFSR_STKERR     (1u << 12)
#define CFSR_BFARVALID  (1u << 15)
#define CFSR_UNDEFINSTR (1u << 16)
#define CFSR_INVSTATE   (1u << 17)
#define CFSR_INVPC      (1u << 18)
#define CFSR_NOCP       (1u << 19)
#define CFSR_UNALIGNED  (1u << 24)
#define CFSR_DIVBYZERO  (1u << 25)
#define HFSR_VECTTBL    (1u << 1)
#define HFSR_FORCED     (1u << 30)
#define HFSR_DEBUGEVT   (1u << 31)
#define CFSR_DEFINED    0x030FBFBBu     /* 이름이 있는 비트 전부 (v7-M, FP 확장 포함) */
#define HFSR_DEFINED    0xC0000002u
#define XPSR_T          (1u << 24)

typedef struct {
    const char *names[24];   /* 켜진 비트의 이름: CFSR bit0→31, 그다음 HFSR VECTTBL, FORCED, DEBUGEVT */
    unsigned    n;
    uint32_t    cfsr_unknown, hfsr_unknown;   /* 켜졌는데 이름 없는 (reserved) 비트 */
    int         mmfar_valid, bfar_valid;
    uint32_t    mmfar, bfar;                  /* *VALID 일 때만 레지스터 값, 아니면 0 */
    int         frame_valid;                  /* (UN)STKERR 류면 0: 쌓인 프레임을 믿지 말 것 */
} fault_info_t;

/* ================= TODO 2: decode_fault ================= */
static const char *const CFSR_NAMES[32] = {
    [0] = "IACCVIOL", [1] = "DACCVIOL", [3] = "MUNSTKERR", [4] = "MSTKERR", [5] = "MLSPERR",
    [7] = "MMARVALID", [8] = "IBUSERR", [9] = "PRECISERR", [10] = "IMPRECISERR", [11] = "UNSTKERR",
    [12] = "STKERR", [13] = "LSPERR", [15] = "BFARVALID", [16] = "UNDEFINSTR", [17] = "INVSTATE",
    [18] = "INVPC", [19] = "NOCP", [24] = "UNALIGNED", [25] = "DIVBYZERO",
};
unsigned decode_fault(uint32_t cfsr, uint32_t hfsr, uint32_t mmfar, uint32_t bfar, fault_info_t *out) {
    out->n = 0;
    for (unsigned b = 0; b < 32u; b++)
        if (((cfsr >> b) & 1u) && CFSR_NAMES[b]) out->names[out->n++] = CFSR_NAMES[b];
    if (hfsr & HFSR_VECTTBL)  out->names[out->n++] = "VECTTBL";
    if (hfsr & HFSR_FORCED)   out->names[out->n++] = "FORCED";
    if (hfsr & HFSR_DEBUGEVT) out->names[out->n++] = "DEBUGEVT";
    out->cfsr_unknown = cfsr & ~CFSR_DEFINED;
    out->hfsr_unknown = hfsr & ~HFSR_DEFINED;
    out->mmfar_valid = (cfsr & CFSR_MMARVALID) != 0u;
    out->bfar_valid  = (cfsr & CFSR_BFARVALID) != 0u;
    out->mmfar = out->mmfar_valid ? mmfar : 0u;
    out->bfar  = out->bfar_valid ? bfar : 0u;
    out->frame_valid = (cfsr & (CFSR_MSTKERR | CFSR_STKERR | CFSR_MUNSTKERR | CFSR_UNSTKERR)) == 0u;
    return out->n;
}

/* ================= TODO 3: thumb_insn_len (ARM-B12 규칙) ================= */
unsigned thumb_insn_len(uint16_t hw1) {
    unsigned top5 = (unsigned)hw1 >> 11;                     /* [15:11] = 0b11101/0b11110/0b11111 → 32 비트 */
    return (top5 >= 0x1Du) ? 4u : 2u;
}

/* ================= 연습 4: it_advance — ITSTATE 한 칸 진행 (Arm ARM 의 ITAdvance) ================= */
/* EPSR 의 IT[7:0] 는 둘로 나뉘어 있다: IT[1:0] = xPSR[26:25], IT[7:2] = xPSR[15:10] */
uint32_t it_get(uint32_t xpsr) { return ((xpsr >> 25) & 3u) | (((xpsr >> 10) & 0x3Fu) << 2); }
uint32_t it_set(uint32_t xpsr, uint32_t it) {
    xpsr &= ~((3u << 25) | (0x3Fu << 10));
    return xpsr | ((it & 3u) << 25) | (((it >> 2) & 0x3Fu) << 10);
}
uint32_t it_advance(uint32_t xpsr) {
    uint32_t it = it_get(xpsr);
    if ((it & 7u) == 0u) it = 0u;                            /* 블록의 마지막 명령이었다 → IT 끝 */
    else it = (it & 0xE0u) | ((it << 1) & 0x1Fu);            /* IT[4:0] <<= 1, IT[7:5] (조건 상위) 유지 */
    return it_set(xpsr, it);
}

/* ================= 연습 3+4: 더 튼튼한 fault_recover ================= */
int g_no_it_advance;                                         /* 연습 4 의 "버그 판" 재현용 스위치 */
unsigned fault_recover(uint32_t *frame, uint32_t cfsr) {
    if ((cfsr & (CFSR_IACCVIOL | CFSR_IBUSERR | CFSR_INVSTATE | CFSR_INVPC)) || !(frame[7] & XPSR_T)) {
        frame[6] = frame[5] & ~1u;                           /* T=0 이면 원인 비트가 무엇이든 "상태" 문제 */
        frame[7] = it_set(frame[7] | XPSR_T, 0u);
        return 0u;
    }
    unsigned len = thumb_insn_len(*(const volatile uint16_t *)(uintptr_t)frame[6]);
    frame[6] += len;
    if (!g_no_it_advance) frame[7] = it_advance(frame[7]);   /* 건너뛴 명령도 IT 슬롯 하나를 쓴다 */
    return len;
}
void fault_clear(uint32_t cfsr, uint32_t hfsr) { SCB_CFSR = cfsr; SCB_HFSR = hfsr; }

/* ---------------- 분석기 (solution.c 와 같은 틀, 출력 축약) ---------------- */
typedef struct {
    uint32_t n, ipsr, exc_return, frame_addr, r0, lr, pc, xpsr, cfsr, hfsr, mmfar, bfar, new_pc;
    unsigned method;
    fault_info_t info;
} fault_rec_t;
fault_rec_t g_rec;
unsigned g_case_faults;
static int in_ram(const uint32_t *p) {
    uint32_t a = ADDR(p);
    return a >= 0x20000000u && a + 32u <= 0x20010000u && (a & 3u) == 0u;
}
static int streq(const char *a, const char *b) {
    while (*a && *a == *b) a++, b++;
    return *a == *b;
}
void hardfault_c(uint32_t *frame, uint32_t exc_return) {
    fault_rec_t *r = &g_rec;
    r->n++;
    g_case_faults++;
    r->ipsr = get_ipsr(); r->exc_return = exc_return; r->frame_addr = ADDR(frame);
    r->cfsr = SCB_CFSR; r->hfsr = SCB_HFSR; r->mmfar = SCB_MMFAR; r->bfar = SCB_BFAR;
    if (!in_ram(frame) || g_case_faults > 3u) { sh_puts("ABORT\n"); finish(); }
    r->r0 = frame[0]; r->lr = frame[5]; r->pc = frame[6]; r->xpsr = frame[7];
    decode_fault(r->cfsr, r->hfsr, r->mmfar, r->bfar, &r->info);
    r->method = fault_recover(frame, r->cfsr);
    if (frame[6] == r->pc) { frame[6] = frame[5] & ~1u; frame[7] |= XPSR_T; }
    r->new_pc = frame[6];
    fault_clear(r->cfsr, r->hfsr);
    sh_puts("  #"); sh_dec(r->n); sh_puts(" IPSR="); sh_dec(r->ipsr); sh_puts(" CFSR="); sh_hex(r->cfsr);
    sh_puts(" HFSR="); sh_hex(r->hfsr); sh_puts(" pc="); sh_hex(r->pc); sh_puts(" lr="); sh_hex(r->lr);
    sh_puts(" xpsr="); sh_hex(r->xpsr); sh_puts(" exc_ret="); sh_hex(exc_return);
    sh_puts(" -> "); sh_puts(r->method ? "skip " : "LR"); if (r->method) sh_dec(r->method); sh_puts("\n");
}

/* 연습 1: 쌓인 xPSR[8:0] = fault 가 끊은 문맥의 예외 번호 (0 = Thread) */
const char *ctx_name(uint32_t stacked_xpsr) {
    switch (stacked_xpsr & 0x1FFu) {
    case 0: return "Thread";
    case 2: return "NMI";
    case 11: return "SVCall";
    case 14: return "PendSV";
    case 15: return "SysTick";
    default: return (stacked_xpsr & 0x1FFu) >= 16u ? "IRQ" : "?";
    }
}

int main(void) {
    sh_puts("ARM-M12 practice (QEMU mps2-an385)\n");
    SCB_CCR |= (1u << 4) | (1u << 3);
    SCB_SHCSR |= (1u << 16) | (1u << 17) | (1u << 18);
    __asm__ volatile("dsb\n isb" ::: "memory");

    /* 연습 1: SVC 핸들러 안의 fault. SVCall 우선순위 0x80 → UsageFault(0) 가 선점 */
    g_tag = "ex1";
    SCB_SHPR2 = 0x80u << 24;
    g_case_faults = 0;
    do_svc();
    CHECK(g_rec.ipsr == 6u && g_rec.exc_return == 0xFFFFFFF1u && g_rec.pc == ADDR(svc_udf_insn));
    CHECK((g_rec.xpsr & 0x1FFu) == 11u && streq(ctx_name(g_rec.xpsr), "SVCall") && g_rec.lr == 0xFFFFFFF9u);
    CHECK(g_rec.method == 2u && g_case_faults == 1u);
    SCB_SHPR2 = 0u;                                           /* 같은 우선순위 0 → 선점 불가 → HardFault */
    g_case_faults = 0;
    do_svc();
    CHECK(g_rec.ipsr == 3u && g_rec.hfsr == HFSR_FORCED && g_rec.cfsr == CFSR_UNDEFINSTR &&
          g_rec.exc_return == 0xFFFFFFF1u && (g_rec.xpsr & 0x1FFu) == 11u);

    /* 연습 2: FPU 없는 M3 에서 VFP 명령 → NOCP */
    g_tag = "ex2";
    g_case_faults = 0;
    uint32_t ret = run_trigger(fault_nocp, 5u, 0u);
    CHECK(g_rec.ipsr == 6u && g_rec.cfsr == CFSR_NOCP && g_rec.pc == ADDR(fault_nocp_insn));
    CHECK(g_rec.method == 4u && ret == 6u && g_case_faults == 1u);

    /* 연습 3: 짝수 목표 (≡ 2 mod 4). QEMU 는 UNALIGNED 를 낸다 → 원인 비트가 아니라 xPSR.T 로 판단 */
    g_tag = "ex3";
    g_case_faults = 0;
    ret = run_trigger((trig_t)(ADDR(fault_tbit2) & ~1u), 0x77u, 0u);
    CHECK(g_rec.pc == (ADDR(fault_tbit2) & ~1u) && (g_rec.xpsr & XPSR_T) == 0u);
    CHECK(g_rec.cfsr == CFSR_UNALIGNED);                       /* QEMU 10.2 실측 (실칩 기대값 = INVSTATE) */
    CHECK(g_rec.method == 0u && ret == 0x77u && g_case_faults == 1u);

    /* 연습 4: IT 블록 안에서 fault → 건너뛸 때 ITSTATE 도 진행 */
    g_tag = "ex4";
    CHECK(it_get(it_set(0u, 0xA5u)) == 0xA5u && it_set(0u, 0xFFu) == 0x0600FC00u);
    CHECK(it_advance(it_set(0x01000000u, 0x18u)) == 0x01000000u);   /* IT[2:0]=000 = 마지막 슬롯 → IT 끝 */
    CHECK(it_get(it_advance(it_set(0u, 0x0Cu))) == 0x18u);          /* ITE EQ: 0000_1100 → 0001_1000 (NE) */
    g_case_faults = 0;
    ret = fault_it_div(5u, 0u, 0u);
    uint32_t it_at_fault = it_get(g_rec.xpsr);
    CHECK(g_rec.cfsr == CFSR_DIVBYZERO && g_rec.pc == ADDR(fault_it_div_insn) && it_at_fault == 0x0Cu);
    CHECK(ret == 6u);                                           /* MOVNE 는 건너뛰어져야: 5 + 1 */
    g_no_it_advance = 1;                                        /* 버그 판: PC 만 옮기고 ITSTATE 는 그대로 */
    ret = fault_it_div(5u, 0u, 0u);
    g_no_it_advance = 0;
    CHECK(ret == 7u);                                           /* MOVNE 가 EQ 로 실행, ADDS 는 NE 로 무시 */
    CHECK(fault_it_div(5u, 0u, 1u) == 8u);                      /* flag != 0: SDIVEQ 무시, MOVNE 실행 → 7+1 */

    /* 연습 5: 저장 fault 와 ACTLR.DISDEFWBUF */
    g_tag = "ex5";
    g_case_faults = 0;
    ret = run_trigger(fault_str, 0x30000004u, 0xABCDu);
    CHECK(g_rec.ipsr == 5u && g_rec.cfsr == (CFSR_PRECISERR | CFSR_BFARVALID) && g_rec.info.bfar == 0x30000004u);
    CHECK(g_rec.pc == ADDR(fault_str_insn) && ret == 0x30000005u);   /* QEMU: 정밀. 실칩: IMPRECISERR 가능 */
    uint32_t a0 = SCB_ACTLR;
    SCB_ACTLR = a0 | 2u;
    __asm__ volatile("dsb\n isb" ::: "memory");
    uint32_t a1 = SCB_ACTLR;
    sh_puts("  ACTLR before="); sh_hex(a0); sh_puts(" after |= DISDEFWBUF: "); sh_hex(a1); sh_puts("\n");
    CHECK(g_rec.n == 7u && a1 == a0);                           /* fault 는 정확히 7 번; QEMU ACTLR = RAZ/WI */
    finish();
}
