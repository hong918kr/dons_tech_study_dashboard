/* ARCH-M01 5단 파이프라인 시뮬레이터 — 데이터 해저드와 스톨 (포워딩 없음)
 *
 *   ISA = ARCH-B08/B09 ISA를 그대로 넣었다 (16비트, r0 = 0 고정, imem[256]/dmem[256] 워드 주소, mod 2^16).
 *   파이프라인 규칙 (ARCH-M02 와 공유):
 *     IF ID EX MEM WB, 순서대로(in-order). 레지스터 파일은 split-phase: WB 가 전반부에 쓰고 ID 가 후반부에 읽는다.
 *     레지스터 읽기 = ID, ALU + BEQ 비교 + BEQ/JAL 목표 = EX, dmem = MEM, 레지스터 쓰기 = WB (JAL 의 rd 도 WB).
 *     포워딩 없음: 소비자를 ID 에 붙잡고(IF/ID 유지) EX 에 버블 → 생산자가 WB 에 올 때까지. 거리 1/2/≥3 → 스톨 2/1/0.
 *     정적 not-taken 예측: taken BEQ 또는 모든 JAL 은 EX 에서 IF·ID 두 개를 flush → 2 사이클 손해.
 *     HALT 가 EX 에 오면 IF·ID flush + fetch 중단. HALT 가 WB 에 있는 사이클 번호 = 총 사이클. retired 는 HALT 포함.
 *     imem 끝(PC >= 256)을 넘는 투기적 fetch 는 NOP(0x0000).
 *   Part A  pipe_advance — 파이프라인 레지스터 이동(정상 / 스톨 / flush / HALT). 해저드 없는 N 명령 = N + 4 사이클
 *   Part B  insn_dst / insn_srcs / hazard_stall — RAW 탐지, analytic_cycles — 해석적 사이클 공식, 랜덤 프로그램 fuzz
 *   Part C  배열 합 · memcpy 루프 → 다이어그램 + CPI, 스케줄링한 루프, 단일 사이클 vs 파이프라인 시간 (ARCH-B13 Fmax)
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ===================== ISA (ARCH-B08/B09 ISA를 그대로 넣었다) ===================== */
enum { NREG = 8, IMEM_WORDS = 256, DMEM_WORDS = 256 };
enum { OP_ADD = 0x0, OP_ADDI = 0x1, OP_LD = 0x2, OP_ST = 0x3, OP_BEQ = 0x4, OP_JAL = 0x5, OP_HALT = 0xF };

typedef struct { uint8_t op, rd, rs, rt; int16_t imm; } insn_t;

static int16_t sext(uint16_t v, unsigned bits) {
    uint16_t m = (uint16_t)(1u << (bits - 1));
    v &= (uint16_t)((1u << bits) - 1u);
    return (int16_t)((int)(v ^ m) - (int)m);
}

/* B09 디코더: 읽는 레지스터는 rs/rt, 쓰는 레지스터는 rd 로 정규화 (안 쓰는 필드는 0). */
static bool decode(uint16_t w, insn_t *d) {
    unsigned a = (w >> 9) & 7u, b = (w >> 6) & 7u, c = (w >> 3) & 7u;
    memset(d, 0, sizeof *d);
    d->op = (uint8_t)(w >> 12);
    switch (d->op) {
    case OP_ADD:  if (w & 7u) return false;
                  d->rd = (uint8_t)a; d->rs = (uint8_t)b; d->rt = (uint8_t)c; return true;
    case OP_ADDI:
    case OP_LD:   d->rd = (uint8_t)a; d->rs = (uint8_t)b; d->imm = sext(w, 6); return true;
    case OP_ST:   d->rt = (uint8_t)a; d->rs = (uint8_t)b; d->imm = sext(w, 6); return true;
    case OP_BEQ:  d->rs = (uint8_t)a; d->rt = (uint8_t)b; d->imm = sext(w, 6); return true;
    case OP_JAL:  d->rd = (uint8_t)a; d->imm = sext(w, 9); return true;
    case OP_HALT: return (w & 0x0FFFu) == 0;
    default:      return false;
    }
}

#define I_ADD(rd, rs, rt)   ((uint16_t)((0x0u << 12) | ((rd) << 9) | ((rs) << 6) | ((rt) << 3)))
#define I_ADDI(rd, rs, imm) ((uint16_t)((0x1u << 12) | ((rd) << 9) | ((rs) << 6) | ((unsigned)(imm) & 0x3Fu)))
#define I_LD(rd, rs, imm)   ((uint16_t)((0x2u << 12) | ((rd) << 9) | ((rs) << 6) | ((unsigned)(imm) & 0x3Fu)))
#define I_ST(rt, rs, imm)   ((uint16_t)((0x3u << 12) | ((rt) << 9) | ((rs) << 6) | ((unsigned)(imm) & 0x3Fu)))
#define I_BEQ(rs, rt, off)  ((uint16_t)((0x4u << 12) | ((rs) << 9) | ((rt) << 6) | ((unsigned)(off) & 0x3Fu)))
#define I_JAL(rd, off)      ((uint16_t)((0x5u << 12) | ((rd) << 9) | ((unsigned)(off) & 0x1FFu)))
#define I_HALT              ((uint16_t)0xF000u)
#define I_NOP               ((uint16_t)0x0000u)   /* = ADD r0, r0, r0 */

/* 다이어그램 라벨용 디스어셈블러 (B08 의 정식 형식, 0x0000 은 NOP 로 표시) */
static void disasm(uint16_t w, char *s, size_t cap) {
    insn_t d;
    if (!decode(w, &d)) { snprintf(s, cap, ".word 0x%04X", w); return; }
    switch (d.op) {
    case OP_ADD:  if (w == 0) snprintf(s, cap, "NOP");
                  else snprintf(s, cap, "ADD r%u, r%u, r%u", d.rd, d.rs, d.rt); break;
    case OP_ADDI: snprintf(s, cap, "ADDI r%u, r%u, %d", d.rd, d.rs, d.imm); break;
    case OP_LD:   snprintf(s, cap, "LD r%u, %d(r%u)", d.rd, d.imm, d.rs); break;
    case OP_ST:   snprintf(s, cap, "ST r%u, %d(r%u)", d.rt, d.imm, d.rs); break;
    case OP_BEQ:  snprintf(s, cap, "BEQ r%u, r%u, %+d", d.rs, d.rt, d.imm); break;
    case OP_JAL:  snprintf(s, cap, "JAL r%u, %+d", d.rd, d.imm); break;
    default:      snprintf(s, cap, "HALT"); break;
    }
}

/* ===================== Part B 기초: 누가 무엇을 읽고 쓰나 ===================== */
/* 이 명령이 WB 에서 쓰는 레지스터. 안 쓰거나 r0 에 쓰면 0 (r0 는 해저드가 아니다). */
static unsigned insn_dst(const insn_t *d) {
    (void)d;
    return 0; /* TODO: ADD/ADDI/LD/JAL → rd (r0 이면 0), 나머지 → 0 */
}

/* ID 에서 읽는 레지스터를 s[0], s[1] 에 (없으면 0). ST 의 데이터 레지스터(rt)와 BEQ 의 두 피연산자도 소스다. */
static void insn_srcs(const insn_t *d, unsigned s[2]) {
    s[0] = s[1] = 0;
    (void)d; /* TODO: 명령별로 ID 에서 읽는 레지스터 (decode 가 rs/rt 로 정규화해 두었다) */
}

/* ===================== 파이프라인 상태 ===================== */
enum { S_IF, S_ID, S_EX, S_MEM, S_WB, NSTAGE };

/* 한 칸 = 그 단계에 있는 명령 = 그 단계 앞의 파이프라인 레지스터 내용.
 * st[S_ID] ≈ IF/ID, st[S_EX] ≈ ID/EX, st[S_MEM] ≈ EX/MEM, st[S_WB] ≈ MEM/WB. valid = 0 이면 빈 칸/버블. */
typedef struct {
    bool valid, bad;      /* bad = illegal 워드 (투기적 fetch 라면 flush 되어 문제 없음) */
    bool wb;              /* WB 에서 레지스터를 쓰나 */
    uint16_t pc, word;
    insn_t d;
    uint16_t a, b;        /* ID 에서 읽은 rs, rt 값 (ST 의 b = 저장할 데이터) */
    uint16_t val;         /* EX: ALU 결과 / 유효 주소 / JAL 링크, MEM 이후: WB 할 값 */
    int row;              /* 다이어그램 행 번호 (동적 명령 순번) */
} slot_t;

enum { DIAG_ROWS = 64, DIAG_COLS = 64 };
enum { C_NONE = 0, C_IF, C_ID, C_EX, C_ME, C_WB, C_XX };
typedef struct {
    unsigned nrows;
    struct { uint16_t pc, word; uint8_t cell[DIAG_COLS]; } r[DIAG_ROWS];
} diag_t;

typedef enum { PIPE_RUNNING, PIPE_HALTED, PIPE_LIMIT, PIPE_ILLEGAL, PIPE_DMEM_FAULT } pipe_status_t;

typedef struct {
    slot_t st[NSTAGE];
    uint16_t pc;          /* 다음에 fetch 할 PC */
    bool fetch_on;
    pipe_status_t status;
    uint16_t reg[NREG], imem[IMEM_WORDS], dmem[DMEM_WORDS];
    uint64_t cycle, retired, stalls, flushes;   /* stalls = 버블 넣은 사이클 수, flushes = taken BEQ/JAL 리다이렉트 수 */
    int nfetched;
    diag_t *dg;           /* NULL 이면 다이어그램 기록 안 함 */
    bool no_hazard_unit;  /* 실험용: true 면 hazard_stall 을 무시 (틀린 결과를 보이기 위해) */
} pipe_t;

static void pipe_load(pipe_t *p, const uint16_t *prog, size_t n, diag_t *dg) {
    memset(p, 0, sizeof *p);
    for (size_t i = 0; i < n && i < IMEM_WORDS; i++) p->imem[i] = prog[i];
    p->fetch_on = true;
    p->status = PIPE_RUNNING;
    p->dg = dg;
    if (dg) memset(dg, 0, sizeof *dg);
}

/* 다이어그램: 이번 사이클에 각 칸의 단계 표시 */
static void diag_mark(pipe_t *p) {
    if (!p->dg || p->cycle > DIAG_COLS) return;
    for (int s = 0; s < NSTAGE; s++) {
        const slot_t *x = &p->st[s];
        if (x->valid && x->row < DIAG_ROWS) p->dg->r[x->row].cell[p->cycle - 1] = (uint8_t)(C_IF + s);
    }
}

/* flush: 그 칸의 명령을 없애고, 다이어그램에는 "다음 사이클" 칸에 xx 를 남긴다. */
static void squash(pipe_t *p, slot_t *x) {
    if (!x->valid) return;
    if (p->dg && x->row < DIAG_ROWS && p->cycle < DIAG_COLS) p->dg->r[x->row].cell[p->cycle] = C_XX;
    memset(x, 0, sizeof *x);
}

/* ---------------- Part B: RAW 해저드 탐지 (포워딩 없음) ----------------
 * ID 의 명령이 읽는 레지스터(r0 제외)를 EX 또는 MEM 에 있는 명령이 쓰려고 하면 스톨.
 * WB 에 있는 생산자는 split-phase 덕분에 같은 사이클에 ID 가 읽을 수 있으므로 해저드가 아니다. */
static bool hazard_stall(const pipe_t *p) {
    (void)p;
    return false; /* TODO: insn_srcs / insn_dst 로 ID 의 소스와 EX·MEM 의 목적지를 비교 */
}

/* ---------------- Part A: 파이프라인 레지스터 이동 (사이클 끝의 클럭 에지) ----------------
 * 우선순위: EX 의 리다이렉트(taken BEQ / JAL) 또는 HALT > 스톨 > 정상 진행. MEM/WB 쪽은 언제나 앞으로 간다.
 *   리다이렉트: ID·IF 를 squash, EX 에 버블, pc = target, flushes++
 *   HALT in EX: ID·IF 를 squash, EX 에 버블, fetch_on = false
 *   스톨:       EX 에 버블, ID·IF 는 그대로(다음 사이클에 같은 단계가 반복 표시됨), stalls++
 *   정상:       EX ← ID, ID ← IF, IF 는 비운다 (다음 사이클 시작에 pc 에서 새로 fetch) */
static void pipe_advance(pipe_t *p, bool stall, bool redirect, uint16_t target, bool halt_ex) {
    (void)p; (void)stall; (void)redirect; (void)target; (void)halt_ex; (void)squash;
    /* TODO: 위 규칙대로 st[] 를 옮긴다. 빈 칸/버블 = memset 0, flush 는 squash() 를 쓴다 */
}

/* 한 사이클. 하드웨어처럼 "뒤 단계부터" 처리한다: WB 가 먼저 쓰고(전반부) ID 가 나중에 읽는다(후반부). */
static void pipe_cycle(pipe_t *p) {
    p->cycle++;
    /* IF: 빈 IF 칸이면 pc 에서 fetch (imem 끝 너머는 NOP) */
    if (p->fetch_on && !p->st[S_IF].valid) {
        slot_t *f = &p->st[S_IF];
        memset(f, 0, sizeof *f);
        f->valid = true;
        f->pc = p->pc;
        f->word = p->pc < IMEM_WORDS ? p->imem[p->pc] : I_NOP;
        f->row = p->nfetched++;
        if (p->dg && f->row < DIAG_ROWS) {
            p->dg->r[f->row].pc = f->pc; p->dg->r[f->row].word = f->word;
            p->dg->nrows = (unsigned)f->row + 1;
        }
        p->pc = (uint16_t)(p->pc + 1u);
    }
    diag_mark(p);

    /* WB (전반부 쓰기) */
    slot_t *w = &p->st[S_WB];
    if (w->valid) {
        if (w->wb && w->d.rd != 0) p->reg[w->d.rd] = w->val;
        p->retired++;
        if (w->d.op == OP_HALT) { p->status = PIPE_HALTED; return; }
    }
    /* MEM */
    slot_t *m = &p->st[S_MEM];
    if (m->valid && (m->d.op == OP_LD || m->d.op == OP_ST)) {
        if (m->val >= DMEM_WORDS) { p->status = PIPE_DMEM_FAULT; return; }   /* 테스트 프로그램은 폴트 없음 */
        if (m->d.op == OP_LD) m->val = p->dmem[m->val];
        else                  p->dmem[m->val] = m->b;
    }
    /* EX: ALU · 분기 비교 · 목표 계산 */
    slot_t *x = &p->st[S_EX];
    bool redirect = false, halt_ex = false;
    uint16_t target = 0;
    if (x->valid) {
        if (x->bad) { p->status = PIPE_ILLEGAL; return; }
        uint16_t next = (uint16_t)(x->pc + 1u);
        switch (x->d.op) {
        case OP_ADD:  x->val = (uint16_t)(x->a + x->b); x->wb = true; break;
        case OP_ADDI: x->val = (uint16_t)(x->a + (uint16_t)x->d.imm); x->wb = true; break;
        case OP_LD:   x->val = (uint16_t)(x->a + (uint16_t)x->d.imm); x->wb = true; break;
        case OP_ST:   x->val = (uint16_t)(x->a + (uint16_t)x->d.imm); break;
        case OP_BEQ:  if (x->a == x->b) { redirect = true; target = (uint16_t)(next + (uint16_t)x->d.imm); } break;
        case OP_JAL:  x->val = next; x->wb = true; redirect = true; target = (uint16_t)(next + (uint16_t)x->d.imm); break;
        case OP_HALT: halt_ex = true; break;
        }
    }
    /* ID (후반부 읽기): 디코드 + 레지스터 읽기. WB 가 방금 쓴 값이 보인다. */
    slot_t *id = &p->st[S_ID];
    if (id->valid) {
        id->bad = !decode(id->word, &id->d);
        id->a = p->reg[id->d.rs];
        id->b = p->reg[id->d.rt];
    }
    bool stall = !p->no_hazard_unit && hazard_stall(p);
    pipe_advance(p, stall, redirect, target, halt_ex);
}

static pipe_status_t pipe_run(pipe_t *p, uint64_t max_cycles) {
    while (p->status == PIPE_RUNNING) {
        if (p->cycle >= max_cycles) return PIPE_LIMIT;
        pipe_cycle(p);
    }
    return p->status;
}

/* 다이어그램을 문자열로. 행 = 동적 명령(flush 된 것 포함), 열 = 사이클. 줄 끝 공백은 지운다. */
static size_t diag_render(const diag_t *dg, uint64_t ncyc, char *out, size_t cap) {
    static const char *cell[] = { "   ", "IF ", "ID ", "EX ", "ME ", "WB ", "xx " };
    size_t len = 0;
    char line[512], dis[32];
    if (ncyc > DIAG_COLS) ncyc = DIAG_COLS;
    for (int r = -1; r < (int)dg->nrows; r++) {
        int n;
        if (r < 0) n = snprintf(line, sizeof line, "%-23s", " pc  instruction");
        else {
            disasm(dg->r[r].word, dis, sizeof dis);
            n = snprintf(line, sizeof line, "%3u  %-17s ", dg->r[r].pc, dis);
        }
        for (uint64_t c = 0; c < ncyc && n + 4 < (int)sizeof line; c++) {
            if (r < 0) n += snprintf(line + n, sizeof line - (size_t)n, "%-3u", (unsigned)(c + 1));
            else       n += snprintf(line + n, sizeof line - (size_t)n, "%s", cell[dg->r[r].cell[c]]);
        }
        while (n > 0 && line[n - 1] == ' ') line[--n] = '\0';
        if (len + (size_t)n + 2 < cap) { memcpy(out + len, line, (size_t)n); len += (size_t)n; out[len++] = '\n'; out[len] = '\0'; }
    }
    return len;
}

static void pipe_summary(const pipe_t *p, char *s, size_t cap) {
    snprintf(s, cap, "cycles=%llu retired=%llu CPI=%.2f stalls=%llu flushes=%llu",
             (unsigned long long)p->cycle, (unsigned long long)p->retired,
             p->retired ? (double)p->cycle / (double)p->retired : 0.0,
             (unsigned long long)p->stalls, (unsigned long long)p->flushes);
}

/* ===================== 정답지: 단일 사이클 B09 모델 (+ retire 트레이스) ===================== */
typedef struct { insn_t d; bool redirect; } trace_t;   /* redirect = taken BEQ 또는 JAL */
typedef struct { uint16_t reg[NREG], dmem[DMEM_WORDS]; uint64_t retired; bool ok; } ref_t;

static void ref_run(const uint16_t *imem, size_t n, const uint16_t *dmem0, ref_t *r,
                    trace_t *tr, size_t trcap, size_t *ntr) {
    memset(r, 0, sizeof *r);
    memcpy(r->dmem, dmem0, sizeof r->dmem);
    uint16_t pc = 0;
    size_t k = 0;
    for (uint64_t step = 0; step < 100000; step++) {
        uint16_t w = pc < n ? imem[pc] : I_NOP;
        insn_t d;
        if (pc >= IMEM_WORDS || !decode(w, &d)) return;
        uint16_t a = r->reg[d.rs], b = r->reg[d.rt], next = (uint16_t)(pc + 1u), v = 0;
        bool wb = false, redir = false;
        switch (d.op) {
        case OP_ADD:  v = (uint16_t)(a + b); wb = true; break;
        case OP_ADDI: v = (uint16_t)(a + (uint16_t)d.imm); wb = true; break;
        case OP_LD: case OP_ST: {
            uint16_t ea = (uint16_t)(a + (uint16_t)d.imm);
            if (ea >= DMEM_WORDS) return;
            if (d.op == OP_LD) { v = r->dmem[ea]; wb = true; } else r->dmem[ea] = b;
            break;
        }
        case OP_BEQ:  if (a == b) { redir = true; next = (uint16_t)(next + (uint16_t)d.imm); } break;
        case OP_JAL:  v = next; wb = true; redir = true; next = (uint16_t)(next + (uint16_t)d.imm); break;
        case OP_HALT: break;
        }
        if (wb && d.rd != 0) r->reg[d.rd] = v;
        r->retired++;
        if (tr && k < trcap) { tr[k].d = d; tr[k].redirect = redir; }
        k++;
        if (d.op == OP_HALT) { r->ok = true; break; }
        pc = next;
    }
    if (ntr) *ntr = k;
}

/* ---------------- Part B: 해석적 사이클 공식 (시뮬레이터와 독립인 두 번째 정답지) ----------------
 * EX(k) = 명령 k 가 EX 에 들어가는 사이클. EX(0) = 3.
 *   순서:      EX(k) >= EX(k-1) + 1, k-1 이 리다이렉트(taken BEQ/JAL)면 + 3 (2 사이클 flush)
 *   RAW:       k 가 읽는 레지스터의 가장 최근 생산자 p 에 대해 EX(k) >= EX(p) + 3
 *              (소비자의 ID = EX(k) - 1 >= 생산자의 WB = EX(p) + 2, split-phase)
 *   총 사이클 = EX(HALT) + 2 (HALT 의 WB). */
static uint64_t analytic_cycles(const trace_t *t, size_t n) {
    (void)t; (void)n;
    return 0; /* TODO: 위 세 줄의 규칙. ready[r] = r 을 읽는 소비자가 들어갈 수 있는 가장 이른 EX 사이클 */
}

/* ===================== Part C: 루프 프로그램 ===================== */
/* B09 배열 합. dmem[0] = N, a[] = dmem[16..], 결과 dmem[1]. 명령 수 6N + 6 */
static const uint16_t prog_sum[] = {
    I_LD(1, 0, 0),        /*  0:       LD   r1, 0(r0)      ; N                 */
    I_ADDI(2, 0, 16),     /*  1:       ADDI r2, r0, 16     ; p                 */
    I_ADD(3, 0, 0),       /*  2:       ADD  r3, r0, r0     ; sum = 0           */
    I_BEQ(1, 0, 5),       /*  3: loop: BEQ  r1, r0, done                       */
    I_LD(4, 2, 0),        /*  4:       LD   r4, 0(r2)                          */
    I_ADD(3, 3, 4),       /*  5:       ADD  r3, r3, r4     ; r4 거리 1 → 스톨 2 */
    I_ADDI(2, 2, 1),      /*  6:       ADDI r2, r2, 1                          */
    I_ADDI(1, 1, -1),     /*  7:       ADDI r1, r1, -1                         */
    I_BEQ(0, 0, -6),      /*  8:       BEQ  r0, r0, loop   ; taken → flush 2   */
    I_ST(3, 0, 1),        /*  9: done: ST   r3, 1(r0)                          */
    I_HALT,               /* 10:       HALT                                    */
};

/* 같은 일을 하되 루프 몸통을 재배치해 스톨 0 (명령 수 6N + 6 그대로). 9: done 의 위치도 그대로. */
static const uint16_t prog_sum_sched[] = {
    I_LD(1, 0, 0),        /*  0:       LD   r1, 0(r0)                          */
    I_ADDI(2, 0, 16),     /*  1:       ADDI r2, r0, 16                         */
    I_ADD(3, 0, 0),       /*  2:       ADD  r3, r0, r0                         */
    I_BEQ(1, 0, 5),       /*  3: loop: BEQ  r1, r0, done                       */
    I_LD(4, 2, 0),        /*  4:       LD   r4, 0(r2)      ; TODO: 4~7 을 재배치해  */
    I_ADD(3, 3, 4),       /*  5:       ADD  r3, r3, r4     ;   스톨 0 으로 만든다   */
    I_ADDI(2, 2, 1),      /*  6:       ADDI r2, r2, 1      ;   (지금은 prog_sum 과  */
    I_ADDI(1, 1, -1),     /*  7:       ADDI r1, r1, -1     ;    같은 순서)          */
    I_BEQ(0, 0, -6),      /*  8:       BEQ  r0, r0, loop                       */
    I_ST(3, 0, 1),        /*  9: done: ST   r3, 1(r0)                          */
    I_HALT,               /* 10:       HALT                                    */
};

/* B09 memcpy. dmem[0] = n, dmem[1] = src, dmem[2] = dst. 명령 수 7n + 5 */
static const uint16_t prog_memcpy[] = {
    I_LD(1, 0, 0), I_LD(2, 0, 1), I_LD(3, 0, 2),
    I_BEQ(1, 0, 6),       /*  3: loop: BEQ  r1, r0, done                       */
    I_LD(4, 2, 0),        /*  4:       LD   r4, 0(r2)                          */
    I_ST(4, 3, 0),        /*  5:       ST   r4, 0(r3)      ; 데이터 r4 거리 1   */
    I_ADDI(2, 2, 1),      /*  6:       ADDI r2, r2, 1                          */
    I_ADDI(3, 3, 1),      /*  7:       ADDI r3, r3, 1                          */
    I_ADDI(1, 1, -1),     /*  8:       ADDI r1, r1, -1                         */
    I_BEQ(0, 0, -7),      /*  9:       BEQ  r0, r0, loop                       */
    I_HALT,               /* 10: done: HALT                                    */
};

/* memcpy 재배치판: LD 와 ST 사이에 ADDI 두 개 → 거리 3 */
static const uint16_t prog_memcpy_sched[] = {
    I_LD(1, 0, 0), I_LD(2, 0, 1), I_LD(3, 0, 2),
    I_BEQ(1, 0, 6),       /*  3: loop: BEQ  r1, r0, done                       */
    I_LD(4, 2, 0),        /*  4:       LD   r4, 0(r2)                          */
    I_ADDI(2, 2, 1),      /*  5:       ADDI r2, r2, 1                          */
    I_ADDI(1, 1, -1),     /*  6:       ADDI r1, r1, -1                         */
    I_ST(4, 3, 0),        /*  7:       ST   r4, 0(r3)      ; r4 거리 3          */
    I_ADDI(3, 3, 1),      /*  8:       ADDI r3, r3, 1                          */
    I_BEQ(0, 0, -7),      /*  9:       BEQ  r0, r0, loop                       */
    I_HALT,               /* 10: done: HALT                                    */
};

/* ---------------- Part C: 클럭 주기 ----------------
 * 단계 지연 (B09 의 P&H 예제 값, ps): IF 200 (imem), ID 100 (reg read), EX 200 (ALU), MEM 200 (dmem), WB 100 (reg write).
 * 레지스터 오버헤드 = Tclk-q + Tsetup (ARCH-B13). 이 레슨의 가정값 30 + 20 = 50 ps.
 * 파이프라인: Tclk = 가장 느린 단계 + 오버헤드. 단일 사이클: Tclk = 모든 단계의 합 + 오버헤드 (B09 의 LD 경로 800 ps). */
static const uint32_t stage_ps[NSTAGE] = { 200, 100, 200, 200, 100 };
enum { REG_OVERHEAD_PS = 50 };

static uint32_t pipe_tclk_ps(const uint32_t *st, unsigned n, uint32_t overhead) {
    (void)st; (void)n; (void)overhead;
    return 0; /* TODO: 가장 느린 단계 + 오버헤드 */
}

static uint32_t single_tclk_ps(const uint32_t *st, unsigned n, uint32_t overhead) {
    (void)st; (void)n; (void)overhead;
    return 0; /* TODO: 모든 단계의 합 + 오버헤드 */
}

/* ===================== 테스트 하네스 ===================== */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
#define NELEM(a) (sizeof(a) / sizeof((a)[0]))

static uint32_t rng_state = 0x1F2E3D4Cu;
static uint32_t xs32(void) {
    uint32_t x = rng_state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return rng_state = x;
}

static diag_t g_dg;
static char g_buf[8192];

/* 프로그램을 파이프라인과 단일 사이클 모델 양쪽에서 돌려 최종 상태·retired 비교 + 사이클 == 해석 공식. */
static bool run_both(const uint16_t *prog, size_t n, const uint16_t *dmem0, pipe_t *p, diag_t *dg) {
    static ref_t r;
    static trace_t tr[4096];
    size_t ntr = 0;
    ref_run(prog, n, dmem0, &r, tr, NELEM(tr), &ntr);
    pipe_load(p, prog, n, dg);
    memcpy(p->dmem, dmem0, sizeof p->dmem);
    pipe_status_t s = pipe_run(p, 100000);
    return r.ok && s == PIPE_HALTED && memcmp(p->reg, r.reg, sizeof r.reg) == 0 &&
           memcmp(p->dmem, r.dmem, sizeof r.dmem) == 0 && p->retired == r.retired &&
           ntr <= NELEM(tr) && analytic_cycles(tr, ntr) == p->cycle;
}

static uint64_t cycles_of(const uint16_t *prog, size_t n, uint64_t *stalls, uint64_t *flushes) {
    static pipe_t p;
    static const uint16_t z[DMEM_WORDS];
    pipe_load(&p, prog, n, NULL);
    memcpy(p.dmem, z, sizeof z);
    if (pipe_run(&p, 100000) != PIPE_HALTED) return 0;
    if (stalls) *stalls = p.stalls;
    if (flushes) *flushes = p.flushes;
    return p.cycle;
}

static void test_part_a(void) {
    static pipe_t p;
    /* 파이프라인 레지스터: pipe_cycle 이 끝난 시점 = 클럭 에지 직후. 3 사이클 뒤
     * IF/ID(st[S_ID]) = pc2, ID/EX = pc1, EX/MEM = pc0, IF 칸은 비어 있고 다음 fetch pc = 3 */
    static const uint16_t p_ind[] = { I_ADDI(1, 0, 1), I_ADDI(2, 0, 2), I_ADDI(3, 0, 3), I_ADDI(4, 0, 4),
                                      I_ADDI(5, 0, 5), I_HALT };
    pipe_load(&p, p_ind, NELEM(p_ind), NULL);
    for (int i = 0; i < 3; i++) pipe_cycle(&p);
    CHECK(!p.st[S_IF].valid && p.st[S_ID].pc == 2 && p.st[S_EX].pc == 1 && p.st[S_MEM].pc == 0 &&
          p.st[S_MEM].val == 1 && !p.st[S_WB].valid && p.pc == 3);
    pipe_cycle(&p); pipe_cycle(&p);                             /* 사이클 5: pc0 이 WB 에서 r1 을 쓰고 retire */
    CHECK(p.retired == 1 && p.reg[1] == 1 && p.st[S_WB].pc == 1 && p.st[S_ID].pc == 4 && p.reg[2] == 0);
    CHECK(pipe_run(&p, 1000) == PIPE_HALTED && p.cycle == 6 + 4 && p.retired == 6);
    CHECK(p.reg[1] == 1 && p.reg[5] == 5 && p.stalls == 0 && p.flushes == 0);

    /* 해저드 없는 N 명령 = N + 4 사이클 (N = 1..60, NOP 들 + HALT) */
    bool all = true;
    static uint16_t nops[64];
    for (unsigned n = 1; n <= 60; n++) {
        memset(nops, 0, sizeof nops);
        nops[n - 1] = I_HALT;
        uint64_t st = 99, fl = 99;
        all = all && cycles_of(nops, n, &st, &fl) == n + 4 && st == 0 && fl == 0;
    }
    CHECK(all);

    /* 다이어그램: HALT 가 EX 에 오면 뒤의 투기적 NOP 2개가 xx */
    static const uint16_t p3[] = { I_ADDI(1, 0, 1), I_ADDI(2, 0, 2), I_HALT };
    pipe_load(&p, p3, NELEM(p3), &g_dg);
    CHECK(pipe_run(&p, 100) == PIPE_HALTED && p.cycle == 7);
    diag_render(&g_dg, p.cycle, g_buf, sizeof g_buf);
    printf("%s", g_buf);
    CHECK(strcmp(g_buf,
        " pc  instruction       1  2  3  4  5  6  7\n"
        "  0  ADDI r1, r0, 1    IF ID EX ME WB\n"
        "  1  ADDI r2, r0, 2       IF ID EX ME WB\n"
        "  2  HALT                    IF ID EX ME WB\n"
        "  3  NOP                        IF ID xx\n"
        "  4  NOP                           IF xx\n") == 0);

    /* taken BEQ: EX 에서 해결 → 2개 flush, 목표는 그 다음 사이클에 IF */
    static const uint16_t p_br[] = { I_BEQ(0, 0, 2), I_ADDI(1, 0, 7), I_ADDI(1, 0, 8), I_ADDI(2, 0, 9), I_HALT };
    pipe_load(&p, p_br, NELEM(p_br), &g_dg);
    CHECK(pipe_run(&p, 100) == PIPE_HALTED && p.reg[1] == 0 && p.reg[2] == 9 && p.retired == 3);
    CHECK(p.flushes == 1 && p.cycle == 3 + 4 + 2);
    diag_render(&g_dg, p.cycle, g_buf, sizeof g_buf);
    printf("%s", g_buf);
    CHECK(strcmp(g_buf,
        " pc  instruction       1  2  3  4  5  6  7  8  9\n"
        "  0  BEQ r0, r0, +2    IF ID EX ME WB\n"
        "  1  ADDI r1, r0, 7       IF ID xx\n"
        "  2  ADDI r1, r0, 8          IF xx\n"
        "  3  ADDI r2, r0, 9             IF ID EX ME WB\n"
        "  4  HALT                          IF ID EX ME WB\n"
        "  5  NOP                              IF ID xx\n"
        "  6  NOP                                 IF xx\n") == 0);

    /* not-taken BEQ 는 공짜, JAL 은 항상 2 사이클 (오프셋 0 이어도) */
    static const uint16_t p_nt[] = { I_ADDI(1, 0, 1), I_ADDI(0, 0, 0), I_ADDI(0, 0, 0), I_BEQ(1, 0, 5), I_JAL(0, 0), I_HALT };
    pipe_load(&p, p_nt, NELEM(p_nt), NULL);
    CHECK(pipe_run(&p, 100) == PIPE_HALTED && p.retired == 6 && p.flushes == 1 && p.stalls == 0 && p.cycle == 6 + 4 + 2);

    /* imem 끝(PC 255 의 HALT) 뒤 투기적 fetch 는 PC 256, 257 → NOP 로 처리되고 flush */
    static uint16_t big[IMEM_WORDS];
    memset(big, 0, sizeof big);
    big[255] = I_HALT;
    pipe_load(&p, big, NELEM(big), NULL);
    CHECK(pipe_run(&p, 1000) == PIPE_HALTED && p.retired == 256 && p.cycle == 260 && p.pc == 258);
}

static void test_part_b(void) {
    static pipe_t p;
    static const uint16_t z[DMEM_WORDS];
    /* insn_dst / insn_srcs 표 */
    insn_t d; unsigned s[2];
    decode(I_ADD(3, 1, 2), &d);   insn_srcs(&d, s); CHECK(insn_dst(&d) == 3 && s[0] == 1 && s[1] == 2);
    decode(I_ADDI(4, 5, -1), &d); insn_srcs(&d, s); CHECK(insn_dst(&d) == 4 && s[0] == 5 && s[1] == 0);
    decode(I_LD(6, 7, 3), &d);    insn_srcs(&d, s); CHECK(insn_dst(&d) == 6 && s[0] == 7 && s[1] == 0);
    decode(I_ST(2, 3, 1), &d);    insn_srcs(&d, s); CHECK(insn_dst(&d) == 0 && s[0] == 3 && s[1] == 2);
    decode(I_BEQ(1, 2, 4), &d);   insn_srcs(&d, s); CHECK(insn_dst(&d) == 0 && s[0] == 1 && s[1] == 2);
    decode(I_JAL(7, 3), &d);      insn_srcs(&d, s); CHECK(insn_dst(&d) == 7 && s[0] == 0 && s[1] == 0);
    decode(I_ADDI(0, 1, 1), &d);  CHECK(insn_dst(&d) == 0);                 /* r0 에 쓰기 = 생산자 아님 */
    decode(I_HALT, &d);           insn_srcs(&d, s); CHECK(insn_dst(&d) == 0 && s[0] == 0 && s[1] == 0);

    /* 거리 1/2/3/4 → 스톨 2/1/0/0. 사이에 NOP 를 끼워 거리를 만든다 */
    uint64_t want[] = { 2, 1, 0, 0 };
    bool dist_ok = true;
    for (unsigned dist = 1; dist <= 4; dist++) {
        uint16_t prog[8]; size_t n = 0;
        prog[n++] = I_ADDI(1, 0, 5);
        for (unsigned i = 1; i < dist; i++) prog[n++] = I_NOP;
        prog[n++] = I_ADD(2, 1, 1);
        prog[n++] = I_HALT;
        bool ok = run_both(prog, n, z, &p, NULL) && p.stalls == want[dist - 1] && p.reg[2] == 10 &&
                  p.cycle == n + 4 + want[dist - 1];
        if (!ok) printf("  dist %u: stalls=%llu cycles=%llu\n", dist, (unsigned long long)p.stalls, (unsigned long long)p.cycle);
        dist_ok = dist_ok && ok;
    }
    CHECK(dist_ok);

    /* 거리 1 다이어그램: ID 가 3번, 뒤 명령은 IF 가 3번 */
    static const uint16_t p_raw[] = { I_ADDI(1, 0, 5), I_ADD(2, 1, 1), I_ADDI(3, 0, 1), I_HALT };
    CHECK(run_both(p_raw, NELEM(p_raw), z, &p, &g_dg) && p.stalls == 2 && p.cycle == 4 + 4 + 2);
    diag_render(&g_dg, p.cycle, g_buf, sizeof g_buf);
    printf("%s", g_buf);
    CHECK(strcmp(g_buf,
        " pc  instruction       1  2  3  4  5  6  7  8  9  10\n"
        "  0  ADDI r1, r0, 5    IF ID EX ME WB\n"
        "  1  ADD r2, r1, r1       IF ID ID ID EX ME WB\n"
        "  2  ADDI r3, r0, 1          IF IF IF ID EX ME WB\n"
        "  3  HALT                             IF ID EX ME WB\n"
        "  4  NOP                                 IF ID xx\n"
        "  5  NOP                                    IF xx\n") == 0);

    struct { const char *name; uint16_t prog[6]; size_t n; uint64_t stalls, flushes; } hz[] = {
        /* r0 는 해저드 아님: r0 에 쓰고 r0 를 읽어도 스톨 없음 */
        { "r0",          { I_ADDI(0, 0, 5), I_ADD(1, 0, 0), I_HALT }, 3, 0, 0 },
        /* 두 소스 중 더 늦은 쪽이 결정 (합이 아니라 최댓값): r1 거리 2(1), r2 거리 1(2) → 2 */
        { "max-not-sum", { I_ADDI(1, 0, 1), I_ADDI(2, 0, 2), I_ADD(3, 1, 2), I_HALT }, 4, 2, 0 },
        /* BEQ 의 소스도 RAW (not-taken) */
        { "BEQ src",     { I_ADDI(1, 0, 1), I_BEQ(1, 0, 1), I_ADDI(2, 0, 2), I_HALT }, 4, 2, 0 },
        /* ST 의 데이터 레지스터(rt)도 소스 */
        { "ST data",     { I_ADDI(1, 0, 7), I_ST(1, 0, 3), I_HALT }, 3, 2, 0 },
        /* ST 의 주소 레지스터(rs) */
        { "ST addr",     { I_ADDI(2, 0, 9), I_ST(0, 2, 1), I_HALT }, 3, 2, 0 },
        /* LD 도 M01 에선 ALU 와 같다 (포워딩이 없으니 load-use 구분이 없다; ARCH-M02) */
        { "LD prod",     { I_LD(1, 0, 4), I_ADD(2, 1, 1), I_HALT }, 3, 2, 0 },
        /* JAL 의 rd → 목표 명령: flush 2 사이클 동안 JAL 이 WB 에 도착 → 스톨 0 */
        { "JAL->target", { I_JAL(7, 0), I_ADD(1, 7, 7), I_HALT }, 3, 0, 1 },
        /* JAL r7, +1: pc1 을 건너뛰고 목표(pc2)가 r7 을 읽는다 → 여전히 0 (retired 3) */
        { "JAL skip",    { I_JAL(7, 1), I_ADDI(1, 0, 1), I_ADD(2, 7, 0), I_HALT }, 4, 0, 1 },
        /* taken BEQ 너머의 생산자: flush 가 거리를 벌려 준다 */
        { "over BEQ",    { I_ADDI(1, 0, 3), I_BEQ(0, 0, 0), I_ADD(2, 1, 1), I_HALT }, 4, 0, 1 },
        /* 같은 레지스터 연속 갱신: 가장 최근 생산자 기준 */
        { "chain",       { I_ADDI(1, 0, 1), I_ADDI(1, 1, 1), I_ADDI(1, 1, 1), I_HALT }, 4, 4, 0 },
    };
    bool hz_ok = true;
    for (unsigned i = 0; i < NELEM(hz); i++) {
        uint16_t dm[DMEM_WORDS] = { 0 };
        dm[4] = 21;
        bool ok = run_both(hz[i].prog, hz[i].n, dm, &p, NULL) && p.stalls == hz[i].stalls && p.flushes == hz[i].flushes &&
                  p.cycle == p.retired + 4 + hz[i].stalls + 2 * hz[i].flushes;
        if (!ok) printf("  %-12s stalls=%llu flushes=%llu cycles=%llu\n", hz[i].name,
                        (unsigned long long)p.stalls, (unsigned long long)p.flushes, (unsigned long long)p.cycle);
        hz_ok = hz_ok && ok;
    }
    CHECK(hz_ok);

    /* 랜덤 프로그램 fuzz: 직선 코드 + 앞으로만 가는 BEQ/JAL, 끝은 HALT → 반드시 끝난다.
     * r6 은 ADDI r6, r0, k (0..31) 로만 쓰고 LD/ST 의 베이스는 r0 또는 r6 → 주소 0..62, 폴트 없음.
     * 확인: 최종 레지스터·dmem·retired == 단일 사이클 모델, 사이클 == 해석적 공식,
     *       사이클 == retired + 4 + stalls + 2·flushes. */
    static uint16_t prog[IMEM_WORDS];
    static uint16_t dm[DMEM_WORDS];
    int bad_seed = -1;
    uint64_t tot_c = 0, tot_r = 0, tot_s = 0, tot_f = 0;
    for (int t = 0; t < 3000; t++) {
        size_t L = 8 + xs32() % 113;
        for (size_t i = 0; i + 1 < L; i++) {
            unsigned room = (unsigned)(L - 2 - i);          /* 목표 <= HALT 자리 */
            unsigned k = xs32() % 16, rs = xs32() & 7, rt = xs32() & 7;
            unsigned rd = xs32() % 7; if (rd == 6) rd = 7;  /* r6 제외 */
            unsigned base = (xs32() & 1) ? 6 : 0, off5 = xs32() % 32;
            int imm6 = (int)(xs32() % 64) - 32;
            if (k < 4)       prog[i] = I_ADD(rd, rs, rt);
            else if (k < 7)  prog[i] = I_ADDI(rd, rs, imm6);
            else if (k < 8)  prog[i] = I_ADDI(6, 0, off5);
            else if (k < 10) prog[i] = I_LD(rd, base, off5);
            else if (k < 12) prog[i] = I_ST(rt, base, off5);
            else if (k < 14) prog[i] = I_BEQ(rs, rt, xs32() % ((room < 31 ? room : 31) + 1));
            else if (k < 15) prog[i] = I_JAL(rd, xs32() % (room + 1));
            else             prog[i] = I_NOP;
        }
        prog[L - 1] = I_HALT;
        for (unsigned i = 0; i < DMEM_WORDS; i++) dm[i] = (uint16_t)xs32();
        bool ok = run_both(prog, L, dm, &p, NULL) && p.cycle == p.retired + 4 + p.stalls + 2 * p.flushes;
        if (!ok) { bad_seed = t; break; }
        tot_c += p.cycle; tot_r += p.retired; tot_s += p.stalls; tot_f += p.flushes;
    }
    if (bad_seed >= 0) printf("  fuzz: first mismatch at program %d\n", bad_seed);
    CHECK(bad_seed < 0);
    printf("  fuzz 3000 programs: retired=%llu cycles=%llu stalls=%llu flushes=%llu CPI=%.3f\n",
           (unsigned long long)tot_r, (unsigned long long)tot_c, (unsigned long long)tot_s,
           (unsigned long long)tot_f, tot_r ? (double)tot_c / (double)tot_r : 0.0);

    /* 해저드 유닛을 끄면 틀린다: 같은 p_raw 를 스톨 없이 돌리면 ADD 가 아직 안 쓴 r1(= 0)을 읽는다 */
    pipe_load(&p, p_raw, NELEM(p_raw), NULL);
    p.no_hazard_unit = true;
    CHECK(pipe_run(&p, 100) == PIPE_HALTED);
    CHECK(p.reg[1] == 5 && p.reg[2] == 0 && p.cycle == 8);     /* 정답은 10 — 스톨 없이 8 사이클에 틀린 답 */
}

static void test_part_c(void) {
    static pipe_t p;
    static uint16_t dm[DMEM_WORDS];
    char sum[128];
    /* 배열 합 N=1 다이어그램: LD→ADD 스톨 2, 뒤로 가는 BEQ flush 2, 탈출 BEQ flush 2 */
    memset(dm, 0, sizeof dm);
    dm[0] = 1; dm[16] = 1234;
    CHECK(run_both(prog_sum, NELEM(prog_sum), dm, &p, &g_dg) && p.dmem[1] == 1234);
    diag_render(&g_dg, p.cycle, g_buf, sizeof g_buf);
    pipe_summary(&p, sum, sizeof sum);
    printf("%s%s\n", g_buf, sum);
    CHECK(p.cycle == 22 && p.retired == 12 && p.stalls == 2 && p.flushes == 2);

    memset(dm, 0, sizeof dm);
    dm[0] = 1; dm[16] = 1234;
    CHECK(run_both(prog_sum_sched, NELEM(prog_sum_sched), dm, &p, &g_dg) && p.dmem[1] == 1234);
    pipe_summary(&p, sum, sizeof sum);
    printf("  sum_sched N=1: %s\n", sum);
    CHECK(p.cycle == 20 && p.stalls == 0);

    /* CPI 표: N 을 바꿔 가며. 결과는 매번 단일 사이클 모델 + 해석 공식과 일치해야 한다 */
    struct { const char *name; const uint16_t *prog; size_t n; bool is_copy; } progs[] = {
        { "sum",          prog_sum,          NELEM(prog_sum),          false },
        { "sum_sched",    prog_sum_sched,    NELEM(prog_sum_sched),    false },
        { "memcpy",       prog_memcpy,       NELEM(prog_memcpy),       true  },
        { "memcpy_sched", prog_memcpy_sched, NELEM(prog_memcpy_sched), true  },
    };
    static const unsigned Ns[] = { 1, 10, 100 };
    uint64_t cyc[4][3] = { { 0 } }, ret[4][3] = { { 0 } };
    bool table_ok = true;
    printf("  %-13s %18s %18s %18s\n", "program", "N=1", "N=10", "N=100");
    for (unsigned i = 0; i < NELEM(progs); i++) {
        printf("  %-13s", progs[i].name);
        for (unsigned j = 0; j < NELEM(Ns); j++) {
            unsigned n = Ns[j];
            memset(dm, 0, sizeof dm);
            dm[0] = (uint16_t)n;
            if (progs[i].is_copy) { dm[1] = 16; dm[2] = 128; }
            for (unsigned k = 0; k < n; k++) dm[16 + k] = (uint16_t)xs32();
            bool ok = run_both(progs[i].prog, progs[i].n, dm, &p, NULL);
            if (progs[i].is_copy) ok = ok && memcmp(&p.dmem[128], &dm[16], n * sizeof(uint16_t)) == 0;
            table_ok = table_ok && ok;
            cyc[i][j] = p.cycle; ret[i][j] = p.retired;
            printf("   %5llu/%-5llu %4.2f", (unsigned long long)p.cycle, (unsigned long long)p.retired,
                   p.retired ? (double)p.cycle / (double)p.retired : 0.0);
        }
        printf("\n");
    }
    CHECK(table_ok);
    /* 손으로 센 공식: sum = 10N+12 (반복당 6 명령 + 스톨 2 + flush 2), sched = 8N+12, memcpy = 11N+11, memcpy_sched = 9N+11 */
    bool formula = true;
    for (unsigned j = 0; j < NELEM(Ns); j++) {
        uint64_t N = Ns[j];
        formula = formula && cyc[0][j] == 10 * N + 12 && cyc[1][j] == 8 * N + 12 &&
                  cyc[2][j] == 11 * N + 11 && cyc[3][j] == 9 * N + 11 &&
                  ret[0][j] == 6 * N + 6 && ret[2][j] == 7 * N + 5;
    }
    CHECK(formula);

    /* 시간: 단일 사이클 (CPI 1, 긴 클럭) vs 파이프라인 (CPI > 1, 짧은 클럭) */
    uint32_t tp = pipe_tclk_ps(stage_ps, NSTAGE, REG_OVERHEAD_PS);
    uint32_t ts = single_tclk_ps(stage_ps, NSTAGE, REG_OVERHEAD_PS);
    CHECK(tp == 250 && ts == 850);
    CHECK(tp && ts && 1000000u / tp == 4000 && 1000000u / ts == 1176);   /* Fmax MHz (내림, ARCH-B13) */
    uint64_t t_single = ret[0][2] * ts, t_pipe = cyc[0][2] * tp, t_sched = cyc[1][2] * tp;
    printf("  sum N=100: single %llu ps (606 x 850), pipe %llu ps (1012 x 250), sched %llu ps (812 x 250)\n",
           (unsigned long long)t_single, (unsigned long long)t_pipe, (unsigned long long)t_sched);
    if (t_pipe && t_sched && tp)
        printf("  speedup vs single: pipe %.2fx, sched %.2fx, ideal (CPI 1) %.2fx\n",
               (double)t_single / (double)t_pipe, (double)t_single / (double)t_sched, (double)ts / (double)tp);
    CHECK(t_single == 515100 && t_pipe == 253000 && t_sched == 203000);
}

int main(void) {
    printf("Part A — 파이프라인 레지스터 · N+4 · flush\n");
    test_part_a();
    printf("Part B — RAW 탐지 · 스톨 · 해석 공식 · fuzz\n");
    test_part_b();
    printf("Part C — 루프 · CPI · 단일 사이클 대비 시간\n");
    test_part_c();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
