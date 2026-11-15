/* ARCH-A06 비순차 실행 개념 모델: 스코어보드와 ROB
 *
 * 작은 레지스터 ISA (toy ISA 가 아니다 — 지연이 다른 여러 사이클 FU 가 필요해서 새로 정의):
 *   ADD/SUB/MUL/DIV rd, rs, rt    ADDI rd, rs, imm    LD rd, imm(rs)    ST rt, imm(rs)
 *   r0..r7, r0 = 0 고정(쓰기 무시). 32-bit 값, DIV 는 부호 없는 나눗셈. 데이터 메모리 64 워드(워드 주소).
 *   예외: DIV 의 rt == 0 → DIV0, LD/ST 주소 ≥ 64 → MEM (둘 다 execute 에서 발견).
 *
 *   Part A  Tomasulo (예약 스테이션 + 레지스터 상태표 → ROB 태그 + CDB 1개 + ROB, in-order commit)
 *           TODO: rename_operands (issue 때 피연산자 읽기/태그 달기), cdb_write (브로드캐스트 + 깨우기)
 *           + CDC 6600 식 스코어보드 (리네이밍 없음) TODO: sb_issue_ok (구조·WAW), sb_write_ok (WAR)
 *           → WAR/WAW 가 있는 시퀀스에서 스코어보드는 멈추고 Tomasulo 는 안 멈추는 것을 사이클로 비교.
 *   Part B  TODO: commit (in-order, 예외는 commit 에서 받고 젊은 명령 flush) → 무작위 프로그램에서
 *           아키텍처 상태 == 순차 기준 인터프리터(예외 명령 직전까지). ROB 없는 Tomasulo(CDB 때 레지스터
 *           파일에 씀)는 같은 프로그램에서 부정확(imprecise)한 상태를 남긴다.
 *   Part C  ROB 크기 · RS 개수 sweep → 커널 3개(의존 사슬 / 독립 MUL / 내적)의 IPC 표.
 *
 * 사이클 규칙 (한 사이클 안에서 뒤 단계부터 처리, "t-1 까지 생긴 것만 쓴다"):
 *   commit(t)   : 머리 엔트리가 t-1 까지 완료(CDB/ST 확정) → 레지스터 파일·메모리에 반영, 최대 W 개
 *   CDB(t)      : 실행이 t-1 까지 끝난 것 중 가장 오래된 1 개만 방송 (ST 는 CDB 를 안 쓰고 확정만)
 *   execute(t)  : 피연산자가 t-1 까지 모였고 FU 가 비면 시작 (오래된 것부터). 끝 = t + lat - 1
 *   issue(t)    : 사이클당 1 개, in-order. ROB 빈칸(사이클 시작 시점 기준) + 해당 클래스 RS 빈칸 필요.
 *                 같은 사이클 CDB 값은 issue 가 잡는다(bypass). RS 는 CDB 쓰기 때 해제(H&P 방식).
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so
 */
#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NREG 8
#define MEMW 64
#define MAXPROG 512
#define MAXROB 64
#define MAXRS 16
#define MAXFU 4

typedef enum { OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_ADDI, OP_LD, OP_ST, NOP } op_t;
static const char *const OP_NAME[NOP] = {"ADD", "SUB", "MUL", "DIV", "ADDI", "LD", "ST"};
typedef enum { FU_ADD, FU_MUL, FU_DIV, FU_MEM, NFU } fu_t;
static const char *const FU_NAME[NFU] = {"ADD", "MUL", "DIV", "MEM"};
static const fu_t OP_FU[NOP] = {FU_ADD, FU_ADD, FU_MUL, FU_DIV, FU_ADD, FU_MEM, FU_MEM};
typedef enum { EXC_NONE, EXC_DIV0, EXC_MEM } exc_t;
static const char *const EXC_NAME[3] = {"none", "DIV0", "MEM"};

typedef struct { op_t op; uint8_t rd, rs, rt; int32_t imm; } insn_t;
typedef struct { uint32_t r[NREG]; uint32_t mem[MEMW]; } arch_t;

/* 피연산자 j, k 와 목적지. 없으면 -1. r0 목적지는 "쓰기 없음" (-1) 으로 본다. */
static int src_j(const insn_t *in) { return in->rs; }
static int src_k(const insn_t *in) {
    return (in->op == OP_ADDI || in->op == OP_LD) ? -1 : in->rt;
}
static int dst(const insn_t *in) { return (in->op == OP_ST || in->rd == 0) ? -1 : in->rd; }

/* 명령 하나의 결과. a = j 값, b = k 값. LD/ST 는 주소를 *ea 에 (범위 검사 포함). */
static uint32_t alu(const insn_t *in, uint32_t a, uint32_t b, uint32_t *ea, exc_t *exc) {
    *exc = EXC_NONE;
    switch (in->op) {
    case OP_ADD: return a + b;
    case OP_SUB: return a - b;
    case OP_MUL: return a * b;
    case OP_DIV: if (b == 0) { *exc = EXC_DIV0; return 0; } return a / b;
    case OP_ADDI: return a + (uint32_t)in->imm;
    case OP_LD: case OP_ST:
        *ea = a + (uint32_t)in->imm;
        if (*ea >= MEMW) *exc = EXC_MEM;
        return b;                                   /* ST: 저장할 데이터 */
    default: return 0;
    }
}

/* ---------------- 순차 기준 인터프리터 (정답의 정의) ---------------- */
/* 예외가 나면 그 명령은 아무것도 안 바꾸고 멈춘다. 반환 = 예외 명령 번호 (없으면 n). */
static int ref_run(const insn_t *p, int n, arch_t *A, exc_t *exc) {
    *exc = EXC_NONE;
    for (int i = 0; i < n; i++) {
        const insn_t *in = &p[i];
        uint32_t a = A->r[src_j(in)], b = src_k(in) >= 0 ? A->r[src_k(in)] : 0, ea = 0;
        exc_t e;
        uint32_t v = alu(in, a, b, &ea, &e);
        if (e != EXC_NONE) { *exc = e; return i; }
        if (in->op == OP_LD) v = A->mem[ea];
        if (in->op == OP_ST) A->mem[ea] = v;
        else if (dst(in) >= 0) A->r[dst(in)] = v;
    }
    return n;
}

/* ---------------- 설정 ---------------- */
typedef struct {
    int lat[NFU];     /* 실행 지연 (사이클). ST 는 주소 계산만 1 사이클 */
    int nfu[NFU];     /* FU 개수 */
    int piped[NFU];   /* 1 = 매 사이클 새 연산 시작 가능 */
    int nrs[NFU];     /* 예약 스테이션 개수 */
    int rob;          /* ROB 크기 (ROB 없는 모드에선 태그 개수) */
    int commit_w;     /* 사이클당 commit 최대 개수 */
    bool use_rob;     /* false = ROB 없는 Tomasulo: CDB 때 레지스터 파일/메모리에 바로 씀 */
} cfg_t;

static cfg_t cfg_default(void) {      /* 지연은 가정(대표값): ADD 2, MUL 6, DIV 12, LD 3 */
    cfg_t c = {{2, 6, 12, 3}, {1, 1, 1, 1}, {1, 1, 0, 1}, {3, 2, 2, 3}, 8, 1, true};
    return c;
}
static int op_lat(const cfg_t *c, op_t op) { return op == OP_ST ? 1 : c->lat[OP_FU[op]]; }

/* ---------------- Tomasulo + ROB 시뮬레이터 ---------------- */
typedef struct { int iss, xs, xe, wr, cmt; exc_t exc; bool flushed; } tl_t;   /* 0 = 아직/없음 */

typedef struct {
    bool busy; int idx;          /* 프로그램 번호 (= 나이, 분기 없는 직선 코드라 동적 순서와 같다) */
    int rd;                      /* 목적 레지스터 (-1 = 없음) */
    bool fin; int xe;            /* 실행 끝남 (CDB 대기) */
    bool done; int done_cyc;     /* 결과가 ROB 에 있음 (CDB 쓴 사이클; ST 는 주소·데이터 확정 사이클) */
    uint32_t val, addr; exc_t exc;
    int rsf, rsi;                /* 이 명령이 잡고 있는 RS (해제용) */
} rob_t;

typedef struct {
    bool busy, started; int rob;
    uint32_t vj, vk; int qj, qk; /* q = -1 이면 v 가 유효, 아니면 그 ROB 태그를 기다림 */
    int rdy;                     /* 피연산자가 다 모인 사이클 (실행은 그 다음 사이클부터) */
    int freed;                   /* 해제된 사이클 (다음 사이클부터 재사용) */
} rs_t;

typedef struct {
    long cdb, stall_rob, stall_rs, flushed, occ_sum;
} stats_t;

typedef struct {
    cfg_t c; const insn_t *prog; int n;
    arch_t a;                    /* 아키텍처 상태: 레지스터 파일 + 메모리 */
    rob_t rob[MAXROB]; int head, count;
    rs_t rs[NFU][MAXRS];
    int regstat[NREG];           /* -1 = 레지스터 파일이 최신, 아니면 값을 만들 ROB 태그 */
    int fu_free[NFU][MAXFU];
    int pc, cyc, committed, last_event;
    bool stopped, deadlock;
    int exc_idx; exc_t exc;      /* 받은 예외 (exc_idx = -1: 없음) */
    tl_t tl[MAXPROG];
    stats_t st;
} sim_t;

static int rob_slot(const sim_t *s, int k) { return (s->head + k) % s->c.rob; }

/* ======================= Part A: 리네이밍 + CDB (TODO) ======================= */

/* TODO(Part A-1): issue 때 호출된다. 하네스가 ROB 엔트리 robi 와 RS 엔트리 r 을 이미 잡아 두었다 (r->rob = robi, busy).
 * 할 일: 두 소스 피연산자(src_j, src_k; -1 이면 없음 → 값 0, 준비됨)를 읽어 r->vj/qj, r->vk/qk 를 채우고,
 *       r->rdy 를 정하고, 목적지(dst)가 있으면 레지스터 상태표를 이 엔트리로 바꾼다. */
static void rename_operands(sim_t *s, rs_t *r, int robi, const insn_t *in, int t) {
    /* 피연산자마다 세 경우: 레지스터 파일이 최신 / 생산자가 끝났지만 commit 전 / 아직 계산 중. 그리고 리네이밍. */
    (void)s; (void)r; (void)robi; (void)in; (void)t;
}

/* TODO(Part A-2): CDB 사이클 t 에 ROB 엔트리 e 의 결과(s->rob[e].val)를 방송한다 (하네스가 가장 오래된 것을 골랐고,
 * ROB 없는 모드의 예외는 하네스가 먼저 처리했다). 할 일: ROB 엔트리를 완료로 표시(done, done_cyc),
 * 이 태그를 기다리는 모든 RS 를 깨우고(값 채우기, q = -1, rdy 갱신), ROB 없는 모드면 레지스터 파일에 바로 쓴다
 * (단, 상태표가 아직 이 태그를 가리킬 때만 — 더 젊은 쓰기가 있으면 WAW: 쓰지 않는다). */
static void cdb_write(sim_t *s, int e, int t) {
    /* 완료 표시 → 기다리는 RS 깨우기 → (ROB 없음) 레지스터 파일 쓰기 (WAW 조건) */
    (void)s; (void)e; (void)t;
}

/* ======================= Part B: in-order commit (TODO) ======================= */

static void flush_all(sim_t *s, int t);   /* 하네스: ROB · RS · 상태표를 모두 비우고 멈춤 */

/* TODO(Part B): ROB 모드에서만 불린다. 머리부터 최대 c.commit_w 개: 머리 엔트리가 완료(done, done_cyc < t)면 commit.
 *   예외가 표시된 엔트리 → s->exc_idx/exc 기록, flush_all(s, t) 하고 끝 (그 명령은 아무것도 안 씀).
 *   ST → 메모리에 쓰기. 목적지가 있으면 레지스터 파일에 쓰고, 상태표가 아직 이 엔트리를 가리키면 -1 로.
 *   엔트리 해제(busy=false, head 전진, count--), s->tl[idx].cmt = t, s->committed++, s->last_event = t. */
static void commit(sim_t *s, int t) {
    /* 머리부터 순서대로, 완료된 것만, 최대 W 개. 예외면 기록 + flush_all 하고 끝. */
    (void)s; (void)t; (void)flush_all;
}

/* ======================= 하네스: 나머지 단계 ======================= */

static void flush_all(sim_t *s, int t) {
    for (int k = 0; k < s->count; k++) {
        rob_t *R = &s->rob[rob_slot(s, k)];
        if (k > 0) { s->tl[R->idx].flushed = true; s->st.flushed++; }
        R->busy = false;
    }
    s->count = 0;
    for (int f = 0; f < NFU; f++) for (int i = 0; i < MAXRS; i++) s->rs[f][i].busy = false;
    for (int r = 0; r < NREG; r++) s->regstat[r] = -1;
    s->stopped = true; s->last_event = t;
}

static void sim_init(sim_t *s, const cfg_t *c, const insn_t *p, int n, const arch_t *init) {
    memset(s, 0, sizeof *s);
    s->c = *c; s->prog = p; s->n = n; s->a = *init; s->a.r[0] = 0;
    for (int r = 0; r < NREG; r++) s->regstat[r] = -1;
    s->exc_idx = -1;
}

/* ST 확정: 실행(주소 계산)이 t-1 까지 끝난 ST 는 CDB 없이 done. ROB 없는 모드에서는 이때 메모리에 쓴다 —
 * 단 더 오래된 메모리 연산이 모두 끝난 뒤에만 (메모리 WAR/WAW 를 막는 최소한의 순서). */
static void store_complete(sim_t *s, int t) {
    for (int k = 0; k < s->count; k++) {
        int e = rob_slot(s, k);
        rob_t *R = &s->rob[e];
        if (s->prog[R->idx].op != OP_ST || !R->fin || R->done || R->xe >= t) continue;
        if (!s->c.use_rob) {
            bool older_pending = false;
            for (int m = 0; m < k; m++) {
                rob_t *O = &s->rob[rob_slot(s, m)];
                op_t op = s->prog[O->idx].op;
                if ((op == OP_ST && !O->done) || (op == OP_LD && !O->fin)) older_pending = true;
            }
            if (older_pending) continue;
            if (R->exc != EXC_NONE) {                     /* ROB 없음: 발견 즉시 예외, 그때의 상태가 남는다 */
                s->exc_idx = R->idx; s->exc = R->exc; s->stopped = true; s->last_event = t;
                return;
            }
            s->a.mem[R->addr] = R->val;
        }
        R->done = true; R->done_cyc = t;
        s->tl[R->idx].wr = t;
        s->rs[R->rsf][R->rsi].busy = false; s->rs[R->rsf][R->rsi].freed = t;
        s->last_event = t;
    }
}

static void do_cdb(sim_t *s, int t) {
    int best = -1;
    for (int k = 0; k < s->count; k++) {                  /* ROB 순서 = 나이 순: 처음 찾은 것이 가장 오래됨 */
        int e = rob_slot(s, k);
        rob_t *R = &s->rob[e];
        if (s->prog[R->idx].op == OP_ST || !R->fin || R->done || R->xe >= t) continue;
        best = e; break;
    }
    if (best < 0) return;
    rob_t *R = &s->rob[best];
    if (!s->c.use_rob && R->exc != EXC_NONE) {            /* ROB 없음: 발견한 그 순간이 예외 시점 */
        s->exc_idx = R->idx; s->exc = R->exc; s->stopped = true; s->last_event = t;
        return;
    }
    cdb_write(s, best, t);
    s->tl[R->idx].wr = t;
    s->rs[R->rsf][R->rsi].busy = false;                   /* H&P: RS 는 결과를 쓸 때 해제 */
    s->rs[R->rsf][R->rsi].freed = t;
    s->st.cdb++; s->last_event = t;
}

/* LD 가 지금 실행해도 되나: 더 오래된 ST 가 모두 주소·데이터 확정(done)이어야 한다. 같은 주소면 가장 젊은
 * 오래된 ST 의 값을 받는다 (store-to-load forwarding). */
static bool load_ready(sim_t *s, int k_ld, uint32_t ea, int t, bool *fwd, uint32_t *v) {
    *fwd = false;
    for (int m = 0; m < k_ld; m++) {
        rob_t *O = &s->rob[rob_slot(s, m)];
        if (s->prog[O->idx].op != OP_ST) continue;
        if (!O->done || O->done_cyc >= t) return false;   /* t-1 까지 확정된 것만 */
        if (O->exc == EXC_NONE && O->addr == ea) { *fwd = true; *v = O->val; }
    }
    return true;
}

static void do_execute(sim_t *s, int t) {
    for (int k = 0; k < s->count; k++) {                  /* 오래된 것부터 */
        int e = rob_slot(s, k);
        rob_t *R = &s->rob[e];
        if (R->fin || R->done) continue;
        rs_t *r = &s->rs[R->rsf][R->rsi];
        if (!r->busy || r->started || r->qj >= 0 || r->qk >= 0 || r->rdy >= t) continue;
        int f = R->rsf, u;
        for (u = 0; u < s->c.nfu[f]; u++) if (s->fu_free[f][u] <= t) break;
        if (u == s->c.nfu[f]) continue;                   /* 이 클래스 FU 가 모두 바쁨 */
        const insn_t *in = &s->prog[R->idx];
        uint32_t ea = 0; exc_t ex;
        uint32_t v = alu(in, r->vj, r->vk, &ea, &ex);
        if (in->op == OP_LD && ex == EXC_NONE) {
            bool fwd; uint32_t fv = 0;
            if (!load_ready(s, k, ea, t, &fwd, &fv)) continue;   /* 메모리 의존: 기다림 */
            v = fwd ? fv : s->a.mem[ea];
        }
        int lat = op_lat(&s->c, in->op);
        r->started = true;
        R->fin = true; R->xe = t + lat - 1; R->val = ex ? 0 : v; R->addr = ea; R->exc = ex;
        s->fu_free[f][u] = s->c.piped[f] ? t + 1 : t + lat;
        s->tl[R->idx].xs = t; s->tl[R->idx].xe = R->xe; s->tl[R->idx].exc = ex;
        s->last_event = t;
    }
}

static void do_issue(sim_t *s, int t, int count_at_start) {
    if (s->pc >= s->n) return;
    const insn_t *in = &s->prog[s->pc];
    fu_t f = OP_FU[in->op];
    if (count_at_start >= s->c.rob || s->count >= s->c.rob) { s->st.stall_rob++; return; }
    int i;
    for (i = 0; i < s->c.nrs[f]; i++) if (!s->rs[f][i].busy && s->rs[f][i].freed < t) break;
    if (i == s->c.nrs[f]) { s->st.stall_rs++; return; }
    int e = rob_slot(s, s->count);
    rob_t *R = &s->rob[e];
    memset(R, 0, sizeof *R);
    R->busy = true; R->idx = s->pc; R->rd = dst(in); R->rsf = f; R->rsi = i;
    rs_t *r = &s->rs[f][i];
    memset(r, 0, sizeof *r);
    r->busy = true; r->rob = e;
    rename_operands(s, r, e, in, t);
    s->count++;
    s->tl[s->pc].iss = t;
    s->pc++; s->last_event = t;
}

/* ROB 없는 모드: 아키텍처 상태는 CDB 때 이미 썼다. 태그(ROB 슬롯)만 순서대로 돌려받는다 — 상태 영향 없음. */
static void retire_tags(sim_t *s) {
    while (s->count > 0) {
        rob_t *R = &s->rob[s->head];
        if (!R->done) break;
        R->busy = false;
        s->head = (s->head + 1) % s->c.rob; s->count--; s->committed++;
    }
}

static void sim_step(sim_t *s) {
    int t = ++s->cyc;
    int count_at_start = s->count;
    s->st.occ_sum += s->count;
    if (s->c.use_rob) commit(s, t); else retire_tags(s);
    if (s->stopped) return;
    store_complete(s, t);
    if (s->stopped) return;
    do_cdb(s, t);
    if (s->stopped) return;
    do_execute(s, t);
    do_issue(s, t, count_at_start);
}

/* 끝까지 돌린다. 반환 = 총 사이클 (교착이면 -1). */
static int sim_run(sim_t *s) {
    while (!s->stopped && !(s->pc >= s->n && s->count == 0)) {
        sim_step(s);
        if (s->cyc - s->last_event > 100 || s->cyc > 200 + 64 * s->n) { s->deadlock = true; return -1; }
    }
    return s->cyc;
}

/* ---------------- 스코어보드 (CDC 6600 식, 리네이밍 없음) ---------------- */
/* 단계: Issue(구조·WAW 검사, in-order) → Read operands(RAW: 생산자가 쓸 때까지) → Execute → Write(WAR 검사).
 * FU 는 issue 부터 write 까지 잡힌다 (예약 스테이션이 없다). 결과 버스 1개(레지스터 쓰기 사이클당 1개).
 * MEM FU 는 1 개로 고정 → 메모리 연산은 직렬 (메모리 순서 문제를 피하는 가장 단순한 방법). */
typedef struct { int iss, ro, xs, xe, wr; int pj, pk; uint32_t a, b, val, ea; } sbi_t;
typedef struct {
    cfg_t c; const insn_t *prog; int n; arch_t a;
    sbi_t I[MAXPROG]; int pc, cyc;
    long stall_struct, stall_waw, stall_war, stall_raw;
} sb_t;

static bool sb_active(const sbi_t *x, int t) { return x->iss > 0 && (x->wr == 0 || x->wr >= t); }
static int sb_units(const sb_t *b, fu_t f) { return f == FU_MEM ? 1 : b->c.nfu[f]; }

/* TODO(Part A-3): 명령 i 를 사이클 t 에 issue 해도 되나?
 *   구조 해저드: 같은 FU 클래스에서 활성(issue 했고 t-1 까지 write 안 함) 명령 수 < sb_units → 아니면
 *   b->stall_struct++ 하고 false.  WAW: 목적지(dst)가 같은 활성 명령이 있으면 b->stall_waw++ 하고 false. */
static bool sb_issue_ok(sb_t *b, int i, int t) {
    (void)b; (void)i; (void)t; (void)sb_units;
    return true;
}

/* TODO(Part A-3): 실행이 끝난 명령 i 가 사이클 t 에 결과를 써도 되나? (WAR)
 *   목적지가 없으면(ST) true. 더 오래된(j < i) 활성 명령 중 아직 피연산자를 안 읽은 것(ro == 0 || ro >= t)이
 *   이 목적지를 소스(src_j/src_k)로 쓰면 → b->stall_war++ 하고 false (그 명령이 옛 값을 읽어야 하니까). */
static bool sb_write_ok(sb_t *b, int i, int t) {
    (void)b; (void)i; (void)t;
    return true;
}

static int sb_producer(const sb_t *b, int i, int reg, int t) {   /* 이 레지스터를 쓸 활성 명령 (WAW 덕에 최대 1개) */
    if (reg <= 0) return -1;
    for (int j = i - 1; j >= 0; j--)
        if (sb_active(&b->I[j], t) && dst(&b->prog[j]) == reg) return j;
    return -1;
}

static int sb_run(sb_t *b, const cfg_t *c, const insn_t *p, int n, const arch_t *init) {
    memset(b, 0, sizeof *b);
    b->c = *c; b->prog = p; b->n = n; b->a = *init; b->a.r[0] = 0;
    for (int t = 1; t < 100000; t++) {
        b->cyc = t;
        bool all = (b->pc >= n);
        for (int i = 0; i < b->pc; i++) if (b->I[i].wr == 0) all = false;
        if (all) return t - 1;
        bool bus_used = false;                                    /* ① write (오래된 것부터, 레지스터 쓰기 1개) */
        for (int i = 0; i < b->pc; i++) {
            sbi_t *x = &b->I[i];
            if (x->xs == 0 || x->wr || x->xe >= t) continue;
            const insn_t *in = &p[i];
            bool regw = dst(in) >= 0;
            if (regw && bus_used) continue;
            if (!sb_write_ok(b, i, t)) continue;
            x->wr = t;
            if (in->op == OP_ST) b->a.mem[x->ea] = x->val;
            else if (regw) { b->a.r[dst(in)] = x->val; bus_used = true; }
        }
        for (int i = 0; i < b->pc; i++) {                         /* ② execute 시작 */
            sbi_t *x = &b->I[i];
            if (x->ro == 0 || x->ro >= t || x->xs) continue;
            const insn_t *in = &p[i];
            exc_t ex;
            x->val = alu(in, x->a, x->b, &x->ea, &ex);
            if (ex != EXC_NONE) { x->val = 0; x->ea = 0; }       /* 스코어보드 비교는 예외 없는 프로그램만 */
            else if (in->op == OP_LD) x->val = b->a.mem[x->ea];
            x->xs = t; x->xe = t + op_lat(c, in->op) - 1;
        }
        for (int i = 0; i < b->pc; i++) {                         /* ③ read operands (RAW) */
            sbi_t *x = &b->I[i];
            if (x->ro || x->iss >= t) continue;
            bool ok = true;
            if (x->pj >= 0 && !(b->I[x->pj].wr && b->I[x->pj].wr < t)) ok = false;
            if (x->pk >= 0 && !(b->I[x->pk].wr && b->I[x->pk].wr < t)) ok = false;
            if (!ok) { b->stall_raw++; continue; }
            x->ro = t;
            x->a = b->a.r[src_j(&p[i])];
            x->b = src_k(&p[i]) >= 0 ? b->a.r[src_k(&p[i])] : 0;
        }
        if (b->pc < n && sb_issue_ok(b, b->pc, t)) {              /* ④ issue (in-order, 1개) */
            sbi_t *x = &b->I[b->pc];
            x->iss = t;
            x->pj = sb_producer(b, b->pc, src_j(&p[b->pc]), t);
            x->pk = src_k(&p[b->pc]) >= 0 ? sb_producer(b, b->pc, src_k(&p[b->pc]), t) : -1;
            b->pc++;
        }
    }
    return -1;
}

/* ---------------- 작은 어셈블러 / 출력 ---------------- */
static op_t parse_op(const char *m) {
    for (int o = 0; o < NOP; o++) if (strcmp(m, OP_NAME[o]) == 0) return (op_t)o;
    return NOP;
}
static const char *skip(const char *q) { while (*q == ' ' || *q == '\t' || *q == ',') q++; return q; }
static const char *reg(const char *q, uint8_t *r) {
    q = skip(q);
    if (*q != 'r' || q[1] < '0' || q[1] > '7') return NULL;
    *r = (uint8_t)(q[1] - '0'); return q + 2;
}
static const char *num(const char *q, int32_t *v) {
    q = skip(q); char *e; long x = strtol(q, &e, 10);
    if (e == q) return NULL; *v = (int32_t)x; return e;
}
/* 한 줄에 명령 하나, ';' 주석. 반환 = 명령 수, 오류면 -1 */
static int assemble(const char *src, insn_t *out, int max) {
    int n = 0; char line[128];
    while (*src) {
        int L = 0;
        while (*src && *src != '\n') { if (L < 127) line[L++] = *src; src++; }
        if (*src) src++;
        line[L] = 0;
        char *c = strchr(line, ';'); if (c) *c = 0;
        const char *q = skip(line);
        if (!*q) continue;
        char m[8]; int k = 0;
        while (isalpha((unsigned char)*q) && k < 7) m[k++] = *q++;
        m[k] = 0;
        insn_t in = {parse_op(m), 0, 0, 0, 0};
        if (in.op == NOP || n >= max) return -1;
        if (in.op == OP_LD || in.op == OP_ST) {
            uint8_t *first = in.op == OP_LD ? &in.rd : &in.rt;
            if (!(q = reg(q, first)) || !(q = num(q, &in.imm)) || *q != '(' || !(q = reg(q + 1, &in.rs)) || *q != ')')
                return -1;
        } else if (in.op == OP_ADDI) {
            if (!(q = reg(q, &in.rd)) || !(q = reg(q, &in.rs)) || !(q = num(q, &in.imm))) return -1;
        } else {
            if (!(q = reg(q, &in.rd)) || !(q = reg(q, &in.rs)) || !(q = reg(q, &in.rt))) return -1;
        }
        out[n++] = in;
    }
    return n;
}

static void fmt_insn(const insn_t *in, char *buf, size_t sz) {
    switch (in->op) {
    case OP_LD: snprintf(buf, sz, "LD   r%d, %d(r%d)", in->rd, in->imm, in->rs); break;
    case OP_ST: snprintf(buf, sz, "ST   r%d, %d(r%d)", in->rt, in->imm, in->rs); break;
    case OP_ADDI: snprintf(buf, sz, "ADDI r%d, r%d, %d", in->rd, in->rs, in->imm); break;
    default: snprintf(buf, sz, "%-4s r%d, r%d, r%d", OP_NAME[in->op], in->rd, in->rs, in->rt); break;
    }
}

static void print_cfg(const cfg_t *c) {
    printf("  cfg:");
    for (int f = 0; f < NFU; f++)
        printf(" %s lat=%d fu=%d%s rs=%d |", FU_NAME[f], c->lat[f], c->nfu[f], c->piped[f] ? "p" : "", c->nrs[f]);
    printf(" ROB=%d W=%d%s\n", c->rob, c->commit_w, c->use_rob ? "" : " (ROB 없음)");
}

static void pcell(int v) { if (v) printf(" %4d", v); else printf("    -"); }

static void print_timeline(const sim_t *s) {
    printf("   #  instr              iss  x.s  x.e   wr  cmt\n");
    for (int i = 0; i < s->n; i++) {
        const tl_t *x = &s->tl[i];
        char b[32]; fmt_insn(&s->prog[i], b, sizeof b);
        printf("  %2d  %-17s", i, b);
        pcell(x->iss); pcell(x->xs); pcell(x->xe); pcell(x->wr);
        if (x->flushed) printf(" flush");
        else if (s->exc_idx == i) printf("  EXC");
        else pcell(x->cmt);
        if (x->exc) printf("  (%s 발견)", EXC_NAME[x->exc]);
        printf("\n");
    }
}

static void print_sb(const sb_t *b) {
    printf("   #  instr              iss   ro  x.s  x.e   wr\n");
    for (int i = 0; i < b->n; i++) {
        const sbi_t *x = &b->I[i];
        char buf[32]; fmt_insn(&b->prog[i], buf, sizeof buf);
        printf("  %2d  %-17s", i, buf);
        pcell(x->iss); pcell(x->ro); pcell(x->xs); pcell(x->xe); pcell(x->wr);
        printf("\n");
    }
}

/* ---------------- 무작위 프로그램 / 설정 ---------------- */
static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }
static int rnd(uint32_t *s, int n) { return (int)(xs32(s) % (uint32_t)n); }

/* fault=false: 예외가 절대 안 나는 프로그램 — DIV 는 ADDI 로 바꾸고(제수가 0 이 될 수 있으니),
 * LD/ST 는 r0 기준 절대 주소 0..15. fault=true: DIV 의 1/4 은 r0 제수, LD/ST 의 1/8 은 임의 레지스터
 * 기준(대개 범위 밖). */
static void gen_prog(uint32_t *sd, insn_t *p, int n, bool fault) {
    for (int i = 0; i < n; i++) {
        insn_t in = {OP_ADD, (uint8_t)rnd(sd, NREG), (uint8_t)rnd(sd, NREG), (uint8_t)rnd(sd, NREG), 0};
        int k = rnd(sd, 100);
        if (k < 20) in.op = OP_ADD;
        else if (k < 30) in.op = OP_SUB;
        else if (k < 42) in.op = OP_ADDI, in.imm = rnd(sd, 21) - 10;
        else if (k < 57) in.op = OP_MUL;
        else if (k < 67) in.op = OP_DIV;
        else if (k < 84) in.op = OP_LD;
        else in.op = OP_ST;
        if (in.op == OP_DIV) {
            if (!fault) { in.op = OP_ADDI; in.imm = 3; }
            else if (rnd(sd, 4) == 0) in.rt = 0;
        }
        if (in.op == OP_LD || in.op == OP_ST) {
            in.rs = 0; in.imm = rnd(sd, 16);                /* 16 워드 안에서 자주 겹치게 */
            if (fault && rnd(sd, 8) == 0) { in.rs = (uint8_t)(1 + rnd(sd, NREG - 1)); in.imm = rnd(sd, 8); }
        }
        p[i] = in;
    }
}

static void arch_init(arch_t *a, uint32_t *sd) {
    for (int r = 0; r < NREG; r++) a->r[r] = sd ? (uint32_t)rnd(sd, 40) : (uint32_t)(r * 10);
    a->r[0] = 0;
    for (int m = 0; m < MEMW; m++) a->mem[m] = 1000u + (uint32_t)m;
}

static cfg_t cfg_random(uint32_t *sd) {
    cfg_t c;
    c.lat[FU_ADD] = 1 + rnd(sd, 3); c.lat[FU_MUL] = 2 + rnd(sd, 7);
    c.lat[FU_DIV] = 4 + rnd(sd, 13); c.lat[FU_MEM] = 1 + rnd(sd, 4);
    for (int f = 0; f < NFU; f++) {
        c.nfu[f] = 1 + rnd(sd, 2); c.piped[f] = rnd(sd, 2); c.nrs[f] = 1 + rnd(sd, 4);
    }
    c.piped[FU_DIV] = 0;
    c.rob = 2 + rnd(sd, 15); c.commit_w = 1 + rnd(sd, 3); c.use_rob = true;
    return c;
}

static bool arch_eq(const arch_t *x, const arch_t *y) {
    return memcmp(x->r, y->r, sizeof x->r) == 0 && memcmp(x->mem, y->mem, sizeof x->mem) == 0;
}

/* 타임라인 불변식: issue in-order 사이클당 ≤1, x.s > iss, x.e = x.s+lat-1, wr > x.e, cmt > wr,
 * commit 은 프로그램 순서·사이클당 ≤ W, CDB(ST 제외)는 사이클당 ≤ 1. 위반이면 false. */
static bool timeline_ok(const sim_t *s) {
    int last_cmt = 0, per[4096] = {0}, cdb[4096] = {0};
    for (int i = 0; i < s->n; i++) {
        const tl_t *x = &s->tl[i];
        if (!x->iss) continue;
        if (i > 0 && s->tl[i - 1].iss >= x->iss) return false;
        if (x->xs && (x->xs <= x->iss || x->xe != x->xs + op_lat(&s->c, s->prog[i].op) - 1)) return false;
        if (x->wr && x->wr <= x->xe) return false;
        if (x->wr && s->prog[i].op != OP_ST && x->wr < 4096 && ++cdb[x->wr] > 1) return false;
        if (x->cmt) {
            if (x->cmt <= x->wr || x->cmt < last_cmt || x->cmt >= 4096) return false;
            last_cmt = x->cmt;
            if (++per[x->cmt] > s->c.commit_w) return false;
        }
    }
    return true;
}

/* ---------------- Part C 커널 ---------------- */
typedef enum { K_CHAIN, K_INDEP, K_DOT, NK } kern_t;
static const char *const K_NAME[NK] = {"chain  MUL/ADD dependent", "indep  MUL x4 independent", "dot    LD,LD,MUL,ADD"};

static int make_kernel(kern_t k, insn_t *p) {
    int n = 0;
    for (int it = 0; it < 30; it++) {
        if (k == K_CHAIN) {                                /* r1 ← r1*r2 ; r1 ← r1+r3 ... 모두 앞 명령에 의존 */
            p[n++] = (insn_t){OP_MUL, 1, 1, 2, 0}; p[n++] = (insn_t){OP_ADD, 1, 1, 3, 0};
            p[n++] = (insn_t){OP_MUL, 1, 1, 2, 0}; p[n++] = (insn_t){OP_ADD, 1, 1, 3, 0};
        } else if (k == K_INDEP) {                         /* 서로 무관한 MUL (목적지 재사용 → WAW 투성이) */
            for (int j = 0; j < 4; j++) p[n++] = (insn_t){OP_MUL, (uint8_t)(1 + j), 6, 7, 0};
        } else {                                           /* s += a[i]*b[i]: r1,r2,r3 을 매 반복 재사용 */
            p[n++] = (insn_t){OP_LD, 1, 0, 0, it % 16};
            p[n++] = (insn_t){OP_LD, 2, 0, 0, 16 + it % 16};
            p[n++] = (insn_t){OP_MUL, 3, 1, 2, 0};
            p[n++] = (insn_t){OP_ADD, 4, 4, 3, 0};
        }
    }
    return n;
}

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static sim_t g_sim, g_sim2;
static sb_t g_sb;

static const char *K_WARWAW =
    "DIV r1, r2, r3   ; 12 사이클\n"
    "ADD r4, r1, r5   ; RAW r1\n"
    "MUL r5, r6, r7   ; WAR r5 (1 이 아직 r5 를 안 읽음)\n"
    "LD  r1, 0(r0)    ; WAW r1 (0 이 아직 r1 을 안 씀)\n"
    "ADDI r6, r6, 1   ; WAR r6 (2 가 읽음) + ADD FU\n";

static const char *K_DIV0 =
    "DIV r1, r2, r0   ; r0 = 0 → DIV0, 12 사이클 뒤에야 끝남\n"
    "ADD r3, r4, r5   ; 젊고 짧다\n"
    "ST  r3, 5(r0)    ; 젊은 store\n"
    "MUL r6, r6, r6\n";

static const char *K_YOUNG_FAULT =
    "DIV r1, r2, r3   ; 정상, 느림\n"
    "LD  r4, 99(r0)   ; 범위 밖 → MEM 예외, 빨리 발견됨\n"
    "ADD r5, r6, r7\n";

static void part_a(void) {
    printf("== Part A: 리네이밍으로 WAR/WAW 제거 — 스코어보드 vs Tomasulo ==\n");
    insn_t p[MAXPROG]; arch_t init, ref; exc_t e;
    insn_t q[4];
    CHECK(assemble("LD r1, -2(r3)\nST r4, 7(r0) ; c\n\nADDI r2, r2, -1\n", q, 4) == 3);
    CHECK(q[0].op == OP_LD && q[0].rd == 1 && q[0].rs == 3 && q[0].imm == -2 && q[1].rt == 4 && q[2].imm == -1);
    CHECK(assemble("ADD r1, r2\n", q, 4) == -1 && assemble("FOO r1, r2, r3\n", q, 4) == -1 &&
          assemble("ADD r8, r1, r1\n", q, 4) == -1);

    int n = assemble(K_WARWAW, p, MAXPROG);
    arch_init(&init, NULL);
    init.r[2] = 100; init.r[3] = 7; init.r[5] = 5; init.r[6] = 3; init.r[7] = 4;
    ref = init; CHECK(ref_run(p, n, &ref, &e) == n);
    cfg_t c = cfg_default();
    print_cfg(&c);
    sim_init(&g_sim, &c, p, n, &init);
    int cy = sim_run(&g_sim);
    print_timeline(&g_sim);
    printf("  Tomasulo+ROB: cycles=%d committed=%d IPC=%.3f cdb=%ld\n", cy, g_sim.committed,
           cy > 0 ? (double)g_sim.committed / cy : 0.0, g_sim.st.cdb);
    int sbc = sb_run(&g_sb, &c, p, n, &init);
    print_sb(&g_sb);
    printf("  scoreboard  : cycles=%d  stalls struct=%ld WAW=%ld WAR=%ld RAW=%ld\n", sbc,
           g_sb.stall_struct, g_sb.stall_waw, g_sb.stall_war, g_sb.stall_raw);
    int serial = 0; for (int i = 0; i < n; i++) serial += op_lat(&c, p[i].op);
    printf("  직렬(겹침 없음, 지연 합) = %d\n", serial);
    /* 손으로 센 타임라인 (풀이 표와 같다) */
    const int want[5][5] = {{1, 2, 13, 14, 15}, {2, 15, 16, 17, 18}, {3, 4, 9, 10, 19}, {4, 5, 7, 8, 20}, {5, 6, 7, 9, 21}};
    bool tl_ok = true;
    for (int i = 0; i < n; i++) {
        const tl_t *x = &g_sim.tl[i];
        if (x->iss != want[i][0] || x->xs != want[i][1] || x->xe != want[i][2] || x->wr != want[i][3] || x->cmt != want[i][4])
            tl_ok = false;
    }
    CHECK(cy == 21 && tl_ok);
    CHECK(arch_eq(&g_sim.a, &ref));
    CHECK(g_sim.a.r[4] == 100 / 7 + 5);                   /* ADD 은 MUL 이 덮기 전의 r5(=5)를 읽었다 */
    CHECK(sbc == 24 && arch_eq(&g_sb.a, &ref));
    CHECK(g_sb.I[3].iss == 16 && g_sb.I[2].wr == 17 && g_sb.I[4].iss == 20);   /* WAW 로 issue 정지, WAR 로 write 지연 */
    CHECK(g_sb.stall_waw > 0 && g_sb.stall_war > 0 && g_sb.stall_struct > 0);
    CHECK(cy < sbc && sbc < serial);

    /* WAW 순서: 느린 옛 쓰기 + 빠른 새 쓰기 → 마지막 값은 새 쓰기 (ROB 모드, ROB 없는 모드, 스코어보드 모두) */
    n = assemble("DIV r1, r2, r3\nADDI r1, r0, 42\nADD r2, r1, r1\n", p, MAXPROG);
    ref = init; ref_run(p, n, &ref, &e);
    bool ok3 = true;
    for (int m = 0; m < 2; m++) {
        cfg_t c2 = cfg_default(); c2.use_rob = (m == 0);
        sim_init(&g_sim, &c2, p, n, &init); sim_run(&g_sim);
        ok3 = ok3 && arch_eq(&g_sim.a, &ref) && g_sim.a.r[1] == 42 && g_sim.a.r[2] == 84;
    }
    CHECK(ok3);
    CHECK(sb_run(&g_sb, &c, p, n, &init) > 0 && arch_eq(&g_sb.a, &ref));

    /* 무작위 예외 없는 프로그램 2000 개 × 무작위 설정: 세 모델 모두 결과 == 순차 기준, 타임라인 불변식 */
    uint32_t sd = 0xA06u;
    int bad = 0, badtl = 0, badsb = 0, badnr = 0, dead = 0; long ooo = 0;
    for (int it = 0; it < 2000; it++) {
        int len = 4 + rnd(&sd, 40);
        gen_prog(&sd, p, len, false);
        arch_init(&init, &sd);
        cfg_t rc = cfg_random(&sd);
        ref = init; ref_run(p, len, &ref, &e);
        sim_init(&g_sim, &rc, p, len, &init);
        if (sim_run(&g_sim) < 0) dead++;
        if (!arch_eq(&g_sim.a, &ref) || g_sim.committed != len) bad++;
        if (!timeline_ok(&g_sim)) badtl++;
        for (int i = 1; i < len; i++) if (g_sim.tl[i].xs < g_sim.tl[i - 1].xs) { ooo++; break; }
        rc.use_rob = false;
        sim_init(&g_sim2, &rc, p, len, &init); sim_run(&g_sim2);
        if (!arch_eq(&g_sim2.a, &ref)) badnr++;
        if (sb_run(&g_sb, &rc, p, len, &init) < 0 || !arch_eq(&g_sb.a, &ref)) badsb++;
    }
    printf("  random 2000 (예외 없음): ROB 불일치 %d, 타임라인 위반 %d, 교착 %d, ROB 없음 불일치 %d, "
           "스코어보드 불일치 %d, 비순차 실행이 일어난 프로그램 %ld\n", bad, badtl, dead, badnr, badsb, ooo);
    CHECK(bad == 0 && badtl == 0 && dead == 0);
    CHECK(badnr == 0 && badsb == 0);
    CHECK(ooo > 500);                                     /* 실제로 순서가 뒤바뀐 실행을 충분히 지나갔다 */
}

static void part_b(void) {
    printf("== Part B: 정밀 예외 — commit 에서 받기 vs ROB 없는 Tomasulo ==\n");
    insn_t p[MAXPROG]; arch_t init, ref; exc_t e;
    arch_init(&init, NULL);
    int n = assemble(K_DIV0, p, MAXPROG);
    ref = init; int ri = ref_run(p, n, &ref, &e);
    CHECK(ri == 0 && e == EXC_DIV0);
    cfg_t c = cfg_default();
    sim_init(&g_sim, &c, p, n, &init); sim_run(&g_sim);
    printf("  [ROB] DIV0 프로그램\n"); print_timeline(&g_sim);
    printf("  [ROB] exc at #%d (%s) cycle %d, r3=%u mem[5]=%u flushed=%ld\n", g_sim.exc_idx, EXC_NAME[g_sim.exc],
           g_sim.cyc, g_sim.a.r[3], g_sim.a.mem[5], g_sim.st.flushed);
    CHECK(g_sim.exc_idx == 0 && g_sim.exc == EXC_DIV0 && arch_eq(&g_sim.a, &ref));
    CHECK(g_sim.tl[1].wr > 0 && g_sim.tl[1].wr < g_sim.tl[0].wr);   /* 젊은 ADD 는 먼저 끝났지만 commit 안 됨 */
    CHECK(g_sim.tl[0].cmt == 0 && g_sim.cyc == 15 && g_sim.st.flushed == 3);
    cfg_t nr = c; nr.use_rob = false;
    sim_init(&g_sim2, &nr, p, n, &init); sim_run(&g_sim2);
    printf("  [no ROB] DIV0 프로그램\n"); print_timeline(&g_sim2);
    printf("  [no ROB] exc at #%d (%s) cycle %d, r3=%u mem[5]=%u r6=%u (기준: r3=%u mem[5]=%u r6=%u)\n",
           g_sim2.exc_idx, EXC_NAME[g_sim2.exc], g_sim2.cyc, g_sim2.a.r[3], g_sim2.a.mem[5], g_sim2.a.r[6],
           ref.r[3], ref.mem[5], ref.r[6]);
    CHECK(g_sim2.exc_idx == 0 && !arch_eq(&g_sim2.a, &ref));
    CHECK(g_sim2.a.r[3] == 90 && g_sim2.a.mem[5] == 90 && g_sim2.a.r[6] == 3600);   /* 젊은 명령 3개가 이미 씀 */

    /* 젊은 LD 가 먼저 예외를 발견: ROB 는 오래된 DIV 를 끝내고 LD 에서 정밀하게, ROB 없음은 DIV 결과가 빠진 상태 */
    n = assemble(K_YOUNG_FAULT, p, MAXPROG);
    init.r[2] = 100; init.r[3] = 7;
    ref = init; ri = ref_run(p, n, &ref, &e);
    sim_init(&g_sim, &c, p, n, &init); sim_run(&g_sim);
    sim_init(&g_sim2, &nr, p, n, &init); sim_run(&g_sim2);
    printf("  young-fault: ref exc #%d r1=%u | ROB exc #%d r1=%u r5=%u | no-ROB exc #%d r1=%u r5=%u (cycle %d)\n",
           ri, ref.r[1], g_sim.exc_idx, g_sim.a.r[1], g_sim.a.r[5], g_sim2.exc_idx, g_sim2.a.r[1], g_sim2.a.r[5],
           g_sim2.cyc);
    CHECK(ri == 1 && g_sim.exc_idx == 1 && g_sim.a.r[1] == 14 && arch_eq(&g_sim.a, &ref));
    CHECK(g_sim2.exc_idx == 1 && g_sim2.a.r[1] == 10 && !arch_eq(&g_sim2.a, &ref));   /* r1 은 아직 옛 값 */

    /* 둘 다 예외: 시간상 먼저 발견된 것(젊은 LD)이 아니라 프로그램 순서상 먼저인 것(DIV0)을 받는다 */
    n = assemble("DIV r1, r2, r0\nLD r4, 99(r0)\n", p, MAXPROG);
    sim_init(&g_sim, &c, p, n, &init); sim_run(&g_sim);
    CHECK(g_sim.exc_idx == 0 && g_sim.exc == EXC_DIV0 && g_sim.tl[1].exc == EXC_MEM && g_sim.tl[1].wr < g_sim.tl[0].wr);
    sim_init(&g_sim2, &nr, p, n, &init); sim_run(&g_sim2);
    CHECK(g_sim2.exc_idx == 1 && g_sim2.exc == EXC_MEM);  /* ROB 없음: 틀린 명령을 보고 */

    /* 무작위: 예외가 섞인 프로그램 3000 개 × 무작위 설정 */
    uint32_t sd = 0xB06u;
    int withexc = 0, bad = 0, badtl = 0, imprecise = 0, wrongidx = 0;
    for (int it = 0; it < 3000; it++) {
        int len = 4 + rnd(&sd, 40);
        gen_prog(&sd, p, len, true);
        arch_init(&init, &sd);
        cfg_t rc = cfg_random(&sd);
        ref = init; ri = ref_run(p, len, &ref, &e);
        sim_init(&g_sim, &rc, p, len, &init);
        if (sim_run(&g_sim) < 0) { bad++; continue; }
        int got = g_sim.exc_idx < 0 ? len : g_sim.exc_idx;
        if (got != ri || !arch_eq(&g_sim.a, &ref) || (ri < len && g_sim.exc != e) || g_sim.committed != ri) bad++;
        if (!timeline_ok(&g_sim)) badtl++;
        if (ri < len) {
            withexc++;
            rc.use_rob = false;
            sim_init(&g_sim2, &rc, p, len, &init); sim_run(&g_sim2);
            int g2 = g_sim2.exc_idx < 0 ? len : g_sim2.exc_idx;
            if (g2 != ri) wrongidx++;
            if (g2 != ri || !arch_eq(&g_sim2.a, &ref)) imprecise++;
        }
    }
    printf("  random 3000: 예외 있는 프로그램 %d | ROB: 불일치 %d, 타임라인 위반 %d | "
           "ROB 없음: 부정확 %d (그중 틀린 명령 보고 %d)\n", withexc, bad, badtl, imprecise, wrongidx);
    CHECK(withexc > 1000 && bad == 0 && badtl == 0);
    CHECK(imprecise > withexc / 4 && wrongidx > 0);

    /* commit 폭 W: 결과는 같고, 끝난 채 기다리던 엔트리가 한꺼번에 나가면 사이클이 준다 */
    n = assemble(K_WARWAW, p, MAXPROG);
    arch_init(&init, NULL); ref = init; ref_run(p, n, &ref, &e);
    int cw_cyc[3]; bool same = true;
    for (int w = 1, k = 0; w <= 4; w *= 2, k++) {
        cfg_t cw = cfg_default(); cw.commit_w = w;
        sim_init(&g_sim, &cw, p, n, &init); cw_cyc[k] = sim_run(&g_sim);
        printf("  commit W=%d: cycles=%d  cmt =", w, cw_cyc[k]);
        for (int i = 0; i < n; i++) printf(" %d", g_sim.tl[i].cmt);
        printf("\n");
        if (!arch_eq(&g_sim.a, &ref)) same = false;
    }
    CHECK(same && cw_cyc[0] == 21 && cw_cyc[1] == 19 && cw_cyc[2] == 18);
}

static double ipc_of(kern_t k, int rob, int nrs, bool use_rob, int *cyc_out) {
    static insn_t p[MAXPROG]; arch_t init;
    int n = make_kernel(k, p);
    arch_init(&init, NULL);
    cfg_t c = cfg_default(); c.rob = rob; c.use_rob = use_rob;
    for (int f = 0; f < NFU; f++) c.nrs[f] = nrs;
    sim_init(&g_sim, &c, p, n, &init);
    int cy = sim_run(&g_sim);
    if (cyc_out) *cyc_out = cy;
    return cy > 0 ? (double)g_sim.committed / cy : 0.0;
}

static void part_c(void) {
    printf("== Part C: ROB 크기 · RS 개수 → IPC (지연 가정 ADD 2, MUL 6 pipelined, LD 3, 이슈 1/사이클, CDB 1) ==\n");
    static const int robs[] = {4, 8, 16, 32, 64}, rss[] = {1, 2, 4, 8, 16};
    double t1[NK][5], t2[NK][5];
    printf("  %-32s", "IPC vs ROB size (RS=16 each)"); for (int i = 0; i < 5; i++) printf("  %5d", robs[i]); printf("\n");
    for (int k = 0; k < NK; k++) {
        printf("  %-32s", K_NAME[k]);
        for (int i = 0; i < 5; i++) { t1[k][i] = ipc_of((kern_t)k, robs[i], 16, true, NULL); printf("  %.3f", t1[k][i]); }
        printf("\n");
    }
    printf("  %-32s", "IPC vs RS per class (ROB=64)"); for (int i = 0; i < 5; i++) printf("  %5d", rss[i]); printf("\n");
    for (int k = 0; k < NK; k++) {
        printf("  %-32s", K_NAME[k]);
        for (int i = 0; i < 5; i++) { t2[k][i] = ipc_of((kern_t)k, 64, rss[i], true, NULL); printf("  %.3f", t2[k][i]); }
        printf("\n");
    }
    printf("  scoreboard (FU 1 each, same lat) ");
    double sbipc[NK];
    for (int k = 0; k < NK; k++) {
        static insn_t p[MAXPROG]; arch_t init; arch_init(&init, NULL);
        int n = make_kernel((kern_t)k, p);
        cfg_t c = cfg_default();
        int cy = sb_run(&g_sb, &c, p, n, &init);
        sbipc[k] = (double)n / cy;
        printf(" %s=%.3f", k == K_CHAIN ? "chain" : k == K_INDEP ? "indep" : "dot", sbipc[k]);
    }
    printf("\n");
    /* chain: ROB/RS 와 무관 = 1 / (평균 (지연+1)) = 2 / (7 + 3) = 0.2 */
    bool flat = true;
    for (int i = 0; i < 5; i++) if (t1[K_CHAIN][i] < 0.19 || t1[K_CHAIN][i] > 0.21) flat = false;
    CHECK(flat);
    bool mono = true;
    for (int k = 0; k < NK; k++) for (int i = 1; i < 5; i++) if (t1[k][i] + 1e-9 < t1[k][i - 1]) mono = false;
    CHECK(mono);                                          /* ROB 를 키워서 느려지지는 않는다 */
    CHECK(t1[K_INDEP][0] < 0.5 && t1[K_INDEP][4] > 0.93); /* 독립 MUL: ROB 4 는 지연을 못 가린다, 64 는 거의 1 */
    CHECK(t2[K_INDEP][0] < 0.2 && t2[K_INDEP][4] > 0.93); /* RS 1 개면 MUL 이 issue~write 동안 RS 를 잡는다 */
    CHECK(t1[K_DOT][4] > 2.0 * sbipc[K_DOT] && t1[K_INDEP][4] > 4.0 * sbipc[K_INDEP]);
}

int main(void) {
    part_a();
    part_b();
    part_c();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
