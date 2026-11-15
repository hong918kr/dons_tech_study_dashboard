/* ENV: qemu */
/* ARM-M12 — HardFault 분석기: CFSR/HFSR 디코딩과 PC 복원 (Cortex-M3, QEMU mps2-an385).
 * 빌드: run_qemu.sh <v>  →  <v>.c + <v>.S (Fault_Entry, TODO 1) + shared_startup.c + shared_triggers.S
 * TODO 2 decode_fault   TODO 3 thumb_insn_len   TODO 4 fault_recover   TODO 5 fault_clear
 * 출력: 핸들러의 한 줄 보고 + PASS/FAIL 줄, "N/M passed", SYS_EXIT 코드 (0 = 전부 통과).
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

/* ================= TODO 4: fault_recover — 쌓인 PC 를 어디로? ================= */
/* 반환: 2 또는 4 = 그 명령을 건너뜀, 0 = PC 자체가 나빠 호출자(쌓인 LR)로 돌아감 */
unsigned fault_recover(uint32_t *frame, uint32_t cfsr) {
    if (cfsr & (CFSR_IACCVIOL | CFSR_IBUSERR | CFSR_INVSTATE | CFSR_INVPC)) {
        frame[6] = frame[5] & ~1u;                           /* PC = 쌓인 LR (트리거는 leaf 라 LR 이 살아 있다) */
        frame[7] |= XPSR_T;                                  /* INVSTATE 면 T=0 으로 쌓였다 → 안 고치면 또 fault */
        return 0u;
    }
    unsigned len = thumb_insn_len(*(const volatile uint16_t *)(uintptr_t)frame[6]);
    frame[6] += len;                                         /* 다음 명령으로 */
    return len;
}

/* ================= TODO 5: fault_clear — 끈적한(sticky) 상태 비트 지우기 ================= */
void fault_clear(uint32_t cfsr, uint32_t hfsr) {
    SCB_CFSR = cfsr;                                         /* W1C: 읽은 비트만 1 을 써서 지운다 */
    SCB_HFSR = hfsr;
}

/* ================= 여기부터 주어진 코드 (TODO 없음) ================= */
typedef struct {
    uint32_t n, ipsr, exc_return, frame_addr;
    uint32_t r0, lr, pc, xpsr;           /* 하드웨어가 쌓은 값 */
    uint32_t cfsr, hfsr, mmfar, bfar;    /* 진입 순간의 레지스터 */
    uint32_t new_pc;
    unsigned method;                     /* fault_recover 의 반환 */
    int bailed;                          /* 안전망이 대신 LR 로 돌려보냈나 */
    fault_info_t info;
} fault_rec_t;
fault_rec_t g_rec;
unsigned g_case_faults;                  /* 지금 케이스에서 fault 몇 번 */
static uint32_t task_stack[256] __attribute__((aligned(8)));   /* PSP "태스크" 스택 1 KB */

static const char *exc_name(uint32_t ipsr) {
    switch (ipsr) {
    case 3: return "HardFault ";
    case 4: return "MemManage ";
    case 5: return "BusFault  ";
    case 6: return "UsageFault";
    default: return "?         ";
    }
}
static const char *sym_of(uint32_t pc) {             /* 펌웨어 안의 미니 심볼 표 (실전: addr2line/gdb) */
    static const struct { const uint16_t *a; const char *n; } T[] = {
        { fault_div_insn, "fault_div_insn" }, { fault_ldr_insn, "fault_ldr_insn" },
        { fault_ldrd_insn, "fault_ldrd_insn" }, { fault_udf16_insn, "fault_udf16_insn" },
        { fault_udf32_insn, "fault_udf32_insn" }, { trigger_ret, "trigger_ret" },
    };
    for (size_t i = 0; i < sizeof T / sizeof T[0]; i++)
        if (ADDR(T[i].a) == (pc & ~1u)) return T[i].n;
    if (pc == ADDR(fault_tbit) - 1u) return "fault_tbit (even!)";
    return "?";
}
static void names_join(const fault_info_t *f, char *buf, size_t cap) {   /* "PRECISERR BFARVALID" */
    size_t k = 0;
    for (unsigned i = 0; i < f->n; i++) {
        if (i && k + 1 < cap) buf[k++] = ' ';
        for (const char *s = f->names[i]; *s && k + 1 < cap; s++) buf[k++] = *s;
    }
    buf[k] = 0;
}
static int streq(const char *a, const char *b) {
    while (*a && *a == *b) a++, b++;
    return *a == *b;
}
static int in_ram(const uint32_t *p) {
    uint32_t a = ADDR(p);
    return a >= 0x20000000u && a + 32u <= 0x20010000u && (a & 3u) == 0u;
}

void hardfault_c(uint32_t *frame, uint32_t exc_return) {     /* Fault_Entry 가 b 로 넘어온다 */
    fault_rec_t *r = &g_rec;
    r->n++;
    g_case_faults++;
    r->ipsr = get_ipsr();
    r->exc_return = exc_return;
    r->frame_addr = ADDR(frame);
    r->cfsr = SCB_CFSR; r->hfsr = SCB_HFSR; r->mmfar = SCB_MMFAR; r->bfar = SCB_BFAR;
    if (!in_ram(frame) || g_case_faults > 3u) {            /* 안전망 1: 복구가 안 되고 있다 → 보고하고 끝 */
        sh_puts("ABORT: frame="); sh_hex(r->frame_addr); sh_puts(" faults in this case=");
        sh_dec(g_case_faults); sh_puts(" (wrong stack? PC not advanced?)\n");
        finish();
    }
    r->r0 = frame[0]; r->lr = frame[5]; r->pc = frame[6]; r->xpsr = frame[7];
    decode_fault(r->cfsr, r->hfsr, r->mmfar, r->bfar, &r->info);          /* TODO 2 */
    r->method = fault_recover(frame, r->cfsr);                             /* TODO 3, 4 */
#ifdef DEMO_SKIP2
    r->method = 2u; frame[6] = r->pc + 2u;                 /* 7. 실수 2: "Thumb 는 2 바이트" 라고 믿은 판 */
#endif
    r->bailed = 0;
    if (frame[6] == r->pc) {                               /* 안전망 2: PC 그대로면 같은 명령이 또 fault */
        frame[6] = frame[5] & ~1u;
        frame[7] |= XPSR_T;
        r->bailed = 1;
    }
    r->new_pc = frame[6];
    fault_clear(r->cfsr, r->hfsr);                                         /* TODO 5 */

    char nb[160];
    names_join(&r->info, nb, sizeof nb);
    sh_puts("  #"); sh_dec(r->n); sh_puts(" "); sh_puts(exc_name(r->ipsr));
    sh_puts(" ["); sh_puts(nb); sh_puts("] pc="); sh_hex(r->pc); sh_puts(" <"); sh_puts(sym_of(r->pc));
    sh_puts("> lr="); sh_hex(r->lr); sh_puts((exc_return & 4u) ? " PSP" : " MSP");
    sh_puts(" exc_ret="); sh_hex(exc_return);
    if (r->info.bfar_valid) { sh_puts(" BFAR="); sh_hex(r->info.bfar); }
    if (r->info.mmfar_valid) { sh_puts(" MMFAR="); sh_hex(r->info.mmfar); }
    if (r->bailed) sh_puts(" -> (safety net) return to LR\n");
    else if (r->method) { sh_puts(" -> skip "); sh_dec(r->method); sh_puts("\n"); }
    else sh_puts(" -> return to LR\n");
}

/* ---------------- Part A: 디코더 단위 시험 (레지스터 없이, 순수 C) ---------------- */
static const struct { uint8_t hfsr, bit; const char *name; } REF[] = {   /* 시험용 독립 표 */
    {0, 0, "IACCVIOL"}, {0, 1, "DACCVIOL"}, {0, 3, "MUNSTKERR"}, {0, 4, "MSTKERR"}, {0, 5, "MLSPERR"},
    {0, 7, "MMARVALID"}, {0, 8, "IBUSERR"}, {0, 9, "PRECISERR"}, {0, 10, "IMPRECISERR"},
    {0, 11, "UNSTKERR"}, {0, 12, "STKERR"}, {0, 13, "LSPERR"}, {0, 15, "BFARVALID"},
    {0, 16, "UNDEFINSTR"}, {0, 17, "INVSTATE"}, {0, 18, "INVPC"}, {0, 19, "NOCP"},
    {0, 24, "UNALIGNED"}, {0, 25, "DIVBYZERO"}, {1, 1, "VECTTBL"}, {1, 30, "FORCED"}, {1, 31, "DEBUGEVT"},
};
#define NREF (sizeof REF / sizeof REF[0])
static uint32_t xs = 0x12345678u;
static uint32_t xorshift32(void) { xs ^= xs << 13; xs ^= xs >> 17; xs ^= xs << 5; return xs; }

static void test_decoder(void) {
    fault_info_t f;
    unsigned bad = 0;
    g_tag = "A";
    for (unsigned reg = 0; reg < 2u; reg++)                     /* 단일 비트 64 가지 전수 */
        for (unsigned b = 0; b < 32u; b++) {
            const char *want = 0;
            for (size_t i = 0; i < NREF; i++)
                if (REF[i].hfsr == reg && REF[i].bit == b) want = REF[i].name;
            uint32_t v = 1u << b;
            unsigned n = reg ? decode_fault(0, v, 0, 0, &f) : decode_fault(v, 0, 0, 0, &f);
            uint32_t unk = reg ? f.hfsr_unknown : f.cfsr_unknown;
            if (want ? !(n == 1u && streq(f.names[0], want) && unk == 0u) : !(n == 0u && unk == v)) bad++;
        }
    CHECK(bad == 0u);                                             /* 64 단일 비트 */
    CHECK(decode_fault(0, 0, 0x1234u, 0x5678u, &f) == 0u && f.mmfar == 0u && f.bfar == 0u && f.frame_valid);
    decode_fault(CFSR_DACCVIOL, 0, 0x20000040u, 0x30000000u, &f);           /* VALID 없음 → 주소 버림 */
    CHECK(!f.mmfar_valid && f.mmfar == 0u && !f.bfar_valid && f.bfar == 0u);
    decode_fault(CFSR_DACCVIOL | CFSR_MMARVALID, 0, 0x20000040u, 0x30000000u, &f);
    CHECK(f.mmfar_valid && f.mmfar == 0x20000040u && !f.bfar_valid && f.bfar == 0u);
    decode_fault(CFSR_PRECISERR | CFSR_BFARVALID, HFSR_FORCED, 0x11u, 0x30000000u, &f);
    CHECK(f.n == 3u && streq(f.names[0], "PRECISERR") && streq(f.names[1], "BFARVALID") &&
          streq(f.names[2], "FORCED") && f.bfar == 0x30000000u && f.mmfar == 0u);
    decode_fault(CFSR_STKERR, 0, 0, 0, &f);
    CHECK(!f.frame_valid);                                        /* 쌓기 실패 → 프레임 믿지 말 것 */
    bad = 0;                                                      /* 무작위 다중 비트 5000 건 vs 기준 모델 */
    for (unsigned t = 0; t < 5000u; t++) {
        uint32_t c = xorshift32(), h = xorshift32(), mm = xorshift32(), bf = xorshift32();
        if (t & 1u) { c &= xorshift32(); h &= xorshift32(); }    /* 비트가 적은 경우도 */
        unsigned n = decode_fault(c, h, mm, bf, &f), k = 0;
        for (size_t i = 0; i < NREF; i++) {                       /* REF 순서 = CFSR 0→31, HFSR 1,30,31 */
            uint32_t v = REF[i].hfsr ? h : c;
            if ((v >> REF[i].bit) & 1u) {
                if (k >= n || !streq(f.names[k], REF[i].name)) bad++;
                k++;
            }
        }
        if (k != n || f.cfsr_unknown != (c & ~CFSR_DEFINED) || f.hfsr_unknown != (h & ~HFSR_DEFINED)) bad++;
        if (f.bfar != (((c >> 15) & 1u) ? bf : 0u) || f.mmfar != (((c >> 7) & 1u) ? mm : 0u)) bad++;
    }
    CHECK(bad == 0u);
    CHECK(thumb_insn_len(0xDE00u) == 2u && thumb_insn_len(0xF7F0u) == 4u && thumb_insn_len(0xFB90u) == 4u &&
          thumb_insn_len(0xE7FEu) == 2u && thumb_insn_len(0xE800u) == 4u && thumb_insn_len(0x6800u) == 2u);
}

/* ---------------- Part B/C: 실제 fault ---------------- */
typedef struct {
    const char *tag; trig_t fn; uint32_t a, b;
    uint32_t pc, cfsr, exc; const char *names; uint32_t bfar; unsigned method; uint32_t ret;
} fcase_t;
static uint32_t g_buf[4] = { 0x44332211u, 0x88776655u, 0xCCBBAA99u, 0x00FFEEDDu };

static void run_case(const fcase_t *c, int forced) {
    char nb[160], want[64];
    g_tag = c->tag;
    g_case_faults = 0;
    uint32_t n0 = g_rec.n;
    uint32_t ret = run_trigger(c->fn, c->a, c->b);
    const fault_rec_t *r = &g_rec;
    names_join(&r->info, nb, sizeof nb);
    size_t k = 0;                                                 /* want = names (+ " FORCED") */
    for (const char *s = c->names; *s; s++) want[k++] = *s;
    for (const char *s = forced ? " FORCED" : ""; *s; s++) want[k++] = *s;
    want[k] = 0;
    CHECK(r->n == n0 + 1u && r->ipsr == (forced ? 3u : c->exc) && r->hfsr == (forced ? HFSR_FORCED : 0u));
    CHECK(r->cfsr == c->cfsr);                                    /* 정확히 그 비트만 (이전 fault 의 찌꺼기 없이) */
    CHECK(r->pc == c->pc);                                        /* 쌓인 PC = fault 를 낸 명령 */
    CHECK(r->lr == (ADDR(trigger_ret) | 1u) && r->exc_return == 0xFFFFFFF9u);
    CHECK(streq(nb, want) && r->info.bfar == c->bfar && r->info.mmfar == 0u);
    CHECK(!r->bailed && r->method == c->method && ret == c->ret && g_case_faults == 1u);
}

static uint32_t psp_task(uint32_t arg) { return run_trigger(fault_div, arg, 0u); }

int main(void) {
    sh_puts("ARM-M12 HardFault analyzer (QEMU mps2-an385, Cortex-M3)\n");
    test_decoder();

    /* B0: 트랩 끈 상태 (리셋값 CCR = 0x200) — fault 가 안 나는 것들, 그래도 나는 것 */
    g_tag = "B0";
    uint32_t n0 = g_rec.n;
    CHECK(run_trigger(fault_div, 7u, 0u) == 1u && g_rec.n == n0);          /* 7/0 = 0 (+1), fault 없음 */
    CHECK(run_trigger(fault_ldr, ADDR(g_buf) + 1u, 0u) == 0x55443323u && g_rec.n == n0);   /* 비정렬 LDR 허용 */
    g_case_faults = 0;
    run_trigger(fault_ldrd, ADDR(g_buf) + 2u, 0u);                          /* LDRD 는 트랩과 무관하게 fault */
    CHECK(g_rec.n == n0 + 1u && g_rec.cfsr == CFSR_UNALIGNED && g_rec.pc == ADDR(fault_ldrd_insn));

    SCB_CCR |= (1u << 4) | (1u << 3);                    /* DIV_0_TRP | UNALIGN_TRP (정책은 BM-M13) */
    __asm__ volatile("dsb\n isb" ::: "memory");

    fcase_t cs[8] = {
        { "DIV0     ", fault_div, 7u, 0u, ADDR(fault_div_insn), CFSR_DIVBYZERO, 6, "DIVBYZERO", 0, 4, 8u },
        { "UNALIGN  ", fault_ldr, ADDR(g_buf) + 1u, 0u, ADDR(fault_ldr_insn), CFSR_UNALIGNED, 6, "UNALIGNED",
          0, 2, ADDR(g_buf) + 2u },
        { "LDRD     ", fault_ldrd, ADDR(g_buf) + 2u, 0u, ADDR(fault_ldrd_insn), CFSR_UNALIGNED, 6, "UNALIGNED",
          0, 4, ADDR(g_buf) + 3u },
        { "BUS      ", fault_ldr, 0x30000000u, 0u, ADDR(fault_ldr_insn), CFSR_PRECISERR | CFSR_BFARVALID, 5,
          "PRECISERR BFARVALID", 0x30000000u, 2, 0x30000001u },
        { "UDF16    ", fault_udf16, 0u, 0u, ADDR(fault_udf16_insn), CFSR_UNDEFINSTR, 6, "UNDEFINSTR", 0, 2, 11u },
        { "UDF32    ", fault_udf32, 0u, 0u, ADDR(fault_udf32_insn), CFSR_UNDEFINSTR, 6, "UNDEFINSTR", 0, 4, 21u },
        { "XN-EXEC  ", (trig_t)0xE0000001u, 0x1234u, 0u, 0xE0000000u, CFSR_IACCVIOL, 4, "IACCVIOL", 0, 0,
          0x1234u },
        { "INVSTATE ", (trig_t)(ADDR(fault_tbit) & ~1u), 0x5678u, 0u, ADDR(fault_tbit) & ~1u, CFSR_INVSTATE, 6,
          "INVSTATE", 0, 0, 0x5678u },
    };
    sh_puts("== round 1: SHCSR enables OFF -> every fault escalates to HardFault (FORCED) ==\n");
    for (int i = 0; i < 8; i++) run_case(&cs[i], 1);
    SCB_SHCSR |= (1u << 16) | (1u << 17) | (1u << 18);   /* MEMFAULTENA | BUSFAULTENA | USGFAULTENA */
    __asm__ volatile("dsb\n isb" ::: "memory");
    sh_puts("== round 2: SHCSR enables ON -> MemManage / BusFault / UsageFault ==\n");
    for (int i = 0; i < 8; i++) run_case(&cs[i], 0);

    /* C: PSP 위의 "태스크" 에서 fault — 프레임은 PSP 에 있다 */
    sh_puts("== round 3: fault in a thread running on PSP ==\n");
    g_tag = "PSP";
    g_case_faults = 0;
    uint32_t ret = run_on_psp(&task_stack[256], psp_task, 9u);
    CHECK(g_rec.exc_return == 0xFFFFFFFDu && g_rec.ipsr == 6u && g_rec.cfsr == CFSR_DIVBYZERO);
    CHECK(g_rec.frame_addr >= ADDR(task_stack) && g_rec.frame_addr < ADDR(&task_stack[256]));
    CHECK(g_rec.pc == ADDR(fault_div_insn) && g_rec.method == 4u && ret == 10u && get_control() == 0u);
    finish();
}
