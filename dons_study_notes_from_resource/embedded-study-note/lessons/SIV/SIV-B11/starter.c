/* SIV-B11 인터럽트 검증 기초: Trigger-Check-Clear
 *
 * 가상의 MIPI CSI-2 수신기 IP(CSI2RX)와 인터럽트 컨트롤러(INTC)를 host 배열 대신 switch 로 흉내 낸 mock 위에서,
 * 각 인터럽트 소스를 FORCE(test) 레지스터로 강제로 세우고(trigger) → RAW / masked STAT / IRQ 라인을
 * 확인하고(check) → W1C 로 지운 뒤 모두 내려갔는지(clear) 확인하는 테스트 매트릭스를 만든다.
 * mock 에는 fault_t 로 결함(mask 무시, W1C 불량, 라우팅 오류 ...)을 주입할 수 있고, 테스트는
 * "좋은 mock 은 통과, 결함 mock 은 정확한 소스·단계·비트로 실패" 를 둘 다 보여야 한다.
 * (SIV-B02 의 mock 을 이 IP 에 필요한 만큼만 축약해 넣었다. 실보드에서는 reg_read32/reg_write32 가
 *  *(volatile uint32_t *)(BASE + off) 로 바뀐다.)
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ================= 레지스터 맵: CSI2RX IP + INTC ================= */
#define IP_NAME    "CSI2RX"
#define IRQ_RAW    0x000u   /* W1C: raw status. 이벤트가 나면 enable 과 무관하게 1        */
#define IRQ_EN     0x004u   /* RW : 1 = 소스 enable (unmask)                               */
#define IRQ_STAT   0x008u   /* RO : masked status = RAW & EN                               */
#define IRQ_FORCE  0x00Cu   /* WO : 1 을 쓴 소스의 RAW 를 세운다 (test 레지스터), 읽으면 0 */
#define IP_CTRL    0x010u   /* bit0 SRST: 1 쓰면 RAW/EN 을 리셋값(0)으로, 자동 복귀          */
#define INTC_RAW   0x800u   /* RO : INTC 입력 라인 레벨 (입력 n = bit n)                    */

#define CTRL_SRST  (1u << 0)
#define IRQ_ID     12u                  /* CSI2RX irq 출력이 연결돼야 하는 INTC 입력 번호 */
#define LINE_BIT   (1u << IRQ_ID)

#define SRC_FS     0u   /* FRAME_START */
#define SRC_FE     1u   /* FRAME_END   */
#define SRC_CRC    2u   /* CRC_ERR     (payload CRC) */
                        /* bit 3 reserved */
#define SRC_ECC    4u   /* ECC_ERR     (packet header ECC) */
#define SRC_OVF    5u   /* FIFO_OVF    (line buffer overflow) */
#define VALID_MASK 0x37u
#define NO_SRC     32u  /* 특정 소스가 아닌 실패 (crosstalk 전체 단계) */

static const char *const SRC_NAME[33] = {
    [0] = "FRAME_START", [1] = "FRAME_END", [2] = "CRC_ERR", [4] = "ECC_ERR", [5] = "FIFO_OVF", [32] = "ALL"};

/* ================= mock (주어진 코드 — 고치지 않는다) ================= */
typedef struct {
    uint32_t mask_ignored;       /* 이 소스들은 enable 게이트 고장: EN=0 이어도 STAT/irq 에 보인다 */
    bool     line_from_raw;      /* irq 출력을 STAT 가 아니라 RAW 의 OR 로 만듦                     */
    uint32_t clear_stuck;        /* W1C 를 써도 안 지워지는 소스                                    */
    bool     w1c_as_rw;          /* RAW 가 W1C 가 아니라 RW 로 구현됨                               */
    uint32_t force_dead;         /* FORCE 로 세울 수 없는 소스 (test 로직 미연결)                   */
    int      route_delta;        /* irq 가 INTC 입력 IRQ_ID + route_delta 에 연결됨 (0 = 정상)      */
    bool     disable_clears_raw; /* EN 비트를 끄면 그 소스의 pending 이 사라짐                       */
} fault_t;

static struct { uint32_t raw, en; fault_t f; unsigned nacc; } M;

static void mock_init(const fault_t *f) {
    memset(&M, 0, sizeof M);
    if (f) M.f = *f;
}
static uint32_t stat_now(void) { return M.raw & (M.en | M.f.mask_ignored) & VALID_MASK; }
static bool irq_out(void) { return M.f.line_from_raw ? M.raw != 0 : stat_now() != 0; }

static uint32_t reg_read32(uint32_t off) {
    M.nacc++;
    switch (off) {
    case IRQ_RAW:   return M.raw;
    case IRQ_EN:    return M.en;
    case IRQ_STAT:  return stat_now();
    case IRQ_FORCE: return 0;                 /* WO: read-as-zero */
    case IP_CTRL:   return 0;                 /* SRST self-clearing */
    case INTC_RAW:  return irq_out() ? 1u << (IRQ_ID + (unsigned)M.f.route_delta) : 0;
    default:        return 0xDEADBEEFu;       /* 디코드 안 되는 주소 */
    }
}
static void reg_write32(uint32_t off, uint32_t v) {
    M.nacc++;
    switch (off) {
    case IRQ_RAW:
        if (M.f.w1c_as_rw) M.raw = v & VALID_MASK;
        else               M.raw &= ~(v & VALID_MASK & ~M.f.clear_stuck);
        break;
    case IRQ_EN: {
        uint32_t nen = v & VALID_MASK;        /* reserved 비트는 저장 안 됨 */
        if (M.f.disable_clears_raw) M.raw &= ~(M.en & ~nen);
        M.en = nen;
        break;
    }
    case IRQ_FORCE: M.raw |= v & VALID_MASK & ~M.f.force_dead; break;
    case IP_CTRL:   if (v & CTRL_SRST) { M.raw = 0; M.en = 0; } break;
    default:        break;                    /* RO / 미디코드: 무시 */
    }
}
static void ip_soft_reset(void) { reg_write32(IP_CTRL, CTRL_SRST); }

/* ================= 테스트 공통 타입 (주어진 코드) ================= */
enum { EN_NONE, EN_SELF, EN_OTHERS, EN_ALL };
static const char *const MODE_NAME[] = {"NONE", "SELF", "OTHERS", "ALL"};

typedef struct { unsigned src; int mode; uint32_t en; } tcc_case_t;

enum { ST_PRE, ST_RAW, ST_STAT, ST_LINE, ST_CLR_RAW, ST_CLR_STAT, ST_CLR_LINE,
       ST_MASK_RAW, ST_MASK_STAT, ST_MASK_LINE, ST_UNMASK_STAT, ST_UNMASK_LINE,
       ST_ALL_RAW, ST_XCLR };
static const char *const STEP_NAME[] = {"PRE", "RAW", "STAT", "LINE", "CLR_RAW", "CLR_STAT", "CLR_LINE",
                                        "MASK_RAW", "MASK_STAT", "MASK_LINE", "UNMASK_STAT", "UNMASK_LINE",
                                        "ALL_RAW", "XCLR"};

typedef struct { const char *test; unsigned src; int mode; int step; uint32_t off, exp, act; } fail_t;

/* 한 번 읽어 기대값과 비교. 다르면 f 에 단계·주소·기대·실제를 남기고 false */
static bool expect(fail_t *f, int step, uint32_t off, uint32_t exp) {
    uint32_t act = reg_read32(off);
    if (act == exp) return true;
    f->step = step; f->off = off; f->exp = exp; f->act = act;
    return false;
}

/* ================= Part A: 매트릭스 생성 + Trigger-Check-Clear ================= */
/* valid 의 각 소스(오름차순) × {NONE, SELF, OTHERS} 케이스를 out 에 채우고 개수를 돌려준다.
 * en: NONE = 0, SELF = 1<<s, OTHERS = valid & ~(1<<s).  max 를 넘으면 -1. */
static int build_matrix(uint32_t valid, tcc_case_t *out, int max) {
    (void)valid; (void)out; (void)max;
    return 0; /* TODO: valid 의 비트마다 NONE/SELF/OTHERS 세 케이스, 넘치면 -1 */
}

/* 케이스 하나: PRE(RAW==0) → EN 설정 → FORCE(trigger) → RAW/STAT/LINE 확인 → W1C clear →
 * RAW/STAT/LINE 모두 0 확인. 첫 실패에서 멈추고 f 를 채운다. 끝나면 항상 soft reset 으로 격리. */
static bool tcc_run_case(const tcc_case_t *c, fail_t *f) {
    f->test = "tcc"; f->src = c->src; f->mode = c->mode;
    bool ok = expect(f, ST_PRE, IRQ_RAW, 0);
    /* TODO: EN=c->en → FORCE=bit (trigger)
     *       → expect RAW==bit, STAT==en&bit, INTC_RAW==(en&bit ? LINE_BIT : 0)   (check)
     *       → W1C bit → expect RAW/STAT/INTC_RAW 모두 0                           (clear)
     *       첫 실패에서 멈춘다 (ok 가 false 면 다음 단계로 가지 않는다) */
    ok = false;
    ip_soft_reset();
    return ok;
}

/* ================= Part B: mask 토글 · crosstalk ================= */
/* pending 상태에서 mask → pending 유지·STAT/LINE 내려감 → unmask → 즉시 다시 올라감 → clear */
static bool mask_toggle_test(unsigned src, fail_t *f) {
    f->test = "mask"; f->src = src; f->mode = EN_SELF;
    /* TODO: PRE → EN=bit, FORCE=bit → RAW, LINE 확인
     *       → EN=0: MASK_RAW(pending 유지) / MASK_STAT=0 / MASK_LINE=0
     *       → EN=bit: UNMASK_STAT=bit / UNMASK_LINE=LINE_BIT → W1C → CLR_RAW / CLR_LINE, soft reset */
    return false;
}

/* 전부 enable + FORCE 0xFFFFFFFF(reserved 포함) → RAW == valid, LINE=1 →
 * 소스를 하나씩(오름차순) W1C: 지운 비트만 사라지고 나머지는 남아야 하며, LINE 은 마지막에만 내려간다 */
static bool crosstalk_test(uint32_t valid, fail_t *f) {
    f->test = "xtalk"; f->src = NO_SRC; f->mode = EN_ALL;
    /* TODO: PRE → EN=valid, FORCE=0xFFFFFFFF → ALL_RAW==valid, LINE=1
     *       → 소스마다(오름차순, f->src=s) W1C 1<<s → XCLR: RAW==남은 비트, LINE 은 남은 게 있을 때만 1
     *       끝나면 soft reset */
    (void)valid;
    return false;
}

/* ================= Part C: 실패 → 원인 분류 ================= */
enum { C_NONE, C_STALE, C_TRIGGER, C_SPURIOUS, C_MASK_IGNORED, C_ENABLE_DEAD, C_LINE_UNGATED,
       C_LINE_STUCK, C_LINE_DEAD, C_ROUTING, C_CLEAR_STUCK, C_CROSS_CLEAR, C_PENDING_LOST, C_COUNT };
static const char *const CAUSE_NAME[] = {"NONE", "STALE_STATE", "TRIGGER_PATH", "SPURIOUS_RAW", "MASK_IGNORED",
                                         "ENABLE_DEAD", "LINE_UNGATED", "LINE_STUCK", "LINE_DEAD", "IRQ_ROUTING",
                                         "CLEAR_STUCK", "CROSS_CLEAR", "PENDING_LOST"};

/* 실패 기록 하나를 보고 가장 그럴듯한 원인을 고른다 (step + 빠진/남는 비트) */
static int classify(const fail_t *f) {
    (void)f;
    return C_NONE; /* TODO: step 별로 missing(exp&~act) / extra(act&~exp) 를 보고 원인 선택 */
}

/* ================= 스위트 실행과 리포트 (주어진 코드) ================= */
#define MAXLOG 64
typedef struct { fail_t log[MAXLOG]; int n, runs; unsigned nacc; char grid[33][4]; char xtalk; } suite_t;

static void log_result(suite_t *S, bool ok, const fail_t *f) {
    S->runs++;
    if (!ok && S->n < MAXLOG) S->log[S->n++] = *f;
}
static void run_suite(const fault_t *flt, suite_t *S) {
    tcc_case_t cases[96];
    memset(S, 0, sizeof *S);
    memset(S->grid, '-', sizeof S->grid);
    mock_init(flt);
    int n = build_matrix(VALID_MASK, cases, 96);
    for (int i = 0; i < n; i++) {
        fail_t f = {0};
        bool ok = tcc_run_case(&cases[i], &f);
        S->grid[cases[i].src][cases[i].mode] = ok ? 'P' : 'F';
        log_result(S, ok, &f);
    }
    for (unsigned s = 0; s < 32; s++) {
        if (!(VALID_MASK & (1u << s))) continue;
        fail_t f = {0};
        bool ok = mask_toggle_test(s, &f);
        S->grid[s][3] = ok ? 'P' : 'F';
        log_result(S, ok, &f);
    }
    fail_t f = {0};
    bool ok = crosstalk_test(VALID_MASK, &f);
    S->xtalk = ok ? 'P' : 'F';
    log_result(S, ok, &f);
    S->nacc = M.nacc;
}
static const char *reg_name(uint32_t off) {
    switch (off) {
    case IRQ_RAW: return "IRQ_RAW"; case IRQ_EN: return "IRQ_EN"; case IRQ_STAT: return "IRQ_STAT";
    case IRQ_FORCE: return "IRQ_FORCE"; case IP_CTRL: return "IP_CTRL"; case INTC_RAW: return "INTC_RAW";
    default: return "?";
    }
}
static void print_fail(const fail_t *f) {
    uint32_t diff = f->exp ^ f->act;
    char bits[128] = "";
    size_t k = 0;
    for (unsigned b = 0; b < 32 && k < sizeof bits - 4; b++)
        if (diff & (1u << b)) k += (size_t)snprintf(bits + k, sizeof bits - k, "%s%u", k ? "," : "", b);
    unsigned s = f->src <= 32 ? f->src : 32;
    printf("FAIL %s %s src=%s(%u) en=%s step=%s reg=%s off=0x%03X exp=0x%08X act=0x%08X diff=0x%08X bits=[%s] cause=%s\n",
           IP_NAME, f->test ? f->test : "?", SRC_NAME[s] ? SRC_NAME[s] : "?", s,
           MODE_NAME[f->mode & 3], STEP_NAME[f->step], reg_name(f->off), (unsigned)f->off,
           (unsigned)f->exp, (unsigned)f->act, (unsigned)diff, bits, CAUSE_NAME[classify(f)]);
}
static void print_grid(const suite_t *S) {
    printf("  %-12s NONE SELF OTHERS MASK\n", "src");
    for (unsigned s = 0; s < 32; s++)
        if (VALID_MASK & (1u << s))
            printf("  %-12s %c    %c    %c      %c\n", SRC_NAME[s],
                   S->grid[s][0], S->grid[s][1], S->grid[s][2], S->grid[s][3]);
    printf("  %-12s %c\n", "crosstalk", S->xtalk ? S->xtalk : '-');
}

/* ================= 테스트 하네스 ================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static bool run_case(const fault_t *flt, unsigned src, int mode, fail_t *f) {
    tcc_case_t c = {src, mode, mode == EN_NONE ? 0 : mode == EN_SELF ? 1u << src : VALID_MASK & ~(1u << src)};
    mock_init(flt);
    memset(f, 0, sizeof *f);
    return tcc_run_case(&c, f);
}
static int count_cause(const suite_t *S, int cause) {
    int n = 0;
    for (int i = 0; i < S->n; i++) n += classify(&S->log[i]) == cause;
    return n;
}

int main(void) {
    fail_t f;
    tcc_case_t cs[96];

    /* ---- mock sanity (주어진 코드가 스펙대로인지) ---- */
    mock_init(NULL);
    CHECK(reg_read32(IRQ_RAW) == 0 && reg_read32(IRQ_EN) == 0 && reg_read32(INTC_RAW) == 0);
    reg_write32(IRQ_EN, 0xFFFFFFFFu);
    CHECK(reg_read32(IRQ_EN) == VALID_MASK);                    /* reserved 비트는 0 으로 읽힘 */
    reg_write32(IRQ_FORCE, 1u << SRC_FE);
    CHECK(reg_read32(IRQ_FORCE) == 0 && reg_read32(IRQ_STAT) == (1u << SRC_FE));
    CHECK(reg_read32(0x7FC) == 0xDEADBEEFu);

    /* ---- Part A: 매트릭스 생성 ---- */
    int n = build_matrix(VALID_MASK, cs, 96);
    CHECK(n == 15);
    CHECK(cs[0].src == 0 && cs[0].mode == EN_NONE && cs[0].en == 0);
    CHECK(cs[1].src == 0 && cs[1].mode == EN_SELF && cs[1].en == 0x01);
    CHECK(cs[2].src == 0 && cs[2].mode == EN_OTHERS && cs[2].en == 0x36);
    CHECK(cs[9].src == SRC_ECC && cs[9].mode == EN_NONE);       /* bit 3(reserved) 은 건너뜀 */
    CHECK(cs[14].src == SRC_OVF && cs[14].en == 0x17);
    CHECK(build_matrix(VALID_MASK, cs, 10) == -1);
    CHECK(build_matrix(0, cs, 96) == 0);

    /* ---- Part A: 좋은 mock 은 15 케이스 모두 통과 ---- */
    n = build_matrix(VALID_MASK, cs, 96);
    mock_init(NULL);
    int pass = 0;
    for (int i = 0; i < n; i++) { memset(&f, 0, sizeof f); pass += tcc_run_case(&cs[i], &f); }
    CHECK(n == 15 && pass == 15);
    CHECK(M.raw == 0 && M.en == 0);                             /* 끝나면 깨끗 */

    /* ---- Part A: mask 무시 결함 (CRC_ERR) — task 가 요구한 주입 ---- */
    fault_t fm = {.mask_ignored = 1u << SRC_CRC};
    CHECK(!run_case(&fm, SRC_CRC, EN_NONE, &f));
    CHECK(f.step == ST_STAT && f.off == IRQ_STAT && f.exp == 0 && f.act == (1u << SRC_CRC));
    CHECK(run_case(&fm, SRC_CRC, EN_SELF, &f));
    CHECK(!run_case(&fm, SRC_CRC, EN_OTHERS, &f) && f.step == ST_STAT);
    CHECK(run_case(&fm, SRC_FS, EN_NONE, &f));                  /* 다른 소스는 영향 없음 */
    /* 실패한 케이스 뒤에도 다음 케이스는 PRE 를 통과해야 한다 (soft reset 격리) */
    mock_init(&fm);
    tcc_case_t c_bad = {SRC_CRC, EN_NONE, 0}, c_next = {SRC_FS, EN_SELF, 1u << SRC_FS};
    memset(&f, 0, sizeof f);
    CHECK(!tcc_run_case(&c_bad, &f));
    memset(&f, 0, sizeof f);
    CHECK(tcc_run_case(&c_next, &f));
    /* trigger 경로 결함: FORCE 가 FRAME_END 를 못 세움 */
    fault_t fd = {.force_dead = 1u << SRC_FE};
    CHECK(!run_case(&fd, SRC_FE, EN_SELF, &f));
    CHECK(f.step == ST_RAW && f.exp == 0x2 && f.act == 0);

    /* ---- Part B: mask 토글 / crosstalk 은 좋은 mock 에서 통과 ---- */
    mock_init(NULL);
    int mp = 0;
    for (unsigned s = 0; s < 32; s++)
        if (VALID_MASK & (1u << s)) { memset(&f, 0, sizeof f); mp += mask_toggle_test(s, &f); }
    CHECK(mp == 5);
    memset(&f, 0, sizeof f);
    CHECK(crosstalk_test(VALID_MASK, &f));
    CHECK(M.raw == 0 && M.en == 0);

    /* disable 하면 pending 이 사라지는 결함: 매트릭스는 못 잡는다(EN 을 trigger 전에만 쓰므로) */
    fault_t fl = {.disable_clears_raw = true};
    mock_init(&fl);
    pass = 0;
    for (int i = 0; i < n; i++) { memset(&f, 0, sizeof f); pass += tcc_run_case(&cs[i], &f); }
    CHECK(pass == 15);                                          /* 매트릭스의 사각지대 */
    mock_init(&fl);
    memset(&f, 0, sizeof f);
    CHECK(!mask_toggle_test(SRC_CRC, &f));
    CHECK(f.step == ST_MASK_RAW && f.exp == 0x4 && f.act == 0);

    /* irq 출력이 mask 를 안 거침: STAT 는 맞는데 LINE 이 틀림 */
    fault_t fr = {.line_from_raw = true};
    CHECK(run_case(&fr, SRC_FS, EN_SELF, &f));
    CHECK(!run_case(&fr, SRC_FS, EN_NONE, &f));
    CHECK(f.step == ST_LINE && f.off == INTC_RAW && f.exp == 0 && f.act == LINE_BIT);
    mock_init(&fr);
    memset(&f, 0, sizeof f);
    CHECK(!mask_toggle_test(SRC_ECC, &f) && f.step == ST_MASK_LINE);

    /* W1C 가 RW 로 구현됨: 하나 지우면 나머지가 다 사라진다 */
    fault_t fw = {.w1c_as_rw = true};
    mock_init(&fw);
    memset(&f, 0, sizeof f);
    CHECK(!crosstalk_test(VALID_MASK, &f));
    CHECK(f.step == ST_XCLR && f.src == SRC_FS && f.exp == 0x36 && f.act == 0x01);
    /* FIFO_OVF 가 안 지워짐: crosstalk 마지막 단계에서 */
    fault_t fs = {.clear_stuck = 1u << SRC_OVF};
    mock_init(&fs);
    memset(&f, 0, sizeof f);
    CHECK(!crosstalk_test(VALID_MASK, &f));
    CHECK(f.step == ST_XCLR && f.src == SRC_OVF && f.exp == 0 && f.act == 0x20);

    /* ---- Part C: classify 단위 테스트 (손으로 만든 실패 기록) ---- */
    struct { fail_t f; int cause; } ct[] = {
        {{"tcc", 2, EN_NONE, ST_STAT, IRQ_STAT, 0x0, 0x4}, C_MASK_IGNORED},
        {{"tcc", 2, EN_SELF, ST_STAT, IRQ_STAT, 0x4, 0x0}, C_ENABLE_DEAD},
        {{"tcc", 1, EN_SELF, ST_RAW, IRQ_RAW, 0x2, 0x0}, C_TRIGGER},
        {{"tcc", 1, EN_SELF, ST_RAW, IRQ_RAW, 0x2, 0x6}, C_SPURIOUS},
        {{"tcc", 0, EN_SELF, ST_LINE, INTC_RAW, LINE_BIT, LINE_BIT << 1}, C_ROUTING},
        {{"tcc", 0, EN_SELF, ST_LINE, INTC_RAW, LINE_BIT, 0}, C_LINE_DEAD},
        {{"tcc", 0, EN_NONE, ST_LINE, INTC_RAW, 0, LINE_BIT}, C_LINE_UNGATED},
        {{"tcc", 0, EN_SELF, ST_CLR_LINE, INTC_RAW, 0, LINE_BIT}, C_LINE_STUCK},
        {{"tcc", 5, EN_NONE, ST_CLR_RAW, IRQ_RAW, 0x0, 0x20}, C_CLEAR_STUCK},
        {{"xtalk", 0, EN_ALL, ST_XCLR, IRQ_RAW, 0x36, 0x01}, C_CROSS_CLEAR},
        {{"mask", 2, EN_SELF, ST_MASK_RAW, IRQ_RAW, 0x4, 0x0}, C_PENDING_LOST},
        {{"tcc", 0, EN_NONE, ST_PRE, IRQ_RAW, 0x0, 0x20}, C_STALE},
    };
    int cok = 0;
    for (size_t i = 0; i < sizeof ct / sizeof ct[0]; i++) cok += classify(&ct[i].f) == ct[i].cause;
    CHECK(cok == (int)(sizeof ct / sizeof ct[0]));

    /* ---- Part C: 결함 주입 캠페인 ---- */
    static suite_t S;
    run_suite(NULL, &S);
    CHECK(S.runs == 21 && S.n == 0);
    printf("[good] runs=%d failures=%d bus_accesses=%u\n", S.runs, S.n, S.nacc);

    struct { const char *name; fault_t f; int nfail; const char *test; unsigned src; int step; int cause; } camp[] = {
        {"mask_ignored(CRC_ERR)", {.mask_ignored = 1u << SRC_CRC}, 3, "tcc", SRC_CRC, ST_STAT, C_MASK_IGNORED},
        {"line_from_raw",         {.line_from_raw = true}, 15, "tcc", SRC_FS, ST_LINE, C_LINE_UNGATED},
        {"clear_stuck(FIFO_OVF)", {.clear_stuck = 1u << SRC_OVF}, 5, "tcc", SRC_OVF, ST_CLR_RAW, C_CLEAR_STUCK},
        {"w1c_as_rw",             {.w1c_as_rw = true}, 21, "tcc", SRC_FS, ST_CLR_RAW, C_CLEAR_STUCK},
        {"route_delta=+1",        {.route_delta = 1}, 11, "tcc", SRC_FS, ST_LINE, C_ROUTING},
        {"force_dead(FRAME_END)", {.force_dead = 1u << SRC_FE}, 5, "tcc", SRC_FE, ST_RAW, C_TRIGGER},
        {"disable_clears_raw",    {.disable_clears_raw = true}, 5, "mask", SRC_FS, ST_MASK_RAW, C_PENDING_LOST},
    };
    for (size_t i = 0; i < sizeof camp / sizeof camp[0]; i++) {
        run_suite(&camp[i].f, &S);
        printf("[%s] failures=%d/%d first: ", camp[i].name, S.n, S.runs);
        if (S.n) print_fail(&S.log[0]); else printf("(none)\n");
        CHECK(S.n == camp[i].nfail);
        CHECK(S.n > 0 && strcmp(S.log[0].test, camp[i].test) == 0 && S.log[0].src == camp[i].src &&
              S.log[0].step == camp[i].step && classify(&S.log[0]) == camp[i].cause);
    }
    /* w1c_as_rw: CLEAR_STUCK 20 개 + crosstalk 의 CROSS_CLEAR 1 개 → 둘을 함께 봐야 진짜 원인이 보인다 */
    run_suite(&camp[3].f, &S);
    CHECK(count_cause(&S, C_CLEAR_STUCK) == 20 && count_cause(&S, C_CROSS_CLEAR) == 1);

    /* 리포트: task 가 요구한 mask 무시 결함 */
    run_suite(&camp[0].f, &S);
    CHECK(count_cause(&S, C_MASK_IGNORED) == 3);
    printf("---- report: %s fault=%s ----\n", IP_NAME, camp[0].name);
    for (int i = 0; i < S.n; i++) print_fail(&S.log[i]);
    print_grid(&S);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
