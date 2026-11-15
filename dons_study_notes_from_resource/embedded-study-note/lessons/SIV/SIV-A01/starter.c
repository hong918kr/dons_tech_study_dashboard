/* SIV-A01 Constrained-Random 테스트와 Scoreboard
 *
 * 가상 SoC "KES1" 의 DMA-lite + 인터럽트 상태 블록을 host 에서 흉내 낸다.
 *   DUT   = 실리콘 대역 (cnt 레지스터 + RTL 스타일 next-state, 결함 스위치 bugs)
 *   Model = 골든 참조 모델 (절대 cycle 의 done_at 타임스탬프 — DUT 와 코드를 공유하지 않는다)
 * 한 op = 버스 1 cycle (OP_IDLE 만 n cycle). DMA 완료는 LEN cycle 뒤에 저절로 일어나므로
 * "버스 쓰기 cycle 과 완료 cycle 이 겹치는" 순서가 생긴다 — 이 레슨의 사냥감이다.
 *
 * 레지스터 (spec):
 *   0x00 CTRL    RW  [0] EN (0 이면 START 는 ERR)
 *   0x04 STATUS  RO  [1:0] BUSY1:BUSY0
 *   0x08 INT_STS W1C [0] DONE0 [1] DONE1 [2] EVT [3] ERR — HW set 과 W1C 가 같은 cycle 이면 set 이 이긴다
 *   0x0C INT_EN  RW  [3:0]
 *   0x10 LEN0 / 0x14 LEN1  RW [7:0] 리셋값 4, BUSY 인 채널의 LEN 쓰기는 무시
 *   0x18 CMD     WO  [1:0] START1:START0 — BUSY/EN=0/LEN=0 이면 시작 안 하고 ERR
 *   0x1C IRQ     RO  [0] = (INT_STS & INT_EN) != 0
 *   BUSY 판정은 전부 "cycle 시작 시점" 값. 읽기도 cycle 시작 시점 값을 돌려준다.
 *
 *   ./solution                        채점 테스트
 *   ./solution --table                sequences-to-detect·축소기 측정표 (md 의 표)
 *   ./solution --nightly              야간 회귀 시나리오 (결함별 첫 실패 → 축소 → 판정)
 *   ./solution --replay <seed> <bug>  시드 하나 재현 + 축소 + 리포트 (bug: race|masked|w1call|late|restart)
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ================= 레지스터 맵 ================= */
#define NCH       2u
#define REG_CTRL    0x00u
#define REG_STATUS  0x04u
#define REG_INT_STS 0x08u
#define REG_INT_EN  0x0Cu
#define REG_LEN0    0x10u
#define REG_LEN1    0x14u
#define REG_CMD     0x18u
#define REG_IRQ     0x1Cu

#define STS_DONE(ch) (1u << (ch))
#define STS_EVT     (1u << 2)
#define STS_ERR     (1u << 3)
#define STS_MASK    0xFu
#define LEN_RESET   4u
#define LEN_LEGAL_MAX 8u     /* 제약 모드에서 합법인 LEN 상한 */
#define IDLE_LEGAL_MAX 16u
#define MAGIC_UNLOCK 0x5AA5C33Cu

/* DUT 결함 스위치 (비트 마스크, 조합 가능) */
#define BUG_NONE    0u
#define BUG_W1C_ALL (1u << 0)  /* W1C 가 쓴 비트와 무관하게 전부 지운다 — 쉬운 결함 */
#define BUG_RACE    (1u << 1)  /* EVT 만 W1C 하는 cycle 에 DONE1 완료가 겹치면 DONE1 이 set 안 된다 */
#define BUG_MASKED  (1u << 2)  /* 마스크된 채널의 완료 cycle 에 다른 채널이 BUSY(이번 cycle 시작 포함)면 latch 안 됨 */
#define BUG_RESTART (1u << 3)  /* BUSY 중 START 가 ERR 없이 재시작 — illegal 영역 */
#define BUG_MAGIC   (1u << 4)  /* CTRL == MAGIC_UNLOCK 이면 EVT 가 켜진다 — 값 하나짜리 코너 */
#define BUG_CLEARW  (1u << 5)  /* set 과 clear 동시면 clear 승 — 스펙 오독(모델에도 있으면 공통 원인) */
#define BUG_LATE    (1u << 6)  /* LEN==8 전송이 1 cycle 늦게 끝남 — 타이밍만 어긋남 */

/* 모델 결함 스위치 — "scoreboard mismatch 인데 범인이 모델" 인 경우를 만든다 */
#define MB_NONE       0u
#define MB_CLEAR_WINS (1u << 0)

/* ================= 자극(op) ================= */
enum { OP_WR, OP_RD, OP_EVT, OP_IDLE };

typedef struct {
    uint8_t  kind;
    uint8_t  off;   /* OP_WR/OP_RD 의 레지스터 오프셋 */
    uint32_t val;   /* OP_WR 데이터, OP_IDLE 의 cycle 수 */
} op_t;

#define WR(o, v)  ((op_t){OP_WR, (uint8_t)(o), (v)})
#define RD(o)     ((op_t){OP_RD, (uint8_t)(o), 0})
#define EVT()     ((op_t){OP_EVT, 0, 0})
#define IDLE(n)   ((op_t){OP_IDLE, 0, (n)})

#define MAX_SEQ   128
#define TRACE_N   8

static uint32_t rnd32(uint32_t *s) {     /* xorshift32 */
    uint32_t x = *s;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    *s = x;
    return x;
}
static uint32_t rnd_below(uint32_t *s, uint32_t n) { return rnd32(s) % n; }
static uint32_t seq_seed_of(uint32_t camp, uint32_t idx) {   /* 캠페인 시드 + 번호 → 시퀀스 시드 */
    uint32_t x = camp * 0x9E3779B1u ^ (idx + 1u) * 0x85EBCA6Bu;
    x ^= x >> 15;
    return x ? x : 1u;
}

/* ================= DUT (주어짐) ================= */
typedef struct {
    uint32_t ctrl, sts, en, len[NCH], cnt[NCH];
} dut_t;

static void dut_reset(dut_t *d) {
    memset(d, 0, sizeof *d);
    d->len[0] = d->len[1] = LEN_RESET;
}

static uint32_t dut_read(const dut_t *d, unsigned off) {
    switch (off) {
    case REG_CTRL:    return d->ctrl;
    case REG_STATUS:  return (d->cnt[1] ? 2u : 0u) | (d->cnt[0] ? 1u : 0u);
    case REG_INT_STS: return d->sts;
    case REG_INT_EN:  return d->en;
    case REG_LEN0:    return d->len[0];
    case REG_LEN1:    return d->len[1];
    case REG_IRQ:     return (d->sts & d->en) != 0;
    default:          return 0;   /* REG_CMD 는 WO */
    }
}

static void dut_cycle(dut_t *d, bool wr, unsigned off, uint32_t w, bool evt, unsigned bugs) {
    bool busy[NCH];
    uint32_t done = 0;
    for (unsigned c = 0; c < NCH; c++) {
        busy[c] = d->cnt[c] != 0;
        if (d->cnt[c] && --d->cnt[c] == 0) done |= STS_DONE(c);
    }
    bool w1c = wr && off == REG_INT_STS;
    uint32_t clr = w1c ? (w & STS_MASK) : 0;
    uint32_t set = done | (evt ? STS_EVT : 0);
    if (wr) {
        if (off == REG_CTRL) {
            if ((bugs & BUG_MAGIC) && w == MAGIC_UNLOCK) set |= STS_EVT;
            d->ctrl = w & 1u;
        } else if (off == REG_INT_EN) {
            d->en = w & STS_MASK;
        } else if (off == REG_LEN0 || off == REG_LEN1) {
            unsigned c = (off - REG_LEN0) / 4u;
            if (!busy[c]) d->len[c] = w & 0xFFu;
        } else if (off == REG_CMD) {
            for (unsigned c = 0; c < NCH; c++) {
                if (!(w & (1u << c))) continue;
                if (busy[c]) {
                    if (bugs & BUG_RESTART) d->cnt[c] = d->len[c];
                    else set |= STS_ERR;
                } else if (!(d->ctrl & 1u) || d->len[c] == 0) {
                    set |= STS_ERR;
                } else {
                    d->cnt[c] = d->len[c] + (((bugs & BUG_LATE) && d->len[c] == LEN_LEGAL_MAX) ? 1u : 0u);
                }
            }
        }
    }
    if (bugs & BUG_MASKED)
        for (unsigned c = 0; c < NCH; c++)
            if ((done & STS_DONE(c)) && !(d->en & STS_DONE(c)) && d->cnt[1u - c] != 0) set &= ~STS_DONE(c);
    if ((bugs & BUG_W1C_ALL) && w1c && clr) clr = STS_MASK;
    if ((bugs & BUG_RACE) && w1c && clr == STS_EVT) set &= ~STS_DONE(1);  /* 비트 1 set 경로가 비트 2 strobe 로 잘못 gate */
    if (bugs & BUG_CLEARW)              d->sts = (d->sts | set) & ~clr;
    else                                     d->sts = (d->sts & ~clr) | set;
}

static uint32_t dut_apply(dut_t *d, const op_t *o, unsigned bugs) {
    uint32_t rd = 0;
    switch (o->kind) {
    case OP_WR:   dut_cycle(d, true, o->off, o->val, false, bugs); break;
    case OP_RD:   rd = dut_read(d, o->off); dut_cycle(d, false, 0, 0, false, bugs); break;
    case OP_EVT:  dut_cycle(d, false, 0, 0, true, bugs); break;
    default:      for (uint32_t i = 0; i < o->val; i++) dut_cycle(d, false, 0, 0, false, bugs); break;
    }
    return rd;
}

/* ================= 골든 모델 (Part A) ================= */
typedef struct {
    uint64_t now;              /* 지금 실행할 cycle 번호 (1 부터) */
    uint64_t done_at[NCH];     /* 완료될 cycle, 0 = idle */
    uint32_t ctrl, sts, en, len[NCH];
} model_t;

static void model_reset(model_t *m) {
    memset(m, 0, sizeof *m);
    m->now = 1;
    m->len[0] = m->len[1] = LEN_RESET;
}

static uint32_t model_busy(const model_t *m) { return (m->done_at[1] ? 2u : 0u) | (m->done_at[0] ? 1u : 0u); }

static uint32_t model_read(const model_t *m, unsigned off) {
    switch (off) {
    case REG_CTRL:    return m->ctrl;
    case REG_STATUS:  return model_busy(m);
    case REG_INT_STS: return m->sts;
    case REG_INT_EN:  return m->en;
    case REG_LEN0:    return m->len[0];
    case REG_LEN1:    return m->len[1];
    case REG_IRQ:     return (m->sts & m->en) != 0;
    default:          return 0;
    }
}

/* TODO(A1): 1 cycle 진행. 순서: ① 이번 cycle 시작 시점의 BUSY 를 저장 ② now 에 끝나는 채널 → set
 * ③ 버스 쓰기 효과(CTRL/INT_EN/LEN/CMD, W1C 는 clr 마스크로) ④ STS 갱신: 스펙은 set 승,
 * mb & MB_CLEAR_WINS 면 일부러 clear 승 ⑤ now++ */
static void model_cycle(model_t *m, bool wr, unsigned off, uint32_t w, bool evt, unsigned mb) {
    (void)wr; (void)off; (void)w; (void)evt; (void)mb;
    m->now++;
}

static uint32_t model_apply(model_t *m, const op_t *o, unsigned mb) {
    uint32_t rd = 0;
    switch (o->kind) {
    case OP_WR:   model_cycle(m, true, o->off, o->val, false, mb); break;
    case OP_RD:   rd = model_read(m, o->off); model_cycle(m, false, 0, 0, false, mb); break;
    case OP_EVT:  model_cycle(m, false, 0, 0, true, mb); break;
    default:      for (uint32_t i = 0; i < o->val; i++) model_cycle(m, false, 0, 0, false, mb); break;
    }
    return rd;
}

/* ================= Generator (Part A) ================= */
enum { I_CTRL, I_EN, I_LEN, I_START, I_EVT, I_W1C, I_RD, I_IDLE, I_N };

typedef struct {
    const char *name;
    uint8_t w[I_N];
    uint8_t len_max;         /* LEN 은 1..len_max */
    uint8_t idle_max;
    uint8_t illegal_pct;     /* 이 비율만큼 제약 밖 op */
    uint8_t aware_pct;       /* 다음 cycle 완료를 모델로 보고 W1C 를 겨눈다 */
    bool    no_w1c_busy;     /* 과잉 제약: BUSY 중에는 W1C 안 함 */
} profile_t;

/*                                   CTRL EN LEN START EVT W1C RD IDLE */
static const profile_t P_UNIFORM = {"uniform",    {2, 2, 2, 2, 2, 2, 2, 2},   8, 8, 0, 0, false};
static const profile_t P_TUNED   = {"tuned",      {1, 1, 3, 6, 4, 8, 1, 2},   4, 2, 0, 0, false};
static const profile_t P_AWARE   = {"aware",      {1, 1, 3, 6, 4, 4, 1, 2},   4, 2, 0, 60, false};
static const profile_t P_OVER    = {"overconstr", {1, 1, 3, 6, 4, 8, 1, 2},   4, 2, 0, 0, true};
static const profile_t P_ILLEGAL = {"illegal15",  {2, 2, 2, 4, 2, 2, 2, 2},   8, 8, 15, 0, false};

/* 합법 op 판정: 제약 모드의 규칙을 한 곳에 — generator 가 어기면 안 되고, 축소기도 이걸로 걸러낸다 */
static bool op_legal(const model_t *m, const op_t *o) {
    uint32_t busy = model_busy(m), pend = m->sts & STS_MASK;
    switch (o->kind) {
    case OP_EVT:  return true;
    case OP_IDLE: return o->val >= 1 && o->val <= IDLE_LEGAL_MAX;
    case OP_RD:   return o->off <= REG_IRQ && (o->off & 3u) == 0 && o->off != REG_CMD;
    default: break;
    }
    switch (o->off) {
    case REG_CTRL:    return (o->val & ~1u) == 0;
    case REG_INT_EN:  return (o->val & ~STS_MASK) == 0;
    case REG_INT_STS: return o->val != 0 && (o->val & ~pend) == 0;          /* 제약: pending 인 비트만 clear */
    case REG_LEN0: case REG_LEN1: {
        unsigned c = (o->off - REG_LEN0) / 4u;
        return !(busy & (1u << c)) && o->val >= 1 && o->val <= LEN_LEGAL_MAX;
    }
    case REG_CMD:     /* 제약: EN=1, 대상 채널 idle, 한 번에 채널 하나 */
        return (m->ctrl & 1u) && (o->val == 1u || o->val == 2u) && !(busy & o->val);
    default:          return false;
    }
}

/* TODO(A2): 현재 모델 상태에서 다음 op 하나를 뽑는다 (모델은 읽기만).
 *  ① illegal_pct 확률로 gen_illegal  ② aware_pct: 다음 cycle(done_at==now)에 DONE1 이 켜지고 EVT 가 pending 이면 EVT 만 W1C 로 겨눈다
 *  ③ 불가능한 intent 는 가중치 0 (idle 채널 없음·EN=0 → START 0, pending 없음 → W1C 0, ...)
 *  ④ 가중 추첨 후 인자를 채운다: W1C 는 pending 의 비어 있지 않은 부분집합, LEN 은 1..len_max, START/LEN 은 idle 채널 중 하나 */
static void gen_illegal(uint32_t *s, const profile_t *p, op_t *o);
static void gen_next(const model_t *m, uint32_t *s, const profile_t *p, op_t *o) {
    (void)m; (void)s; (void)p; (void)gen_illegal;
    *o = IDLE(1);
}

/* 일부러 제약을 어기는 자극 — "illegal 도 스펙대로 ERR 로 끝나는가" (주어짐) */
static void gen_illegal(uint32_t *s, const profile_t *p, op_t *o) {
    (void)p;
    switch (rnd_below(s, 5)) {
    case 0:  *o = WR(REG_CMD, 1u + rnd_below(s, 3)); break;           /* BUSY/EN=0 무시하고 START, 양 채널 동시 */
    case 1:  *o = WR(REG_INT_STS, 1u + rnd_below(s, 15)); break;      /* pending 아닌 비트 W1C */
    case 2:  *o = WR(REG_LEN0 + 4u * rnd_below(s, 2), rnd_below(s, 256)); break;   /* LEN=0, >8, BUSY 중 */
    case 3:  *o = WR(REG_CTRL, rnd32(s)); break;                     /* reserved 비트에 쓰기 */
    default: *o = WR(REG_INT_EN, rnd32(s)); break;
    }
}

/* 시퀀스 생성: CTRL.EN=1 로 시작, 이후 모델 상태를 보며 n-1 개. DUT 는 쳐다보지 않는다 → 재현성 */
static void seq_generate(uint32_t seed, const profile_t *p, int n, op_t *out) {
    uint32_t s = seed ? seed : 1u;
    model_t m;
    model_reset(&m);
    out[0] = WR(REG_CTRL, 1);
    model_apply(&m, &out[0], MB_NONE);
    for (int i = 1; i < n; i++) {
        gen_next(&m, &s, p, &out[i]);
        model_apply(&m, &out[i], MB_NONE);
    }
}

static bool seq_legal(const op_t *seq, int n) {
    model_t m;
    model_reset(&m);
    for (int i = 0; i < n; i++) {
        if (!op_legal(&m, &seq[i])) return false;
        model_apply(&m, &seq[i], MB_NONE);
    }
    return true;
}

/* ================= Scoreboard (Part A) ================= */
enum { OBS_EVERY, OBS_READ, OBS_END };   /* 관찰 방식: 매 op 전체 비교 / 시퀀스의 RD 값만 / 끝에 한 번 */

static const uint8_t OBS_REGS[] = {REG_CTRL, REG_STATUS, REG_INT_STS, REG_INT_EN, REG_LEN0, REG_LEN1, REG_IRQ};
#define NOBS ((int)(sizeof OBS_REGS / sizeof OBS_REGS[0]))

static const char *reg_name(unsigned off) {
    switch (off) {
    case REG_CTRL:    return "CTRL";
    case REG_STATUS:  return "STATUS";
    case REG_INT_STS: return "INT_STS";
    case REG_INT_EN:  return "INT_EN";
    case REG_LEN0:    return "LEN0";
    case REG_LEN1:    return "LEN1";
    case REG_CMD:     return "CMD";
    case REG_IRQ:     return "IRQ";
    default:          return "?";
    }
}

typedef struct {
    bool     mismatch;
    uint32_t seed;
    int      step;            /* 첫 불일치가 드러난 op 번호 */
    uint64_t cycle;           /* 그 op 의 마지막 cycle 번호 */
    unsigned off;
    uint32_t exp, act;        /* exp = 모델, act = DUT */
    int      trace_first, trace_n;
    op_t     trace[TRACE_N];  /* 불일치 직전까지 마지막 op 들 */
} sb_report_t;

/* TODO(A3): seq 를 DUT 와 모델에 똑같이 넣고 비교한다. 첫 불일치에서 *멈추고* rep 를 채운다 (seed 는 호출자).
 *  OBS_EVERY: 매 op 뒤 OBS_REGS 순서로 전체 비교 / OBS_READ: OP_RD 의 반환값만 / OBS_END: 시퀀스 끝에서 전체 비교.
 *  trace 는 불일치 op 포함 마지막 TRACE_N 개를 복사. 반환값 = mismatch */
static bool sb_run(const op_t *seq, int n, unsigned bugs, unsigned mb, int mode, sb_report_t *rep) {
    (void)seq; (void)n; (void)bugs; (void)mb; (void)mode; (void)OBS_REGS;
    memset(rep, 0, sizeof *rep);
    rep->step = -1;
    return false;
}

/* seed 하나 = 시퀀스 생성 + scoreboard */
static bool run_seed(uint32_t seed, const profile_t *p, int n, unsigned bugs, unsigned mb, int mode, op_t *seq, sb_report_t *rep) {
    seq_generate(seed, p, n, seq);
    bool hit = sb_run(seq, n, bugs, mb, mode, rep);
    rep->seed = seed;
    return hit;
}

/* ================= Directed 테스트 (주어짐) — 사람이 쓰는 절차적 시퀀스 ================= */
#define DSEQ(...) { (op_t[]){__VA_ARGS__}, (int)(sizeof((op_t[]){__VA_ARGS__}) / sizeof(op_t)) }
typedef struct { const op_t *ops; int n; } dseq_t;

static const dseq_t DIRECTED[] = {
    /* 0: 채널 0 전송 → 폴링 → DONE 확인 → W1C */
    DSEQ(WR(REG_CTRL, 1), WR(REG_INT_EN, STS_DONE(0)), WR(REG_LEN0, 2), WR(REG_CMD, 1), IDLE(4), RD(REG_INT_STS), RD(REG_IRQ),
         WR(REG_INT_STS, STS_DONE(0)), RD(REG_INT_STS), RD(REG_IRQ)),
    /* 1: 채널 1 전송 */
    DSEQ(WR(REG_CTRL, 1), WR(REG_LEN1, 3), WR(REG_CMD, 2), IDLE(5), RD(REG_INT_STS), WR(REG_INT_STS, STS_DONE(1)), RD(REG_INT_STS)),
    /* 2: 외부 이벤트 → pending → W1C */
    DSEQ(WR(REG_CTRL, 1), EVT(), RD(REG_INT_STS), WR(REG_INT_STS, STS_EVT), RD(REG_INT_STS)),
    /* 3: 두 채널을 차례로 — DONE0 pending 인 채로 EVT, 하나씩 clear */
    DSEQ(WR(REG_CTRL, 1), WR(REG_CMD, 1), IDLE(6), EVT(), WR(REG_CMD, 2), IDLE(6), RD(REG_INT_STS),
         WR(REG_INT_STS, STS_DONE(0)), RD(REG_INT_STS), WR(REG_INT_STS, STS_DONE(1)), RD(REG_INT_STS), WR(REG_INT_STS, STS_EVT), RD(REG_INT_STS)),
    /* 4: 마스크된 완료 — pending 은 보이고 IRQ 는 안 뜸, unmask 하면 뜸 */
    DSEQ(WR(REG_CTRL, 1), WR(REG_INT_EN, 0), WR(REG_CMD, 1), IDLE(6), RD(REG_INT_STS), RD(REG_IRQ),
         WR(REG_INT_EN, STS_DONE(0)), RD(REG_IRQ), WR(REG_INT_STS, STS_DONE(0)), RD(REG_IRQ)),
    /* 5: EN=0 START → ERR, BUSY 중 START → ERR (spec 의 에러 경로) */
    DSEQ(WR(REG_CMD, 1), RD(REG_INT_STS), WR(REG_INT_STS, STS_ERR), WR(REG_CTRL, 1), WR(REG_CMD, 1), WR(REG_CMD, 1), RD(REG_INT_STS),
         IDLE(6), RD(REG_INT_STS), WR(REG_INT_STS, STS_ERR | STS_DONE(0)), RD(REG_INT_STS)),
    /* 6: LEN=0 → ERR, BUSY 중 LEN 쓰기 무시 */
    DSEQ(WR(REG_CTRL, 1), WR(REG_LEN0, 0), WR(REG_CMD, 1), RD(REG_INT_STS), WR(REG_INT_STS, STS_ERR), WR(REG_LEN0, 3), WR(REG_CMD, 1),
         WR(REG_LEN0, 7), RD(REG_LEN0), IDLE(4), RD(REG_INT_STS), RD(REG_STATUS)),
};
#define NDIRECTED ((int)(sizeof DIRECTED / sizeof DIRECTED[0]))

/* 전체 directed 를 돌려 실패한 시퀀스 번호를 반환, 없으면 -1 (관찰은 가장 강한 OBS_EVERY) */
static int directed_run(unsigned bugs, unsigned mb, sb_report_t *rep) {
    for (int i = 0; i < NDIRECTED; i++) {
        if (sb_run(DIRECTED[i].ops, DIRECTED[i].n, bugs, mb, OBS_EVERY, rep)) return i;
    }
    return -1;
}

/* ================= 캠페인 통계 (Part B) ================= */
#define SEQ_LEN 40

/* TODO(B1): 캠페인 시드 camp 로 시퀀스를 차례로 돌려(seq_seed_of(camp, i)), 처음 불일치가 난 시퀀스의 개수(1부터)를 반환.
 * budget 개까지 안 나오면 -1. */
static int seqs_to_detect(uint32_t camp, const profile_t *p, int seq_len, unsigned bugs, unsigned mb, int mode, int budget) {
    (void)camp; (void)p; (void)seq_len; (void)bugs; (void)mb; (void)mode; (void)budget;
    return -1;
}

typedef struct { int runs, detected, median, worst; double mean; } camp_stat_t;

static int cmp_int(const void *a, const void *b) { return *(const int *)a - *(const int *)b; }

static camp_stat_t campaign(const profile_t *p, int seq_len, unsigned bugs, unsigned mb, int mode, int runs, int budget) {
    camp_stat_t st = {runs, 0, 0, 0, 0.0};
    int v[256], k = 0;
    double sum = 0;
    if (runs > 256) runs = st.runs = 256;
    for (int c = 0; c < runs; c++) {
        int n = seqs_to_detect(0xC0DE0000u + (uint32_t)c, p, seq_len, bugs, mb, mode, budget);
        if (n > 0) { st.detected++; sum += n; v[k++] = n; }
    }
    if (k) {
        qsort(v, (size_t)k, sizeof v[0], cmp_int);
        st.mean = sum / k;
        st.median = v[k / 2];
        st.worst = v[k - 1];
    }
    return st;
}

/* ================= 축소 + 판정 (Part C) ================= */
typedef struct { unsigned off; uint32_t diff; } fail_sig_t;   /* 실패 시그니처 = 어느 레지스터의 어느 비트 */

static fail_sig_t sig_of(const sb_report_t *r) { return (fail_sig_t){r->off, r->exp ^ r->act}; }
static bool sig_eq(fail_sig_t a, fail_sig_t b) { return a.off == b.off && a.diff == b.diff; }

typedef struct {
    unsigned bugs, mb;
    int      mode;
    bool     require_legal;
    bool     match_sig;
    fail_sig_t    want;
    int      tests;     /* 후보를 몇 번 돌렸나 */
} shrink_ctx_t;

/* 축소 후보가 "같은 실패" 인가 — 합법성 → 불일치 → 시그니처 순 */
static bool still_fails(const op_t *cand, int n, shrink_ctx_t *c) {
    sb_report_t r;
    c->tests++;
    if (n <= 0) return false;
    if (c->require_legal && !seq_legal(cand, n)) return false;
    if (!sb_run(cand, n, c->bugs, c->mb, c->mode, &r)) return false;
    return !c->match_sig || sig_eq(sig_of(&r), c->want);
}

/* TODO(C1): ddmin (complement-only) — seq 를 제자리에서 줄이고 새 길이를 돌려준다.
 * gran=2 에서 시작. i 번째 조각 [i*n/gran, (i+1)*n/gran) 를 뺀 후보가 still_fails 면 받아들이고 gran=max(gran-1,2).
 * 한 바퀴 돌아 못 줄였으면 gran==n 일 때 끝(그때가 1-minimal), 아니면 gran=min(2*gran, n). */
static int ddmin(op_t *seq, int n, shrink_ctx_t *c) {
    (void)seq; (void)c;
    return n;
}

/* TODO(C1-b): ddmin 만 쓰면 "시간을 메우던 op"(타이밍 필러)를 못 치운다 — 빼는 순간 뒤 op 들의 cycle 이 밀린다.
 * 해법: ① ddmin ② 각 op 를 IDLE(1) 로 바꿔 봐서(타이밍 보존) 실패가 유지되면 교체 ③ 인접한 IDLE 합치기(합 ≤ IDLE_LEGAL_MAX)
 * ④ 길이 2~4 슬라이딩 창 제거 (의존 짝 op 제거) ⑤ 바뀐 게 있으면 ①로. 후보 검사는 still_fails. 새 길이를 반환. */
static int shrink_full(op_t *seq, int n, shrink_ctx_t *c) {
    return ddmin(seq, n, c);
}

/* 스펙 단정(assertion) — 모델과 무관하게 관찰값만으로 판정한다. 위반 없으면 0.
 * 인자: 직전 관찰 P, 직후 관찰 N, 사이에 들어간 op. obs 는 STATUS/INT_STS/INT_EN/IRQ 네 값. */
typedef struct { uint32_t status, sts, en, irq; } obs_t;
enum { SA_NONE, SA1_DONE_ON_FALL, SA2_W1C_CLEARS, SA3_STICKY, SA4_IRQ_LINE, SA5_ERR_ON_BAD_START, SA_N };
static const char *const SA_NAME[SA_N] = {"none", "SA1 done-on-busy-fall", "SA2 w1c-clears", "SA3 sticky", "SA4 irq-line", "SA5 err-on-bad-start"};

/* TODO(C2): 단정 다섯 개를 op 하나에 대해 검사하고 처음 위반한 번호를 돌려준다.
 *  fell = P.status & ~N.status (이번 op 사이 BUSY 가 내려간 채널) — fell 인 채널은 N.sts 의 DONE 이 1 이어야 한다 (SA1)
 *  W1C 쓴 비트 중 fell 로 다시 set 된 비트를 뺀 나머지는 N.sts 에서 0 (SA2)
 *  W1C 대상이 아니었던, P.sts 에서 1 인 비트는 N.sts 에서도 1 (SA3)
 *  N.irq == ((N.sts & N.en) != 0) (SA4)
 *  CMD 쓰기가 BUSY 채널을 건드렸으면 N.sts.ERR == 1 (SA5) */
static int spec_check(const obs_t *P, const op_t *o, const obs_t *N) {
    (void)P; (void)o; (void)N;
    return SA_NONE;
}

typedef struct { int step, sa; } spec_hit_t;

static obs_t obs_dut(const dut_t *d) {
    return (obs_t){dut_read(d, REG_STATUS), dut_read(d, REG_INT_STS), dut_read(d, REG_INT_EN), dut_read(d, REG_IRQ)};
}
static obs_t obs_model(const model_t *m) {
    return (obs_t){model_read(m, REG_STATUS), model_read(m, REG_INT_STS), model_read(m, REG_INT_EN), model_read(m, REG_IRQ)};
}

static spec_hit_t spec_scan_dut(const op_t *seq, int n, unsigned bugs) {
    dut_t d;
    dut_reset(&d);
    for (int i = 0; i < n; i++) {
        obs_t P = obs_dut(&d);
        dut_apply(&d, &seq[i], bugs);
        obs_t N = obs_dut(&d);
        int sa = spec_check(&P, &seq[i], &N);
        if (sa) return (spec_hit_t){i, sa};
    }
    return (spec_hit_t){-1, SA_NONE};
}

static spec_hit_t spec_scan_model(const op_t *seq, int n, unsigned mb) {
    model_t m;
    model_reset(&m);
    for (int i = 0; i < n; i++) {
        obs_t P = obs_model(&m);
        model_apply(&m, &seq[i], mb);
        obs_t N = obs_model(&m);
        int sa = spec_check(&P, &seq[i], &N);
        if (sa) return (spec_hit_t){i, sa};
    }
    return (spec_hit_t){-1, SA_NONE};
}

enum { CLS_DUT_BUG, CLS_MODEL_BUG, CLS_BOTH_VIOLATE, CLS_INCONCLUSIVE };
static const char *const CLS_NAME[] = {"DUT_BUG", "MODEL_BUG", "SPEC_MISREAD?", "INCONCLUSIVE"};

/* TODO(C3): 축소된 시퀀스를 DUT 쪽/모델 쪽 각각 spec_scan 해서 누가 스펙을 어겼는지로 분류한다.
 *  DUT 만 위반 → DUT_BUG, 모델만 위반 → MODEL_BUG, 둘 다 → BOTH_VIOLATE, 아무도 안 어김 → INCONCLUSIVE (스펙 단정으로 못 가림).
 *  *sa_out 에 위반한 단정 번호 (없으면 SA_NONE) */
static int classify(const op_t *seq, int n, unsigned bugs, unsigned mb, int *sa_out) {
    (void)seq; (void)n; (void)bugs; (void)mb; (void)spec_scan_dut; (void)spec_scan_model;
    *sa_out = SA_NONE;
    return CLS_INCONCLUSIVE;
}

static int op_fmt(char *b, size_t cap, const op_t *o) {
    switch (o->kind) {
    case OP_WR:  return snprintf(b, cap, "WR %s=0x%X", reg_name(o->off), (unsigned)o->val);
    case OP_RD:  return snprintf(b, cap, "RD %s", reg_name(o->off));
    case OP_EVT: return snprintf(b, cap, "EVT");
    default:     return snprintf(b, cap, "IDLE %u", (unsigned)o->val);
    }
}

/* TODO(C4): 디버그 엔지니어용 한 줄:
 *   FAIL sb seed=0x… step=… cycle=… reg=INT_STS off=0x008 exp=0x… act=0x… diff=0x… bits=[…] class=… sa=… | trace(last N): [k] WR …; … | shrunk=<len> ops
 * bits 는 diff 의 켜진 비트 번호를 낮은 것부터, 쉼표로. 반환값 = 쓴 길이. */
static int format_report(char *b, size_t cap, const sb_report_t *r, int cls, int sa, int shrunk) {
    (void)r; (void)cls; (void)sa; (void)shrunk; (void)CLS_NAME; (void)SA_NAME;
    return snprintf(b, cap, "TODO");
}

/* 한 건을 끝까지: 시드 재현 → 축소 → 판정 → 리포트 (주어짐) */
static int triage_one(uint32_t seed, const profile_t *p, unsigned bugs, unsigned mb, bool legal, char *out, size_t cap, op_t *min, int *min_n, int *cls) {
    op_t seq[MAX_SEQ];
    sb_report_t rep;
    *min_n = 0;
    *cls = CLS_INCONCLUSIVE;
    if (cap) out[0] = '\0';
    if (!run_seed(seed, p, SEQ_LEN, bugs, mb, OBS_EVERY, seq, &rep)) return -1;
    shrink_ctx_t c = {bugs, mb, OBS_EVERY, legal, true, sig_of(&rep), 0};
    memcpy(min, seq, sizeof(op_t) * (size_t)SEQ_LEN);
    *min_n = shrink_full(min, rep.step + 1, &c);     /* 첫 불일치 뒤의 op 는 어차피 필요 없다 */
    sb_report_t small;
    sb_run(min, *min_n, bugs, mb, OBS_EVERY, &small);
    small.seed = seed;
    int sa;
    *cls = classify(min, *min_n, bugs, mb, &sa);
    format_report(out, cap, &small, *cls, sa, *min_n);
    return c.tests;
}

/* 슬리피지 측정 (주어짐): 결함 둘을 넣은 DUT 에서 시드 want 개, 시그니처 안 보고 축소하면 몇 개가 다른 실패로 미끄러지나 */
static void slip_measure(int want, int *tried, int *slipped, int *kept) {
    unsigned two = BUG_RACE | BUG_MASKED;
    op_t seq[MAX_SEQ], cand[MAX_SEQ];
    *tried = *slipped = *kept = 0;
    for (uint32_t i = 0; i < 400 && *tried < want; i++) {
        uint32_t s = seq_seed_of(0xBEEF0000u, i);
        sb_report_t ro, sr;
        if (!run_seed(s, &P_TUNED, SEQ_LEN, two, MB_NONE, OBS_EVERY, seq, &ro)) continue;
        (*tried)++;
        fail_sig_t want_sig = sig_of(&ro);
        memcpy(cand, seq, sizeof(op_t) * (size_t)(ro.step + 1));
        shrink_ctx_t any = {two, MB_NONE, OBS_EVERY, true, false, want_sig, 0};
        int n_any = ddmin(cand, ro.step + 1, &any);
        sb_run(cand, n_any, two, MB_NONE, OBS_EVERY, &sr);
        if (!sig_eq(sig_of(&sr), want_sig)) (*slipped)++;
        memcpy(cand, seq, sizeof(op_t) * (size_t)(ro.step + 1));
        shrink_ctx_t keep = {two, MB_NONE, OBS_EVERY, true, true, want_sig, 0};
        int n_keep = ddmin(cand, ro.step + 1, &keep);
        sb_run(cand, n_keep, two, MB_NONE, OBS_EVERY, &sr);
        if (sig_eq(sig_of(&sr), want_sig)) (*kept)++;
    }
}

/* 캠페인에서 첫 실패 시드 찾기 (주어짐) */
static uint32_t first_fail_seed(uint32_t camp, const profile_t *p, unsigned bugs, unsigned mb, int budget, int *seq_index) {
    op_t seq[MAX_SEQ];
    sb_report_t r;
    for (int i = 0; i < budget; i++) {
        uint32_t s = seq_seed_of(camp, (uint32_t)i);
        if (run_seed(s, p, SEQ_LEN, bugs, mb, OBS_EVERY, seq, &r)) { *seq_index = i; return s; }
    }
    *seq_index = -1;
    return 0;
}

static void print_ops(const char *tag, const op_t *ops, int n) {
    printf("%s (%d ops):", tag, n);
    for (int i = 0; i < n; i++) {
        char b[48];
        op_fmt(b, sizeof b, &ops[i]);
        printf(" %s%s", b, i + 1 < n ? ";" : "");
    }
    printf("\n");
}

/* ================= 측정표 (CLI) ================= */
static const struct { const char *name; unsigned bugs; } BUG_TAB[] = {
    {"w1call", BUG_W1C_ALL}, {"race", BUG_RACE}, {"masked", BUG_MASKED},
    {"late", BUG_LATE}, {"restart", BUG_RESTART}, {"magic", BUG_MAGIC},
};
#define NBUGTAB ((int)(sizeof BUG_TAB / sizeof BUG_TAB[0]))

static void print_cell(camp_stat_t s) {
    if (s.detected == 0) { printf("%22s", "0 (missed all)"); return; }
    char b[48];
    snprintf(b, sizeof b, "%d/%d mean %.1f med %d max %d", s.detected, s.runs, s.mean, s.median, s.worst);
    printf("%32s", b);
}

typedef struct { const char *name; unsigned bugs, mb; const profile_t *p; bool legal; int want_cls; int want_sa; } nightly_row_t;

static const nightly_row_t NIGHTLY[] = {
    {"race (W1C of EVT vs DONE1)",     BUG_RACE,    MB_NONE,        &P_TUNED,   true,  CLS_DUT_BUG,      SA1_DONE_ON_FALL},
    {"masked (done masked, other busy)", BUG_MASKED, MB_NONE,       &P_TUNED,   true,  CLS_DUT_BUG,      SA1_DONE_ON_FALL},
    {"w1call (W1C clears everything)", BUG_W1C_ALL, MB_NONE,        &P_TUNED,   true,  CLS_DUT_BUG,      SA3_STICKY},
    {"late (LEN=8 finishes late)",     BUG_LATE,    MB_NONE,        &P_UNIFORM, true,  CLS_INCONCLUSIVE, SA_NONE},
    {"restart (illegal START)",        BUG_RESTART, MB_NONE,        &P_ILLEGAL, false, CLS_DUT_BUG,      SA5_ERR_ON_BAD_START},
    {"model bug (clear wins in model)", BUG_NONE,   MB_CLEAR_WINS,  &P_TUNED,   true,  CLS_MODEL_BUG,    SA1_DONE_ON_FALL},
};
#define NNIGHTLY ((int)(sizeof NIGHTLY / sizeof NIGHTLY[0]))

static void print_nightly(void) {
    printf("[C] nightly: 시드 2000개 안에서 첫 실패를 찾아 축소·판정 (camp=0xC0DE0000)\n");
    for (int i = 0; i < NNIGHTLY; i++) {
        const nightly_row_t *n = &NIGHTLY[i];
        int idx, mn_n, cls;
        op_t mn[MAX_SEQ];
        char rep[640];
        uint32_t seed = first_fail_seed(0xC0DE0000u, n->p, n->bugs, n->mb, 2000, &idx);
        if (idx < 0) { printf("%-34s no failure\n", n->name); continue; }
        int tests = triage_one(seed, n->p, n->bugs, n->mb, n->legal, rep, sizeof rep, mn, &mn_n, &cls);
        printf("%-34s seq#%-4d candidates=%-4d min=%d ops class=%s\n", n->name, idx, tests, mn_n, CLS_NAME[cls]);
        print_ops("  repro", mn, mn_n);
        printf("  %s\n", rep);
    }
}

static void print_shrink_table(void) {
    printf("[C] 축소기 비교: race, tuned, 첫 실패 시드 16개 (camp 0xC0DE0000~0F)\n");
    int tot_orig = 0, tot_dd = 0, tot_full = 0, tests_dd = 0, tests_full = 0, nm = 0, worst_dd = 0, worst_full = 0;
    for (uint32_t c = 0; c < 16; c++) {
        int idx;
        uint32_t seed = first_fail_seed(0xC0DE0000u + c, &P_TUNED, BUG_RACE, MB_NONE, 2000, &idx);
        if (idx < 0) continue;
        op_t seq[MAX_SEQ], a[MAX_SEQ], b[MAX_SEQ];
        sb_report_t r;
        run_seed(seed, &P_TUNED, SEQ_LEN, BUG_RACE, MB_NONE, OBS_EVERY, seq, &r);
        shrink_ctx_t cd = {BUG_RACE, MB_NONE, OBS_EVERY, true, true, sig_of(&r), 0};
        shrink_ctx_t cf = cd;
        memcpy(a, seq, sizeof a); memcpy(b, seq, sizeof b);
        int nd = ddmin(a, r.step + 1, &cd), nf = shrink_full(b, r.step + 1, &cf);
        tot_orig += r.step + 1; tot_dd += nd; tot_full += nf; tests_dd += cd.tests; tests_full += cf.tests; nm++;
        if (nd > worst_dd) worst_dd = nd;
        if (nf > worst_full) worst_full = nf;
    }
    int tr, sl, ke;
    slip_measure(20, &tr, &sl, &ke);
    printf("  슬리피지(race+masked 동시): 시드 %d개 중 시그니처 무시 ddmin 은 %d개가 다른 실패로 미끄러짐, 시그니처 유지 ddmin 은 %d/%d 유지\n", tr, sl, ke, tr);
    printf("  시드 %d개 평균: 첫 불일치까지 %.1f ops → ddmin %.1f ops (최장 %d, 후보 %.0f회) → shrink_full %.1f ops (최장 %d, 후보 %.0f회)\n",
           nm, tot_orig / (double)nm, tot_dd / (double)nm, worst_dd, tests_dd / (double)nm, tot_full / (double)nm, worst_full, tests_full / (double)nm);
}

static void print_table(void) {
    const profile_t *profs[] = {&P_UNIFORM, &P_TUNED, &P_AWARE, &P_OVER, &P_ILLEGAL};
    int nprof = (int)(sizeof profs / sizeof profs[0]);
    printf("[B] sequences-to-detect (OBS_EVERY, SEQ_LEN=%d, 32 campaign seeds, budget 2000 seq)\n", SEQ_LEN);
    for (int b = 0; b < NBUGTAB; b++) {
        printf("  %-8s", BUG_TAB[b].name);
        for (int i = 0; i < nprof; i++) {
            camp_stat_t s = campaign(profs[i], SEQ_LEN, BUG_TAB[b].bugs, MB_NONE, OBS_EVERY, 32, 2000);
            printf(" | %s: ", profs[i]->name);
            print_cell(s);
        }
        printf("\n");
    }
    printf("[B] 관찰 방식에 따른 차이 — bug=race, profile=tuned, 32 seeds, budget 2000\n");
    const char *mn[] = {"OBS_EVERY", "OBS_READ", "OBS_END"};
    for (int m = 0; m < 3; m++) {
        camp_stat_t s = campaign(&P_TUNED, SEQ_LEN, BUG_RACE, MB_NONE, m, 32, 2000);
        printf("  %-9s ", mn[m]);
        print_cell(s);
        printf("\n");
    }
    printf("[B] 시퀀스 길이 비교 — bug=race, profile=tuned, 32 seeds (ops-to-detect = 평균 시퀀스 × 길이)\n");
    int lens[] = {10, 20, 40, 80};
    for (int i = 0; i < 4; i++) {
        camp_stat_t s = campaign(&P_TUNED, lens[i], BUG_RACE, MB_NONE, OBS_EVERY, 32, 4000);
        printf("  len %-3d %d/%d mean %.1f seq → mean %.0f ops\n", lens[i], s.detected, s.runs, s.mean, s.mean * lens[i]);
    }
    printf("[B] 공통 원인 (DUT, 모델 둘 다 clear-wins, 시퀀스 %d개):\n", 2000);
    camp_stat_t sh = campaign(&P_TUNED, SEQ_LEN, BUG_CLEARW, MB_CLEAR_WINS, OBS_EVERY, 32, 2000);
    printf("  shared misread: "); print_cell(sh); printf("\n");
}

/* ================= 채점 하네스 ================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

/* 간단한 표: 상태 일치 검사용 (act/exp 모델 전용) */
static bool m_expect(const model_t *m, uint32_t status, uint32_t sts, uint32_t irq) {
    return model_read(m, REG_STATUS) == status && model_read(m, REG_INT_STS) == sts && model_read(m, REG_IRQ) == irq;
}

static void test_model(void) {
    model_t m;
    model_reset(&m);
    CHECK(m_expect(&m, 0, 0, 0) && model_read(&m, REG_LEN0) == 4);
    /* cycle 1: EN=1. cycle 2: LEN0=2. cycle 3: START0 → done_at=5. cycle 4: IDLE. */
    op_t s1[] = {WR(REG_CTRL, 1), WR(REG_LEN0, 2), WR(REG_CMD, 1), IDLE(1)};
    for (unsigned i = 0; i < 4; i++) model_apply(&m, &s1[i], MB_NONE);
    CHECK(m_expect(&m, 1, 0, 0));
    /* cycle 5: done 발생 */
    op_t idle = IDLE(1);
    model_apply(&m, &idle, MB_NONE);
    CHECK(m_expect(&m, 0, STS_DONE(0), 0));   /* INT_EN=0 이라 IRQ 는 0 */
    /* W1C 와 완료가 같은 cycle — 스펙은 set 승 */
    model_reset(&m);
    op_t s2[] = {WR(REG_CTRL, 1), WR(REG_LEN0, 2), WR(REG_CMD, 1), IDLE(2), WR(REG_CMD, 1), RD(REG_INT_STS), WR(REG_INT_STS, STS_DONE(0))};
    for (unsigned i = 0; i < 7; i++) model_apply(&m, &s2[i], MB_NONE);
    CHECK(m_expect(&m, 0, STS_DONE(0), 0));
    /* 같은 시퀀스, clear-wins 모델이면 지워진다 — 차이를 만드는 곳이 정확히 여기 */
    model_reset(&m);
    for (unsigned i = 0; i < 7; i++) model_apply(&m, &s2[i], MB_CLEAR_WINS);
    CHECK(m_expect(&m, 0, 0, 0));
    /* 에러 경로: EN=0 START, BUSY 중 START, LEN=0 */
    model_reset(&m);
    op_t s3[] = {WR(REG_CMD, 1)};
    model_apply(&m, &s3[0], MB_NONE);
    CHECK(m_expect(&m, 0, STS_ERR, 0));
    model_reset(&m);
    op_t s4[] = {WR(REG_CTRL, 1), WR(REG_CMD, 1), WR(REG_CMD, 1)};
    for (unsigned i = 0; i < 3; i++) model_apply(&m, &s4[i], MB_NONE);
    CHECK(m_expect(&m, 1, STS_ERR, 0));
    /* 마스크/IRQ, LEN 쓰기 무시, reserved 비트 */
    model_reset(&m);
    op_t s5[] = {WR(REG_INT_EN, 0xFFFFFFFFu), EVT(), WR(REG_CTRL, 0xFFFFFFFFu), WR(REG_CMD, 1), WR(REG_LEN0, 7)};
    for (unsigned i = 0; i < 5; i++) model_apply(&m, &s5[i], MB_NONE);
    CHECK(model_read(&m, REG_INT_EN) == 0xF && model_read(&m, REG_CTRL) == 1 && m_expect(&m, 1, STS_EVT, 1) && model_read(&m, REG_LEN0) == 4);
}

static void test_part_a(void) {
    test_model();
    /* generator: 결정성 */
    op_t a[MAX_SEQ], b[MAX_SEQ];
    seq_generate(0xABCD1234u, &P_UNIFORM, SEQ_LEN, a);
    seq_generate(0xABCD1234u, &P_UNIFORM, SEQ_LEN, b);
    CHECK(memcmp(a, b, sizeof(op_t) * SEQ_LEN) == 0);
    seq_generate(0xABCD1235u, &P_UNIFORM, SEQ_LEN, b);
    CHECK(memcmp(a, b, sizeof(op_t) * SEQ_LEN) != 0);
    /* 합법성: 제약 프로파일은 200 시퀀스 모두 합법 */
    int illegal = 0, kinds = 0, iw[I_N] = {0};
    for (uint32_t s = 1; s <= 200; s++) {
        seq_generate(seq_seed_of(7, s), &P_UNIFORM, SEQ_LEN, a);
        if (!seq_legal(a, SEQ_LEN)) illegal++;
        seq_generate(seq_seed_of(8, s), &P_TUNED, SEQ_LEN, a);
        if (!seq_legal(a, SEQ_LEN)) illegal++;
        for (int i = 0; i < SEQ_LEN; i++) {
            const op_t *o = &a[i];
            int k = o->kind == OP_EVT ? I_EVT : o->kind == OP_IDLE ? I_IDLE : o->kind == OP_RD ? I_RD :
                    o->off == REG_CTRL ? I_CTRL : o->off == REG_INT_EN ? I_EN : o->off == REG_INT_STS ? I_W1C :
                    o->off == REG_CMD ? I_START : I_LEN;
            iw[k]++; kinds++;
        }
    }
    CHECK(illegal == 0);
    /* 분포: tuned 는 RD(가중 1) 가 W1C(가중 8) 보다 적어야 한다. W1C 는 pending 이 있을 때만 후보이므로 비율은 가중치 그대로는 아니다 */
    CHECK(iw[I_START] > iw[I_RD] * 3 && iw[I_W1C] > iw[I_RD] * 3 && kinds == 200 * SEQ_LEN);
    /* illegal 프로파일은 합법이 아닌 시퀀스를 만들어야 한다 */
    int legal_count = 0;
    for (uint32_t s = 1; s <= 200; s++) {
        seq_generate(seq_seed_of(9, s), &P_ILLEGAL, SEQ_LEN, a);
        if (seq_legal(a, SEQ_LEN)) legal_count++;
    }
    CHECK(legal_count < 20);

    /* 정상 DUT: 200 시퀀스 mismatch 0 (세 프로파일) */
    sb_report_t r;
    int bad = 0;
    for (uint32_t s = 1; s <= 200; s++) {
        if (run_seed(seq_seed_of(1, s), &P_UNIFORM, SEQ_LEN, BUG_NONE, MB_NONE, OBS_EVERY, a, &r)) bad++;
        if (run_seed(seq_seed_of(2, s), &P_TUNED, SEQ_LEN, BUG_NONE, MB_NONE, OBS_EVERY, a, &r)) bad++;
        if (run_seed(seq_seed_of(3, s), &P_ILLEGAL, SEQ_LEN, BUG_NONE, MB_NONE, OBS_EVERY, a, &r)) bad++;
    }
    CHECK(bad == 0);

    /* 쉬운 결함은 첫 불일치 리포트가 정확해야 한다: W1C 가 쓴 비트만 지워야 하는데 전부 지움 */
    op_t s[] = {WR(REG_CTRL, 1), EVT(), WR(REG_CMD, 1), IDLE(4), WR(REG_INT_STS, STS_EVT)};
    CHECK(sb_run(s, 5, BUG_W1C_ALL, MB_NONE, OBS_EVERY, &r) && r.step == 4 && r.off == REG_INT_STS && r.exp == STS_DONE(0) && r.act == 0);
    CHECK(r.trace_n == 5 && r.trace_first == 0 && r.trace[4].kind == OP_WR && r.trace[4].val == STS_EVT);
    CHECK(r.cycle == 8);  /* CTRL·EVT·CMD 3 cycle + IDLE 4 + W1C 1 = 8 번째 cycle */
    /* OBS_READ 는 RD 없이는 못 본다 */
    CHECK(!sb_run(s, 5, BUG_W1C_ALL, MB_NONE, OBS_READ, &r));
    /* OBS_END 는 시퀀스 끝 상태만 본다 — 이 경우는 끝이 망가져서 보임 */
    CHECK(sb_run(s, 5, BUG_W1C_ALL, MB_NONE, OBS_END, &r) && r.step == 4);
}

static void test_part_b(void) {
    sb_report_t r;
    /* directed: 정상 통과, 쉬운 결함은 잡고, 좁은 결함은 놓친다 */
    CHECK(directed_run(BUG_NONE, MB_NONE, &r) == -1);
    CHECK(directed_run(BUG_W1C_ALL, MB_NONE, &r) >= 0);
    CHECK(directed_run(BUG_RACE, MB_NONE, &r) == -1);
    CHECK(directed_run(BUG_MASKED, MB_NONE, &r) == -1);
    CHECK(directed_run(BUG_LATE, MB_NONE, &r) == -1);
    /* directed 5번이 BUSY 중 START 를 다루므로 RESTART 는 directed 가 잡는다 */
    CHECK(directed_run(BUG_RESTART, MB_NONE, &r) == 5);
    /* constrained-random 은 RACE 를 찾는다 */
    camp_stat_t u = campaign(&P_UNIFORM, SEQ_LEN, BUG_RACE, MB_NONE, OBS_EVERY, 16, 4000);
    camp_stat_t t = campaign(&P_TUNED, SEQ_LEN, BUG_RACE, MB_NONE, OBS_EVERY, 16, 4000);
    camp_stat_t a = campaign(&P_AWARE, SEQ_LEN, BUG_RACE, MB_NONE, OBS_EVERY, 16, 4000);
    camp_stat_t ov = campaign(&P_OVER, SEQ_LEN, BUG_RACE, MB_NONE, OBS_EVERY, 16, 4000);
    CHECK(u.detected == 16 && t.detected == 16 && a.detected == 16);
    CHECK(t.mean < u.mean && a.mean < t.mean);    /* 가중치 → 모델 인식, 순서대로 빨라진다 */
    CHECK(ov.detected == 0);                       /* 과잉 제약은 아예 못 찾는다 */
    camp_stat_t m1 = campaign(&P_TUNED, SEQ_LEN, BUG_MASKED, MB_NONE, OBS_EVERY, 16, 4000);
    CHECK(m1.detected == 16);
    /* illegal 없이는 RESTART 를 못 찾는다 (BUSY 중 START 가 제약에 막혀서) */
    camp_stat_t r0 = campaign(&P_TUNED, SEQ_LEN, BUG_RESTART, MB_NONE, OBS_EVERY, 16, 2000);
    camp_stat_t r1 = campaign(&P_ILLEGAL, SEQ_LEN, BUG_RESTART, MB_NONE, OBS_EVERY, 16, 2000);
    CHECK(r0.detected == 0 && r1.detected == 16);
    /* 한계 1: 32비트 값 하나짜리 코너는 illegal 랜덤으로도 안 걸린다 */
    camp_stat_t mg = campaign(&P_ILLEGAL, SEQ_LEN, BUG_MAGIC, MB_NONE, OBS_EVERY, 16, 2000);
    CHECK(mg.detected == 0);
    /* 한계 2: 모델과 DUT 가 같은 오해 → scoreboard 영원히 일치 */
    camp_stat_t sh = campaign(&P_AWARE, SEQ_LEN, BUG_CLEARW, MB_CLEAR_WINS, OBS_EVERY, 16, 2000);
    CHECK(sh.detected == 0);
    /* 한계 3: 관찰을 줄이면 가려진다 — OBS_END 평균 시퀀스 수가 OBS_EVERY 보다 많다 (W1C 가 오류를 지움) */
    camp_stat_t e0 = campaign(&P_TUNED, SEQ_LEN, BUG_RACE, MB_NONE, OBS_EVERY, 16, 2000);
    camp_stat_t e2 = campaign(&P_TUNED, SEQ_LEN, BUG_RACE, MB_NONE, OBS_END, 16, 2000);
    CHECK(e2.detected > 0 && e2.mean > e0.mean);
}

static void test_part_c(void) {
    char rep1[640] = "", rep2[640] = "";
    op_t mn[MAX_SEQ], seq[MAX_SEQ];
    int mn_n, cls;
    sb_report_t r0, r1;
    /* 첫 실패 시드 찾기 */
    uint32_t seed = 0;
    for (uint32_t i = 0; i < 2000 && !seed; i++) {
        uint32_t s = seq_seed_of(0xC0DE0000u, i);
        if (run_seed(s, &P_TUNED, SEQ_LEN, BUG_RACE, MB_NONE, OBS_EVERY, seq, &r0)) seed = s;
    }
    CHECK(seed != 0);
    /* 시드 재현: 두 번 돌리면 같은 step, 같은 리포트 */
    run_seed(seed, &P_TUNED, SEQ_LEN, BUG_RACE, MB_NONE, OBS_EVERY, seq, &r0);
    run_seed(seed, &P_TUNED, SEQ_LEN, BUG_RACE, MB_NONE, OBS_EVERY, seq, &r1);
    CHECK(r0.mismatch && r1.mismatch && r0.step == r1.step && r0.cycle == r1.cycle && r0.exp == r1.exp && r0.act == r1.act);
    int t1 = triage_one(seed, &P_TUNED, BUG_RACE, MB_NONE, true, rep1, sizeof rep1, mn, &mn_n, &cls);
    int t2 = triage_one(seed, &P_TUNED, BUG_RACE, MB_NONE, true, rep2, sizeof rep2, mn, &mn_n, &cls);
    CHECK(t1 > 0 && t1 == t2 && strcmp(rep1, rep2) == 0);
    CHECK(cls == CLS_DUT_BUG);
    /* 축소 결과 불변 조건: 합법, 같은 시그니처로 실패, 1-minimal, 충분히 짧음 */
    CHECK(seq_legal(mn, mn_n));
    sb_report_t rm;
    CHECK(sb_run(mn, mn_n, BUG_RACE, MB_NONE, OBS_EVERY, &rm) && sig_eq(sig_of(&rm), sig_of(&r0)));
    shrink_ctx_t c = {BUG_RACE, MB_NONE, OBS_EVERY, true, true, sig_of(&r0), 0};
    bool one_min = true;
    for (int i = 0; i < mn_n; i++) {
        op_t cand[MAX_SEQ];
        int m = 0;
        for (int k = 0; k < mn_n; k++) if (k != i) cand[m++] = mn[k];
        if (still_fails(cand, m, &c)) one_min = false;
    }
    CHECK(one_min);
    CHECK(mn_n <= 7 && mn_n >= 4 && mn_n < r0.step + 1);
    /* 축소된 시퀀스의 마지막 op 는 W1C, 불일치는 INT_STS 의 DONE */
    CHECK(mn_n > 0 && mn[mn_n - 1].kind == OP_WR && mn[mn_n - 1].off == REG_INT_STS);
    CHECK(rm.off == REG_INT_STS && (rm.exp ^ rm.act) != 0 && ((rm.exp ^ rm.act) & ~3u) == 0);

    /* 야간 회귀 표: 결함별로 첫 실패를 찾아 축소·판정 — class 와 단정이 기대대로 */
    for (int i = 0; i < NNIGHTLY; i++) {
        const nightly_row_t *n = &NIGHTLY[i];
        int idx, nn, klass, sa2 = -1;
        op_t mm[MAX_SEQ];
        char out[640] = "";
        uint32_t s = first_fail_seed(0xC0DE0000u, n->p, n->bugs, n->mb, 2000, &idx);
        CHECK(idx >= 0);
        int t = triage_one(s, n->p, n->bugs, n->mb, n->legal, out, sizeof out, mm, &nn, &klass);
        classify(mm, nn, n->bugs, n->mb, &sa2);
        CHECK(t > 0 && nn <= 10 && klass == n->want_cls && sa2 == n->want_sa);
    }
    /* 슬리피지: 결함이 둘이면 시그니처 없이 줄일 때 다른 결함으로 미끄러지는 시드가 있다 */
    int slipped = 0, kept = 0, tried = 0;
    slip_measure(20, &tried, &slipped, &kept);
    CHECK(tried == 20 && kept == 20);
    CHECK(slipped > 0);

    /* 모델 버그 vs DUT 버그 분리 */
    int sa;
    op_t q[] = {WR(REG_CTRL, 1), WR(REG_LEN0, 2), WR(REG_CMD, 1), IDLE(2), WR(REG_CMD, 1), RD(REG_INT_STS), WR(REG_INT_STS, STS_DONE(0))};
    CHECK(sb_run(q, 7, BUG_NONE, MB_CLEAR_WINS, OBS_EVERY, &r0) && r0.off == REG_INT_STS);
    CHECK(classify(q, 7, BUG_NONE, MB_CLEAR_WINS, &sa) == CLS_MODEL_BUG && sa == SA1_DONE_ON_FALL);
    op_t qr[] = {WR(REG_CTRL, 1), EVT(), WR(REG_LEN1, 2), WR(REG_CMD, 2), IDLE(1), WR(REG_INT_STS, STS_EVT)};
    CHECK(sb_run(qr, 6, BUG_RACE, MB_NONE, OBS_EVERY, &r0) && r0.step == 5 && r0.off == REG_INT_STS && r0.exp == STS_DONE(1) && r0.act == 0);
    CHECK(classify(qr, 6, BUG_RACE, MB_NONE, &sa) == CLS_DUT_BUG && sa == SA1_DONE_ON_FALL);
    /* 타이밍만 어긋난 결함: 단정이 안 잡으면 INCONCLUSIVE — 정직하게 "스펙 담당자에게" */
    op_t q2[] = {WR(REG_CTRL, 1), WR(REG_LEN0, 8), WR(REG_CMD, 1), IDLE(8)};
    CHECK(sb_run(q2, 4, BUG_LATE, MB_NONE, OBS_EVERY, &r0) && r0.off == REG_STATUS);
    CHECK(classify(q2, 4, BUG_LATE, MB_NONE, &sa) == CLS_INCONCLUSIVE && sa == SA_NONE);
    /* 공통 원인: scoreboard 는 일치하지만 단정이 둘 다 어겼다고 말한다 */
    CHECK(!sb_run(q, 7, BUG_CLEARW, MB_CLEAR_WINS, OBS_EVERY, &r0));
    CHECK(classify(q, 7, BUG_CLEARW, MB_CLEAR_WINS, &sa) == CLS_BOTH_VIOLATE);
    /* 리포트 형식 */
    CHECK(strstr(rep1, "FAIL sb seed=0x") == rep1);
    CHECK(strstr(rep1, "reg=INT_STS off=0x008") != NULL && strstr(rep1, "class=DUT_BUG") != NULL);
    CHECK(strstr(rep1, "sa=SA1 done-on-busy-fall") != NULL && strstr(rep1, "trace(last") != NULL);
    char b[256];
    sb_report_t rr;
    memset(&rr, 0, sizeof rr);
    rr.seed = 0x1234u; rr.step = 5; rr.cycle = 9; rr.off = REG_INT_STS; rr.exp = 0x6; rr.act = 0x4; rr.trace_n = 1; rr.trace_first = 5;
    rr.trace[0] = WR(REG_INT_STS, 4);
    format_report(b, sizeof b, &rr, CLS_DUT_BUG, SA1_DONE_ON_FALL, 3);
    CHECK(strcmp(b, "FAIL sb seed=0x00001234 step=5 cycle=9 reg=INT_STS off=0x008 exp=0x00000006 act=0x00000004 diff=0x00000002 bits=[1] class=DUT_BUG sa=SA1 done-on-busy-fall | trace(last 1): [5] WR INT_STS=0x4 | shrunk=3 ops") == 0);
}

int main(int argc, char **argv) {
    if (argc >= 2 && strcmp(argv[1], "--table") == 0) {
        print_table();
        print_shrink_table();
        return 0;
    }
    if (argc >= 2 && strcmp(argv[1], "--nightly") == 0) {
        print_nightly();
        return 0;
    }
    if (argc >= 4 && strcmp(argv[1], "--replay") == 0) {
        uint32_t seed = (uint32_t)strtoul(argv[2], NULL, 0);
        unsigned bugs = BUG_NONE;
        for (int b = 0; b < NBUGTAB; b++) if (strcmp(BUG_TAB[b].name, argv[3]) == 0) bugs = BUG_TAB[b].bugs;
        char rep[640];
        op_t mn[MAX_SEQ];
        int mn_n, cls;
        int tests = triage_one(seed, &P_TUNED, bugs, MB_NONE, true, rep, sizeof rep, mn, &mn_n, &cls);
        if (tests < 0) { printf("no mismatch: seed=0x%08X bug=%s\n", (unsigned)seed, argv[3]); return 1; }
        print_ops("minimal repro", mn, mn_n);
        printf("%s\n(ddmin candidates evaluated: %d)\n", rep, tests);
        return 0;
    }
    test_part_a();
    test_part_b();
    test_part_c();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
