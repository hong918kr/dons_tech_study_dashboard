/* MEM-B09 추가 연습 — 한 번에 검증
 *   연습 1  ⇕ 를 ⇓ 로 돌려도 커버리지 표가 같은가 (⇕ 를 쓸 자격)
 *   연습 2  March Y (8n) 를 문자열 하나로 추가하고 커버리지 측정
 *   연습 3  linked fault: CFid+CFid / TF+CFid 같은 victim — March C- vs March A vs March B
 *   연습 4  march_format(): 정규형 문자열로 되돌리기 + parse(format(x)) == x 왕복
 *   연습 5  bring-up 시간 표: k·n × background × ns/access
 * (Part A/B 엔진·메모리 모델은 solution.c 와 같은 코드를 복사해 넣었다 — 파일 하나로 자기완결)
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
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


/* ======================= 연습 3: linked fault 인스턴스 ======================= */
/* (a) CFid + CFid, 같은 victim, 서로 다른 aggressor 둘 (모든 종류 조합 4x4)
 * (b) 그중 "가리기(masking)" 쌍 = 같은 전이 방향 + 반대 강제값 (<up;0>+<up;1>, <dn;0>+<dn;1>)
 * (c) TF + CFid, 같은 victim (TF 2종 x CFid 4종)                                         */
enum { L_CFID_ALL, L_CFID_MASK, L_TF_CFID, NLROWS };
static const char *const LROW_NAME[NLROWS] = { "CFid+CFid (all)", "CFid+CFid mask", "TF+CFid" };

static int linked_coverage(const march_alg_t *alg, int lrow, int n, int *total) {
    int hit = 0, cnt = 0;
    for (int v = 0; v < n; v++) {
        for (int a1 = 0; a1 < n; a1++) {
            if (a1 == v) continue;
            if (lrow == L_TF_CFID) {
                for (int t = 0; t < 2; t++)
                    for (int c = 0; c < 4; c++) {
                        fault_t f[2] = { { (fkind_t)(F_TF_UP + t), v, -1, 0 }, { (fkind_t)(F_CFID_U0 + c), v, a1, 0 } };
                        cnt++; hit += detected(alg, f, 2, n);
                    }
                continue;
            }
            for (int a2 = 0; a2 < n; a2++) {
                if (a2 == v || a2 == a1) continue;
                for (int c1 = 0; c1 < 4; c1++)
                    for (int c2 = 0; c2 < 4; c2++) {
                        /* c = 2*dir + val : 0=<up;0> 1=<up;1> 2=<dn;0> 3=<dn;1> */
                        bool mask = (c1 >> 1) == (c2 >> 1) && (c1 & 1) != (c2 & 1);
                        if (lrow == L_CFID_MASK && !mask) continue;
                        fault_t f[2] = { { (fkind_t)(F_CFID_U0 + c1), v, a1, 0 }, { (fkind_t)(F_CFID_U0 + c2), v, a2, 0 } };
                        cnt++; hit += detected(alg, f, 2, n);
                    }
            }
        }
    }
    *total = cnt;
    return hit;
}

/* ======================= 연습 4: 정규형 출력 ======================= */
static int march_format(const march_alg_t *a, char *buf, size_t cap, bool ascii) {
    static const char *const UNI[3] = { "⇑", "⇓", "⇕" }, *const ASC[3] = { "up", "down", "any" };
    static const char *const OPS[4] = { "r0", "r1", "w0", "w1" };
    size_t o = 0;
    int w = snprintf(buf, cap, "{");
    if (w < 0 || (size_t)w >= cap) return -1;
    o += (size_t)w;
    for (int i = 0; i < a->nelem; i++) {
        w = snprintf(buf + o, cap - o, "%s%s(", i ? "; " : "", (ascii ? ASC : UNI)[a->e[i].dir]);
        if (w < 0 || (size_t)w >= cap - o) return -1;
        o += (size_t)w;
        for (int j = 0; j < a->e[i].nops; j++) {
            w = snprintf(buf + o, cap - o, "%s%s", j ? "," : "", OPS[a->e[i].ops[j]]);
            if (w < 0 || (size_t)w >= cap - o) return -1;
            o += (size_t)w;
        }
        w = snprintf(buf + o, cap - o, ")");
        if (w < 0 || (size_t)w >= cap - o) return -1;
        o += (size_t)w;
    }
    w = snprintf(buf + o, cap - o, "}");
    if (w < 0 || (size_t)w >= cap - o) return -1;
    return (int)(o + (size_t)w);
}

/* ============================ 하네스 ============================ */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

enum { A_MATSP, A_MATSPP, A_MARCHX, A_MARCHY, A_MARCHCM, A_MARCHA, A_MARCHB, NALG };
static const struct { const char *name; const char *text; int k; } LIB[NALG] = {
    { "MATS+",    "{⇕(w0); ⇑(r0,w1); ⇓(r1,w0)}", 5 },
    { "MATS++",   "{⇕(w0); ⇑(r0,w1); ⇓(r1,w0,r0)}", 6 },
    { "March X",  "{⇕(w0); ⇑(r0,w1); ⇓(r1,w0); ⇕(r0)}", 6 },
    { "March Y",  "{⇕(w0); ⇑(r0,w1,r1); ⇓(r1,w0,r0); ⇕(r0)}", 8 },
    { "March C-", "{⇕(w0); ⇑(r0,w1); ⇑(r1,w0); ⇓(r0,w1); ⇓(r1,w0); ⇕(r0)}", 10 },
    { "March A",  "{⇕(w0); ⇑(r0,w1,w0,w1); ⇑(r1,w0,w1); ⇓(r1,w0,w1,w0); ⇓(r0,w1,w0)}", 15 },
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

int main(void) {
    march_alg_t alg[NALG];
    parse_err_t er;
    for (int i = 0; i < NALG; i++) {
        CHECK(march_parse(LIB[i].text, &alg[i], &er) == P_OK);
        CHECK(march_ops_per_cell(&alg[i]) == LIB[i].k);
    }

    /* ---- 연습 1 + 2: 단일 결함 표, ⇕ = ⇑ 와 ⇕ = ⇓ 둘 다 ---- */
    const int N = 12;                                   /* 2회 x 7 알고리즘 — < 2 s 유지 위해 12셀 */
    int hit[2][NALG][NROWS], tot[NROWS];
    for (int any = 0; any < 2; any++) {
        g_any_as = any ? DIR_DOWN : DIR_UP;
        for (int i = 0; i < NALG; i++)
            for (int row = 0; row < NROWS; row++) hit[any][i][row] = coverage_row(&alg[i], row, N, &tot[row]);
    }
    g_any_as = DIR_UP;
    printf("[연습 1/2] single faults, n=%d, cell = %%(any=up) [/ %%(any=down) if different]\n", N);
    printf("%-15s", "fault model");
    for (int i = 0; i < NALG; i++) printf(" %9s", LIB[i].name);
    printf("\n");
    int ndiff = 0;
    for (int row = 0; row < NROWS; row++) {
        printf("%-15s", ROW_NAME[row]);
        for (int i = 0; i < NALG; i++) {
            if (hit[0][i][row] == hit[1][i][row]) printf(" %8.1f%%", 100.0 * hit[0][i][row] / tot[row]);
            else { printf(" %4.0f/%-4.0f", 100.0 * hit[0][i][row] / tot[row], 100.0 * hit[1][i][row] / tot[row]); ndiff++; }
        }
        printf("\n");
    }
    printf("cells that change with any-direction: %d\n", ndiff);
    CHECK(ndiff == 0);                                      /* 이 7개 알고리즘은 ⇕ 를 정직하게 쓴다 */
    {   /* ⇕ 를 잘못 쓴 알고리즘: March C- 의 M3/M4 를 ⇕ 로 적으면 ⇕=⇑ 일 때 CFid 절반이 빠진다 */
        march_alg_t bad; int t1, t2;
        CHECK(march_parse("{⇕(w0); ⇑(r0,w1); ⇑(r1,w0); ⇕(r0,w1); ⇕(r1,w0); ⇕(r0)}", &bad, &er) == P_OK);
        g_any_as = DIR_UP;   int up = coverage_row(&bad, R_CFID_U0, N, &t1);
        g_any_as = DIR_DOWN; int dn = coverage_row(&bad, R_CFID_U0, N, &t2);
        g_any_as = DIR_UP;
        printf("March C- with M3/M4 written as any: CFid<up;0> %d/%d (any=up) vs %d/%d (any=down)\n", up, t1, dn, t2);
        CHECK(2 * up == t1 && dn == t2);
    }
    /* 연습 2: March Y = TF, CFin 전부 + AF/SAF, CFid 는 일부 */
    bool y_ok = true;
    for (int row = R_SAF0; row <= R_CFIN_D_HI; row++) y_ok &= (hit[0][A_MARCHY][row] == tot[row]);
    for (int row = R_AF_ALIAS; row <= R_AF_MULTI; row++) y_ok &= (hit[0][A_MARCHY][row] == tot[row]);
    CHECK(y_ok);
    CHECK(hit[0][A_MARCHY][R_CFID_U0] < tot[R_CFID_U0]);
    CHECK(hit[0][A_MARCHA][R_CFST] == tot[R_CFST] && hit[0][A_MARCHB][R_CFST] == tot[R_CFST]);

    /* ---- 연습 3: linked faults (n = 8) ---- */
    const int NL = 8;
    int lh[NALG][NLROWS], lt[NLROWS];
    printf("\n[연습 3] linked faults on one victim, n=%d cells\n%-16s %5s", NL, "linked model", "#inst");
    for (int i = 0; i < NALG; i++) printf(" %9s", LIB[i].name);
    printf("\n");
    for (int row = 0; row < NLROWS; row++) {
        for (int i = 0; i < NALG; i++) lh[i][row] = linked_coverage(&alg[i], row, NL, &lt[row]);
        printf("%-16s %5d", LROW_NAME[row], lt[row]);
        for (int i = 0; i < NALG; i++) printf(" %8.1f%%", 100.0 * lh[i][row] / lt[row]);
        printf("\n");
    }
    CHECK(lt[L_CFID_ALL] == NL * (NL - 1) * (NL - 2) * 16 && lt[L_TF_CFID] == NL * (NL - 1) * 8);
    CHECK(lh[A_MARCHCM][L_CFID_MASK] < lt[L_CFID_MASK]);            /* March C- 는 가리기 쌍을 놓친다 */
    CHECK(lh[A_MARCHA][L_CFID_MASK] == lt[L_CFID_MASK] && lh[A_MARCHB][L_CFID_MASK] == lt[L_CFID_MASK]);
    {   /* 구체 예: a1=2 <up;1>, a2=4 <up;0>, v=7 — M1 ⇑ 에서 v 가 1 이 됐다가 다시 0 → r0 통과 */
        fault_t f[2] = { { F_CFID_U1, 7, 2, 0 }, { F_CFID_U0, 7, 4, 0 } };
        bool cm = detected(&alg[A_MARCHCM], f, 2, NL), ma = detected(&alg[A_MARCHA], f, 2, NL);
        printf("linked <up;1>@2 + <up;0>@4 -> v=7: March C- %s, March A %s\n", cm ? "detects" : "MISSES", ma ? "detects" : "MISSES");
        CHECK(!cm && ma);
    }

    /* ---- 연습 4: 정규형 왕복 ---- */
    char buf[256];
    for (int i = 0; i < NALG; i++) {
        march_alg_t back;
        int len = march_format(&alg[i], buf, sizeof buf, false);
        CHECK(len > 0 && march_parse(buf, &back, &er) == P_OK && alg_equal(&back, &alg[i]));
        len = march_format(&alg[i], buf, sizeof buf, true);
        CHECK(len > 0 && march_parse(buf, &back, &er) == P_OK && alg_equal(&back, &alg[i]));
    }
    march_format(&alg[A_MARCHCM], buf, sizeof buf, true);
    printf("\n[연습 4] %s\n", buf);
    CHECK(strcmp(buf, "{any(w0); up(r0,w1); up(r1,w0); down(r0,w1); down(r1,w0); any(r0)}") == 0);
    CHECK(march_format(&alg[A_MARCHB], buf, 20, false) == -1);      /* 작은 버퍼 → -1, 넘치지 않음 */

    /* ---- 연습 5: 시간 표 ---- */
    static const struct { const char *what; uint64_t words; unsigned nbg, ns; } SZ[] = {
        { "128 KiB TCM, 32b, 1 bg, 5 ns",       32768u,     1, 5 },
        { "2 MiB SRAM, 32b, 6 bg, 10 ns",       524288u,    6, 10 },
        { "1 GiB DDR, 64b, 1 bg, 2 ns (seq)",   134217728u, 1, 2 },
    };
    printf("\n[연습 5] k*n*bg*ns\n%-34s", "memory");
    for (int i = 0; i < NALG; i++) printf(" %9s", LIB[i].name);
    printf("\n");
    for (size_t s = 0; s < sizeof SZ / sizeof SZ[0]; s++) {
        printf("%-34s", SZ[s].what);
        for (int i = 0; i < NALG; i++) {
            uint64_t ns = (uint64_t)march_ops_per_cell(&alg[i]) * SZ[s].words * SZ[s].nbg * SZ[s].ns;
            printf(" %7.1fms", ns / 1e6);
        }
        printf("\n");
    }
    CHECK((uint64_t)march_ops_per_cell(&alg[A_MARCHCM]) * 524288u * 6u * 10u == 314572800u);
    CHECK((uint64_t)march_ops_per_cell(&alg[A_MARCHB]) * 134217728u * 2u == 4563402752u);

    printf("\n%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
