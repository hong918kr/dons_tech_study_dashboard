/* ARCH-M02 포워딩 · 로드-유즈 해저드 · 분기 예측기
 *
 *   ISA      = ARCH-B08/B09 ISA 그대로 (16비트, r0~r7 중 r0 = 0 고정, imem/dmem 256 워드, 워드 주소).
 *   파이프라인 = ARCH-M01 기준선을 축소해 넣었다: 5단 IF ID EX MEM WB, split-phase 레지스터 파일
 *              (WB 가 앞 절반에 쓰고 ID 가 뒤 절반에 읽는다), 분기 비교·BEQ/JAL 목표는 EX 에서 결정,
 *              데이터 해저드는 ID 에서 생산자가 WB 에 갈 때까지 스톨, 정적 not-taken, HALT 는 EX 에서 fetch 정지.
 *   M02 기능은 설정 플래그 (M01 동작은 fwd=0, bp=NT 로 그대로 재현된다):
 *     cfg.fwd = 1   EX/MEM→EX, MEM/WB→EX 포워딩 (ALU 두 입력, BEQ 비교 입력, ST 데이터) + load-use 1사이클 스톨
 *     cfg.bp  = 2BIT 16 엔트리 2비트 포화 카운터 (PC & 15, 초기값 01 = 약한 not-taken), ID 에서 조회,
 *                   예측 taken → ID 에서 fetch 방향 전환 (1사이클), EX 에서 틀림 발견 → IF+ID 플러시 (2사이클),
 *                   JAL 은 ID 에서 항상 전환 (1사이클)
 *   Part A  포워딩 MUX fwd_src: 거리 1/2/3, 생산자 두 개, BEQ·ST 데이터·JAL 링크, r0
 *   Part B  load-use (hazard_stall), 2비트 카운터 (ctr2_next/ctr2_taken), EX 재조정 (ex_redirect), 퍼징 오라클
 *   Part C  벤치 4개 CPI 표 {no-fwd, fwd} × {NT, 2BIT}, M01 손계산 공식, 식 모델 eq_cycles ↔ 사이클 시뮬레이터
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ===================== ISA (ARCH-B08 ISA를 그대로 넣었다) ===================== */
enum { NREG = 8, IMEM_WORDS = 256, DMEM_WORDS = 256 };
enum { OP_ADD = 0x0, OP_ADDI = 0x1, OP_LD = 0x2, OP_ST = 0x3, OP_BEQ = 0x4, OP_JAL = 0x5, OP_HALT = 0xF };

typedef struct { uint8_t op, rd, rs, rt; int16_t imm; } insn_t;

static int16_t sext(uint16_t v, unsigned bits) {
    uint16_t m = (uint16_t)(1u << (bits - 1));
    v &= (uint16_t)((1u << bits) - 1u);
    return (int16_t)((int)(v ^ m) - (int)m);
}

/* 읽는 레지스터는 항상 rs/rt, 쓰는 건 rd 로 정규화 (안 쓰는 필드는 0 = r0). */
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

static void disasm(uint16_t w, char *b, size_t n) {
    insn_t d;
    if (!decode(w, &d)) { snprintf(b, n, ".word 0x%04X", w); return; }
    switch (d.op) {
    case OP_ADD:  if (w == 0) snprintf(b, n, "NOP");
                  else snprintf(b, n, "ADD  r%d, r%d, r%d", d.rd, d.rs, d.rt); break;
    case OP_ADDI: snprintf(b, n, "ADDI r%d, r%d, %d", d.rd, d.rs, d.imm); break;
    case OP_LD:   snprintf(b, n, "LD   r%d, %d(r%d)", d.rd, d.imm, d.rs); break;
    case OP_ST:   snprintf(b, n, "ST   r%d, %d(r%d)", d.rt, d.imm, d.rs); break;
    case OP_BEQ:  snprintf(b, n, "BEQ  r%d, r%d, %+d", d.rs, d.rt, d.imm); break;
    case OP_JAL:  snprintf(b, n, "JAL  r%d, %+d", d.rd, d.imm); break;
    default:      snprintf(b, n, "HALT"); break;
    }
}

/* 해저드 판정에 쓰는 성질. r0 은 생산자도 소비자도 아니다. */
static bool writes_rd(const insn_t *d) {
    return (d->op == OP_ADD || d->op == OP_ADDI || d->op == OP_LD || d->op == OP_JAL) && d->rd != 0;
}
static int src_regs(const insn_t *d, uint8_t out[2]) {
    int n = 0;
    switch (d->op) {
    case OP_ADD: case OP_ST: case OP_BEQ:          /* ST: rs = 주소 베이스, rt = 저장할 데이터 */
        if (d->rs) out[n++] = d->rs;
        if (d->rt) out[n++] = d->rt;
        break;
    case OP_ADDI: case OP_LD:
        if (d->rs) out[n++] = d->rs;
        break;
    default: break;                                 /* JAL, HALT: 읽는 레지스터 없음 */
    }
    return n;
}

/* ===================== 단일 사이클 기준 모델 (ARCH-B09 축소판) ===================== */
typedef struct { uint16_t pc, w; bool taken; } dyn_t;          /* 동적 명령 흐름 한 칸 */
typedef struct { uint16_t reg[NREG]; uint16_t dmem[DMEM_WORDS]; uint64_t retired; } arch_t;

/* 정상 HALT 이면 true. 폴트·step limit 이면 false. tr 이 있으면 은퇴 순서대로 {pc, word, taken} 기록. */
static bool iss_run(const uint16_t imem[IMEM_WORDS], arch_t *s, dyn_t *tr, size_t cap, size_t *ntr, uint64_t max_steps) {
    uint16_t pc = 0;
    size_t n = 0;
    for (uint64_t step = 0; step < max_steps; step++) {
        if (pc >= IMEM_WORDS) return false;
        uint16_t w = imem[pc];
        insn_t d;
        if (!decode(w, &d)) return false;
        uint16_t a = s->reg[d.rs], b = s->reg[d.rt], npc = (uint16_t)(pc + 1u), val = 0;
        bool wb = false, taken = false;
        switch (d.op) {
        case OP_ADD:  val = (uint16_t)(a + b); wb = true; break;
        case OP_ADDI: val = (uint16_t)(a + (uint16_t)d.imm); wb = true; break;
        case OP_LD: case OP_ST: {
            uint16_t ea = (uint16_t)(a + (uint16_t)d.imm);
            if (ea >= DMEM_WORDS) return false;
            if (d.op == OP_LD) { val = s->dmem[ea]; wb = true; } else s->dmem[ea] = b;
            break;
        }
        case OP_BEQ:  taken = a == b; if (taken) npc = (uint16_t)(npc + (uint16_t)d.imm); break;
        case OP_JAL:  val = npc; wb = true; taken = true; npc = (uint16_t)(npc + (uint16_t)d.imm); break;
        default: break;
        }
        if (wb && d.rd) s->reg[d.rd] = val;
        s->retired++;
        if (tr && n < cap) tr[n] = (dyn_t){ pc, w, taken };
        n++;
        if (d.op == OP_HALT) { if (ntr) *ntr = n; return true; }
        pc = npc;
    }
    return false;
}

/* ===================== 5단 파이프라인 ===================== */
enum { S_IF, S_ID, S_EX, S_MEM, S_WB, NSTAGE };
typedef enum { BP_NT = 0, BP_2BIT = 1 } bpol_t;
typedef struct { bool fwd; bpol_t bp; } cfg_t;
enum { FWD_NONE = 0, FWD_EXMEM = 1, FWD_MEMWB = 2 };
enum { DROWS = 48, DCOLS = 60 };

/* 스테이지 하나 = 래치 하나. s[S_MEM] 이 곧 EX/MEM 래치, s[S_WB] 가 MEM/WB 래치다. */
typedef struct {
    bool v;                 /* 유효 명령 (false = 거품) */
    uint32_t seq;           /* fetch 순번 = 다이어그램 행 */
    uint16_t pc, w;
    insn_t d;
    bool wb;                /* rd(≠ r0) 에 쓴다 */
    uint16_t a, b;          /* rs, rt 값 (ID 에서 읽고 EX 에서 포워딩으로 덮어쓸 수 있다) */
    uint16_t res;           /* EX 결과 (ALU 값 / LD·ST 주소 / JAL 링크). MEM 을 지나면 LD 는 읽은 값 */
    bool pred_taken;        /* fetch 가 따라간 방향 (NT 는 항상 false) */
} slot_t;

typedef struct {
    cfg_t cfg;
    uint16_t reg[NREG], imem[IMEM_WORDS], dmem[DMEM_WORDS];
    slot_t s[NSTAGE];
    bool fetch_on, done, mem_fault;
    uint32_t next_seq;
    uint8_t pht[16];                        /* 2비트 카운터 표 */
    uint64_t cycles, retired, stalls, flushes, halt_squash, beq, mispred, jal, fwd_used[3];
    uint32_t mp_pc[IMEM_WORDS];             /* PC 별 BEQ 예측 실패 수 */
    uint16_t drow_pc[DROWS], drow_w[DROWS]; /* 다이어그램 */
    uint8_t dcell[DROWS][DCOLS];
} pipe_t;

/* ---------------- Part A: 포워딩 MUX ----------------
 * EX 에 있는 명령이 레지스터 r 을 읽을 때 값을 어디서 가져오나.
 *   FWD_EXMEM : 바로 앞 명령 (지금 MEM, 래치 p->s[S_MEM]) 이 r 을 쓰고, 그 값이 이미 EX 에서 계산된 경우
 *   FWD_MEMWB : 두 칸 앞 명령 (지금 WB, 래치 p->s[S_WB]) 이 r 을 쓰는 경우 (LD 가 읽은 값 포함)
 *   FWD_NONE  : r == 0, 쓰는 명령이 없음 → ID 에서 읽은 값 그대로 (거리 3 이상은 split-phase 로 이미 맞다)
 * 둘 다 r 을 쓰면 더 젊은 쪽 (EX/MEM) 이 이긴다. MEM 의 LD 값은 아직 없다 — 그 경우는 load-use 스톨이 막는다. */
static int fwd_src(const pipe_t *p, uint8_t r) {
    const slot_t *m = &p->s[S_MEM], *w = &p->s[S_WB];
    if (r == 0) return FWD_NONE;
    if (m->v && m->wb && m->d.rd == r && m->d.op != OP_LD) return FWD_EXMEM;
    if (w->v && w->wb && w->d.rd == r) return FWD_MEMWB;
    return FWD_NONE;
}

/* ---------------- Part A + B: ID 단계 해저드 검출 ----------------
 * ID 에 있는 명령 d 가 지금 EX 로 가면 틀린 값을 쓰게 되는가 → true 면 이번 사이클 스톨 (IF/ID 붙잡고 EX 에 거품).
 * 지금 EX 래치 = p->s[S_EX] (바로 앞 명령), MEM 래치 = p->s[S_MEM] (두 칸 앞). WB 에 있는 생산자는 split-phase 로 해결. */
static bool hazard_stall(const pipe_t *p, const insn_t *d) {
    const slot_t *ex = &p->s[S_EX], *mem = &p->s[S_MEM];
    uint8_t src[2];
    int n = src_regs(d, src);
    for (int i = 0; i < n; i++) {
        uint8_t r = src[i];
        if (!p->cfg.fwd) {
            /* M01 기준선: 생산자가 WB 에 갈 때까지 기다린다 → 거리 1 = 2사이클, 거리 2 = 1사이클 */
            if ((ex->v && ex->wb && ex->d.rd == r) || (mem->v && mem->wb && mem->d.rd == r)) return true;
        } else {
            /* 포워딩 ON: 바로 앞이 LD 이고 그 rd 를 읽을 때만 (load-use) */
            if (ex->v && ex->wb && ex->d.op == OP_LD && ex->d.rd == r) return true;
        }
    }
    return false;
}

/* ---------------- Part B: 2비트 포화 카운터 ----------------
 * 상태 0 = 강한 not-taken, 1 = 약한 not-taken, 2 = 약한 taken, 3 = 강한 taken.
 * taken 이면 +1 (3 에서 멈춤), not-taken 이면 -1 (0 에서 멈춤). 2 이상이면 taken 으로 예측. */
static uint8_t ctr2_next(uint8_t c, bool taken) {
    if (taken) return c < 3 ? (uint8_t)(c + 1) : 3;
    return c > 0 ? (uint8_t)(c - 1) : 0;
}
static bool ctr2_taken(uint8_t c) {
    return c >= 2;
}

/* ---------------- Part B: EX 에서 fetch 를 바로잡아야 하나 ----------------
 * d 는 EX 에 있는 명령, pred_taken = fetch 가 따라간 방향, taken = EX 에서 계산한 실제 방향 (BEQ 만 의미).
 * true 를 돌려주면 IF·ID 의 두 명령을 버리고 *npc 부터 다시 fetch 한다. */
static bool ex_redirect(bpol_t pol, const insn_t *d, uint16_t pc, bool pred_taken, bool taken, uint16_t *npc) {
    uint16_t target = (uint16_t)(pc + 1u + (uint16_t)d->imm);
    if (pol == BP_NT) {                                 /* M01 제어 그대로 */
        if (d->op == OP_JAL || (d->op == OP_BEQ && taken)) { *npc = target; return true; }
        return false;
    }
    /* 2BIT: JAL 은 ID 에서 이미 돌렸다. BEQ 는 예측과 실제가 다를 때만, 올바른 쪽으로 */
    if (d->op == OP_BEQ && pred_taken != taken) {
        *npc = taken ? target : (uint16_t)(pc + 1u);
        return true;
    }
    return false;
}

/* ---------------- 사이클 엔진 (M01 기준선 + 위 함수들) ---------------- */
static void dmark(pipe_t *p, uint32_t seq, uint64_t cyc, uint8_t code) {
    if (seq < DROWS && cyc >= 1 && cyc <= DCOLS) p->dcell[seq][cyc - 1] = code;
}

static slot_t pipe_fetch(pipe_t *p, uint16_t pc) {
    slot_t s;
    memset(&s, 0, sizeof s);
    s.v = true;
    s.seq = p->next_seq++;
    s.pc = pc;
    s.w = pc < IMEM_WORDS ? p->imem[pc] : 0;            /* 추측 fetch 가 imem 끝을 넘으면 NOP */
    if (!decode(s.w, &s.d)) { s.w = 0; decode(0, &s.d); } /* 폴트 없는 프로그램만 다룬다 (예외는 ARCH-M10) */
    s.wb = writes_rd(&s.d);
    if (s.seq < DROWS) { p->drow_pc[s.seq] = pc; p->drow_w[s.seq] = s.w; }
    return s;
}

static void squash(pipe_t *p, const slot_t *s, bool by_halt) {
    if (!s->v) return;
    if (by_halt) p->halt_squash++; else p->flushes++;
    dmark(p, s->seq, p->cycles + 1, 6);
}

static uint16_t fwd_value(pipe_t *p, uint8_t r, uint16_t id_val) {
    int src = fwd_src(p, r);
    if (src == FWD_EXMEM) { p->fwd_used[FWD_EXMEM]++; return p->s[S_MEM].res; }
    if (src == FWD_MEMWB) { p->fwd_used[FWD_MEMWB]++; return p->s[S_WB].res; }
    return id_val;
}

static void pipe_init(pipe_t *p, const uint16_t *prog, size_t n, const uint16_t *dmem, cfg_t cfg) {
    memset(p, 0, sizeof *p);
    p->cfg = cfg;
    for (size_t i = 0; i < n && i < IMEM_WORDS; i++) p->imem[i] = prog[i];
    if (dmem) memcpy(p->dmem, dmem, sizeof p->dmem);
    memset(p->pht, 1, sizeof p->pht);                   /* 01 = 약한 not-taken */
    p->fetch_on = true;
    p->s[S_IF] = pipe_fetch(p, 0);                      /* 사이클 1 = PC 0 첫 fetch */
}

/* 한 사이클: 현재 래치에서 다음 래치 nx[] 를 계산하고 마지막에 한꺼번에 바꾼다 (two-phase). */
static void pipe_cycle(pipe_t *p) {
    p->cycles++;
    for (int st = 0; st < NSTAGE; st++)
        if (p->s[st].v) dmark(p, p->s[st].seq, p->cycles, (uint8_t)(st + 1));
    const slot_t *IF = &p->s[S_IF], *ID = &p->s[S_ID], *EX = &p->s[S_EX], *ME = &p->s[S_MEM], *WB = &p->s[S_WB];
    slot_t nx[NSTAGE];
    memset(nx, 0, sizeof nx);

    /* WB: 사이클 앞 절반에 레지스터 파일을 쓴다 → 같은 사이클 ID 가 새 값을 읽는다 */
    if (WB->v) {
        if (WB->wb) p->reg[WB->d.rd] = WB->res;
        p->retired++;
        if (WB->d.op == OP_HALT) { p->done = true; return; }
    }
    /* MEM: 결과는 다음 MEM/WB 래치로. 지금 EX/MEM 래치(ME->res = 주소)는 건드리지 않는다 */
    if (ME->v) {
        nx[S_WB] = *ME;
        if (ME->d.op == OP_LD || ME->d.op == OP_ST) {
            uint16_t ea = ME->res;
            if (ea >= DMEM_WORDS) p->mem_fault = true;
            else if (ME->d.op == OP_LD) nx[S_WB].res = p->dmem[ea];
            else p->dmem[ea] = ME->b;
        }
    }
    /* EX: 포워딩 → ALU / 분기 결정 / 예측 표 갱신 */
    bool ex_flush = false, halt_stop = false;
    uint16_t ex_npc = 0;
    if (EX->v) {
        slot_t e = *EX;
        bool taken = false;
        if (p->cfg.fwd) { e.a = fwd_value(p, e.d.rs, e.a); e.b = fwd_value(p, e.d.rt, e.b); }
        switch (e.d.op) {
        case OP_ADD:  e.res = (uint16_t)(e.a + e.b); break;
        case OP_ADDI: case OP_LD: case OP_ST: e.res = (uint16_t)(e.a + (uint16_t)e.d.imm); break;
        case OP_BEQ:  taken = e.a == e.b; break;
        case OP_JAL:  e.res = (uint16_t)(e.pc + 1u); p->jal++; break;
        default:      halt_stop = true; break;          /* HALT: 이 아래로 오는 건 전부 젊은 명령 */
        }
        if (e.d.op == OP_BEQ) {
            p->beq++;
            if (e.pred_taken != taken) { p->mispred++; p->mp_pc[e.pc]++; }
            if (p->cfg.bp == BP_2BIT) p->pht[e.pc & 15u] = ctr2_next(p->pht[e.pc & 15u], taken);
        }
        if (!halt_stop) ex_flush = ex_redirect(p->cfg.bp, &e.d, e.pc, e.pred_taken, taken, &ex_npc);
        nx[S_MEM] = e;
    }
    /* ID: 레지스터 읽기 (WB 가 쓴 뒤) → 해저드 검사 → (2BIT) 예측 */
    bool stall = false, id_redirect = false;
    uint16_t id_npc = 0;
    slot_t id = *ID;
    if (id.v && !ex_flush && !halt_stop) {
        id.a = p->reg[id.d.rs];
        id.b = p->reg[id.d.rt];
        stall = hazard_stall(p, &id.d);
        if (!stall && p->cfg.bp == BP_2BIT) {
            uint16_t t = (uint16_t)(id.pc + 1u + (uint16_t)id.d.imm);
            if (id.d.op == OP_JAL || (id.d.op == OP_BEQ && ctr2_taken(p->pht[id.pc & 15u]))) {
                id.pred_taken = true; id_redirect = true; id_npc = t;
            }
        }
    }
    /* 다음 래치. 우선순위: HALT(EX) > EX 재조정 > ID 스톨 > ID 방향 전환 > 순차 */
    if (halt_stop) {
        squash(p, ID, true); squash(p, IF, true);
        p->fetch_on = false;
    } else if (ex_flush) {
        squash(p, ID, false); squash(p, IF, false);
        nx[S_IF] = pipe_fetch(p, ex_npc);
    } else if (stall) {
        p->stalls++;
        nx[S_ID] = *ID;                                 /* IF/ID 붙잡기, EX 에는 거품 */
        nx[S_IF] = *IF;
    } else {
        nx[S_EX] = id;
        if (id_redirect) {
            squash(p, IF, false);
            nx[S_IF] = pipe_fetch(p, id_npc);
        } else {
            nx[S_ID] = *IF;
            if (p->fetch_on && IF->v) nx[S_IF] = pipe_fetch(p, (uint16_t)(IF->pc + 1u));
        }
    }
    memcpy(p->s, nx, sizeof nx);
}

static bool pipe_run(pipe_t *p, uint64_t max_cycles) {
    while (!p->done && p->cycles < max_cycles) pipe_cycle(p);
    return p->done;
}

static void pipe_summary(const pipe_t *p) {
    printf("      cycles=%llu retired=%llu CPI=%.3f stalls=%llu flushes=%llu\n",
           (unsigned long long)p->cycles, (unsigned long long)p->retired,
           p->retired ? (double)p->cycles / (double)p->retired : 0.0,
           (unsigned long long)p->stalls, (unsigned long long)p->flushes);
}

static void pipe_diagram(const pipe_t *p) {
    static const char *cell[] = { "  ", "IF", "ID", "EX", "ME", "WB", "xx" };
    unsigned rows = p->next_seq < DROWS ? p->next_seq : DROWS;
    unsigned cols = p->cycles < DCOLS ? (unsigned)p->cycles : DCOLS;
    printf("      %-22s", "pc  instruction");
    for (unsigned c = 1; c <= cols; c++) printf("%3u", c);
    printf("\n");
    for (unsigned r = 0; r < rows; r++) {
        char buf[32];
        disasm(p->drow_w[r], buf, sizeof buf);
        printf("      %3u %-18s", p->drow_pc[r], buf);
        for (unsigned c = 0; c < cols; c++) printf(" %s", cell[p->dcell[r][c]]);
        printf("\n");
    }
}

/* ---------------- Part C: 손계산 식 모델 ----------------
 * 시뮬레이터 없이, 은퇴한 명령 흐름(tr[0..n-1], HALT 로 끝남)만 보고 각 명령이 EX 에 들어가는 사이클 ex[k] 를 계산한다.
 *   ex[0] = 3,  ex[k] ≥ ex[k-1] + 1 + extra[k-1]           (extra = 앞 명령의 제어 거품 수)
 *   소비자 ex[k] ≥ 생산자 j 기준  no-fwd: ex[j] + 3 / fwd: LD 면 ex[j] + 2, 그 밖은 ex[j] + 1
 *   extra:  NT   — BEQ taken 또는 JAL → 2
 *           2BIT — JAL → 1, BEQ 예측 맞음+taken → 1, 예측 틀림 → 2, 예측 맞음+not-taken → 0
 *           (2BIT 카운터는 프로그램 순서대로 조회·갱신, 초기값 1)
 *   cycles = ex[n-1] + 2 (HALT 의 WB),  stalls = Σ(데이터 때문에 밀린 사이클),  flushes = Σ extra,
 *   mispred = fetch 방향과 실제가 다른 BEQ 수 (NT 에서는 taken BEQ 수). */
typedef struct { uint64_t cycles, stalls, flushes, mispred; } eqres_t;

static eqres_t eq_cycles(const dyn_t *tr, size_t n, cfg_t cfg) {
    eqres_t r = { 0, 0, 0, 0 };
    int64_t ready[NREG] = { 0 };            /* 이 레지스터를 읽는 소비자가 EX 에 들어갈 수 있는 가장 이른 사이클 */
    uint8_t pht[16];
    memset(pht, 1, sizeof pht);
    int64_t ex = 0;
    unsigned extra = 0;
    for (size_t k = 0; k < n; k++) {
        insn_t d;
        decode(tr[k].w, &d);
        int64_t t = k == 0 ? 3 : ex + 1 + (int64_t)extra, base = t;
        uint8_t src[2];
        int ns = src_regs(&d, src);
        for (int i = 0; i < ns; i++)
            if (ready[src[i]] > t) t = ready[src[i]];
        r.stalls += (uint64_t)(t - base);
        ex = t;
        if (writes_rd(&d)) ready[d.rd] = !cfg.fwd ? t + 3 : (d.op == OP_LD ? t + 2 : t + 1);
        extra = 0;
        if (d.op == OP_JAL) extra = cfg.bp == BP_NT ? 2 : 1;
        else if (d.op == OP_BEQ) {
            bool taken = tr[k].taken, pred = false;
            if (cfg.bp == BP_2BIT) {
                unsigned i = tr[k].pc & 15u;
                pred = ctr2_taken(pht[i]);
                pht[i] = ctr2_next(pht[i], taken);
            }
            if (pred != taken) { extra = 2; r.mispred++; }
            else if (taken) extra = 1;
        }
        r.flushes += extra;
    }
    r.cycles = (uint64_t)ex + 2;
    return r;
}

/* ===================== 프로그램 (손 어셈블, 오프셋 = 목표 - (PC+1)) ===================== */
/* (1) 배열 합 = ARCH-B09 prog_sum 그대로. dmem[0] = N, a[] = dmem[16..]. 결과 dmem[1]. load-use 가 있다 (4→5) */
static const uint16_t prog_sum[] = {
    I_LD(1, 0, 0),        /*  0:       LD   r1, 0(r0)      ; N                 */
    I_ADDI(2, 0, 16),     /*  1:       ADDI r2, r0, 16     ; p = &a[0]         */
    I_ADD(3, 0, 0),       /*  2:       ADD  r3, r0, r0     ; sum = 0           */
    I_BEQ(1, 0, 5),       /*  3: loop: BEQ  r1, r0, done   ; 9 - 4 = +5        */
    I_LD(4, 2, 0),        /*  4:       LD   r4, 0(r2)                          */
    I_ADD(3, 3, 4),       /*  5:       ADD  r3, r3, r4     ; ← load-use        */
    I_ADDI(2, 2, 1),      /*  6:       ADDI r2, r2, 1                          */
    I_ADDI(1, 1, -1),     /*  7:       ADDI r1, r1, -1                         */
    I_BEQ(0, 0, -6),      /*  8:       BEQ  r0, r0, loop   ; 3 - 9 = -6        */
    I_ST(3, 0, 1),        /*  9: done: ST   r3, 1(r0)                          */
    I_HALT,               /* 10:       HALT                                    */
};

/* (2) 의존 ALU 체인: x = 10x + 7 을 K 번 (mod 2^16). dmem[0] = x0, dmem[1] = K, 결과 dmem[2]. 거리 1 RAW 4개/반복 */
static const uint16_t prog_hash[] = {
    I_LD(1, 0, 0),        /*  0:       LD   r1, 0(r0)      ; x                 */
    I_LD(5, 0, 1),        /*  1:       LD   r5, 1(r0)      ; K                 */
    I_BEQ(5, 0, 7),       /*  2: loop: BEQ  r5, r0, done   ; 10 - 3 = +7       */
    I_ADD(2, 1, 1),       /*  3:       ADD  r2, r1, r1     ; 2x                */
    I_ADD(3, 2, 2),       /*  4:       ADD  r3, r2, r2     ; 4x                */
    I_ADD(3, 3, 3),       /*  5:       ADD  r3, r3, r3     ; 8x                */
    I_ADD(1, 3, 2),       /*  6:       ADD  r1, r3, r2     ; 10x               */
    I_ADDI(1, 1, 7),      /*  7:       ADDI r1, r1, 7      ; 10x + 7           */
    I_ADDI(5, 5, -1),     /*  8:       ADDI r5, r5, -1                         */
    I_BEQ(0, 0, -8),      /*  9:       BEQ  r0, r0, loop   ; 2 - 10 = -8       */
    I_ST(1, 0, 2),        /* 10: done: ST   r1, 2(r0)                          */
    I_HALT,               /* 11:       HALT                                    */
};

/* (3) 데이터 의존 분기: a[i] == key 개수. dmem[0] = n, dmem[1] = key, a[] = dmem[16..], 결과 dmem[2].
 *     뒤로 가는 점프는 JAL r0 (2BIT 에서 항상 ID 전환 1사이클) */
static const uint16_t prog_count[] = {
    I_LD(1, 0, 0),        /*  0:       LD   r1, 0(r0)      ; n                 */
    I_LD(2, 0, 1),        /*  1:       LD   r2, 1(r0)      ; key               */
    I_ADDI(3, 0, 16),     /*  2:       ADDI r3, r0, 16     ; p                 */
    I_ADD(4, 0, 0),       /*  3:       ADD  r4, r0, r0     ; cnt = 0           */
    I_BEQ(1, 0, 7),       /*  4: loop: BEQ  r1, r0, done   ; 12 - 5 = +7       */
    I_LD(5, 3, 0),        /*  5:       LD   r5, 0(r3)                          */
    I_ADDI(3, 3, 1),      /*  6:       ADDI r3, r3, 1                          */
    I_ADDI(1, 1, -1),     /*  7:       ADDI r1, r1, -1                         */
    I_BEQ(5, 2, 1),       /*  8:       BEQ  r5, r2, hit    ; 10 - 9 = +1  ← 데이터 의존 */
    I_JAL(0, -6),         /*  9:       JAL  r0, loop       ; 4 - 10 = -6       */
    I_ADDI(4, 4, 1),      /* 10: hit:  ADDI r4, r4, 1                          */
    I_JAL(0, -8),         /* 11:       JAL  r0, loop       ; 4 - 12 = -8       */
    I_ST(4, 0, 2),        /* 12: done: ST   r4, 2(r0)                          */
    I_HALT,               /* 13:       HALT                                    */
};

/* 이중 루프 (2비트 히스테리시스 확인). r3 = M × K. PC 3 = 안쪽 루프 탈출 분기 */
#define NEST_M 4
#define NEST_K 5
static const uint16_t prog_nest[] = {
    I_ADDI(1, 0, NEST_M), /*  0:        ADDI r1, r0, M                         */
    I_BEQ(1, 0, 7),       /*  1: outer: BEQ  r1, r0, done  ; 9 - 2 = +7        */
    I_ADDI(2, 0, NEST_K), /*  2:        ADDI r2, r0, K                         */
    I_BEQ(2, 0, 3),       /*  3: inner: BEQ  r2, r0, inext ; 7 - 4 = +3        */
    I_ADDI(3, 3, 1),      /*  4:        ADDI r3, r3, 1                         */
    I_ADDI(2, 2, -1),     /*  5:        ADDI r2, r2, -1                        */
    I_BEQ(0, 0, -4),      /*  6:        BEQ  r0, r0, inner ; 3 - 7 = -4        */
    I_ADDI(1, 1, -1),     /*  7: inext: ADDI r1, r1, -1                        */
    I_BEQ(0, 0, -8),      /*  8:        BEQ  r0, r0, outer ; 1 - 9 = -8        */
    I_ST(3, 0, 0),        /*  9: done:  ST   r3, 0(r0)                         */
    I_HALT,               /* 10:        HALT                                   */
};

/* ===================== 테스트 하네스 ===================== */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; printf("PASS  %s\n", #expr); } \
    else printf("FAIL  %s  (line %d)\n", #expr, __LINE__); } while (0)
#define NELEM(a) (sizeof(a) / sizeof((a)[0]))

static uint32_t rng_state = 0x2545F491u;
static uint32_t xs32(void) {
    uint32_t x = rng_state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return rng_state = x;
}

static const cfg_t CFGS[4] = { { false, BP_NT }, { true, BP_NT }, { false, BP_2BIT }, { true, BP_2BIT } };
static const char *CFG_NAME[4] = { "noFwd+NT", "Fwd+NT", "noFwd+2BIT", "Fwd+2BIT" };
enum { M01 = 0, FWD_NT = 1, NOFWD_2B = 2, FWD_2B = 3 };

static pipe_t g_p;                                          /* 큰 구조체는 정적으로 */

static pipe_t *run(const uint16_t *prog, size_t n, const uint16_t *dmem, int cfg) {
    pipe_init(&g_p, prog, n, dmem, CFGS[cfg]);
    pipe_run(&g_p, 200000);
    return &g_p;
}

/* 같은 프로그램을 기준 모델로 돌려 아키텍처 상태가 같은가 + 사이클 항등식 */
static bool same_as_iss(const pipe_t *p, const uint16_t *prog, size_t n, const uint16_t *dmem) {
    static uint16_t im[IMEM_WORDS];
    arch_t a;
    memset(im, 0, sizeof im);
    memset(&a, 0, sizeof a);
    for (size_t i = 0; i < n && i < IMEM_WORDS; i++) im[i] = prog[i];
    if (dmem) memcpy(a.dmem, dmem, sizeof a.dmem);
    if (!iss_run(im, &a, NULL, 0, NULL, 100000)) return false;
    return p->done && !p->mem_fault && p->retired == a.retired
        && memcmp(p->reg, a.reg, sizeof a.reg) == 0 && memcmp(p->dmem, a.dmem, sizeof a.dmem) == 0
        && p->cycles == p->retired + 4 + p->stalls + p->flushes;
}

static void test_part_a(void) {
    pipe_t *p;
    /* 거리 1 체인: no-fwd 는 거리 1 마다 2사이클, fwd 는 0 */
    static const uint16_t chain[] = { I_ADDI(1, 0, 5), I_ADD(2, 1, 1), I_ADD(3, 2, 1), I_HALT };
    p = run(chain, NELEM(chain), NULL, M01);
    printf("      [chain, noFwd+NT]\n");
    pipe_diagram(p); pipe_summary(p);
    CHECK(p->cycles == 12 && p->stalls == 4 && p->reg[2] == 10 && p->reg[3] == 15);
    p = run(chain, NELEM(chain), NULL, FWD_NT);
    printf("      [chain, Fwd+NT]\n");
    pipe_diagram(p); pipe_summary(p);
    CHECK(p->cycles == 8 && p->stalls == 0 && p->reg[2] == 10 && p->reg[3] == 15);
    CHECK(p->fwd_used[FWD_EXMEM] == 3 && p->fwd_used[FWD_MEMWB] == 1);

    /* 거리 d = 1, 2, 3: no-fwd 스톨 2/1/0 (M01 규칙), fwd 경로 EX/MEM / MEM/WB / 레지스터 파일 */
    bool dist_ok = true;
    for (int dist = 1; dist <= 3; dist++) {
        uint16_t prog[8];
        size_t n = 0;
        prog[n++] = I_ADDI(1, 0, 9);
        for (int i = 1; i < dist; i++) prog[n++] = I_ADDI(5, 0, 1);
        prog[n++] = I_ADD(2, 1, 0);
        prog[n++] = I_HALT;
        p = run(prog, n, NULL, M01);
        bool ok = p->stalls == (uint64_t)(3 - dist) && p->reg[2] == 9;
        p = run(prog, n, NULL, FWD_NT);
        ok = ok && p->stalls == 0 && p->reg[2] == 9
            && p->fwd_used[FWD_EXMEM] == (uint64_t)(dist == 1) && p->fwd_used[FWD_MEMWB] == (uint64_t)(dist == 2);
        printf("      distance %d: noFwd stalls %d, Fwd path %s\n", dist, 3 - dist,
               dist == 1 ? "EX/MEM" : dist == 2 ? "MEM/WB" : "regfile (split-phase)");
        dist_ok = dist_ok && ok;
    }
    CHECK(dist_ok);

    /* 생산자 두 개: 더 젊은 쪽(EX/MEM) 값이 이긴다 */
    static const uint16_t two[] = { I_ADDI(1, 0, 1), I_ADDI(1, 0, 2), I_ADD(2, 1, 1), I_HALT };
    p = run(two, NELEM(two), NULL, FWD_NT);
    CHECK(p->reg[2] == 4 && p->stalls == 0);

    /* BEQ 비교 입력 포워딩 (r2 거리 1, r1 거리 2): taken 이어야 한다 */
    static const uint16_t beqf[] = { I_ADDI(1, 0, 4), I_ADDI(2, 0, 4), I_BEQ(1, 2, 1), I_ADDI(3, 0, 1), I_HALT };
    p = run(beqf, NELEM(beqf), NULL, FWD_NT);
    CHECK(p->reg[3] == 0 && p->stalls == 0 && p->flushes == 2);
    p = run(beqf, NELEM(beqf), NULL, M01);
    CHECK(p->reg[3] == 0 && p->stalls == 2);

    /* ST: 베이스(거리 1)와 데이터(거리 2) 둘 다 포워딩 */
    static const uint16_t stf[] = { I_ADDI(1, 0, 7), I_ADDI(2, 0, 9), I_ST(1, 2, 1), I_HALT };
    p = run(stf, NELEM(stf), NULL, FWD_NT);
    CHECK(p->dmem[10] == 7 && p->stalls == 0 && p->fwd_used[FWD_EXMEM] == 1 && p->fwd_used[FWD_MEMWB] == 1);

    /* JAL 링크도 생산자: 네 설정 모두 r1 = 2 (JAL r7 → 링크 1) */
    static const uint16_t jl[] = { I_JAL(7, 0), I_ADD(1, 7, 7), I_HALT };
    bool jal_ok = true;
    for (int c = 0; c < 4; c++) { p = run(jl, NELEM(jl), NULL, c); jal_ok = jal_ok && p->reg[1] == 2 && p->reg[7] == 1; }
    CHECK(jal_ok);
    p = run(jl, NELEM(jl), NULL, FWD_2B);
    CHECK(p->fwd_used[FWD_MEMWB] == 2 && p->stalls == 0 && p->flushes == 1);   /* ID 전환 1 거품 뒤 거리 2 */
    p = run(jl, NELEM(jl), NULL, NOFWD_2B);
    CHECK(p->stalls == 1);

    /* r0: 쓰기는 버려지고 해저드도 아니다 (no-fwd 에서도 스톨 0) */
    static const uint16_t z[] = { I_ADDI(0, 0, 5), I_ADD(1, 0, 0), I_HALT };
    p = run(z, NELEM(z), NULL, M01);
    CHECK(p->stalls == 0 && p->reg[1] == 0);
    p = run(z, NELEM(z), NULL, FWD_NT);
    CHECK(p->reg[1] == 0 && p->fwd_used[FWD_EXMEM] == 0);
}

static void test_part_b(void) {
    pipe_t *p;
    uint16_t dm[DMEM_WORDS];
    memset(dm, 0, sizeof dm);
    dm[0] = 21; dm[5] = 30; dm[30] = 77;

    /* load-use: 포워딩이 있어도 1사이클 */
    static const uint16_t lu[] = { I_LD(1, 0, 0), I_ADD(2, 1, 1), I_HALT };
    p = run(lu, NELEM(lu), dm, FWD_NT);
    printf("      [load-use, Fwd+NT]\n");
    pipe_diagram(p); pipe_summary(p);
    CHECK(p->stalls == 1 && p->cycles == 8 && p->reg[2] == 42 && p->fwd_used[FWD_MEMWB] == 2);
    p = run(lu, NELEM(lu), dm, M01);
    CHECK(p->stalls == 2 && p->cycles == 9 && p->reg[2] == 42);

    /* LD 다음다음 사용: 스톨 없음 (MEM/WB 포워딩) */
    static const uint16_t lu2[] = { I_LD(1, 0, 0), I_ADDI(5, 0, 1), I_ADD(2, 1, 1), I_HALT };
    p = run(lu2, NELEM(lu2), dm, FWD_NT);
    CHECK(p->stalls == 0 && p->reg[2] == 42);

    /* LD → ST 데이터, LD → BEQ, LD → LD 베이스(포인터 따라가기): 모두 1사이클 */
    static const uint16_t l_st[] = { I_LD(1, 0, 0), I_ST(1, 0, 1), I_HALT };
    p = run(l_st, NELEM(l_st), dm, FWD_NT);
    CHECK(p->stalls == 1 && p->dmem[1] == 21);
    static const uint16_t l_beq[] = { I_LD(1, 0, 2), I_BEQ(1, 0, 1), I_ADDI(2, 0, 1), I_HALT };
    p = run(l_beq, NELEM(l_beq), dm, FWD_NT);
    CHECK(p->stalls == 1 && p->reg[2] == 0);
    static const uint16_t chase[] = { I_ADDI(1, 0, 5), I_LD(2, 1, 0), I_LD(3, 2, 0), I_HALT };   /* dmem[dmem[5]] */
    p = run(chase, NELEM(chase), dm, FWD_NT);
    CHECK(p->stalls == 1 && p->reg[2] == 30 && p->reg[3] == 77);
    /* LD r0 은 해저드가 아니다 */
    static const uint16_t l0[] = { I_LD(0, 0, 0), I_ADD(1, 0, 0), I_HALT };
    p = run(l0, NELEM(l0), dm, FWD_NT);
    CHECK(p->stalls == 0 && p->reg[1] == 0);

    /* 2비트 카운터 전이표 (상태 4 × 결과 2) */
    CHECK(ctr2_next(0, false) == 0 && ctr2_next(0, true) == 1 && ctr2_next(1, false) == 0 && ctr2_next(1, true) == 2);
    CHECK(ctr2_next(2, false) == 1 && ctr2_next(2, true) == 3 && ctr2_next(3, false) == 2 && ctr2_next(3, true) == 3);
    CHECK(!ctr2_taken(0) && !ctr2_taken(1) && ctr2_taken(2) && ctr2_taken(3));

    /* JAL: NT 는 EX 에서 2사이클, 2BIT 는 ID 에서 1사이클 */
    static const uint16_t j1[] = { I_JAL(0, 1), I_ADDI(1, 0, 1), I_ADDI(2, 0, 2), I_HALT };
    p = run(j1, NELEM(j1), NULL, FWD_NT);
    CHECK(p->cycles == 9 && p->flushes == 2 && p->reg[1] == 0);
    p = run(j1, NELEM(j1), NULL, FWD_2B);
    printf("      [JAL, Fwd+2BIT]\n");
    pipe_diagram(p); pipe_summary(p);
    CHECK(p->cycles == 8 && p->flushes == 1 && p->reg[1] == 0 && p->reg[2] == 2);

    /* 예측 taken 인데 실제 not-taken: ID 전환 1 + EX 플러시 1 = 2사이클 */
    static const uint16_t flip[] = {
        I_ADDI(2, 0, 2),  /* 0:       ADDI r2, r0, 2                        */
        I_ADD(1, 0, 0),   /* 1:       ADD  r1, r0, r0                       */
        I_BEQ(1, 0, 1),   /* 2: loop: BEQ  r1, r0, +1   ; 1회 taken, 2회 not */
        I_ADDI(3, 3, 1),  /* 3:       ADDI r3, r3, 1                        */
        I_ADDI(1, 0, 1),  /* 4:       ADDI r1, r0, 1                        */
        I_ADDI(2, 2, -1), /* 5:       ADDI r2, r2, -1                       */
        I_BEQ(2, 0, 1),   /* 6:       BEQ  r2, r0, +1   ; → 8               */
        I_JAL(0, -6),     /* 7:       JAL  r0, loop     ; 2 - 8 = -6        */
        I_HALT,           /* 8 */
    };
    p = run(flip, NELEM(flip), NULL, FWD_2B);
    printf("      [flip, Fwd+2BIT]\n");
    pipe_diagram(p); pipe_summary(p);
    CHECK(p->mp_pc[2] == 2 && p->mp_pc[6] == 1 && p->reg[3] == 1 && p->retired == 13);
    CHECK(p->flushes == 7 && p->stalls == 0 && p->cycles == 24);

    /* 이중 루프: 2비트는 안쪽 루프 탈출에서 바깥 반복마다 한 번만 틀린다 */
    bool nest_ok = true;
    for (int c = 0; c < 4; c++) {
        p = run(prog_nest, NELEM(prog_nest), NULL, c);
        nest_ok = nest_ok && p->reg[3] == NEST_M * NEST_K && p->dmem[0] == NEST_M * NEST_K && same_as_iss(p, prog_nest, NELEM(prog_nest), NULL);
    }
    CHECK(nest_ok);
    p = run(prog_nest, NELEM(prog_nest), NULL, FWD_2B);
    printf("      nest M=%d K=%d Fwd+2BIT: mispred inner-exit(pc3)=%u outer-exit(pc1)=%u back(pc6)=%u back(pc8)=%u total=%llu\n",
           NEST_M, NEST_K, p->mp_pc[3], p->mp_pc[1], p->mp_pc[6], p->mp_pc[8], (unsigned long long)p->mispred);
    CHECK(p->mp_pc[3] == NEST_M && p->mp_pc[1] == 1 && p->mp_pc[6] == 1 && p->mp_pc[8] == 1 && p->mispred == NEST_M + 3);
    p = run(prog_nest, NELEM(prog_nest), NULL, FWD_NT);
    CHECK(p->mispred == NEST_M + NEST_M * NEST_K + NEST_M + 1);   /* NT: taken BEQ 전부 */

    /* 퍼징 오라클: 무작위 프로그램 2000개 × 설정 4개 → 기준 모델과 아키텍처 상태 같음 + 항등식 */
    static uint16_t prog[IMEM_WORDS];
    uint16_t fdm[DMEM_WORDS];
    int bad = 0, runs = 0;
    for (int t = 0; t < 2000; t++) {
        size_t len = 8 + xs32() % 40;
        bool no_land[IMEM_WORDS] = { false };
        uint8_t br[IMEM_WORDS] = { 0 };                 /* 1 = BEQ 자리, 2 = JAL 자리 (오프셋은 2차 패스에서) */
        memset(prog, 0, sizeof prog);
        for (size_t i = 0; i + 1 < len; ) {             /* 1차: 명령 종류, 메모리 쌍 */
            unsigned k = xs32() % 16;
            if (k < 4 && i + 2 < len - 1) {             /* 주소가 늘 0..62: ADDI rb, r0, 0..31 ; LD/ST rx, 0..31(rb) */
                unsigned rb = 1 + xs32() % 7, rx = xs32() % 8;
                prog[i++] = I_ADDI(rb, 0, xs32() % 32);
                no_land[i] = true;                      /* 분기가 쌍의 두 번째 칸에 떨어지면 rb 가 안 정해진다 */
                prog[i++] = (xs32() & 1) ? I_LD(rx, rb, xs32() % 32) : I_ST(rx, rb, xs32() % 32);
            } else if (k < 9) {
                prog[i++] = I_ADD(xs32() % 8, xs32() % 8, xs32() % 8);
            } else if (k < 12) {
                prog[i++] = I_ADDI(xs32() % 8, xs32() % 8, (int)(xs32() % 64) - 32);
            } else {
                br[i++] = k < 15 ? 1 : 2;
            }
        }
        for (size_t i = 0; i + 1 < len; i++) {         /* 2차: 앞으로만 가는 BEQ / JAL */
            if (!br[i]) continue;
            unsigned room = (unsigned)(len - 1 - (i + 1)), off = xs32() % (room + 1);
            if (br[i] == 1 && off > 31) off = 31;
            if (no_land[i + 1 + off]) off = 0;          /* i+1 은 쌍의 두 번째 칸일 수 없다 */
            if (br[i] == 1) {
                unsigned rs = xs32() % 8, rt = (xs32() % 3 == 0) ? rs : xs32() % 8;
                prog[i] = I_BEQ(rs, rt, off);
            } else {
                prog[i] = I_JAL(xs32() % 8, off);
            }
        }
        prog[len - 1] = I_HALT;
        for (int i = 0; i < 64; i++) fdm[i] = (uint16_t)xs32();
        memset(fdm + 64, 0, sizeof fdm - 64 * sizeof fdm[0]);
        for (int c = 0; c < 4; c++) {
            p = run(prog, len, fdm, c);
            runs++;
            if (!same_as_iss(p, prog, len, fdm)) {
                if (bad++ < 3) printf("      fuzz mismatch: program %d (len %zu) cfg %s\n", t, len, CFG_NAME[c]);
            }
        }
    }
    printf("      fuzz: %d runs, %d mismatches\n", runs, bad);
    CHECK(bad == 0);
}

/* ---------------- Part C ---------------- */
typedef struct { const char *name; const uint16_t *prog; size_t n; uint16_t dm[DMEM_WORDS]; uint16_t out_addr, want; } bench_t;

static void make_benches(bench_t b[4]) {
    memset(b, 0, 4 * sizeof *b);
    rng_state = 0xB5297A4Du;                                /* 벤치 데이터 전용 시드 (practice.c 연습 2 와 같은 데이터) */
    /* sum N = 64 */
    b[0].name = "sum N=64 (load-use)"; b[0].prog = prog_sum; b[0].n = NELEM(prog_sum);
    b[0].dm[0] = 64; b[0].out_addr = 1;
    for (int i = 0; i < 64; i++) { b[0].dm[16 + i] = (uint16_t)xs32(); b[0].want = (uint16_t)(b[0].want + b[0].dm[16 + i]); }
    /* hash K = 40 */
    b[1].name = "hash K=40 (ALU chain)"; b[1].prog = prog_hash; b[1].n = NELEM(prog_hash);
    b[1].dm[0] = 1; b[1].dm[1] = 40; b[1].out_addr = 2;
    uint16_t x = 1;
    for (int i = 0; i < 40; i++) x = (uint16_t)(10u * x + 7u);
    b[1].want = x;
    /* count n = 200, key = 7: random 50% vs sorted (matches first) */
    for (int k = 2; k < 4; k++) {
        b[k].name = k == 2 ? "count n=200 random" : "count n=200 sorted";
        b[k].prog = prog_count; b[k].n = NELEM(prog_count);
        b[k].dm[0] = 200; b[k].dm[1] = 7; b[k].out_addr = 2;
    }
    unsigned hits = 0;
    for (int i = 0; i < 200; i++) {
        bool m = xs32() & 1;
        hits += m;
        b[2].dm[16 + i] = m ? 7 : 3;
    }
    for (int i = 0; i < 200; i++) b[3].dm[16 + i] = (unsigned)i < hits ? 7 : 3;
    b[2].want = b[3].want = (uint16_t)hits;
}

static bool eq_matches_sim(const pipe_t *p, const uint16_t *prog, size_t n, const uint16_t *dmem, cfg_t cfg) {
    static uint16_t im[IMEM_WORDS];
    static dyn_t tr[200000];
    arch_t a;
    size_t ntr = 0;
    memset(im, 0, sizeof im);
    memset(&a, 0, sizeof a);
    for (size_t i = 0; i < n && i < IMEM_WORDS; i++) im[i] = prog[i];
    if (dmem) memcpy(a.dmem, dmem, sizeof a.dmem);
    if (!iss_run(im, &a, tr, NELEM(tr), &ntr, 100000)) return false;
    eqres_t e = eq_cycles(tr, ntr, cfg);
    return e.cycles == p->cycles && e.stalls == p->stalls && e.flushes == p->flushes && e.mispred == p->mispred;
}

static void test_part_c(void) {
    pipe_t *p;
    static bench_t b[4];
    make_benches(b);

    /* 벤치 4개 × 설정 4개: 결과값, 기준 모델과 같음, 식 모델과 사이클 일치 */
    uint64_t cyc[4][4], st[4][4], fl[4][4], mp[4][4], ret[4];
    bool ok = true, eq_ok = true;
    for (int i = 0; i < 4; i++) {
        for (int c = 0; c < 4; c++) {
            p = run(b[i].prog, b[i].n, b[i].dm, c);
            ok = ok && p->dmem[b[i].out_addr] == b[i].want && same_as_iss(p, b[i].prog, b[i].n, b[i].dm);
            eq_ok = eq_ok && eq_matches_sim(p, b[i].prog, b[i].n, b[i].dm, CFGS[c]);
            cyc[i][c] = p->cycles; st[i][c] = p->stalls; fl[i][c] = p->flushes; mp[i][c] = p->mispred;
            ret[i] = p->retired;
        }
    }
    CHECK(ok);
    CHECK(eq_ok);
    printf("      %-22s %7s", "program", "retired");
    for (int c = 0; c < 4; c++) printf(" %16s", CFG_NAME[c]);
    printf("\n");
    for (int i = 0; i < 4; i++) {
        printf("      %-22s %7llu", b[i].name, (unsigned long long)ret[i]);
        for (int c = 0; c < 4; c++) printf("   %5llu (%5.3f)", (unsigned long long)cyc[i][c], (double)cyc[i][c] / (double)ret[i]);
        printf("\n");
    }
    printf("      stalls / flushes / mispred:\n");
    for (int i = 0; i < 4; i++) {
        printf("      %-22s        ", b[i].name);
        for (int c = 0; c < 4; c++)
            printf(" %4llu/%4llu/%4llu", (unsigned long long)st[i][c], (unsigned long long)fl[i][c], (unsigned long long)mp[i][c]);
        printf("\n");
    }
    bool order_ok = true;
    for (int i = 0; i < 4; i++)
        order_ok = order_ok && cyc[i][FWD_NT] <= cyc[i][M01] && cyc[i][FWD_2B] <= cyc[i][NOFWD_2B] && cyc[i][FWD_2B] < cyc[i][M01];
    CHECK(order_ok);                                         /* 포워딩은 절대 느려지지 않는다 */
    CHECK(mp[3][FWD_2B] < mp[2][FWD_2B] / 10);                /* 정렬된 데이터는 예측이 쉽다 */

    /* M01 기준선 = 손으로 유도한 공식 (풀이 참고). fwd 끄고 NT 면 M01 과 사이클이 같아야 한다 */
    static uint16_t dm[DMEM_WORDS];
    bool f_ok = true;
    for (unsigned N = 0; N <= 12; N++) {
        memset(dm, 0, sizeof dm);
        dm[0] = (uint16_t)N;
        uint64_t c0 = run(prog_sum, NELEM(prog_sum), dm, M01)->cycles;
        uint64_t c1 = run(prog_sum, NELEM(prog_sum), dm, FWD_NT)->cycles;
        uint64_t c2 = run(prog_sum, NELEM(prog_sum), dm, NOFWD_2B)->cycles;
        uint64_t c3 = run(prog_sum, NELEM(prog_sum), dm, FWD_2B)->cycles;
        f_ok = f_ok && c0 == 10 * N + 12 && c1 == 9 * N + 12
            && c2 == (N ? 9 * N + 13 : 12) && c3 == (N ? 8 * N + 13 : 12);
    }
    CHECK(f_ok);
    bool h_ok = true;
    for (unsigned K = 1; K <= 10; K++) {
        memset(dm, 0, sizeof dm);
        dm[0] = 3; dm[1] = (uint16_t)K;
        h_ok = h_ok && run(prog_hash, NELEM(prog_hash), dm, M01)->cycles == 18 * K + 13
            && run(prog_hash, NELEM(prog_hash), dm, FWD_NT)->cycles == 10 * K + 12
            && run(prog_hash, NELEM(prog_hash), dm, NOFWD_2B)->cycles == 17 * K + 14
            && run(prog_hash, NELEM(prog_hash), dm, FWD_2B)->cycles == 9 * K + 13;
    }
    CHECK(h_ok);

    /* 다이어그램: 배열 합 N = 2, 설정 둘 */
    memset(dm, 0, sizeof dm);
    dm[0] = 2; dm[16] = 5; dm[17] = 6;
    p = run(prog_sum, NELEM(prog_sum), dm, M01);
    printf("      [sum N=2, noFwd+NT]\n");
    pipe_diagram(p); pipe_summary(p);
    CHECK(p->dmem[1] == 11 && p->cycles == 32);
    p = run(prog_sum, NELEM(prog_sum), dm, FWD_2B);
    printf("      [sum N=2, Fwd+2BIT]\n");
    pipe_diagram(p); pipe_summary(p);
    CHECK(p->dmem[1] == 11 && p->cycles == 29);

    /* 식 모델 ↔ 사이클 시뮬레이터: 무작위 프로그램 1000개 × 설정 4개 (Part B 와 같은 생성 규칙, 다른 시드) */
    static uint16_t prog[IMEM_WORDS];
    uint16_t fdm[DMEM_WORDS];
    int bad = 0;
    rng_state = 0x9E3779B9u;
    for (int t = 0; t < 1000; t++) {
        size_t len = 8 + xs32() % 40;
        memset(prog, 0, sizeof prog);
        for (size_t i = 0; i + 1 < len; i++) {
            unsigned k = xs32() % 16, room = (unsigned)(len - 1 - (i + 1)), off = xs32() % (room + 1);
            if (k < 3)       prog[i] = I_LD(xs32() % 8, 0, xs32() % 32);
            else if (k < 4)  prog[i] = I_ST(xs32() % 8, 0, xs32() % 32);
            else if (k < 9)  prog[i] = I_ADD(xs32() % 8, xs32() % 8, xs32() % 8);
            else if (k < 12) prog[i] = I_ADDI(xs32() % 8, xs32() % 8, (int)(xs32() % 64) - 32);
            else if (k < 15) prog[i] = I_BEQ(xs32() % 4, xs32() % 4, off > 31 ? 31 : off);
            else             prog[i] = I_JAL(xs32() % 8, off);
        }
        prog[len - 1] = I_HALT;
        for (int i = 0; i < 32; i++) fdm[i] = (uint16_t)(xs32() & 3);
        memset(fdm + 32, 0, sizeof fdm - 32 * sizeof fdm[0]);
        for (int c = 0; c < 4; c++) {
            p = run(prog, len, fdm, c);
            if (!eq_matches_sim(p, prog, len, fdm, CFGS[c])) bad++;
        }
    }
    printf("      eq model vs cycle sim: 4000 random runs, %d mismatches\n", bad);
    CHECK(bad == 0);
}

int main(void) {
    printf("-- Part A: forwarding --\n");
    test_part_a();
    printf("-- Part B: load-use, 2-bit predictor, fuzz oracle --\n");
    test_part_b();
    printf("-- Part C: benchmarks, M01 formulas, equation model --\n");
    test_part_c();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
