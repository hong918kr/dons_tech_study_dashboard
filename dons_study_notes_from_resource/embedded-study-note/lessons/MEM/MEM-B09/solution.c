/* MEM-B09 March 테스트: MATS+, March C-
 *   Part A — March 표기 문자열 파서 + 비트 단위 March 엔진 (알고리즘 = 문자열, 루프 하드코딩 없음)
 *   Part B — 결함 주입 메모리 (SAF/TF/CFin/CFid/CFst/AF) + 알고리즘 x 결함 모델 커버리지 표
 *   Part C — bring-up 콘솔: 문자열 검증(read 기대값 일관성) + word 메모리 실행 + FAIL 리포트 + 시간 추정
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution
 */
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ============================ Part A: 파서 ============================ */
#define MAX_ELEMS 12
#define MAX_OPS   8

typedef enum { DIR_UP, DIR_DOWN, DIR_ANY } dir_t;
typedef enum { OP_R0, OP_R1, OP_W0, OP_W1 } op_t;          /* bit1 = write, bit0 = 값 */
typedef struct { dir_t dir; int nops; op_t ops[MAX_OPS]; } march_elem_t;
typedef struct { int nelem; march_elem_t e[MAX_ELEMS]; } march_alg_t;

typedef enum {
    P_OK = 0, P_ERR_EMPTY, P_ERR_LBRACE, P_ERR_DIR, P_ERR_LPAREN, P_ERR_OP,
    P_ERR_RPAREN, P_ERR_SEP, P_ERR_TOO_MANY_ELEMS, P_ERR_TOO_MANY_OPS, P_ERR_TRAILING
} perr_t;
typedef struct { perr_t code; int pos; } parse_err_t;     /* pos = 바이트 오프셋 (UTF-8) */

/* 방향 토큰: 유니코드 화살표(UTF-8) + ASCII 별칭. 긴 토큰을 먼저 둔다 */
static const struct { const char *tok; dir_t dir; } DIR_TOK[] = {
    { "⇑", DIR_UP },   { "↑", DIR_UP },   { "up", DIR_UP },     { "^", DIR_UP },
    { "⇓", DIR_DOWN }, { "↓", DIR_DOWN }, { "down", DIR_DOWN }, { "v", DIR_DOWN },
    { "⇕", DIR_ANY },  { "↕", DIR_ANY },  { "any", DIR_ANY },   { "*", DIR_ANY },
};

static const char *skip_ws(const char *p) {
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    return p;
}
/* p 에서 방향 토큰을 찾으면 소비한 바이트 수, 없으면 0 */
static int match_dir(const char *p, dir_t *d) {
    for (size_t i = 0; i < sizeof DIR_TOK / sizeof DIR_TOK[0]; i++) {
        size_t L = strlen(DIR_TOK[i].tok);
        if (strncmp(p, DIR_TOK[i].tok, L) == 0) { *d = DIR_TOK[i].dir; return (int)L; }
    }
    return 0;
}

/* alg := '{' elem (';' elem)* [';'] '}'   elem := dir '(' op (',' op)* ')'   op := [rRwW][01] */
static perr_t march_parse(const char *s, march_alg_t *out, parse_err_t *err) {
    const char *p = s;
    memset(out, 0, sizeof *out);
#define PFAIL(c) do { err->code = (c); err->pos = (int)(p - s); return (c); } while (0)
    p = skip_ws(p);
    if (*p == '\0') PFAIL(P_ERR_EMPTY);
    if (*p != '{') PFAIL(P_ERR_LBRACE);
    p = skip_ws(p + 1);
    for (;;) {
        if (*p == '}' && out->nelem > 0) break;            /* "...;}" 끝의 ';' 허용 */
        if (out->nelem == MAX_ELEMS) PFAIL(P_ERR_TOO_MANY_ELEMS);
        march_elem_t *e = &out->e[out->nelem];
        int k = match_dir(p, &e->dir);
        if (k == 0) PFAIL(P_ERR_DIR);
        p = skip_ws(p + k);
        if (*p != '(') PFAIL(P_ERR_LPAREN);
        p = skip_ws(p + 1);
        for (;;) {
            if (e->nops == MAX_OPS) PFAIL(P_ERR_TOO_MANY_OPS);
            bool rd = (*p == 'r' || *p == 'R'), wr = (*p == 'w' || *p == 'W');
            if (!(rd || wr) || (p[1] != '0' && p[1] != '1')) PFAIL(P_ERR_OP);
            e->ops[e->nops++] = (op_t)((wr ? 2 : 0) + (p[1] - '0'));
            p = skip_ws(p + 2);
            if (*p == ',') { p = skip_ws(p + 1); continue; }
            if (*p == ')') { p = skip_ws(p + 1); break; }
            PFAIL(P_ERR_RPAREN);
        }
        out->nelem++;
        if (*p == ';') { p = skip_ws(p + 1); continue; }
        if (*p == '}') break;
        PFAIL(P_ERR_SEP);
    }
    p = skip_ws(p + 1);
    if (*p != '\0') PFAIL(P_ERR_TRAILING);
#undef PFAIL
    err->code = P_OK; err->pos = (int)(p - s);
    return P_OK;
}

static int march_ops_per_cell(const march_alg_t *a) {      /* k in "k·n" */
    int k = 0;
    for (int i = 0; i < a->nelem; i++) k += a->e[i].nops;
    return k;
}

/* =================== 결함 주입 비트 메모리 (DUT 모델) =================== */
#define MAX_CELLS  32
#define MAX_FAULTS 2

typedef enum {
    F_NONE, F_SAF0, F_SAF1, F_TF_UP, F_TF_DOWN,
    F_CFIN_UP, F_CFIN_DOWN,                          /* <a↑;~v> <a↓;~v>          */
    F_CFID_U0, F_CFID_U1, F_CFID_D0, F_CFID_D1,      /* <a↑;0> <a↑;1> <a↓;0> <a↓;1> */
    F_CFST_00, F_CFST_01, F_CFST_10, F_CFST_11,      /* <a=x; v=y>                */
    F_AF_ALIAS,                                      /* 주소 v → 셀 a (셀 v 는 고아) */
    F_AF_NOACC,                                      /* 주소 v → 셀 없음, 읽으면 val */
    F_AF_MULTI                                       /* 주소 v → 셀 v 와 셀 a, 읽기 val=0 AND / 1 OR */
} fkind_t;

typedef struct { fkind_t kind; int v; int a; int val; } fault_t;   /* a = -1 : 안 씀 */
typedef struct { int n; uint8_t cell[MAX_CELLS]; int nf; fault_t f[MAX_FAULTS]; } bmem_t;

static bool is_dyn_cf(fkind_t k) { return k >= F_CFIN_UP && k <= F_CFID_D1; }

/* 상태 결함 (항상 성립): SAF, CFst — 매 접근 뒤 다시 강제 (주어짐) */
static void bmem_settle(bmem_t *m) {
    for (int i = 0; i < m->nf; i++) {
        const fault_t *f = &m->f[i];
        if (f->kind == F_SAF0) m->cell[f->v] = 0;
        else if (f->kind == F_SAF1) m->cell[f->v] = 1;
        else if (f->kind >= F_CFST_00 && f->kind <= F_CFST_11) {
            int k = (int)(f->kind - F_CFST_00), x = k >> 1, y = k & 1;
            if (m->cell[f->a] == x) m->cell[f->v] = (uint8_t)y;
        }
    }
}

/* 셀 c 에 d 를 저장: TF(전이 실패) + 동적 coupling(CFin/CFid) 의미 */
static void cell_store(bmem_t *m, int c, int d) {
    uint8_t old = m->cell[c], nw = (uint8_t)d;
    for (int i = 0; i < m->nf; i++) {
        const fault_t *f = &m->f[i];
        if (f->v != c) continue;
        if (f->kind == F_TF_UP && old == 0 && d == 1) nw = 0;
        if (f->kind == F_TF_DOWN && old == 1 && d == 0) nw = 1;
    }
    m->cell[c] = nw;
    if (nw == old) return;                         /* 전이가 없으면 coupling 도 없다 */
    bool up = (nw == 1);
    for (int i = 0; i < m->nf; i++) {
        const fault_t *f = &m->f[i];
        if (f->a != c || !is_dyn_cf(f->kind)) continue;
        uint8_t *v = &m->cell[f->v];
        switch (f->kind) {
        case F_CFIN_UP:   if (up)  *v ^= 1u; break;
        case F_CFIN_DOWN: if (!up) *v ^= 1u; break;
        case F_CFID_U0:   if (up)  *v = 0; break;
        case F_CFID_U1:   if (up)  *v = 1; break;
        case F_CFID_D0:   if (!up) *v = 0; break;
        case F_CFID_D1:   if (!up) *v = 1; break;
        default: break;
        }
    }
}

/* 주소 디코더 (주어짐): AF 는 주소 → 셀 연결을 바꾼다 */
static int bmem_read(bmem_t *m, int addr) {
    for (int i = 0; i < m->nf; i++) {
        const fault_t *f = &m->f[i];
        if (f->v != addr) continue;
        if (f->kind == F_AF_NOACC) return f->val;
        if (f->kind == F_AF_ALIAS) return m->cell[f->a];
        if (f->kind == F_AF_MULTI)
            return f->val ? (m->cell[f->v] | m->cell[f->a]) : (m->cell[f->v] & m->cell[f->a]);
    }
    return m->cell[addr];
}
static void bmem_write(bmem_t *m, int addr, int d) {
    for (int i = 0; i < m->nf; i++) {
        const fault_t *f = &m->f[i];
        if (f->v != addr) continue;
        if (f->kind == F_AF_NOACC) return;
        if (f->kind == F_AF_ALIAS) { cell_store(m, f->a, d); bmem_settle(m); return; }
        if (f->kind == F_AF_MULTI) { cell_store(m, f->v, d); cell_store(m, f->a, d); bmem_settle(m); return; }
    }
    cell_store(m, addr, d);
    bmem_settle(m);
}
static void bmem_init(bmem_t *m, int n, const fault_t *f, int nf, uint32_t powerup) {
    memset(m, 0, sizeof *m);
    m->n = n; m->nf = nf;
    for (int i = 0; i < nf; i++) m->f[i] = f[i];
    for (int i = 0; i < n; i++) m->cell[i] = (uint8_t)((powerup >> i) & 1u);
    bmem_settle(m);
}

/* ============================ Part A: 엔진 ============================ */
#define TRACE_CAP 64
typedef struct {
    long nops; int nfail;
    int first_elem, first_op, first_addr, first_exp, first_act;
    int ntrace; int trace[TRACE_CAP];                 /* 처음 TRACE_CAP 개 연산의 주소 */
} run_result_t;

static dir_t g_any_as = DIR_UP;                       /* ⇕ 를 무엇으로 돌릴지 (구현 선택) */

static int march_run(const march_alg_t *alg, bmem_t *m, run_result_t *r) {
    memset(r, 0, sizeof *r);
    r->first_elem = -1;
    for (int e = 0; e < alg->nelem; e++) {
        const march_elem_t *el = &alg->e[e];
        dir_t dir = (el->dir == DIR_ANY) ? g_any_as : el->dir;
        for (int k = 0; k < m->n; k++) {
            int addr = (dir == DIR_DOWN) ? m->n - 1 - k : k;
            for (int j = 0; j < el->nops; j++) {           /* 한 주소에서 op 전부 → 다음 주소 */
                op_t op = el->ops[j];
                if (r->ntrace < TRACE_CAP) r->trace[r->ntrace++] = addr;
                r->nops++;
                if (op == OP_W0 || op == OP_W1) { bmem_write(m, addr, op == OP_W1); continue; }
                int exp = (op == OP_R1), act = bmem_read(m, addr);
                if (act != exp) {
                    if (r->nfail == 0) {
                        r->first_elem = e; r->first_op = j; r->first_addr = addr;
                        r->first_exp = exp; r->first_act = act;
                    }
                    r->nfail++;
                }
            }
        }
    }
    return r->nfail;
}

/* ===================== Part B: 커버리지 (보장 검출) ===================== */
static bool uses_a(fkind_t k) { return k >= F_CFIN_UP && k != F_AF_NOACC; }

/* 결함에 관련된 셀(k ≤ 4)의 power-up 값 2^k 가지 × 나머지 셀 {all0, all1} — 전부에서 FAIL 해야 검출 */
static bool detected(const march_alg_t *alg, const fault_t *f, int nf, int n) {
    int inv[2 * MAX_FAULTS], k = 0;
    for (int i = 0; i < nf; i++) {
        int c[2] = { f[i].v, uses_a(f[i].kind) ? f[i].a : -1 };
        for (int t = 0; t < 2; t++) {
            if (c[t] < 0) continue;
            bool dup = false;
            for (int j = 0; j < k; j++) dup |= (inv[j] == c[t]);
            if (!dup) inv[k++] = c[t];
        }
    }
    uint32_t all = (n >= 32) ? 0xFFFFFFFFu : ((1u << n) - 1u);
    for (int rest = 0; rest < 2; rest++) {
        for (uint32_t combo = 0; combo < (1u << k); combo++) {
            uint32_t pu = rest ? all : 0u;
            for (int j = 0; j < k; j++) {
                if ((combo >> j) & 1u) pu |= 1u << inv[j];
                else pu &= ~(1u << inv[j]);
            }
            bmem_t m; run_result_t r;
            bmem_init(&m, n, f, nf, pu);
            if (march_run(alg, &m, &r) == 0) return false;   /* 한 번이라도 PASS → 보장 못 함 */
        }
    }
    return true;
}

/* 커버리지 표의 행 = 결함 모델. 각 행의 모든 인스턴스를 생성 (주어짐) */
enum { R_SAF0, R_SAF1, R_TF_UP, R_TF_DOWN, R_CFIN_U_LO, R_CFIN_U_HI, R_CFIN_D_LO, R_CFIN_D_HI,
       R_CFID_U0, R_CFID_U1, R_CFID_D0, R_CFID_D1, R_CFST, R_AF_ALIAS, R_AF_NOACC, R_AF_MULTI, NROWS };
static const char *const ROW_NAME[NROWS] = {
    "SAF0", "SAF1", "TF<up>", "TF<down>", "CFin<up> a<v", "CFin<up> a>v", "CFin<dn> a<v", "CFin<dn> a>v",
    "CFid<up;0>", "CFid<up;1>", "CFid<dn;0>", "CFid<dn;1>", "CFst (4 kinds)", "AF alias", "AF no-access",
    "AF multi-acc" };

#define MAX_INST (4 * MAX_CELLS * (MAX_CELLS - 1))
static int gen_row(int row, int n, fault_t *out) {
    int c = 0;
    for (int v = 0; v < n; v++) {
        switch (row) {
        case R_SAF0: out[c++] = (fault_t){ F_SAF0, v, -1, 0 }; continue;
        case R_SAF1: out[c++] = (fault_t){ F_SAF1, v, -1, 0 }; continue;
        case R_TF_UP: out[c++] = (fault_t){ F_TF_UP, v, -1, 0 }; continue;
        case R_TF_DOWN: out[c++] = (fault_t){ F_TF_DOWN, v, -1, 0 }; continue;
        case R_AF_NOACC: for (int val = 0; val < 2; val++) out[c++] = (fault_t){ F_AF_NOACC, v, -1, val }; continue;
        default: break;
        }
        for (int a = 0; a < n; a++) {
            if (a == v) continue;
            switch (row) {
            case R_CFIN_U_LO: if (a < v) out[c++] = (fault_t){ F_CFIN_UP, v, a, 0 }; break;
            case R_CFIN_U_HI: if (a > v) out[c++] = (fault_t){ F_CFIN_UP, v, a, 0 }; break;
            case R_CFIN_D_LO: if (a < v) out[c++] = (fault_t){ F_CFIN_DOWN, v, a, 0 }; break;
            case R_CFIN_D_HI: if (a > v) out[c++] = (fault_t){ F_CFIN_DOWN, v, a, 0 }; break;
            case R_CFID_U0: out[c++] = (fault_t){ F_CFID_U0, v, a, 0 }; break;
            case R_CFID_U1: out[c++] = (fault_t){ F_CFID_U1, v, a, 0 }; break;
            case R_CFID_D0: out[c++] = (fault_t){ F_CFID_D0, v, a, 0 }; break;
            case R_CFID_D1: out[c++] = (fault_t){ F_CFID_D1, v, a, 0 }; break;
            case R_CFST:
                for (int t = 0; t < 4; t++) out[c++] = (fault_t){ (fkind_t)(F_CFST_00 + t), v, a, 0 };
                break;
            case R_AF_ALIAS: out[c++] = (fault_t){ F_AF_ALIAS, v, a, 0 }; break;
            case R_AF_MULTI: for (int val = 0; val < 2; val++) out[c++] = (fault_t){ F_AF_MULTI, v, a, val }; break;
            default: break;
            }
        }
    }
    return c;
}

/* 한 행의 검출 수 (분자). 분모 = gen_row 반환값 */
static int coverage_row(const march_alg_t *alg, int row, int n, int *total) {
    static fault_t inst[MAX_INST];
    int cnt = gen_row(row, n, inst), hit = 0;
    for (int i = 0; i < cnt; i++) hit += detected(alg, &inst[i], 1, n);
    *total = cnt;
    return hit;
}

/* ===================== Part C: 검증 + word 메모리 실행 ===================== */
typedef enum { V_OK = 0, V_ERR_READ_UNKNOWN, V_ERR_READ_MISMATCH, V_ERR_NO_READ } verr_t;
typedef struct { verr_t code; int elem, op; } vres_t;

/* 모든 셀이 같은 op 열을 겪으므로 "셀 하나의 기대 상태" 하나로 추적하면 된다 (-1 = 모름) */
static verr_t march_validate(const march_alg_t *alg, vres_t *res) {
    int state = -1; bool any_read = false;
    res->code = V_OK; res->elem = -1; res->op = -1;
    for (int e = 0; e < alg->nelem; e++) {
        for (int j = 0; j < alg->e[e].nops; j++) {
            op_t op = alg->e[e].ops[j];
            int val = (int)op & 1;
            if (op == OP_W0 || op == OP_W1) { state = val; continue; }
            any_read = true;
            if (state < 0)    { res->code = V_ERR_READ_UNKNOWN;  res->elem = e; res->op = j; return res->code; }
            if (state != val) { res->code = V_ERR_READ_MISMATCH; res->elem = e; res->op = j; return res->code; }
        }
    }
    if (!any_read) res->code = V_ERR_NO_READ;
    return res->code;
}

#define WWORDS 64
typedef struct {
    uint32_t base; uint32_t w[WWORDS];
    int sa0_idx; uint32_t sa0_mask;     /* word sa0_idx 의 sa0_mask 비트가 stuck-at-0 (-1 = 없음) */
    int ignore_bit;                     /* word index 의 이 비트를 디코더가 무시 → alias (-1 = 없음) */
} wmem_t;
static int wmem_phys(const wmem_t *m, int idx) { return m->ignore_bit >= 0 ? (idx & ~(1 << m->ignore_bit)) : idx; }
static uint32_t wmem_rd(const wmem_t *m, int idx) { return m->w[wmem_phys(m, idx)]; }
static void wmem_wr(wmem_t *m, int idx, uint32_t d) {
    int p = wmem_phys(m, idx);
    if (p == m->sa0_idx) d &= ~m->sa0_mask;
    m->w[p] = d;
}

typedef struct { char *buf; size_t cap, len; int lines, max_lines; } report_t;
static void report_add(report_t *r, const char *line) {      /* 용량/줄 수 넘으면 버림 (주어짐) */
    size_t L = strlen(line);
    if (!r->buf || r->lines >= r->max_lines || r->len + L + 2 > r->cap) return;
    memcpy(r->buf + r->len, line, L);
    r->len += L;
    r->buf[r->len++] = '\n';
    r->buf[r->len] = '\0';
    r->lines++;
}
static void fmt_fail(char *line, size_t cap, uint32_t addr, uint32_t exp, uint32_t act, int e, int j) {
    uint32_t diff = exp ^ act;
    char bits[64] = "";
    int nb = __builtin_popcount(diff);
    if (nb > 8) snprintf(bits, sizeof bits, "%d bits", nb);
    else {
        size_t o = 0;
        for (int b = 0; b < 32; b++)
            if ((diff >> b) & 1u) o += (size_t)snprintf(bits + o, sizeof bits - o, o ? ",%d" : "%d", b);
    }
    snprintf(line, cap, "FAIL addr=0x%08" PRIX32 " exp=0x%08" PRIX32 " act=0x%08" PRIX32
             " diff=0x%08" PRIX32 " bits=[%s] elem=M%d.op%d", addr, exp, act, diff, bits, e, j);
}

/* word 단위 실행: 0/1 → background D / ~D. 반환 = 실패 수, *nops = 연산 수 */
static int wmarch_run(const march_alg_t *alg, wmem_t *m, uint32_t bg, report_t *rep, long *nops) {
    int nfail = 0;
    *nops = 0;
    for (int e = 0; e < alg->nelem; e++) {
        const march_elem_t *el = &alg->e[e];
        dir_t dir = (el->dir == DIR_ANY) ? g_any_as : el->dir;
        for (int k = 0; k < WWORDS; k++) {
            int idx = (dir == DIR_DOWN) ? WWORDS - 1 - k : k;
            for (int j = 0; j < el->nops; j++) {
                op_t op = el->ops[j];
                uint32_t d = (op == OP_R1 || op == OP_W1) ? ~bg : bg;
                (*nops)++;
                if (op == OP_W0 || op == OP_W1) { wmem_wr(m, idx, d); continue; }
                uint32_t act = wmem_rd(m, idx);
                if (act != d) {
                    char line[160];
                    fmt_fail(line, sizeof line, m->base + 4u * (uint32_t)idx, d, act, e, j);
                    report_add(rep, line);
                    nfail++;
                }
            }
        }
    }
    return nfail;
}

/* 총 시간 [ns] = k · n_words · n_bg · ns_per_op  (주어짐) */
static uint64_t march_test_ns(const march_alg_t *a, uint64_t nwords, unsigned nbg, unsigned ns_per_op) {
    return (uint64_t)march_ops_per_cell(a) * nwords * nbg * ns_per_op;
}

/* ============================ 테스트 하네스 ============================ */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

enum { A_MATSP, A_MATSPP, A_MARCHX, A_MARCHCM, A_MARCHB, NALG };
static const struct { const char *name; const char *text; int k; } LIB[NALG] = {
    { "MATS+",    "{⇕(w0); ⇑(r0,w1); ⇓(r1,w0)}", 5 },
    { "MATS++",   "{*(w0); ^(r0,w1); v(r1,w0,r0)}", 6 },                         /* ASCII 별칭 */
    { "March X",  "{ any(w0) ; up(r0,w1) ; down(r1,w0) ; any(r0) }", 6 },       /* 키워드 별칭 */
    { "March C-", "{⇕(w0); ⇑(r0,w1); ⇑(r1,w0); ⇓(r0,w1); ⇓(r1,w0); ⇕(r0)}", 10 },
    { "March B",  "{⇕(w0); ⇑(r0,w1,r1,w0,r0,w1); ⇑(r1,w0,w1); ⇓(r1,w0,w1,w0); ⇓(r0,w1,w0)}", 17 },
};

static bool alg_equal(const march_alg_t *x, const march_alg_t *y) {
    if (x->nelem != y->nelem) return false;
    for (int i = 0; i < x->nelem; i++) {
        if (x->e[i].dir != y->e[i].dir || x->e[i].nops != y->e[i].nops) return false;
        for (int j = 0; j < x->e[i].nops; j++) if (x->e[i].ops[j] != y->e[i].ops[j]) return false;
    }
    return true;
}
static bool parse_fails(const char *s, perr_t code, int pos) {
    march_alg_t a; parse_err_t er = { P_OK, -1 };
    perr_t c = march_parse(s, &a, &er);
    return c == code && er.code == code && er.pos == pos;
}

int main(void) {
    march_alg_t alg[NALG];
    parse_err_t er;

    /* ---------------- Part A: 파서 ---------------- */
    for (int i = 0; i < NALG; i++) {
        CHECK(march_parse(LIB[i].text, &alg[i], &er) == P_OK);
        CHECK(march_ops_per_cell(&alg[i]) == LIB[i].k);
    }
    CHECK(alg[A_MARCHCM].nelem == 6 && alg[A_MARCHCM].e[0].dir == DIR_ANY && alg[A_MARCHCM].e[3].dir == DIR_DOWN);
    CHECK(alg[A_MARCHCM].e[1].nops == 2 && alg[A_MARCHCM].e[1].ops[0] == OP_R0 && alg[A_MARCHCM].e[1].ops[1] == OP_W1);
    CHECK(alg[A_MARCHB].nelem == 5 && alg[A_MARCHB].e[1].nops == 6 && alg[A_MARCHB].e[3].ops[3] == OP_W0);
    {   /* 유니코드 ↔ ASCII 별칭 ↔ 대문자 op ↔ 공백/줄바꿈/끝 ';' → 같은 element 리스트 */
        march_alg_t x, y, z;
        CHECK(march_parse("{*(w0);^(r0,w1);up(r1,w0);v(r0,w1);down(r1,w0);any(r0)}", &x, &er) == P_OK);
        CHECK(march_parse("\n { ↕ ( W0 ) ;\t↑(R0, w1); ↑(r1,w0); ↓(r0,w1); ↓(r1,w0); ⇕(r0); }  ", &y, &er) == P_OK);
        CHECK(alg_equal(&x, &alg[A_MARCHCM]) && alg_equal(&y, &alg[A_MARCHCM]));
        CHECK(march_parse("{⇑(r0,w1,r1,w0,r0,w1,r1,w0)}", &z, &er) == P_OK && z.e[0].nops == MAX_OPS);
    }
    /* 오류: 코드 + 바이트 위치 */
    CHECK(parse_fails("", P_ERR_EMPTY, 0));
    CHECK(parse_fails("   ", P_ERR_EMPTY, 3));
    CHECK(parse_fails("(w0)", P_ERR_LBRACE, 0));
    CHECK(parse_fails("{}", P_ERR_DIR, 1));
    CHECK(parse_fails("{left(w0)}", P_ERR_DIR, 1));
    CHECK(parse_fails("{\xE2\x87(w0)}", P_ERR_DIR, 1));              /* 잘린 UTF-8 '⇑' */
    CHECK(parse_fails("{⇑ w0)}", P_ERR_LPAREN, 5));                  /* '⇑' = 3바이트 */
    CHECK(parse_fails("{^(r2)}", P_ERR_OP, 3));
    CHECK(parse_fails("{^()}", P_ERR_OP, 3));
    CHECK(parse_fails("{^(r0,)}", P_ERR_OP, 6));
    CHECK(parse_fails("{^(r0 w1)}", P_ERR_RPAREN, 6));
    CHECK(parse_fails("{^(r10)}", P_ERR_RPAREN, 5));
    CHECK(parse_fails("{^(w0) v(r0)}", P_ERR_SEP, 7));
    CHECK(parse_fails("{^(w0)", P_ERR_SEP, 6));
    CHECK(parse_fails("{^(w0)} x", P_ERR_TRAILING, 8));
    CHECK(parse_fails("{^(r0,w1,r1,w0,r0,w1,r1,w0,r0)}", P_ERR_TOO_MANY_OPS, 27));
    CHECK(parse_fails("{^(w0);^(w0);^(w0);^(w0);^(w0);^(w0);^(w0);^(w0);^(w0);^(w0);^(w0);^(w0);^(w0)}",
                      P_ERR_TOO_MANY_ELEMS, 73));

    /* ---------------- Part A: 엔진 ---------------- */
    const int N = 16;
    bmem_t m; run_result_t r;
    for (int i = 0; i < NALG; i++) {
        bmem_init(&m, N, NULL, 0, 0xA5A5u);
        CHECK(march_run(&alg[i], &m, &r) == 0 && r.nops == (long)LIB[i].k * N);
    }
    bmem_init(&m, N, NULL, 0, 0);
    march_run(&alg[A_MARCHCM], &m, &r);
    CHECK(r.trace[0] == 0 && r.trace[15] == 15);                    /* M0 ⇕ = ⇑ */
    CHECK(r.trace[16] == 0 && r.trace[17] == 0 && r.trace[18] == 1); /* M1: 한 주소에서 r0,w1 */
    {   /* ⇓ element 는 주소 n-1 부터 0 까지 (⇑ 의 정확한 역순) */
        bmem_t m2; run_result_t r2; march_alg_t one;
        CHECK(march_parse("{⇓(r0)}", &one, &er) == P_OK);
        bmem_init(&m2, N, NULL, 0, 0);
        march_run(&one, &m2, &r2);
        CHECK(r2.trace[0] == 15 && r2.trace[15] == 0);
    }
    {
        fault_t sa1 = { F_SAF1, 5, -1, 0 };
        bmem_init(&m, N, &sa1, 1, 0);
        march_run(&alg[A_MARCHCM], &m, &r);
        printf("[Part A] March C- on SAF1@5: first fail M%d op%d addr=%d exp=%d act=%d, nfail=%d, nops=%ld\n",
               r.first_elem, r.first_op, r.first_addr, r.first_exp, r.first_act, r.nfail, r.nops);
        CHECK(r.first_elem == 1 && r.first_op == 0 && r.first_addr == 5 && r.first_act == 1 && r.nfail == 3);
    }

    /* ---------------- Part B: 결함 의미 단위 테스트 ---------------- */
    {
        fault_t tf = { F_TF_UP, 3, -1, 0 };
        bmem_init(&m, 8, &tf, 1, 0);
        bmem_write(&m, 3, 1);  CHECK(bmem_read(&m, 3) == 0);        /* 0→1 실패 */
        bmem_init(&m, 8, &tf, 1, 0xFFu);
        bmem_write(&m, 3, 0);  bmem_write(&m, 3, 1);
        CHECK(bmem_read(&m, 3) == 0);                                /* 1→0 은 되고 다시 0→1 실패 */
        fault_t cf = { F_CFIN_UP, 2, 6, 0 };
        bmem_init(&m, 8, &cf, 1, 0);
        bmem_write(&m, 6, 0);  CHECK(bmem_read(&m, 2) == 0);        /* 0→0 은 전이가 아님 */
        bmem_write(&m, 6, 1);  CHECK(bmem_read(&m, 2) == 1);        /* a↑ → v 반전 */
        bmem_write(&m, 6, 1);  CHECK(bmem_read(&m, 2) == 1);
        fault_t id = { F_CFID_D0, 1, 4, 0 };
        bmem_init(&m, 8, &id, 1, 0xFFu);
        bmem_write(&m, 4, 0);  CHECK(bmem_read(&m, 1) == 0);        /* a↓ → v=0 */
        fault_t st = { F_CFST_11, 0, 7, 0 };
        bmem_init(&m, 8, &st, 1, 0);
        bmem_write(&m, 7, 1);  bmem_write(&m, 0, 0);  CHECK(bmem_read(&m, 0) == 1);
        fault_t al = { F_AF_ALIAS, 2, 5, 0 };
        bmem_init(&m, 8, &al, 1, 0);
        bmem_write(&m, 2, 1);  CHECK(bmem_read(&m, 5) == 1 && m.cell[2] == 0);
        fault_t mu = { F_AF_MULTI, 2, 5, 1 };
        bmem_init(&m, 8, &mu, 1, 0);
        bmem_write(&m, 5, 1);  CHECK(bmem_read(&m, 2) == 1);        /* wired-OR 로 셀 5 가 보인다 */
    }

    /* ---------------- Part B: 커버리지 표 ---------------- */
    int hit[NALG][NROWS], tot[NROWS];
    printf("\n[Part B] guaranteed detection, n=%d cells (involved cells: all 2^k power-ups x rest {0,1})\n", N);
    printf("%-15s %5s", "fault model", "#inst");
    for (int i = 0; i < NALG; i++) printf(" %9s", LIB[i].name);
    printf("\n");
    for (int row = 0; row < NROWS; row++) {
        for (int i = 0; i < NALG; i++) hit[i][row] = coverage_row(&alg[i], row, N, &tot[row]);
        printf("%-15s %5d", ROW_NAME[row], tot[row]);
        for (int i = 0; i < NALG; i++) printf(" %8.1f%%", 100.0 * hit[i][row] / tot[row]);
        printf("\n");
    }
    printf("%-15s %5s", "ops (k*n)", "");
    for (int i = 0; i < NALG; i++) printf(" %8dn", march_ops_per_cell(&alg[i]));
    printf("\n");

    CHECK(tot[R_SAF0] == N && tot[R_CFIN_U_LO] == N * (N - 1) / 2 && tot[R_CFST] == 4 * N * (N - 1));
    CHECK(tot[R_AF_NOACC] == 2 * N && tot[R_AF_MULTI] == 2 * N * (N - 1));
    /* 문헌 주장 (van de Goor): 각 알고리즘이 "잡는다" 고 한 모델은 100% 여야 한다 */
    static const int CLAIM[NALG][NROWS] = {
        /* SAF0 SAF1 TFu TFd CFin x4    CFid x4    CFst AFal AFno AFmu */
        { 1, 1, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  0,  1, 1, 1 },   /* MATS+   : AF, SAF        */
        { 1, 1, 1, 1,  0, 0, 0, 0,  0, 0, 0, 0,  0,  1, 1, 1 },   /* MATS++  : + TF           */
        { 1, 1, 1, 1,  1, 1, 1, 1,  0, 0, 0, 0,  0,  1, 1, 1 },   /* March X : + CFin         */
        { 1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 1,  1,  1, 1, 1 },   /* March C-: + CFid, CFst   */
        { 1, 1, 1, 1,  1, 1, 1, 1,  1, 1, 1, 1,  1,  1, 1, 1 },   /* March B : 단일 결함 전부  */
    };
    for (int i = 0; i < NALG; i++) {
        bool ok = true;
        for (int row = 0; row < NROWS; row++) if (CLAIM[i][row] && hit[i][row] != tot[row]) ok = false;
        CHECK(ok);
    }
    /* 주장 밖에서 실제로 빠지는 곳 (측정이 이론과 맞는지) */
    CHECK(hit[A_MATSP][R_TF_DOWN] == 0 && hit[A_MATSP][R_TF_UP] == N);    /* MATS+ 는 ↓ 전이 뒤 읽기가 없다 */
    CHECK(hit[A_MATSPP][R_CFIN_D_LO] == 0 && hit[A_MARCHX][R_CFIN_D_LO] == tot[R_CFIN_D_LO]); /* 끝 ⇕(r0) 의 몫 */
    CHECK(hit[A_MARCHX][R_CFID_U0] < tot[R_CFID_U0]);                     /* March X 는 CFid 를 다 못 잡는다 */

    /* ---------------- Part C: 검증 ---------------- */
    vres_t vr;
    for (int i = 0; i < NALG; i++) CHECK(march_validate(&alg[i], &vr) == V_OK);
    {
        march_alg_t b;
        march_parse("{⇑(r0,w1); ⇓(r1,w0)}", &b, &er);
        CHECK(march_validate(&b, &vr) == V_ERR_READ_UNKNOWN && vr.elem == 0 && vr.op == 0);
        march_parse("{⇕(w0); ⇑(r0,w1); ⇑(r0,w0)}", &b, &er);
        CHECK(march_validate(&b, &vr) == V_ERR_READ_MISMATCH && vr.elem == 2 && vr.op == 0);
        march_parse("{⇕(w0); ⇑(w1,r1,w0,r1)}", &b, &er);
        CHECK(march_validate(&b, &vr) == V_ERR_READ_MISMATCH && vr.elem == 1 && vr.op == 3);
        march_parse("{⇕(w0); ⇑(w1)}", &b, &er);
        CHECK(march_validate(&b, &vr) == V_ERR_NO_READ);
    }

    /* ---------------- Part C: word 메모리 + 리포트 ---------------- */
    printf("\n[Part C] word-oriented March C- on SRAM @0x20000000, %d x 32-bit, bg=0x55555555\n", WWORDS);
    const uint32_t BG = 0x55555555u;
    char buf[1024]; long nops;
    {
        wmem_t w = { .base = 0x20000000u, .sa0_idx = -1, .ignore_bit = -1 };
        report_t rep = { buf, sizeof buf, 0, 0, 4 }; buf[0] = '\0';
        int nf = wmarch_run(&alg[A_MARCHCM], &w, BG, &rep, &nops);
        printf("  [good]           nfail=%d nops=%ld\n", nf, nops);
        CHECK(nf == 0 && nops == 10L * WWORDS && rep.lines == 0);
    }
    {
        wmem_t w = { .base = 0x20000000u, .sa0_idx = 13, .sa0_mask = 1u << 5, .ignore_bit = -1 };
        report_t rep = { buf, sizeof buf, 0, 0, 4 }; buf[0] = '\0';
        int nf = wmarch_run(&alg[A_MARCHCM], &w, BG, &rep, &nops);
        printf("  [SA0 w13 bit5]   nfail=%d\n%s", nf, buf);
        CHECK(nf == 2);                                             /* ~D 를 기대하는 읽기: M2, M4 */
        CHECK(strncmp(buf, "FAIL addr=0x20000034 exp=0xAAAAAAAA act=0xAAAAAA8A diff=0x00000020 bits=[5] elem=M2.op0\n",
                      88) == 0);
    }
    {
        wmem_t w = { .base = 0x20000000u, .sa0_idx = -1, .ignore_bit = 4 };
        report_t rep = { buf, sizeof buf, 0, 0, 3 }; buf[0] = '\0';
        int nf = wmarch_run(&alg[A_MARCHCM], &w, BG, &rep, &nops);
        printf("  [idx bit4 alias] nfail=%d (first %d lines)\n%s", nf, rep.lines, buf);
        CHECK(nf == 128 && rep.lines == 3);
        CHECK(strstr(buf, "FAIL addr=0x20000040 exp=0x55555555 act=0xAAAAAAAA") == buf);
    }
    {   /* 작은 버퍼: 넘치지 않고, 실패 수는 그대로 */
        char tiny[40];
        wmem_t w = { .base = 0x20000000u, .sa0_idx = -1, .ignore_bit = 4 };
        report_t rep = { tiny, sizeof tiny, 0, 0, 100 }; tiny[0] = '\0';
        int nf = wmarch_run(&alg[A_MARCHCM], &w, BG, &rep, &nops);
        CHECK(nf == 128 && rep.lines == 0 && tiny[0] == '\0');
    }
    /* 시간 추정: 256 KiB SRAM = 65536 word, 6 background, 10 ns/access */
    uint64_t t = march_test_ns(&alg[A_MARCHCM], 65536u, 6u, 10u);
    printf("  March C-, 65536 words x 6 bg x 10 ns = %" PRIu64 " ns (%.2f ms); March B = %.2f ms\n",
           t, t / 1e6, march_test_ns(&alg[A_MARCHB], 65536u, 6u, 10u) / 1e6);
    CHECK(t == 39321600u);

    printf("\n%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
