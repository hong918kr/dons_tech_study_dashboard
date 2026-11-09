/* SIV-A10 LFSR BIST와 MISR 시그니처
 *
 * Logic BIST(LBIST) 의 세 부품을 C 로 모델링하고 실제로 재 본다.
 *   PRPG  : 원시 다항식 LFSR (Galois/Fibonacci) → 의사 랜덤 패턴
 *   CUT   : 조합 회로 넷리스트 모델 + 넷 stuck-at 결함 주입
 *   MISR  : 응답을 n비트 시그니처로 압축 → golden 비교
 *   Part A  LFSR 주기(2^n-1), CUT/결함 시뮬, MISR, 커버리지 곡선
 *   Part B  aliasing 을 실측(정확 열거 + Monte-Carlo) vs 2^-n, 단일비트 오류는 절대 alias 안 됨,
 *           random-pattern-resistant 결함(넓은 AND) → weighted pattern / test point
 *   Part C  mock LBIST0 레지스터에 대한 펌웨어 시퀀스(시작 → poll+timeout → 비교 → 재실행 분류),
 *           X-source 마스킹, done-bit 고장, 틀린 시그니처, 고장 패턴 구간 찾기
 *
 * 다항식 표기: poly 의 bit i = x^i 의 계수 (bit n 과 bit 0 포함). 예) x^4+x+1 = 0x13.
 * 상태를 다항식 S(x) 로 보면 Galois 한 스텝은 S ← x·S mod P(x).
 * RAM 용 March 알고리즘은 SIV-B07 이다 (여기는 조합 논리 BIST). PRBS 링크 시험은 SIV-M06.
 * 결정성: 모든 난수는 고정 시드 xorshift64. 벽시계 시간은 안 쓴다 (mock 은 가짜 사이클 카운터).
 */
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MAYBE_UNUSED __attribute__((unused))

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } } while (0)

/* ---------------- 결정적 난수 ---------------- */
static uint64_t g_rng = 0x9E3779B97F4A7C15ull;
static void rng_seed(uint64_t s) { g_rng = s ? s : 0x9E3779B97F4A7C15ull; }
static uint64_t xs64(void) { uint64_t x = g_rng; x ^= x << 13; x ^= x >> 7; x ^= x << 17; return g_rng = x; }

static uint32_t low_mask(int n) { return n >= 32 ? 0xFFFFFFFFu : (1u << n) - 1u; }
static uint32_t parity32(uint32_t v) { v ^= v >> 16; v ^= v >> 8; v ^= v >> 4; v ^= v >> 2; v ^= v >> 1; return v & 1u; }

/* ================= Part A-1: LFSR ================= */
/* Galois(내부 XOR, left-shift): S ← x·S mod P. 상위 비트가 넘치면 P 를 뺀다(XOR). */
static uint32_t galois_step(uint32_t s, uint32_t poly, int n) {
    (void)s; (void)poly; (void)n; return 0;   /* TODO(A1): 왼쪽 시프트, bit n 이 서면 poly XOR */
}

/* Fibonacci(외부 XOR): 상태의 bit0 = 가장 최근 출력, bit(n-1) = 다음에 나갈 비트.
 * 점화식 a[t+n] = Σ c_i a[t+i] (P = x^n + Σ c_i x^i) 이므로 a[t+i] 는 bit(n-1-i) 에 있다 → 탭 = bit(n-1-i). */
static uint32_t fib_step(uint32_t s, uint32_t poly, int n) {
    (void)s; (void)poly; (void)n; (void)parity32; return 0;   /* TODO(A2): 탭 마스크 → parity → shift-in */
}

typedef uint32_t (*step_fn)(uint32_t, uint32_t, int);

/* seed 에서 출발해 seed 로 돌아올 때까지의 스텝 수. 2^n 스텝 안에 안 돌아오면 0 (P(0)=0 등 비가역).
 * 0 시드는 Galois/Fibonacci 모두 고정점 → 주기 1 을 돌려준다. */
static uint64_t lfsr_period(step_fn step, uint32_t poly, int n, uint32_t seed) {
    (void)step; (void)poly; (void)n; (void)seed; return 0;   /* TODO(A3) */
}

/* 다항식 표 — 전부 시험에서 주기로 확인한다 */
static const struct { int n; uint32_t poly; } PRIM[] = {
    {2, 0x7}, {3, 0xB}, {4, 0x13}, {5, 0x25}, {6, 0x43}, {7, 0x83}, {8, 0x11D}, {9, 0x211}, {10, 0x409},
    {11, 0x805}, {12, 0x1053}, {13, 0x201B}, {14, 0x4443}, {15, 0x8003}, {16, 0x1100B}, {20, 0x100009},
};
#define NPRIM ((int)(sizeof PRIM / sizeof PRIM[0]))
#define P4   0x13u
#define P16  0x1100Bu
#define P20  0x100009u

/* ================= Part A-2: 패턴 원천 ================= */
/* uniform  : 패턴 = LFSR 상태의 하위 npi 비트 (test-per-clock)
 * weighted : PI 마다 LFSR 출력 wbits 개를 OR(1 쪽 가중, P(1)=1-2^-w) 또는 AND(0 쪽, P(1)=2^-w) */
typedef enum { PS_UNIFORM, PS_WEIGHTED } ps_kind_t;
typedef struct { ps_kind_t kind; int n; uint32_t poly, s; int wbits; bool ones; } patsrc_t;

static patsrc_t lfsr_src(int n, uint32_t poly, uint32_t seed) {
    patsrc_t p = { PS_UNIFORM, n, poly, seed, 0, false }; return p;
}
static patsrc_t weighted_src(int n, uint32_t poly, uint32_t seed, int wbits, bool ones) {
    patsrc_t p = { PS_WEIGHTED, n, poly, seed, wbits, ones }; return p;
}

static uint32_t ps_next(patsrc_t *ps, int npi) {
    (void)ps; (void)npi; return 0;   /* TODO(A4): uniform → 하위 npi 비트 후 1 스텝. weighted → 위 주석대로 */
}

/* ================= Part A-3: 조합 회로(CUT) 와 stuck-at ================= */
#define MAX_GATES 96
#define MAX_NETS  128
#define MAX_PO    12
typedef enum { G_AND, G_OR, G_XOR, G_NOT, G_NOR } gate_op_t;
typedef struct { uint8_t op; uint16_t a, b; } gate_t;     /* 출력 넷 번호 = npi + 게이트 인덱스 */
typedef struct {
    const char *name;
    int npi, ngate, npo;
    gate_t g[MAX_GATES];
    uint16_t po[MAX_PO];
} cut_t;
typedef struct { int net, val; } fault_t;                 /* net < 0 → 결함 없음 */
static const fault_t NO_FAULT = { -1, 0 };

/* pi 의 bit i = PI[i]. 게이트는 위상순(앞 넷만 참조). 결함 넷은 값이 정해지자마자 val 로 덮어쓴다.
 * 반환: bit k = PO[k]. */
static uint32_t cut_eval(const cut_t *c, uint32_t pi, fault_t f) {
    (void)c; (void)pi; (void)f; return 0;   /* TODO(A5): 넷 배열을 위상순으로 채우고 결함은 덮어쓰기 */
}

static int add_gate(cut_t *c, int op, int a, int b) {
    int i = c->ngate++;
    c->g[i].op = (uint8_t)op; c->g[i].a = (uint16_t)a; c->g[i].b = (uint16_t)b;
    return c->npi + i;
}

/* 4비트 mini-ALU: PI[3:0]=A, PI[7:4]=B, PI[8]=op0, PI[9]=op1, PI[10]=cin
 * op(1:0)=00 ADD, 01 AND, 10 OR, 11 XOR → PO[3:0]=Y, PO[4]=COUT(adder 고정), PO[5]=ZERO
 * ZERO 출력에 z | (z & a0) 라는 "의도적 중복(redundant)" 게이트 2개를 끼웠다 → 절대 검출 못 하는 결함이 실제로 생긴다. */
static cut_t g_alu;
static void build_mini_alu(cut_t *c) {
    memset(c, 0, sizeof *c);
    c->name = "mini_alu"; c->npi = 11;
    int nop0 = add_gate(c, G_NOT, 8, 8), nop1 = add_gate(c, G_NOT, 9, 9);
    int sel_add = add_gate(c, G_AND, nop0, nop1);
    int sel_and = add_gate(c, G_AND, 8, nop1);
    int sel_or  = add_gate(c, G_AND, nop0, 9);
    int sel_xor = add_gate(c, G_AND, 8, 9);
    int carry = 10, y[4];
    for (int i = 0; i < 4; i++) {
        int a = i, b = 4 + i;
        int ab = add_gate(c, G_AND, a, b), p = add_gate(c, G_XOR, a, b);
        int s = add_gate(c, G_XOR, p, carry);
        int pc = add_gate(c, G_AND, p, carry);
        int co = add_gate(c, G_OR, ab, pc);
        int o = add_gate(c, G_OR, a, b);
        int t0 = add_gate(c, G_AND, sel_add, s), t1 = add_gate(c, G_AND, sel_and, ab);
        int t2 = add_gate(c, G_AND, sel_or, o), t3 = add_gate(c, G_AND, sel_xor, p);
        int u0 = add_gate(c, G_OR, t0, t1), u1 = add_gate(c, G_OR, t2, t3);
        y[i] = add_gate(c, G_OR, u0, u1);
        c->po[c->npo++] = (uint16_t)y[i];
        carry = co;
    }
    c->po[c->npo++] = (uint16_t)carry;
    int v0 = add_gate(c, G_OR, y[0], y[1]), v1 = add_gate(c, G_OR, y[2], y[3]);
    int z = add_gate(c, G_NOR, v0, v1);
    int rd = add_gate(c, G_AND, z, 0);          /* 중복: z & a0  */
    int zr = add_gate(c, G_OR, z, rd);          /* 중복: z | (z & a0) == z */
    c->po[c->npo++] = (uint16_t)zr;
}

/* mini-ALU 의 기대 동작 (C 로 쓴 참조 모델) */
static uint32_t alu_ref(uint32_t pi) {
    uint32_t a = pi & 15, b = (pi >> 4) & 15, op = (pi >> 8) & 3, cin = (pi >> 10) & 1;
    uint32_t sum = a + b + cin, y;
    if (op == 0) y = sum & 15; else if (op == 1) y = a & b; else if (op == 2) y = a | b; else y = a ^ b;
    return y | ((sum >> 4) & 1) << 4 | (uint32_t)(y == 0) << 5;
}

/* k 입력 AND/OR 체인 (k-1 게이트) — random-pattern-resistant 표본 */
static void build_chain(cut_t *c, const char *name, int op, int k) {
    memset(c, 0, sizeof *c);
    c->name = name; c->npi = k;
    int net = 0;
    for (int i = 1; i < k; i++) net = add_gate(c, op, net, i);
    c->po[c->npo++] = (uint16_t)net;
}

/* AND16 을 AND8·AND8 로 쪼개고 test point 삽입: control-1 2개(새 PI 16, 17) + observe 2개(새 PO) */
static void build_and16_tp(cut_t *c) {
    memset(c, 0, sizeof *c);
    c->name = "and16_tp"; c->npi = 18;
    int l = 0, r = 8;
    for (int i = 1; i < 8; i++) l = add_gate(c, G_AND, l, i);
    for (int i = 9; i < 16; i++) r = add_gate(c, G_AND, r, i);
    int lc = add_gate(c, G_OR, l, 16), rc = add_gate(c, G_OR, r, 17);
    int y = add_gate(c, G_AND, lc, rc);
    c->po[c->npo++] = (uint16_t)y;
    c->po[c->npo++] = (uint16_t)l;   /* observe point */
    c->po[c->npo++] = (uint16_t)r;   /* observe point */
}

static int enum_faults(const cut_t *c, fault_t *out) {
    int nf = 0, nets = c->npi + c->ngate;
    for (int net = 0; net < nets; net++) {
        out[nf].net = net; out[nf++].val = 0;
        out[nf].net = net; out[nf++].val = 1;
    }
    return nf;
}

/* 전수 시험으로 검출 가능 여부(정답) — npi 가 작을 때만 */
static bool detectable_exhaustive(const cut_t *c, fault_t f) {
    for (uint32_t p = 0; p < (1u << c->npi); p++)
        if (cut_eval(c, p, f) != cut_eval(c, p, NO_FAULT)) return true;
    return false;
}

/* ================= Part A-4: MISR 와 시그니처 ================= */
typedef struct { int n; uint32_t poly, seed; } misr_cfg_t;

/* 한 사이클: sig ← x·sig mod P ⊕ resp (resp 는 n비트 이내) */
static uint32_t misr_step(uint32_t sig, uint32_t resp, uint32_t poly, int n) {
    (void)sig; (void)resp; (void)poly; (void)n; return 0;   /* TODO(A6) */
}

/* npat 패턴을 src 에서 뽑아 c 에 인가(결함 f)하고 응답을 MISR 로 압축한 시그니처 */
static uint32_t bist_signature(const cut_t *c, patsrc_t src, misr_cfg_t m, uint32_t npat, fault_t f) {
    (void)c; (void)src; (void)m; (void)npat; (void)f; return 0;   /* TODO(A7) */
}

#define NONE UINT32_MAX
/* 결함 응답이 good 과 처음 달라지는 패턴 인덱스 (직접 비교 — 압축 없음). npat 안에 없으면 NONE */
static uint32_t first_detect(const cut_t *c, patsrc_t src, uint32_t npat, fault_t f) {
    (void)c; (void)src; (void)npat; (void)f; return NONE;   /* TODO(A8) */
}

/* 커버리지 곡선: cp[0..ncp-1](오름차순 패턴 수)까지 검출된 결함 수를 covered[] 에.
 * first_detect 를 결함당 한 번만 부르고 cp 로 분류한다. */
static void coverage_curve(const cut_t *c, patsrc_t src, const fault_t *faults, int nf,
                           const uint32_t *cp, int ncp, int *covered) {
    (void)c; (void)src; (void)faults; (void)nf; (void)cp; (void)ncp; memset(covered, 0, (size_t)ncp * sizeof *covered);   /* TODO(A9) */
}

/* 시그니처로 잡은 결함 수와 직접 비교로 잡은 수, 그 차이(aliasing) */
typedef struct { int nf, sig_det, direct_det, aliased; } campaign_t;
static campaign_t bist_campaign(const cut_t *c, patsrc_t src, misr_cfg_t m, uint32_t npat, const fault_t *faults, int nf) {
    campaign_t r = { nf, 0, 0, 0 };
    (void)c; (void)src; (void)m; (void)npat; (void)faults;   /* TODO(A10): 결함마다 sig != golden / first_detect */
    return r;
}

/* ================= Part B-1: aliasing ================= */
/* 오류 스트림 e[0..L-1] (사이클마다 m 비트 오류 워드)의 시그니처: 시드 0 에서 오류만 넣은 것.
 * 선형이므로 이 값이 0 ⟺ 결함이 있어도 시그니처가 golden 과 같다(aliasing). */
static uint32_t err_signature(int n, uint32_t poly, const uint32_t *e, int L) {
    (void)n; (void)poly; (void)e; (void)L; return 0;   /* TODO(B1) */
}

/* m×L 비트 오류 공간(0 아닌 모든 패턴)을 전수 → alias 개수. m*L <= 20 */
static uint64_t alias_exact(int n, uint32_t poly, int m, int L) {
    (void)n; (void)poly; (void)m; (void)L; return 0;   /* TODO(B2) */
}

/* 무작위 오류 스트림 trials 개 → alias 개수 (E=0 은 다시 뽑는다). m ≤ 16 */
static uint64_t alias_mc(int n, uint32_t poly, int m, int L, uint64_t trials) {
    (void)n; (void)poly; (void)m; (void)L; (void)trials; return 0;   /* TODO(B3) */
}

/* 단일 비트 오류(모든 시각 t, 모든 입력 j)를 전수 → alias 개수 (정리: 항상 0) */
static uint64_t single_bit_aliases(int n, uint32_t poly, int m, int L) {
    (void)n; (void)poly; (void)m; (void)L; return 1;   /* TODO(B4) */
}

/* ================= Part C: mock LBIST0 ================= */
enum { R_CTRL = 0x00, R_STATUS = 0x04, R_SEED = 0x08, R_PATCNT = 0x0C, R_XMASK = 0x10, R_SIG = 0x14 };
#define CTRL_START   (1u << 0)
#define CTRL_XMASKEN (1u << 1)
#define CTRL_ABORT   (1u << 2)
#define ST_BUSY      (1u << 0)
#define ST_DONE      (1u << 1)    /* W1C */
#define CYC_PER_PAT  1u
#define OVERHEAD_CYC 32u
#define MMIO_COST    8u
#define X_BIT        (1u << 6)    /* 응답 bit6 = 미초기화 래치 출력 (X-source, 실행마다 값이 다름) */
#define LB_MISR_POLY P16

typedef enum { F_NONE, F_DONE_STUCK1, F_DONE_STUCK0, F_BUSY_STUCK1, F_BUSY_HANG, F_EARLY_DONE,
               F_SEED_RO, F_LOGIC, F_XMASK_BROKEN, F_SIG_BIT } mock_fault_t;

static struct {
    uint32_t seed, patcnt, xmask, ctrl, sig, sig_done;
    bool busy, done, aborted;
    uint64_t now, done_at;
    mock_fault_t fault;
    fault_t logic;
    uint32_t sig_bit;
    uint32_t starts;
    uint64_t x_rng;
} M;

static void mock_init(mock_fault_t f, fault_t logic, uint32_t sig_bit) {
    memset(&M, 0, sizeof M);
    M.fault = f; M.logic = logic; M.sig_bit = sig_bit;
    M.x_rng = 0x123456789ABCDEFull;
}
static uint32_t x_next(void) { uint64_t x = M.x_rng; x ^= x << 13; x ^= x >> 7; x ^= x << 17; M.x_rng = x; return (uint32_t)x; }

/* 엔진: LFSR16 패턴 → mini-ALU(+X 비트) → XMASK → MISR16 */
static uint32_t engine_signature(uint32_t seed, uint32_t patcnt, uint32_t xmask, bool xen, fault_t lf, bool model) {
    patsrc_t src = lfsr_src(16, P16, seed);
    uint32_t sig = 0;
    for (uint32_t i = 0; i < patcnt; i++) {
        uint32_t resp = cut_eval(&g_alu, ps_next(&src, g_alu.npi), lf);
        if (!model && (x_next() & 1u)) resp |= X_BIT;
        if (xen) resp &= ~xmask;
        sig = misr_step(sig, resp, LB_MISR_POLY, 16);
    }
    return sig;
}

static void mock_tick(void) {
    if (M.busy && M.fault != F_BUSY_HANG && M.now >= M.done_at) {
        M.busy = false;
        if (M.fault != F_DONE_STUCK0) M.done = true;
        M.sig = M.sig_done;
    }
}

MAYBE_UNUSED static uint64_t mock_now(void) { return M.now; }

MAYBE_UNUSED static uint32_t reg_read32(uint32_t off) {
    M.now += MMIO_COST;
    mock_tick();
    switch (off) {
    case R_CTRL:   return M.ctrl & CTRL_XMASKEN;
    case R_STATUS: {
        uint32_t st = (M.busy ? ST_BUSY : 0u) | (M.done ? ST_DONE : 0u);
        if (M.fault == F_DONE_STUCK1) st |= ST_DONE;
        if (M.fault == F_BUSY_STUCK1) st |= ST_BUSY;
        if (M.fault == F_BUSY_HANG && M.starts) st |= ST_BUSY;
        return st;
    }
    case R_SEED:   return M.seed;
    case R_PATCNT: return M.patcnt;
    case R_XMASK:  return M.xmask;
    case R_SIG: {
        uint32_t v = M.sig;
        if (M.fault == F_SIG_BIT) v |= M.sig_bit;
        return v;
    }
    default:       return 0xDEADBEEFu;
    }
}

MAYBE_UNUSED static void reg_write32(uint32_t off, uint32_t val) {
    M.now += MMIO_COST;
    mock_tick();
    switch (off) {
    case R_CTRL:
        M.ctrl = val & CTRL_XMASKEN;
        if (val & CTRL_ABORT) { M.busy = false; M.aborted = true; }
        if ((val & CTRL_START) && !M.busy) {
            bool xen = (val & CTRL_XMASKEN) && M.fault != F_XMASK_BROKEN;
            fault_t lf = M.fault == F_LOGIC ? M.logic : NO_FAULT;
            M.sig_done = engine_signature(M.seed, M.patcnt, M.xmask, xen, lf, false);
            M.busy = true; M.done = false; M.starts++;
            uint64_t dur = (uint64_t)M.patcnt * CYC_PER_PAT + OVERHEAD_CYC;
            if (M.fault == F_EARLY_DONE) dur = M.patcnt / 8u;
            M.done_at = M.now + dur;
        }
        break;
    case R_STATUS: if (val & ST_DONE) M.done = false; break;
    case R_SEED:   if (M.fault != F_SEED_RO) M.seed = val; break;
    case R_PATCNT: M.patcnt = val; break;
    case R_XMASK:  M.xmask = val; break;
    default: break;
    }
}

/* golden: pre-silicon 시뮬이 준 표 역할. X 비트는 마스크돼야만 정의된다. */
typedef struct { uint32_t seed, patcnt, xmask, sig; } golden_t;
static uint32_t model_signature(uint32_t seed, uint32_t patcnt, uint32_t xmask) {
    return engine_signature(seed, patcnt, xmask, true, NO_FAULT, true);
}

typedef enum { LB_OK, LB_BAD_CFG, LB_NO_GOLDEN, LB_BUSY_AT_ENTRY, LB_DONE_STUCK, LB_CFG_READBACK,
               LB_TIMEOUT, LB_EARLY_DONE, LB_SIG_MISMATCH, LB_UNSTABLE, LB_NSTAT } lbist_status_t;
MAYBE_UNUSED static const char *const LB_NAME[LB_NSTAT] = {
    "OK", "BAD_CFG", "NO_GOLDEN", "BUSY_AT_ENTRY", "DONE_STUCK", "CFG_READBACK",
    "TIMEOUT", "EARLY_DONE", "SIG_MISMATCH", "UNSTABLE" };

typedef struct { uint32_t seed, patcnt, xmask; } lbist_cfg_t;
typedef struct {
    lbist_status_t st;
    const char *step;          /* 실패한 단계: CHECK / PRECHECK / CONFIG / POLL / COMPARE */
    uint32_t exp, act, act2;   /* 기대값, 1차/2차 실행 시그니처 */
    uint32_t reg_off;          /* CONFIG 실패 시 레지스터 */
    uint64_t elapsed;          /* START → DONE 관측까지 (cycle) */
    bool busy;                 /* TIMEOUT 시점의 BUSY */
    int runs;
} lbist_result_t;


/* 펌웨어 BIST 시퀀스. 표에서 golden 을 찾고, 실행하고, 불일치면 1회 재실행해
 * "재현되는 틀린 시그니처(하드 결함)" 와 "실행마다 다른 시그니처(X/불안정)" 를 분류한다. */
static lbist_status_t lbist_run(const lbist_cfg_t *cfg, const golden_t *tbl, int ntbl, lbist_result_t *r) {
    memset(r, 0, sizeof *r);
    (void)cfg; (void)tbl; (void)ntbl; r->st = LB_BAD_CFG; r->step = "CHECK"; return r->st;   /* TODO(C1) */
}

/* 체크포인트 패턴 수 cps[0..ncp-1](오름차순, 각각 golden 표에 있음)를 앞에서부터 돌려
 * 첫 불일치 구간 (lo, hi] 을 찾는다. 찾으면 cps 인덱스, 전부 일치면 -1, 다른 실패면 -2. */
static int lbist_locate(const lbist_cfg_t *base, const golden_t *tbl, int ntbl, const uint32_t *cps, int ncp, uint32_t *lo, uint32_t *hi) {
    (void)base; (void)tbl; (void)ntbl; (void)cps; (void)ncp; (void)lo; (void)hi; return -2;   /* TODO(C2) */
}

/* 디버그 엔지니어용 한 줄 리포트. snprintf 와 같은 반환 규약 (써야 할 길이). */
static int lbist_format(char *buf, size_t sz, const lbist_cfg_t *cfg, const lbist_result_t *r) {
    (void)cfg; (void)r; if (sz) buf[0] = 0; return 0;   /* TODO(C3) — 예시는 문제 설명 참고 */
}

/* ================= 시험 ================= */
#define FMAX 256

static void test_part_a(void) {
    printf("-- Part A: LFSR --\n");
    for (int i = 0; i < NPRIM; i++) {
        uint64_t exp = (1ull << PRIM[i].n) - 1;
        CHECK(lfsr_period(galois_step, PRIM[i].poly, PRIM[i].n, 1) == exp);
        CHECK(lfsr_period(fib_step, PRIM[i].poly, PRIM[i].n, 1) == exp);
    }
    /* 주기가 2^n-1 이 아닌 다항식 */
    CHECK(lfsr_period(galois_step, 0x1F, 4, 1) == 5);           /* x^4+x^3+x^2+x+1: 기약이지만 원시 아님 */
    CHECK(lfsr_period(galois_step, 0x15, 4, 1) == 6);           /* x^4+x^2+1 = (x^2+x+1)^2 */
    CHECK(lfsr_period(galois_step, 0x11021, 16, 1) == 32767);   /* CRC-16-CCITT: (x+1) 인수 → 주기 절반 */
    CHECK(lfsr_period(galois_step, 0x18, 4, 1) == 0);           /* P(0)=0: 비가역 — 시드로 안 돌아옴 */
    CHECK(lfsr_period(galois_step, P4, 4, 0) == 1);             /* 0 은 lock-up 고정점 */
    CHECK(lfsr_period(fib_step, P4, 4, 0) == 1);

    /* Galois 와 Fibonacci 는 같은 m-sequence (위상만 다름): Galois 출력 n 개로 Fibonacci 를 시드 */
    {
        int n = 8; uint32_t poly = 0x11D, g = 1, f = 0;
        uint32_t gs = g, bits[512];
        for (int i = 0; i < 300; i++) { bits[i] = (gs >> (n - 1)) & 1u; gs = galois_step(gs, poly, n); }
        for (int i = 0; i < n; i++) f |= bits[i] << (n - 1 - i);
        int bad = 0;
        for (int i = 0; i < 280; i++) { bad += ((f >> (n - 1)) & 1u) != bits[i]; f = fib_step(f, poly, n); }
        CHECK(bad == 0);
    }
    /* balance: 한 주기에서 1 이 2^(n-1) 번, 0 이 2^(n-1)-1 번 */
    {
        int n = 10; uint32_t s = 1, ones = 0;
        for (uint32_t i = 0; i < (1u << n) - 1; i++) { ones += s & 1u; s = galois_step(s, 0x409, n); }
        CHECK(ones == 1u << (n - 1));
    }

    printf("-- Part A: CUT --\n");
    build_mini_alu(&g_alu);
    {
        int bad = 0;
        for (uint32_t p = 0; p < 2048; p++) bad += cut_eval(&g_alu, p, NO_FAULT) != alu_ref(p);
        CHECK(bad == 0);
        CHECK(g_alu.ngate == 63 && g_alu.npi + g_alu.ngate == 74);
    }
    /* 결함 시뮬: PO 넷 stuck-at 은 그 출력 비트를 고정, PI stuck 은 그 입력을 고정 */
    {
        fault_t f1 = { g_alu.po[0], 1 }, f2 = { 0, 0 };
        CHECK((cut_eval(&g_alu, 0x000, f1) & 1u) == 1u && (cut_eval(&g_alu, 0x000, NO_FAULT) & 1u) == 0u);
        CHECK(cut_eval(&g_alu, 0x001, f2) == alu_ref(0x000));          /* a0 s-a-0: 패턴 1 이 패턴 0 처럼 */
    }
    static fault_t faults[FMAX];
    int nf = enum_faults(&g_alu, faults);
    CHECK(nf == 148);

    /* 진리(ground truth): 전수로 검출 가능한 결함 수 */
    int detectable = 0, red = 0;
    int red_list[8];
    for (int i = 0; i < nf; i++) {
        if (detectable_exhaustive(&g_alu, faults[i])) detectable++;
        else if (red < 8) red_list[red++] = i;
    }
    printf("faults=%d detectable=%d redundant=%d:", nf, detectable, nf - detectable);
    for (int i = 0; i < red; i++) printf(" net%d/sa%d", faults[red_list[i]].net, faults[red_list[i]].val);
    printf("\n");
    CHECK(detectable == nf - 1 && red == 1);                         /* 정확히 1개: 중복 게이트 rd 의 s-a-0 */
    CHECK(faults[red_list[0]].net == g_alu.npi + g_alu.ngate - 2 && faults[red_list[0]].val == 0);

    /* MISR 선형성: 시드 0 에서 S(R1^R2) == S(R1)^S(R2) */
    {
        uint32_t s1 = 0, s2 = 0, s3 = 0;
        for (int t = 0; t < 200; t++) {
            uint32_t r1 = (uint32_t)xs64() & 0x3F, r2 = (uint32_t)xs64() & 0x3F;
            s1 = misr_step(s1, r1, P16, 16); s2 = misr_step(s2, r2, P16, 16); s3 = misr_step(s3, r1 ^ r2, P16, 16);
        }
        CHECK(s3 == (s1 ^ s2) && s1 != 0);
    }

    /* 커버리지 곡선 + 시그니처 캠페인 */
    patsrc_t src = lfsr_src(16, P16, 0xACE1);
    misr_cfg_t mc = { 16, P16, 0 };
    static const uint32_t cp[] = { 1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096 };
    enum { NCP = sizeof cp / sizeof cp[0] };
    int cov[NCP];
    coverage_curve(&g_alu, src, faults, nf, cp, NCP, cov);
    printf("patterns  covered  coverage%%  testable%%\n");
    for (int i = 0; i < NCP; i++)
        printf("%8u %8d %8.1f %10.1f\n", cp[i], cov[i], 100.0 * cov[i] / nf, 100.0 * cov[i] / detectable);
    int mono = 1;
    for (int i = 1; i < NCP; i++) mono &= cov[i] >= cov[i - 1];
    CHECK(mono);
    CHECK(cov[NCP - 1] == detectable);       /* 4096 패턴이면 검출 가능한 결함을 전부 잡는다 */
    CHECK(cov[0] < nf / 2);                  /* 1 패턴으로는 절반도 못 잡는다 */

    campaign_t k = bist_campaign(&g_alu, src, mc, 4096, faults, nf);
    printf("campaign npat=4096 misr=16: sig_det=%d direct_det=%d aliased=%d\n", k.sig_det, k.direct_det, k.aliased);
    CHECK(k.direct_det == detectable && k.sig_det == detectable && k.aliased == 0);

    /* golden 은 시드·패턴 수에 의존, 같은 입력엔 결정적 */
    uint32_t a = bist_signature(&g_alu, src, mc, 512, NO_FAULT);
    uint32_t b = bist_signature(&g_alu, src, mc, 512, NO_FAULT);
    uint32_t c = bist_signature(&g_alu, lfsr_src(16, P16, 0xACE2), mc, 512, NO_FAULT);
    CHECK(a == b && a != c);
    printf("golden(seed=0xACE1, 512)=0x%04X\n", a);

    {
        uint32_t first = lfsr_src(16, P16, 0xACE1).s & low_mask(g_alu.npi);
        fault_t f1 = { g_alu.po[0], !(cut_eval(&g_alu, first, NO_FAULT) & 1u) };
        CHECK(first_detect(&g_alu, src, 4096, f1) == 0);           /* 첫 패턴에서 바로 뒤집히는 결함 */
        CHECK(first_detect(&g_alu, src, 4096, NO_FAULT) == NONE);  /* 결함 없으면 검출 없음 */
    }
}

static void test_part_b(void) {
    printf("-- Part B: aliasing exact (all nonzero error streams) --\n");
    static const struct { int n; uint32_t poly; int m, L; uint64_t exp; } T[] = {
        {4, P4, 1, 12, 255}, {8, 0x11D, 1, 14, 63}, {4, P4, 1, 4, 0}, {4, P4, 2, 8, 4095},
        {5, 0x25, 3, 5, 1023}, {4, 0x1F, 1, 12, 255},            /* 비원시 x^4+x^3+x^2+x+1 도 같은 개수 */
    };
    printf(" n  m  L  alias   formula 2^(mL-n)-1  alias_rate   2^-n\n");
    for (size_t i = 0; i < sizeof T / sizeof T[0]; i++) {
        uint64_t got = alias_exact(T[i].n, T[i].poly, T[i].m, T[i].L);
        double tot = (double)((1ull << (T[i].m * T[i].L)) - 1);
        printf("%2d %2d %2d %6llu %19llu %11.5f %8.5f\n", T[i].n, T[i].m, T[i].L, (unsigned long long)got,
               (unsigned long long)T[i].exp, (double)got / tot, 1.0 / (double)(1u << T[i].n));
        CHECK(got == T[i].exp);
    }

    /* 단일 비트 오류는 어떤 원시·기약 다항식에서도 alias 불가 */
    CHECK(single_bit_aliases(4, P4, 1, 64) == 0);
    CHECK(single_bit_aliases(4, P4, 3, 64) == 0);
    CHECK(single_bit_aliases(16, P16, 6, 60) == 0);
    CHECK(single_bit_aliases(4, 0x1F, 2, 64) == 0);

    /* 2비트 오류 = 구조적 aliasing 의 예: 시리얼(SISR)에서 간격이 주기의 배수 */
    {
        uint32_t e[40] = {0};
        e[0] = 1; e[15] = 1;
        CHECK(err_signature(4, P4, e, 40) == 0);             /* 원시 x^4+x+1: 간격 15 */
        memset(e, 0, sizeof e); e[0] = 1; e[5] = 1;
        CHECK(err_signature(4, P4, e, 40) != 0);             /* 간격 5 는 원시 다항식에선 안 맞음 */
        CHECK(err_signature(4, 0x1F, e, 40) == 0);           /* 비원시(주기 5): 간격 5 에서 alias */
    }
    /* MISR 대각선 취소: 입력 j, 시각 t 의 오류가 다음 사이클 입력 j+1 의 오류와 상쇄 */
    {
        uint32_t e[8] = {0};
        for (int j = 0; j < 5; j++) {
            memset(e, 0, sizeof e);
            e[2] = 1u << j; e[3] = 1u << (j + 1);
            CHECK(err_signature(16, P16, e, 8) == 0);
        }
    }

    printf("-- Part B: aliasing Monte-Carlo (L=32, 2^20 trials) --\n");
    printf(" n  aliased  rate       2^-n      ratio  bound(4sigma)\n");
    rng_seed(0xB157ull);
    static const struct { int n; uint32_t poly; } MC[] = { {4, P4}, {6, 0x43}, {8, 0x11D}, {10, 0x409}, {12, 0x1053} };
    const uint64_t trials = 1ull << 20;
    for (size_t i = 0; i < sizeof MC / sizeof MC[0]; i++) {
        uint64_t a = alias_mc(MC[i].n, MC[i].poly, 1, 32, trials);
        double p = 1.0 / (double)(1u << MC[i].n), exp = (double)trials * p;
        double sigma = sqrt(exp * (1 - p));
        printf("%2d %8llu %.6f %.6f %6.3f   +-%.0f\n", MC[i].n, (unsigned long long)a, (double)a / (double)trials, p, (double)a / exp, 4 * sigma);
        CHECK((double)a > exp - 4 * sigma && (double)a < exp + 4 * sigma);   /* 4σ: 시드 고정이라 결정적, 정상 표본오차는 통과 */
    }

    printf("-- Part B: fault-level aliasing on mini_alu (32 seeds, 256 pats) --\n");
    {
        static fault_t faults[FMAX];
        int nf = enum_faults(&g_alu, faults);
        static const struct { int n; uint32_t poly; } MS[] = { {6, 0x43}, {8, 0x11D}, {10, 0x409}, {12, 0x1053}, {14, 0x4443}, {16, P16} };
        uint32_t tot[6] = {0}, alias[6] = {0};
        static uint8_t gold[256];
        for (uint32_t sd = 1; sd <= 32; sd++) {
            uint32_t seed = (sd * 0x9E37u) & 0xFFFF;
            if (seed == 0) seed = 1;
            patsrc_t s = lfsr_src(16, P16, seed), p = s;
            for (int t = 0; t < 256; t++) gold[t] = (uint8_t)cut_eval(&g_alu, ps_next(&p, g_alu.npi), NO_FAULT);
            for (int f = 0; f < nf; f++) {
                static uint8_t res[256];
                bool diff = false;
                p = s;
                for (int t = 0; t < 256; t++) {
                    res[t] = (uint8_t)cut_eval(&g_alu, ps_next(&p, g_alu.npi), faults[f]);
                    diff |= res[t] != gold[t];
                }
                if (!diff) continue;
                for (int m = 0; m < 6; m++) {
                    uint32_t e = 0;
                    for (int t = 0; t < 256; t++) e = misr_step(e, (uint32_t)(res[t] ^ gold[t]), MS[m].poly, MS[m].n);
                    tot[m]++;
                    alias[m] += e == 0;
                }
            }
        }
        printf(" n  errored_faults  aliased  rate      2^-n\n");
        for (int m = 0; m < 6; m++)
            printf("%2d %15u %8u %.5f  %.5f\n", MS[m].n, tot[m], alias[m], (double)alias[m] / tot[m], 1.0 / (double)(1u << MS[m].n));
        CHECK(alias[5] == 0);                  /* 16비트에선 관측 안 됨 (4704 개 결함-시드, 기대값 0.07 개) */
        double r6 = (double)alias[0] / tot[0] * 64.0;
        CHECK(alias[0] > 0 && r6 > 1.0 / 3.0 && r6 < 3.0);   /* 6비트: 자릿수만 본다 — 실제 오류 스트림은 균등 난수가 아니다 */
    }

    printf("-- Part B: random-pattern-resistant faults (AND16/OR16, 2048 pats) --\n");
    {
        static cut_t and16, or16, tp;
        build_chain(&and16, "and16", G_AND, 16);
        build_chain(&or16, "or16", G_OR, 16);
        build_and16_tp(&tp);
        static fault_t fa[FMAX], fo[FMAX], ft[FMAX];
        int na = enum_faults(&and16, fa), no = enum_faults(&or16, fo), nt = enum_faults(&tp, ft);
        static const uint32_t cp[] = { 2048 };
        int cv[3];
        patsrc_t uni = lfsr_src(20, P20, 0xACE1);
        patsrc_t w1 = weighted_src(20, P20, 0xABCDE, 3, true);
        patsrc_t w0 = weighted_src(20, P20, 0x1BEEF, 3, false);
        coverage_curve(&and16, uni, fa, na, cp, 1, &cv[0]);
        printf("and16 uniform  : %d/%d\n", cv[0], na);
        CHECK(cv[0] < na);                     /* uniform 은 다 못 잡는다 */
        coverage_curve(&and16, w1, fa, na, cp, 1, &cv[1]);
        printf("and16 weight7/8: %d/%d\n", cv[1], na);
        CHECK(cv[1] == na);                    /* 7/8 가중이면 전부 */
        coverage_curve(&or16, w1, fo, no, cp, 1, &cv[2]);
        printf("or16  weight7/8: %d/%d\n", cv[2], no);
        CHECK(cv[2] < no);                     /* 같은 가중은 OR 에 독 — 가중 세트는 회로별로 */
        coverage_curve(&or16, w0, fo, no, cp, 1, &cv[2]);
        printf("or16  weight1/8: %d/%d\n", cv[2], no);
        CHECK(cv[2] == no);
        coverage_curve(&tp, uni, ft, nt, cp, 1, &cv[0]);
        printf("and16+tp uniform: %d/%d\n", cv[0], nt);
        CHECK(cv[0] == nt);                    /* test point 2+2 개로 uniform 패턴도 전부 */
    }
}

static const uint32_t LB_CPS[] = { 64, 256, 1024, 4096 };
#define NLB_CPS 4

static int build_golden(golden_t *t) {
    int n = 0;
    static const uint32_t seeds[] = { 0xACE1, 0x1D0F };
    static const uint32_t xm[] = { X_BIT, X_BIT | (1u << 4) };
    for (int s = 0; s < 2; s++) for (int p = 0; p < NLB_CPS; p++) for (int x = 0; x < 2; x++) {
        t[n].seed = seeds[s]; t[n].patcnt = LB_CPS[p]; t[n].xmask = xm[x];
        t[n].sig = model_signature(seeds[s], LB_CPS[p], xm[x]);
        n++;
    }
    return n;
}

static void test_part_c(void) {
    printf("-- Part C: firmware LBIST sequence --\n");
    static golden_t tbl[32];
    int nt = build_golden(tbl);
    CHECK(nt == 16);
    lbist_cfg_t cfg = { 0xACE1, 4096, X_BIT };
    lbist_result_t r;
    char line[160];

    /* good */
    mock_init(F_NONE, NO_FAULT, 0);
    CHECK(lbist_run(&cfg, tbl, nt, &r) == LB_OK);
    CHECK(r.act == model_signature(0xACE1, 4096, X_BIT) && r.runs == 1);
    CHECK(r.elapsed >= 4096u * CYC_PER_PAT);
    CHECK(!M.done && !M.busy && M.starts == 1);        /* DONE 을 지우고 끝낸다 */
    lbist_format(line, sizeof line, &cfg, &r);
    printf("%s\n", line);
    CHECK(strstr(line, "PASS ip=LBIST0") == line);

    /* X 비트(실행마다 다른 값)가 있어도 마스크하면 시그니처는 안정 */
    mock_init(F_NONE, NO_FAULT, 0);
    { uint32_t s1, s2; lbist_run(&cfg, tbl, nt, &r); s1 = r.act; lbist_run(&cfg, tbl, nt, &r); s2 = r.act; CHECK(s1 == s2 && r.st == LB_OK); }

    /* 잘못된 설정 */
    { lbist_cfg_t b = cfg; b.seed = 0; mock_init(F_NONE, NO_FAULT, 0); CHECK(lbist_run(&b, tbl, nt, &r) == LB_BAD_CFG); CHECK(M.starts == 0); }
    { lbist_cfg_t b = cfg; b.patcnt = 0; CHECK(lbist_run(&b, tbl, nt, &r) == LB_BAD_CFG); }
    { lbist_cfg_t b = cfg; b.xmask = 0; CHECK(lbist_run(&b, tbl, nt, &r) == LB_BAD_CFG); }   /* X-source 미마스크 설정은 거절 */
    { lbist_cfg_t b = cfg; b.patcnt = 5000; CHECK(lbist_run(&b, tbl, nt, &r) == LB_NO_GOLDEN); }

    /* 하드웨어 결함 → 정확한 원인 */
    mock_init(F_DONE_STUCK1, NO_FAULT, 0);
    CHECK(lbist_run(&cfg, tbl, nt, &r) == LB_DONE_STUCK && !strcmp(r.step, "PRECHECK") && M.starts == 0);
    lbist_format(line, sizeof line, &cfg, &r); printf("%s\n", line);
    CHECK(strstr(line, "FAIL ip=LBIST0 cause=DONE_STUCK step=PRECHECK") == line);

    mock_init(F_BUSY_STUCK1, NO_FAULT, 0);
    CHECK(lbist_run(&cfg, tbl, nt, &r) == LB_BUSY_AT_ENTRY);

    mock_init(F_DONE_STUCK0, NO_FAULT, 0);
    CHECK(lbist_run(&cfg, tbl, nt, &r) == LB_TIMEOUT && !r.busy && M.aborted);
    CHECK(r.elapsed > 2 * 4096u && !strcmp(r.step, "POLL"));
    lbist_format(line, sizeof line, &cfg, &r); printf("%s\n", line);
    CHECK(strstr(line, "cause=TIMEOUT") && strstr(line, "busy=0"));

    mock_init(F_BUSY_HANG, NO_FAULT, 0);
    CHECK(lbist_run(&cfg, tbl, nt, &r) == LB_TIMEOUT && r.busy && M.aborted && !M.busy);

    mock_init(F_EARLY_DONE, NO_FAULT, 0);
    CHECK(lbist_run(&cfg, tbl, nt, &r) == LB_EARLY_DONE && r.elapsed < 4096u);
    lbist_format(line, sizeof line, &cfg, &r); printf("%s\n", line);
    CHECK(strstr(line, "cause=EARLY_DONE") && strstr(line, "min=4096"));

    mock_init(F_SEED_RO, NO_FAULT, 0);
    CHECK(lbist_run(&cfg, tbl, nt, &r) == LB_CFG_READBACK && r.reg_off == R_SEED && M.starts == 0);
    lbist_format(line, sizeof line, &cfg, &r); printf("%s\n", line);
    CHECK(strstr(line, "reg_off=0x08"));

    /* 논리 결함 → 재현되는 틀린 시그니처 */
    {
        fault_t lf = { g_alu.po[1], 0 };          /* Y1 s-a-0 */
        mock_init(F_LOGIC, lf, 0);
        CHECK(lbist_run(&cfg, tbl, nt, &r) == LB_SIG_MISMATCH && r.act == r.act2 && r.act != r.exp && r.runs == 2);
        lbist_format(line, sizeof line, &cfg, &r); printf("%s\n", line);
        CHECK(strstr(line, "cause=SIG_MISMATCH") && strstr(line, "runs=2"));
    }
    /* SIG 읽기 경로의 stuck 비트: 재현되고, diff 가 정확히 그 비트 */
    {
        uint32_t exp = model_signature(0xACE1, 4096, X_BIT), bit = 1;
        while (exp & bit) bit <<= 1;
        mock_init(F_SIG_BIT, NO_FAULT, bit);
        CHECK(lbist_run(&cfg, tbl, nt, &r) == LB_SIG_MISMATCH && (r.exp ^ r.act) == bit);
    }
    /* XMASK 가 하드웨어에서 안 먹음 → 실행마다 달라짐 */
    mock_init(F_XMASK_BROKEN, NO_FAULT, 0);
    CHECK(lbist_run(&cfg, tbl, nt, &r) == LB_UNSTABLE && r.act != r.act2 && r.act != r.exp);
    lbist_format(line, sizeof line, &cfg, &r); printf("%s\n", line);

    /* 한계: 마스크한 출력에서만 보이는 결함은 놓친다 */
    {
        fault_t lf = { g_alu.po[4], 1 };                     /* COUT s-a-1: PO[4] 에서만 관측 */
        lbist_cfg_t cm = cfg; cm.xmask = X_BIT | (1u << 4);
        mock_init(F_LOGIC, lf, 0);
        CHECK(lbist_run(&cm, tbl, nt, &r) == LB_OK);        /* COUT s-a-1 은 bit4 를 가린 설정에서 PASS */
        mock_init(F_LOGIC, lf, 0);
        CHECK(lbist_run(&cfg, tbl, nt, &r) == LB_SIG_MISMATCH);   /* 안 가리면 잡힌다 */
        printf("xmask limit: COUT/sa1 with bit4 masked -> PASS (missed); unmasked -> FAIL\n");
    }
    /* 마스크가 잃는 커버리지 정량: 출력 비트 하나를 가리면 몇 개를 놓치나 (1024 패턴) */
    {
        static fault_t faults[FMAX];
        int nf = enum_faults(&g_alu, faults), lost[6];
        patsrc_t src = lfsr_src(16, P16, 0xACE1);
        printf("mask PO bit -> faults lost:");
        for (int b = 0; b < 6; b++) {
            uint32_t mask = ~(1u << b);
            lost[b] = 0;
            for (int i = 0; i < nf; i++) {
                bool det = false, det_m = false;
                patsrc_t p = src;
                for (uint32_t t = 0; t < 1024 && !det_m; t++) {
                    uint32_t pat = ps_next(&p, g_alu.npi);
                    uint32_t x = cut_eval(&g_alu, pat, faults[i]), y = cut_eval(&g_alu, pat, NO_FAULT);
                    det |= x != y;
                    det_m |= (x & mask) != (y & mask);
                }
                lost[b] += det && !det_m;
            }
            printf(" b%d=%d", b, lost[b]);
        }
        printf("\n");
        CHECK(lost[4] > 0);      /* COUT 은 다른 출력에서 안 보이므로 가리면 놓친다 */
        CHECK(lost[3] == 0);     /* Y3 은 ZERO 에서도 보여 1024 패턴 안에서 놓치는 결함 없음(실측) */
    }

    /* 고장 구간 찾기: 진짜 첫 검출 패턴이 구간 안 */
    {
        fault_t lf = { g_alu.po[0], 1 };                     /* Y0 s-a-1: 초반에 검출 */
        uint32_t d = first_detect(&g_alu, lfsr_src(16, P16, 0xACE1), 4096, lf);
        mock_init(F_LOGIC, lf, 0);
        uint32_t lo = 0, hi = 0;
        int idx = lbist_locate(&cfg, tbl, nt, LB_CPS, NLB_CPS, &lo, &hi);
        printf("locate Y0/sa1: window=(%u,%u] true_first_detect=%u\n", lo, hi, d);
        CHECK(idx >= 0 && lo <= d && d < hi);

        /* 늦게 나타나는 결함: PI 패턴 조건이 희귀한 게이트 → 구간도 뒤쪽 */
        static fault_t faults[FMAX];
        int nf = enum_faults(&g_alu, faults);
        uint32_t latest = 0; fault_t late = NO_FAULT;
        for (int i = 0; i < nf; i++) {
            uint32_t x = first_detect(&g_alu, lfsr_src(16, P16, 0xACE1), 4096, faults[i]);
            if (x != NONE && x >= latest) { latest = x; late = faults[i]; }
        }
        mock_init(F_LOGIC, late, 0);
        idx = lbist_locate(&cfg, tbl, nt, LB_CPS, NLB_CPS, &lo, &hi);
        printf("locate net%d/sa%d: window=(%u,%u] true_first_detect=%u\n", late.net, late.val, lo, hi, latest);
        CHECK(idx >= 0 && lo <= latest && latest < hi);
        mock_init(F_NONE, NO_FAULT, 0);
        CHECK(lbist_locate(&cfg, tbl, nt, LB_CPS, NLB_CPS, &lo, &hi) == -1);
    }
    /* 짧은 버퍼에서도 잘림 안전 */
    {
        char tiny[16];
        mock_init(F_NONE, NO_FAULT, 0);
        lbist_run(&cfg, tbl, nt, &r);
        int need = lbist_format(tiny, sizeof tiny, &cfg, &r);
        CHECK(need > (int)sizeof tiny && strlen(tiny) == sizeof tiny - 1);
    }
}

int main(void) {
    test_part_a();
    test_part_b();
    test_part_c();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
