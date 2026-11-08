/* SIV-B07 SRAM March 테스트 (March C-)
 *   Part A — March 엔진 + March C- 테이블 (1비트/셀 SRAM 모델)
 *   Part B — 결함 모델(SAF/TF/AF/CF/DRDF) 주입 + 검출률 측정 (MATS+ / MATS++ / March C-)
 *   Part C — ISP 라인버퍼 SRAM(256 x 32bit) word 단위 March C- + data background + 실패 리포트
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution
 */
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ======================= SRAM 모델 (DUT, 1비트/셀) ======================= */
#define MAXCELL 64u
#define NOADDR  0xFFFFu

typedef enum {
    F_NONE,
    F_SA0, F_SA1,                               /* stuck-at 0 / 1                         */
    F_TF_UP, F_TF_DN,                           /* transition: 0->1 / 1->0 전이 실패       */
    F_AF_ALIAS, F_AF_MULTI,                     /* 디코더: 주소 v -> 셀 a / 셀 v 와 a 동시 */
    F_CFIN_UP, F_CFIN_DN,                       /* inversion coupling <a 전이; v 반전>    */
    F_CFID_U0, F_CFID_U1, F_CFID_D0, F_CFID_D1, /* idempotent coupling <a 전이; v = 값>   */
    F_CFST_00, F_CFST_01, F_CFST_10, F_CFST_11, /* state coupling <a == x; v = y>         */
    F_DRDF,                                     /* deceptive read destructive: 맞게 읽히고 셀은 뒤집힘 */
    F_KIND_COUNT
} fault_kind_t;

static const char *const FAULT_NAME[F_KIND_COUNT] = {
    "none", "SA0", "SA1", "TF-up", "TF-down", "AF-alias", "AF-multi",
    "CFin<u;~>", "CFin<d;~>", "CFid<u;0>", "CFid<u;1>", "CFid<d;0>", "CFid<d;1>",
    "CFst<0;0>", "CFst<0;1>", "CFst<1;0>", "CFst<1;1>", "DRDF"
};

typedef struct {
    fault_kind_t kind;
    unsigned v;   /* victim 셀 (AF 에서는 고장 난 '주소')       */
    unsigned a;   /* aggressor 셀 (AF 에서는 잘못 연결된 '셀')  */
} fault_t;

typedef struct {
    unsigned n;                    /* 셀 개수 = 주소 0..n-1          */
    uint8_t  cell[MAXCELL];
    fault_t  f;                    /* 단일 결함 가정 (single fault) */
    unsigned nlog;                 /* 접근 주소 기록 (방향 검증용)   */
    uint16_t log[MAXCELL * 12u];
} sram_t;

/* 상태(state) 결함: 언제나 성립하는 조건 — 매 접근 뒤 다시 강제한다 */
static void sram_settle(sram_t *m) {
    const fault_t *f = &m->f;
    switch (f->kind) {
    case F_SA0: m->cell[f->v] = 0; break;
    case F_SA1: m->cell[f->v] = 1; break;
    case F_CFST_00: case F_CFST_01: case F_CFST_10: case F_CFST_11: {
        /* TODO (Part B-3): aggressor 가 x 상태이면 victim 을 y 로 강제 */
        break;
    }
    default: break;
    }
}

/* 물리 셀 c 에 d 를 저장 (디코더를 지난 뒤의 쓰기) */
static void cell_store(sram_t *m, unsigned c, uint8_t d) {
    const fault_t *f = &m->f;
    uint8_t old = m->cell[c], nw = d;
    /* TODO (Part B-1): transition fault — victim 은 해당 방향 전이를 못 한다 */
    m->cell[c] = nw;
    /* TODO (Part B-2): c 가 aggressor 이고 '실제로' 전이했으면 CFin/CFid 를 victim 에 적용 */
    (void)f; (void)old;
}

/* 주소 -> 물리 셀 (최대 2개). 주소 디코더 결함(AF)은 여기서 */
static unsigned decode(const sram_t *m, unsigned addr, unsigned cells[2]) {
    const fault_t *f = &m->f;
    if (addr == f->v && f->kind == F_AF_ALIAS) { cells[0] = f->a; return 1; }
    if (addr == f->v && f->kind == F_AF_MULTI) { cells[0] = addr; cells[1] = f->a; return 2; }
    cells[0] = addr;
    return 1;
}

static void log_access(sram_t *m, unsigned addr) {
    if (m->nlog < sizeof m->log / sizeof m->log[0]) m->log[m->nlog++] = (uint16_t)addr;
}

static void sram_write(sram_t *m, unsigned addr, uint8_t d) {
    unsigned c[2], k = decode(m, addr, c);
    log_access(m, addr);
    for (unsigned i = 0; i < k; i++) cell_store(m, c[i], d);
    sram_settle(m);
}

static uint8_t sram_read(sram_t *m, unsigned addr) {
    unsigned c[2], k = decode(m, addr, c);
    uint8_t v = 1;
    log_access(m, addr);
    for (unsigned i = 0; i < k; i++) v &= m->cell[c[i]];      /* 다중 선택 = wired-AND */
    if (m->f.kind == F_DRDF && c[0] == m->f.v) m->cell[m->f.v] ^= 1u; /* 값은 맞게, 셀은 뒤집힘 */
    sram_settle(m);
    return v;
}

/* power-up 상태: 0=all0 1=all1 2=0101.. 3=1010.. */
static void sram_init(sram_t *m, unsigned n, fault_t f, unsigned powerup) {
    memset(m, 0, sizeof *m);
    m->n = n;
    m->f = f;
    for (unsigned c = 0; c < n; c++)
        m->cell[c] = (uint8_t)(powerup == 0 ? 0u : powerup == 1 ? 1u : powerup == 2 ? (c & 1u) : !(c & 1u));
    sram_settle(m);
}

/* ======================= March 알고리즘 표현 ======================= */
typedef enum { UP, DOWN, ANY } dir_t;          /* ⇑ ⇓ ⇕ (⇕ 는 UP 으로 실행) */
typedef enum { R0, R1, W0, W1 } op_t;
typedef struct { dir_t dir; unsigned nops; op_t ops[4]; } march_elem_t;
typedef struct { const char *name; unsigned nelem; march_elem_t e[8]; } march_alg_t;

/* MATS+  = {⇕(w0); ⇑(r0,w1); ⇓(r1,w0)}          5n */
static const march_alg_t MATS_PLUS = { "MATS+", 3, {
    { ANY, 1, { W0 } }, { UP, 2, { R0, W1 } }, { DOWN, 2, { R1, W0 } } } };
/* MATS++ = {⇕(w0); ⇑(r0,w1); ⇓(r1,w0,r0)}       6n */
static const march_alg_t MATS_PP = { "MATS++", 3, {
    { ANY, 1, { W0 } }, { UP, 2, { R0, W1 } }, { DOWN, 3, { R1, W0, R0 } } } };
/* TODO (Part A-1): March C- = {⇕(w0); ⇑(r0,w1); ⇑(r1,w0); ⇓(r0,w1); ⇓(r1,w0); ⇕(r0)}  10n */
static const march_alg_t MARCH_C_MINUS = { "March C-", 0, {
    { ANY, 0, { W0 } } } };

typedef struct {
    unsigned nfail, nops;
    int      elem, op;          /* 첫 실패 위치 (-1 = 없음) */
    unsigned addr;
    uint8_t  exp, act;
} march_result_t;

/* TODO (Part A-2): element 마다 dir 방향으로 주소를 돌며 ops 를 순서대로 실행.
 * 읽기는 기대값과 비교해 실패 수와 '첫 실패'를 기록한다. 반환 = nfail */
static unsigned march_run(sram_t *m, const march_alg_t *alg, march_result_t *r) {
    memset(r, 0, sizeof *r);
    r->elem = r->op = -1;
    (void)m; (void)alg; (void)sram_write; (void)sram_read;
    return r->nfail;
}

/* ======================= Part B: 검출률 측정 ======================= */
#define NPOWERUP 4u
#define NCOV     16u

/* TODO (Part B-4): '보장 검출' = 어떤 power-up 상태로 시작해도 잡힌다 → 4가지 초기 상태 모두 nfail > 0 */
static bool detects(const march_alg_t *alg, fault_t f, unsigned n) {
    (void)alg; (void)f; (void)n;
    return false;
}

/* kind 하나에 대해 n 셀 메모리의 모든 단일 결함을 나열 (쌍 결함은 a != v 인 모든 순서쌍) */
static unsigned enum_faults(fault_kind_t kind, unsigned n, fault_t *out) {
    unsigned cnt = 0;
    bool pair = kind >= F_AF_ALIAS && kind <= F_CFST_11;
    for (unsigned v = 0; v < n; v++) {
        if (!pair) { out[cnt++] = (fault_t){ kind, v, NOADDR }; continue; }
        for (unsigned a = 0; a < n; a++)
            if (a != v) out[cnt++] = (fault_t){ kind, v, a };
    }
    return cnt;
}

typedef struct { unsigned total, det; } cov_t;

static cov_t coverage(const march_alg_t *alg, fault_kind_t kind, unsigned n) {
    static fault_t list[MAXCELL * MAXCELL];
    cov_t c = { enum_faults(kind, n, list), 0 };
    for (unsigned i = 0; i < c.total; i++) c.det += detects(alg, list[i], n);
    return c;
}

static unsigned ops_per_cell(const march_alg_t *alg) {
    unsigned s = 0;
    for (unsigned e = 0; e < alg->nelem; e++) s += alg->e[e].nops;
    return s;
}

/* ======================= Part C: ISP 라인버퍼 SRAM (word 단위, MMIO 창) ======================= */
#define LBUF_BASE  0x40020000u
#define LBUF_WORDS 256u

typedef struct {
    uint32_t (*rd32)(uint32_t addr);
    void     (*wr32)(uint32_t addr, uint32_t val);
} bus_ops_t;

typedef struct {               /* mock 에 주입할 결함 설정 (-1 = 없음) */
    int      stuck_word;       /* stuck-at: stuck_mask 비트가 stuck_val 로 고정 */
    uint32_t stuck_mask, stuck_val;
    int      ignore_abit;      /* 디코더가 무시하는 word-index 비트 */
    int      cf_word;          /* intra-word CFid<aggr 비트 ↑ ; vict 비트 = val> */
    unsigned cf_aggr, cf_vict, cf_val;
} lbuf_faults_t;

static uint32_t      g_lbuf[LBUF_WORDS];
static lbuf_faults_t g_lf;

static uint32_t apply_stuck(unsigned idx, uint32_t v) {
    if ((int)idx == g_lf.stuck_word) v = (v & ~g_lf.stuck_mask) | (g_lf.stuck_val & g_lf.stuck_mask);
    return v;
}
static void lbuf_reset(lbuf_faults_t f) {
    g_lf = f;
    for (unsigned i = 0; i < LBUF_WORDS; i++) g_lbuf[i] = apply_stuck(i, 0xA5A5A5A5u ^ (i * 0x01010101u));
}
static unsigned lbuf_index(uint32_t addr) {
    unsigned idx = (unsigned)((addr - LBUF_BASE) >> 2);
    if (g_lf.ignore_abit >= 0) idx &= ~(1u << g_lf.ignore_abit);
    return idx % LBUF_WORDS;
}
static uint32_t lbuf_rd32(uint32_t addr) { return g_lbuf[lbuf_index(addr)]; }
static void lbuf_wr32(uint32_t addr, uint32_t val) {
    unsigned i = lbuf_index(addr);
    uint32_t old = g_lbuf[i], nw = val;
    if ((int)i == g_lf.cf_word) {
        uint32_t am = 1u << g_lf.cf_aggr, vm = 1u << g_lf.cf_vict;
        if (!(old & am) && (nw & am)) nw = g_lf.cf_val ? (nw | vm) : (nw & ~vm);
    }
    g_lbuf[i] = apply_stuck(i, nw);
}
static const bus_ops_t LBUF_MOCK_BUS = { lbuf_rd32, lbuf_wr32 };

/* 실보드(JTAG 스크립트 대신 on-chip 펌웨어)라면 같은 엔진에 이 bus ops 를 꽂는다 — 여기선 호출하지 않음 */
static uint32_t mmio_rd32(uint32_t a) { return *(volatile uint32_t *)(uintptr_t)a; }
static void mmio_wr32(uint32_t a, uint32_t v) { *(volatile uint32_t *)(uintptr_t)a = v; }
static const bus_ops_t MMIO_BUS = { mmio_rd32, mmio_wr32 };

typedef struct { const char *name; uint32_t base, words; const bus_ops_t *bus; } mem_region_t;

#define REP_MAXLINES 4u
typedef struct {
    unsigned nfail, nops;
    uint32_t failbits;                 /* 실패한 비트(열/데이터 라인)의 OR */
    unsigned nlines;
    char     line[REP_MAXLINES][192];
} mem_report_t;

static void fmt_bits(uint32_t diff, char *out, size_t sz) {
    unsigned cnt = 0, len = 0;
    for (unsigned b = 0; b < 32; b++) cnt += (diff >> b) & 1u;
    if (cnt > 6) { snprintf(out, sz, "[%u bits]", cnt); return; }
    len += (unsigned)snprintf(out + len, sz - len, "[");
    for (unsigned b = 0, first = 1; b < 32; b++)
        if ((diff >> b) & 1u) { len += (unsigned)snprintf(out + len, sz - len, first ? "%u" : ",%u", b); first = 0; }
    snprintf(out + len, sz - len, "]");
}

static void report_fail(mem_report_t *rep, const mem_region_t *r, uint32_t bg, unsigned elem,
                        uint32_t addr, uint32_t exp, uint32_t act) {
    rep->failbits |= exp ^ act;
    if (rep->nfail++ < REP_MAXLINES) {
        char bits[32];
        fmt_bits(exp ^ act, bits, sizeof bits);
        snprintf(rep->line[rep->nlines++], sizeof rep->line[0],
                 "FAIL mem=%s bg=0x%08" PRIX32 " elem=M%u/r addr=0x%08" PRIX32 " exp=0x%08" PRIX32
                 " act=0x%08" PRIX32 " diff=0x%08" PRIX32 " bits=%s",
                 r->name, bg, elem, addr, exp, act, exp ^ act, bits);
    }
}

/* TODO (Part C): March C- 테이블을 word 단위로 실행. 0/1 대신 D = bg, ~D 를 쓰고 읽는다.
 * background 마다 한 번씩, 읽기 불일치는 report_fail() 로 기록. 반환 = nfail */
static unsigned wmarch_c_minus(const mem_region_t *r, const uint32_t *bgs, unsigned nbg, mem_report_t *rep) {
    memset(rep, 0, sizeof *rep);
    (void)r; (void)bgs; (void)nbg; (void)report_fail;
    return rep->nfail;
}

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static void print_report(const char *title, unsigned nf, const mem_report_t *rep) {
    printf("  [%s] nfail=%u nops=%u failbits=0x%08" PRIX32 "\n", title, nf, rep->nops, rep->failbits);
    for (unsigned i = 0; i < rep->nlines; i++) printf("    %s\n", rep->line[i]);
}

int main(void) {
    const fault_t NOFAULT = { F_NONE, NOADDR, NOADDR };
    sram_t m;
    march_result_t r;
    (void)MMIO_BUS;

    /* ---- Part A: 엔진 + March C- ---- */
    sram_init(&m, 16, NOFAULT, 2);
    CHECK(march_run(&m, &MARCH_C_MINUS, &r) == 0);
    CHECK(r.nops == 10u * 16u);                                  /* 10n */
    CHECK(m.log[80] == 15 && m.log[81] == 15 && m.log[82] == 14); /* M3 은 ⇓ : 15,15,14,... */
    sram_init(&m, 16, NOFAULT, 0);
    CHECK(march_run(&m, &MATS_PLUS, &r) == 0 && r.nops == 5u * 16u);

    sram_init(&m, 16, (fault_t){ F_SA1, 5, NOADDR }, 0);
    CHECK(march_run(&m, &MARCH_C_MINUS, &r) == 3);               /* M1 r0, M3 r0, M5 r0 */
    CHECK(r.elem == 1 && r.op == 0 && r.addr == 5 && r.exp == 0 && r.act == 1);
    sram_init(&m, 16, (fault_t){ F_SA0, 9, NOADDR }, 0);
    CHECK(march_run(&m, &MARCH_C_MINUS, &r) == 2);               /* M2 r1, M4 r1 */
    CHECK(r.elem == 2 && r.op == 0 && r.addr == 9 && r.exp == 1 && r.act == 0);
    printf("[Part A] SA0@9 first fail: M%d op%d addr=%u exp=%u act=%u (nfail=%u)\n",
           r.elem, r.op, r.addr, r.exp, r.act, r.nfail);

    /* ---- Part B: 개별 결함 → 방향과 power-up 의존성 ---- */
    CHECK(!detects(&MATS_PLUS, (fault_t){ F_TF_DN, 3, NOADDR }, NCOV));   /* MATS+ 는 1->0 실패를 못 읽음 */
    CHECK(detects(&MATS_PP, (fault_t){ F_TF_DN, 3, NOADDR }, NCOV));
    CHECK(detects(&MARCH_C_MINUS, (fault_t){ F_TF_DN, 3, NOADDR }, NCOV));
    CHECK(detects(&MATS_PLUS, (fault_t){ F_CFID_U1, 4, 2 }, NCOV));      /* a < v : 잡음 */
    CHECK(!detects(&MATS_PLUS, (fault_t){ F_CFID_U1, 4, 10 }, NCOV));    /* a > v : 놓침 */
    CHECK(detects(&MARCH_C_MINUS, (fault_t){ F_CFID_U1, 4, 10 }, NCOV));
    CHECK(detects(&MARCH_C_MINUS, (fault_t){ F_CFST_01, 7, 3 }, NCOV));
    CHECK(!detects(&MARCH_C_MINUS, (fault_t){ F_DRDF, 6, NOADDR }, NCOV)); /* 커버리지 밖 */

    const march_alg_t *algs[3] = { &MATS_PLUS, &MATS_PP, &MARCH_C_MINUS };
    static cov_t cov[F_KIND_COUNT][3];
    printf("\n[Part B] guaranteed detection (n=%u cells, all %u power-up states)\n", NCOV, NPOWERUP);
    printf("%-10s %8s %9s %9s %9s\n", "fault", "#faults", "MATS+", "MATS++", "March C-");
    for (unsigned k = F_SA0; k < F_KIND_COUNT; k++) {
        printf("%-10s", FAULT_NAME[k]);
        for (unsigned a = 0; a < 3; a++) {
            cov[k][a] = coverage(algs[a], (fault_kind_t)k, NCOV);
            if (a == 0) printf(" %8u", cov[k][a].total);
            printf(" %8.1f%%", 100.0 * cov[k][a].det / cov[k][a].total);
        }
        printf("\n");
    }
    printf("%-10s %8s %8un %8un %8un\n", "ops/cell", "", ops_per_cell(&MATS_PLUS),
           ops_per_cell(&MATS_PP), ops_per_cell(&MARCH_C_MINUS));
    bool cminus_full = true;
    for (unsigned k = F_SA0; k <= F_CFST_11; k++) cminus_full &= cov[k][2].det == cov[k][2].total;
    CHECK(cminus_full);                                           /* March C-: SAF/TF/AF/CF 100% */
    CHECK(cov[F_DRDF][2].det == 0);
    CHECK(cov[F_TF_UP][0].det == cov[F_TF_UP][0].total && cov[F_TF_DN][0].det == 0);
    CHECK(cov[F_SA0][0].det == cov[F_SA0][0].total && cov[F_AF_ALIAS][0].det == cov[F_AF_ALIAS][0].total);
    CHECK(cov[F_TF_DN][1].det == cov[F_TF_DN][1].total);          /* MATS++ 는 TF 를 잡는다 */
    CHECK(ops_per_cell(&MARCH_C_MINUS) == 10 && ops_per_cell(&MATS_PLUS) == 5);

    /* ---- Part C: ISP_LBUF 256x32, data background ---- */
    static const uint32_t BG_SOLID[1] = { 0x00000000u };
    static const uint32_t BG_ALL[6] = { 0x00000000u, 0x55555555u, 0x33333333u,
                                        0x0F0F0F0Fu, 0x00FF00FFu, 0x0000FFFFu };
    const mem_region_t lbuf = { "ISP_LBUF", LBUF_BASE, LBUF_WORDS, &LBUF_MOCK_BUS };
    const lbuf_faults_t NOF = { -1, 0, 0, -1, -1, 0, 0, 0 };
    mem_report_t rep;
    unsigned nf;
    printf("\n[Part C] word-oriented March C- on ISP_LBUF @0x%08X, %u words\n", LBUF_BASE, LBUF_WORDS);

    lbuf_reset(NOF);
    nf = wmarch_c_minus(&lbuf, BG_ALL, 6, &rep);
    print_report("good, 6 bg", nf, &rep);
    CHECK(nf == 0 && rep.nops == 10u * LBUF_WORDS * 6u);

    lbuf_faults_t f1 = NOF;                                       /* word 0x50 bit2 stuck-at-1 */
    f1.stuck_word = 0x50; f1.stuck_mask = 1u << 2; f1.stuck_val = 1u << 2;
    lbuf_reset(f1);
    nf = wmarch_c_minus(&lbuf, BG_ALL, 6, &rep);
    print_report("SA1 word0x50 bit2", nf, &rep);
    CHECK(nf > 0 && rep.failbits == (1u << 2));
    CHECK(strcmp(rep.line[0], "FAIL mem=ISP_LBUF bg=0x00000000 elem=M1/r addr=0x40020140 "
                              "exp=0x00000000 act=0x00000004 diff=0x00000004 bits=[2]") == 0);

    lbuf_faults_t f2 = NOF;                                       /* 디코더가 word-index bit6 무시 */
    f2.ignore_abit = 6;
    lbuf_reset(f2);
    nf = wmarch_c_minus(&lbuf, BG_ALL, 6, &rep);
    print_report("addr bit6 ignored", nf, &rep);
    CHECK(nf > 0 && strstr(rep.line[0], "elem=M1/r addr=0x40020100 ") != NULL);

    lbuf_faults_t f3 = NOF;                                       /* word 0x21: bit3 ↑ → bit4 = 1 */
    f3.cf_word = 0x21; f3.cf_aggr = 3; f3.cf_vict = 4; f3.cf_val = 1;
    lbuf_reset(f3);
    nf = wmarch_c_minus(&lbuf, BG_SOLID, 1, &rep);
    print_report("intra-word CFid, solid bg only", nf, &rep);
    CHECK(nf == 0);                                               /* solid background 은 놓친다 */
    lbuf_reset(f3);
    nf = wmarch_c_minus(&lbuf, BG_ALL, 6, &rep);
    print_report("intra-word CFid, 6 bg", nf, &rep);
    CHECK(nf > 0 && strstr(rep.line[0], "bg=0x55555555 elem=M2/r addr=0x40020084 ") != NULL);
    CHECK(nf > 0 && strstr(rep.line[0], "bits=[4]") != NULL);

    printf("\n%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
