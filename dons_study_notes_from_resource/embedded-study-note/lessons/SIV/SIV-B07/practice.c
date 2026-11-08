/* SIV-B07 추가 연습 — 한 번에 검증
 *   연습 1  March 비용: ops/cell 과 실제 테스트 시간
 *   연습 2  DRDF 와 March C-+R (마지막 element 에 읽기 하나 추가)
 *   연습 3  실패 syndrome 으로 결함 종류 진단 (SA0 vs TF-up, SA1 vs TF-down)
 *   연습 4  word 폭 W 에 대한 data background 생성 (1 + ceil(log2 W) 개)
 *   연습 5  intra-word coupling 커버리지: solid background vs 전체 background
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
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
        /* aggressor 가 x 상태이면 victim 을 y 로 강제 */
        unsigned k = (unsigned)(f->kind - F_CFST_00), x = k >> 1, y = k & 1u;
        if (m->cell[f->a] == x) m->cell[f->v] = (uint8_t)y;
        break;
    }
    default: break;
    }
}

/* 물리 셀 c 에 d 를 저장 (디코더를 지난 뒤의 쓰기) */
static void cell_store(sram_t *m, unsigned c, uint8_t d) {
    const fault_t *f = &m->f;
    uint8_t old = m->cell[c], nw = d;
    /* transition fault — victim 은 해당 방향 전이를 못 한다 */
    if (c == f->v && f->kind == F_TF_UP && old == 0 && d == 1) nw = 0;
    if (c == f->v && f->kind == F_TF_DN && old == 1 && d == 0) nw = 1;
    m->cell[c] = nw;
    /* c 가 aggressor 이고 '실제로' 전이했으면 CFin/CFid 를 victim 에 적용 */
    if (c == f->a && nw != old && f->kind >= F_CFIN_UP && f->kind <= F_CFID_D1) {
        bool up = (nw == 1);
        switch (f->kind) {
        case F_CFIN_UP: if (up)  m->cell[f->v] ^= 1u; break;
        case F_CFIN_DN: if (!up) m->cell[f->v] ^= 1u; break;
        case F_CFID_U0: if (up)  m->cell[f->v] = 0; break;
        case F_CFID_U1: if (up)  m->cell[f->v] = 1; break;
        case F_CFID_D0: if (!up) m->cell[f->v] = 0; break;
        case F_CFID_D1: if (!up) m->cell[f->v] = 1; break;
        default: break;
        }
    }
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
/* March C- = {⇕(w0); ⇑(r0,w1); ⇑(r1,w0); ⇓(r0,w1); ⇓(r1,w0); ⇕(r0)}  10n */
static const march_alg_t MARCH_C_MINUS = { "March C-", 6, {
    { ANY, 1, { W0 } }, { UP, 2, { R0, W1 } }, { UP, 2, { R1, W0 } },
    { DOWN, 2, { R0, W1 } }, { DOWN, 2, { R1, W0 } }, { ANY, 1, { R0 } } } };

typedef struct {
    unsigned nfail, nops;
    int      elem, op;          /* 첫 실패 위치 (-1 = 없음) */
    unsigned addr;
    uint8_t  exp, act;
    uint32_t synd;              /* bit (e*4 + i) = element e 의 i 번째 op 에서 실패 (연습 3) */
} march_result_t;

/* element 마다 dir 방향으로 주소를 돌며 ops 실행. synd = 실패한 (element, op) 위치 비트맵 */
static unsigned march_run(sram_t *m, const march_alg_t *alg, march_result_t *r) {
    memset(r, 0, sizeof *r);
    r->elem = r->op = -1;
    for (unsigned e = 0; e < alg->nelem; e++) {
        const march_elem_t *el = &alg->e[e];
        for (unsigned k = 0; k < m->n; k++) {
            unsigned addr = (el->dir == DOWN) ? m->n - 1u - k : k;
            for (unsigned i = 0; i < el->nops; i++) {
                op_t op = el->ops[i];
                r->nops++;
                if (op == W0 || op == W1) { sram_write(m, addr, op == W1); continue; }
                uint8_t exp = (op == R1), act = sram_read(m, addr);
                if (act != exp) r->synd |= 1u << (e * 4u + i);
                if (act != exp && r->nfail++ == 0) {
                    r->elem = (int)e; r->op = (int)i; r->addr = addr; r->exp = exp; r->act = act;
                }
            }
        }
    }
    return r->nfail;
}

/* ======================= Part B: 검출률 측정 ======================= */
#define NPOWERUP 4u
#define NCOV     16u

/* '보장 검출' = 어떤 power-up 상태로 시작해도 잡힌다 → 4가지 초기 상태 모두 nfail > 0 */
static bool detects(const march_alg_t *alg, fault_t f, unsigned n) {
    for (unsigned p = 0; p < NPOWERUP; p++) {
        sram_t m;
        march_result_t r;
        sram_init(&m, n, f, p);
        if (march_run(&m, alg, &r) == 0) return false;
    }
    return true;
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

/* ======================= 연습 1: 비용 ======================= */
static double test_time_ms(const march_alg_t *alg, uint32_t words, unsigned nbg, double ns_per_access) {
    return (double)ops_per_cell(alg) * words * nbg * ns_per_access / 1e6;
}

/* ======================= 연습 2: March C-+R ======================= */
/* 마지막 ⇕(r0) 를 ⇕(r0,r0) 로: 첫 읽기가 셀을 뒤집으면 두 번째 읽기가 잡는다. 11n */
static const march_alg_t MARCH_C_R = { "March C-+R", 6, {
    { ANY, 1, { W0 } }, { UP, 2, { R0, W1 } }, { UP, 2, { R1, W0 } },
    { DOWN, 2, { R0, W1 } }, { DOWN, 2, { R1, W0 } }, { ANY, 2, { R0, R0 } } } };

/* ======================= 연습 3: syndrome ======================= */
#define SY(e, i) (1u << ((e) * 4u + (i)))
static uint32_t syndrome(fault_t f, unsigned powerup) {
    sram_t m;
    march_result_t r;
    sram_init(&m, NCOV, f, powerup);
    march_run(&m, &MARCH_C_MINUS, &r);
    return r.synd;
}
static void synd_str(uint32_t s, char *out, size_t sz) {
    size_t len = 0;
    out[0] = '\0';
    for (unsigned b = 0; b < 32; b++)
        if ((s >> b) & 1u) len += (size_t)snprintf(out + len, sz - len, len ? " M%u.op%u" : "M%u.op%u", b / 4u, b % 4u);
    if (len == 0) snprintf(out, sz, "(pass)");
}

/* ======================= 연습 4: data background ======================= */
static unsigned make_backgrounds(unsigned w, uint64_t *out) {
    unsigned nb = 0;
    out[nb++] = 0;                                    /* solid: 모든 비트 같은 방향 */
    for (unsigned k = 0; (1u << k) < w; k++) {        /* 비트 번호의 k 번째 자리가 0 인 비트 = 1 */
        uint64_t bg = 0;
        for (unsigned b = 0; b < w; b++)
            if (!((b >> k) & 1u)) bg |= (uint64_t)1 << b;
        out[nb++] = bg;
    }
    return nb;
}
static bool separates_all_pairs(unsigned w, const uint64_t *bg, unsigned nb) {
    for (unsigned i = 0; i < w; i++)
        for (unsigned j = i + 1; j < w; j++) {
            bool sep = false;
            for (unsigned k = 0; k < nb; k++) sep |= (((bg[k] >> i) ^ (bg[k] >> j)) & 1u) != 0;
            if (!sep) return false;
        }
    return true;
}

/* ======================= 연습 5: 1-word intra-word coupling 모델 ======================= */
enum { W_CFID_U0, W_CFID_U1, W_CFID_D0, W_CFID_D1, W_CFIN_U, W_CFIN_D, W_KINDS };
static const char *const W_NAME[W_KINDS] = { "CFid<u;0>", "CFid<u;1>", "CFid<d;0>", "CFid<d;1>", "CFin<u;~>", "CFin<d;~>" };
typedef struct { uint32_t w; unsigned aggr, vict, kind; } wcf_t;

static void wcf_write(wcf_t *c, uint32_t v) {
    uint32_t am = 1u << c->aggr, vm = 1u << c->vict, nw = v;
    bool up = !(c->w & am) && (nw & am), dn = (c->w & am) && !(nw & am);
    switch (c->kind) {
    case W_CFID_U0: if (up) nw &= ~vm; break;
    case W_CFID_U1: if (up) nw |= vm;  break;
    case W_CFID_D0: if (dn) nw &= ~vm; break;
    case W_CFID_D1: if (dn) nw |= vm;  break;
    case W_CFIN_U:  if (up) nw ^= vm;  break;   /* victim 이 '쓰려던 값'의 반대가 된다 */
    case W_CFIN_D:  if (dn) nw ^= vm;  break;
    default: break;
    }
    c->w = nw;
}
/* 1 word 짜리 메모리에 word March C- (bg 별) — 두 power-up(0, ~0) 모두에서 잡혀야 '보장' */
static bool wcf_detect(unsigned kind, unsigned aggr, unsigned vict, const uint64_t *bgs, unsigned nb) {
    for (unsigned p = 0; p < 2; p++) {
        wcf_t c = { p ? 0xFFFFFFFFu : 0u, aggr, vict, kind };
        bool hit = false;
        for (unsigned b = 0; b < nb; b++)
            for (unsigned e = 0; e < MARCH_C_MINUS.nelem; e++)
                for (unsigned i = 0; i < MARCH_C_MINUS.e[e].nops; i++) {
                    op_t op = MARCH_C_MINUS.e[e].ops[i];
                    uint32_t d = (uint32_t)((op == R1 || op == W1) ? ~bgs[b] : bgs[b]);
                    if (op == W0 || op == W1) wcf_write(&c, d);
                    else hit |= (c.w != d);
                }
        if (!hit) return false;
    }
    return true;
}

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
#define NEAR(a, b) ((a) - (b) < 1e-9 && (b) - (a) < 1e-9)

int main(void) {
    /* 연습 1 */
    CHECK(ops_per_cell(&MATS_PLUS) == 5 && ops_per_cell(&MATS_PP) == 6);
    CHECK(ops_per_cell(&MARCH_C_MINUS) == 10 && ops_per_cell(&MARCH_C_R) == 11);
    double t_c = test_time_ms(&MARCH_C_MINUS, 65536u, 1, 10.0);
    double t_c6 = test_time_ms(&MARCH_C_MINUS, 65536u, 6, 10.0);
    double t_m = test_time_ms(&MATS_PLUS, 65536u, 1, 10.0);
    double t_n2 = 65536.0 * 65536.0 * 10.0 / 1e6;      /* O(n^2) 패턴의 하한 n^2 접근 */
    printf("[1] 64Ki words @10ns/access: MATS+ %.4f ms, March C- %.4f ms, March C- x6 bg %.4f ms, n^2 %.1f ms\n",
           t_m, t_c, t_c6, t_n2);
    CHECK(NEAR(t_c, 6.5536) && NEAR(t_m, 3.2768) && NEAR(t_c6, 39.3216));

    /* 연습 2 */
    cov_t d_c = coverage(&MARCH_C_MINUS, F_DRDF, NCOV), d_r = coverage(&MARCH_C_R, F_DRDF, NCOV);
    printf("[2] DRDF guaranteed detection: March C- %u/%u, March C-+R %u/%u\n", d_c.det, d_c.total, d_r.det, d_r.total);
    CHECK(d_c.det == 0 && d_r.det == d_r.total);
    bool still_full = true;
    for (unsigned k = F_SA0; k <= F_CFST_11; k++) {
        cov_t c = coverage(&MARCH_C_R, (fault_kind_t)k, NCOV);
        still_full &= c.det == c.total;
    }
    CHECK(still_full);

    /* 연습 3 */
    const fault_kind_t single[4] = { F_SA0, F_TF_UP, F_SA1, F_TF_DN };
    printf("[3] March C- syndrome at victim cell 7 (M<elem>.op<idx>)\n");
    for (unsigned i = 0; i < 4; i++)
        for (unsigned p = 0; p < 2; p++) {
            char buf[128];
            synd_str(syndrome((fault_t){ single[i], 7, NOADDR }, p), buf, sizeof buf);
            printf("    %-8s power-up=%s : %s\n", FAULT_NAME[single[i]], p ? "all1" : "all0", buf);
        }
    bool sa0_eq_tfup = true;
    for (unsigned p = 0; p < NPOWERUP; p++)
        sa0_eq_tfup &= syndrome((fault_t){ F_SA0, 7, NOADDR }, p) == syndrome((fault_t){ F_TF_UP, 7, NOADDR }, p);
    CHECK(sa0_eq_tfup);
    CHECK(syndrome((fault_t){ F_SA0, 7, NOADDR }, 0) == (SY(2, 0) | SY(4, 0)));
    CHECK(syndrome((fault_t){ F_SA1, 7, NOADDR }, 0) == (SY(1, 0) | SY(3, 0) | SY(5, 0)));
    CHECK(syndrome((fault_t){ F_TF_DN, 7, NOADDR }, 0) == (SY(3, 0) | SY(5, 0)));      /* SA1 과 구별됨 */
    CHECK(syndrome((fault_t){ F_TF_DN, 7, NOADDR }, 1) == syndrome((fault_t){ F_SA1, 7, NOADDR }, 1));

    /* 연습 4 */
    uint64_t bg[8];
    const unsigned widths[5] = { 8, 12, 16, 32, 64 }, expect_nb[5] = { 4, 5, 5, 6, 7 };
    for (unsigned i = 0; i < 5; i++) {
        unsigned nb = make_backgrounds(widths[i], bg);
        CHECK(nb == expect_nb[i] && separates_all_pairs(widths[i], bg, nb));
    }
    unsigned nb32 = make_backgrounds(32, bg);
    printf("[4] W=32 backgrounds:");
    for (unsigned k = 0; k < nb32; k++) printf(" 0x%08llX", (unsigned long long)bg[k]);
    printf("\n");
    CHECK(bg[1] == 0x55555555u && bg[2] == 0x33333333u && bg[5] == 0x0000FFFFu);
    CHECK(!separates_all_pairs(32, bg, 1));

    /* 연습 5 */
    printf("[5] intra-word coupling, 32-bit word, all %u ordered bit pairs\n", 32u * 31u);
    printf("    %-10s %10s %10s\n", "fault", "solid bg", "6 bg");
    unsigned tot_s = 0, tot_a = 0, tot = 0;
    for (unsigned k = 0; k < W_KINDS; k++) {
        unsigned ds = 0, da = 0, n = 0;
        for (unsigned a = 0; a < 32; a++)
            for (unsigned v = 0; v < 32; v++) {
                if (a == v) continue;
                n++;
                ds += wcf_detect(k, a, v, bg, 1);
                da += wcf_detect(k, a, v, bg, nb32);
            }
        printf("    %-10s %9.1f%% %9.1f%%\n", W_NAME[k], 100.0 * ds / n, 100.0 * da / n);
        tot_s += ds; tot_a += da; tot += n;
    }
    printf("    %-10s %9.1f%% %9.1f%%\n", "all", 100.0 * tot_s / tot, 100.0 * tot_a / tot);
    CHECK(tot_a == tot && tot_s < tot);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
