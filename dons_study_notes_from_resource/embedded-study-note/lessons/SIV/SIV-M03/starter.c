/* SIV-M03 인터럽트 지연·유실 검출
 *
 * 타이머 IP(TMR0)가 주기 P 마다 이벤트를 만들고, 이벤트마다 {seq(16-bit), trigger 타임스탬프(32-bit)}
 * 레코드를 깊이 4 의 HW FIFO 에 넣으며 IRQ_RAW.TICK(W1C, sticky)을 세운다. CPU 모델은 스레드 코드의
 * 마스크 구간(critical section)에 막혀 늦게 ISR 에 진입하고, ISR 은 FIFO 를 비우며 레코드마다
 * (seq, trig_ts, entry_ts, pop_ts)를 로그에 남긴다. 이 로그만으로
 *   Part A  latency 히스토그램 (wrap-safe 차이, 분위수)
 *   Part B  seq 로 유실·중복·순서 뒤바뀜 검출 (16-bit wrap, 64-bit 슬라이딩 윈도)
 *   Part C  결함 주입 캠페인 판정 + 늦은 clear 로 인터럽트가 합쳐지는(coalesce) 현상 재현
 * 을 한다. 시간은 전부 시뮬레이션 ns (호스트 시계를 안 씀) → 매번 같은 결과.
 * (SIV-B02 의 mock MMIO, SIV-B11 의 W1C 인터럽트 모델을 이 IP 에 필요한 만큼 축약해 넣었다.)
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ================= 공통 유틸 (주어진 코드) ================= */
static uint32_t xs32(uint32_t *s) {                 /* xorshift32, 고정 seed → 결정적 */
    uint32_t x = *s;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return *s = x;
}
static uint32_t urange(uint32_t *s, uint32_t lo, uint32_t hi) { return lo + xs32(s) % (hi - lo + 1u); }
/* unsigned wrap 차이를 부호 있는 값으로 — (int32_t)u 같은 구현 정의 변환(C11 6.3.1.3p3) 없이 */
static int32_t s32_of(uint32_t u) { return u <= 0x7FFFFFFFu ? (int32_t)u : -(int32_t)(~u) - 1; }
static int32_t s16_of(uint16_t u) { return u < 0x8000u ? (int32_t)u : (int32_t)u - 0x10000; }

/* ================= TMR0 레지스터 맵 + HW 모델 (주어진 코드) ================= */
#define IP_NAME    "TMR0"
#define IRQ_RAW    0x00u   /* W1C: bit0 TICK (push 마다 1, sticky) · bit1 OVF (FIFO full 에서 push → 버림) */
#define IRQ_EN     0x04u   /* RW                                                                    */
#define FIFO_LVL   0x08u   /* RO : FIFO 에 남은 레코드 수                                            */
#define EVT_SEQ    0x0Cu   /* RO : head 레코드의 seq (peek)                                           */
#define EVT_TS     0x10u   /* RC : head 레코드의 trigger 타임스탬프 — 읽으면 pop                       */
#define OVF_CNT    0x14u   /* RO : 버려진 레코드 수                                                   */
#define TICK       (1u << 0)
#define OVF        (1u << 1)
#define FIFO_DEPTH 4u

typedef struct {
    bool     drop_at_wrap;     /* seq 가 0x0000 이 되는 이벤트의 push 가 조용히 사라짐 (OVF 도 안 섬) */
    uint32_t pop_stall_every;  /* N 번째 pop 마다 read 포인터가 안 움직임 (0 = 정상)                 */
    bool     swap_backlog;     /* 레코드가 2개 이상이면 head 다음 것을 먼저 내줌 (포인터 LSB 오류)    */
    bool     ts_at_pop;        /* EVT_TS 가 trigger 시각이 아니라 읽는 순간의 시각                    */
} fault_t;

typedef struct { uint16_t seq; uint32_t ts; } evrec_t;

static struct {
    uint64_t now;              /* 시뮬레이션 시각 (ns)                         */
    uint32_t ts0;              /* now = 0 일 때 free-running 카운터 값          */
    uint32_t raw, en, ovf_cnt, npop;
    uint16_t seq_ctr;
    evrec_t  q[FIFO_DEPTH];
    unsigned head, cnt, nacc;
    fault_t  f;
} H;

static uint32_t ts_now(void) { return H.ts0 + (uint32_t)H.now; }   /* 32-bit, 1 ns/LSB, wrap */
static bool irq_line(void) { return (H.raw & H.en) != 0; }

static void hw_tick(void) {                         /* 타이머 이벤트 1개 */
    uint16_t s = H.seq_ctr++;
    if (H.f.drop_at_wrap && s == 0) return;         /* 결함: 흔적 없이 유실 */
    if (H.cnt == FIFO_DEPTH) { H.ovf_cnt++; H.raw |= OVF; return; }
    H.q[(H.head + H.cnt) % FIFO_DEPTH] = (evrec_t){s, ts_now()};
    H.cnt++;
    H.raw |= TICK;
}
static unsigned head_idx(void) {
    return (H.f.swap_backlog && H.cnt >= 2) ? (H.head + 1) % FIFO_DEPTH : H.head;
}
static uint32_t reg_read32(uint32_t off) {
    H.nacc++;
    switch (off) {
    case IRQ_RAW:  return H.raw;
    case IRQ_EN:   return H.en;
    case FIFO_LVL: return H.cnt;
    case EVT_SEQ:  return H.cnt ? H.q[head_idx()].seq : 0;
    case EVT_TS: {
        if (!H.cnt) return 0;
        unsigned i = head_idx();
        uint32_t ts = H.f.ts_at_pop ? ts_now() : H.q[i].ts;
        bool stall = H.f.pop_stall_every && ++H.npop % H.f.pop_stall_every == 0;
        if (!stall) {
            if (i != H.head) H.q[i] = H.q[H.head];
            H.head = (H.head + 1) % FIFO_DEPTH;
            H.cnt--;
        }
        return ts;
    }
    case OVF_CNT:  return H.ovf_cnt;
    default:       return 0xDEADBEEFu;
    }
}
static void reg_write32(uint32_t off, uint32_t v) {
    H.nacc++;
    switch (off) {
    case IRQ_RAW: H.raw &= ~(v & (TICK | OVF)); break;
    case IRQ_EN:  H.en = v & (TICK | OVF); break;
    default:      break;
    }
}

/* ================= 시나리오 · CPU/ISR 이벤트 시뮬레이션 (주어진 코드) ================= */
enum { CLEAR_FIRST, CLEAR_LAST };
static const char *const POLICY_NAME[] = {"clear-first", "clear-last"};

typedef struct {
    uint32_t period_ns, n_ticks, first_ns, ts0;
    uint16_t seq0;
    uint32_t seed, mask_min_ns, mask_max_ns;        /* 주기마다 critical section 1개 (길이 균등분포) */
    uint32_t long_mask_tick, long_mask_ns;          /* long_mask_ns > 0: 그 tick 직후 긴 마스크      */
    int      policy;                                /* ISR 이 TICK 을 언제 clear 하나               */
    uint32_t work_ns, budget_ns;                    /* ISR 본문 시간 · latency 예산                  */
} scen_t;

#define T_ENTRY 120u      /* 예외 진입: 12 cycle @100 MHz (Cortex-M4, zero-wait 메모리 기준) */
#define T_TAIL   60u      /* tail-chaining: 6 cycle                                          */
#define T_ACC    20u      /* 주변장치 레지스터 접근 1회                                       */
#define MAXT   10000u
#define MAXREC (MAXT + 64u)

typedef struct { uint16_t seq; uint32_t trig_ts, entry_ts, pop_ts, entry; } rec_t;

typedef struct {
    rec_t    rec[MAXREC];
    uint32_t nrec, entries, empty_entries, multi_entries;
    uint32_t strand_exits, strand_entry;            /* ISR 종료 시 LVL>0 인데 TICK=0 인 횟수 · 첫 entry */
    uint16_t strand_seq;
    uint32_t ovf_cnt, end_lvl;
    unsigned nacc;
} run_t;

static struct { uint64_t s, e; } MW[MAXT + 1];
static unsigned NMW, MWI;

static void gen_masks(const scen_t *sc) {           /* PRNG 소비량이 시나리오와 무관 → 비교 가능 */
    uint32_t r = sc->seed;
    uint64_t last_end = 0;
    NMW = MWI = 0;
    for (uint32_t k = 0; k < sc->n_ticks; k++) {
        uint64_t base = sc->first_ns + (uint64_t)k * sc->period_ns;
        uint64_t s = base + urange(&r, 0, sc->period_ns - 1);
        uint64_t len = urange(&r, sc->mask_min_ns, sc->mask_max_ns);
        if (sc->long_mask_ns && k == sc->long_mask_tick) {          /* 긴 마스크는 반드시 넣는다 */
            s = base + 1000 > last_end ? base + 1000 : last_end;
            len = sc->long_mask_ns;
        }
        if (s < last_end) continue;                                 /* 겹치는 짧은 마스크는 버림 */
        MW[NMW].s = s; MW[NMW].e = s + len; NMW++;
        last_end = s + len;
    }
}
static uint64_t unblocked_from(uint64_t t) {        /* t 이후 처음으로 마스크 밖인 시각 */
    while (MWI < NMW && MW[MWI].e <= t) MWI++;
    for (unsigned i = MWI; i < NMW && MW[i].s <= t; i++)
        if (MW[i].e > t) t = MW[i].e;
    return t;
}

enum { OP_IDLE, OP_ENTER, OP_CLEAR1, OP_LVL, OP_SEQ, OP_TS, OP_WORK, OP_CLEAR2, OP_EXIT };
static struct { int op; uint64_t t; uint32_t entry_ts, entry, pops; uint16_t seq; } C;

static void cpu_kick(void) {                        /* 라인이 섰고 CPU 가 놀면 진입 예약 */
    if (C.op == OP_IDLE && irq_line()) { C.op = OP_ENTER; C.t = unblocked_from(H.now) + T_ENTRY; }
}
static void cpu_step(const scen_t *sc, run_t *R) {  /* ISR = 마이크로 op 의 연속 */
    switch (C.op) {
    case OP_ENTER:
        C.entry = ++R->entries; C.entry_ts = ts_now(); C.pops = 0;
        C.op = sc->policy == CLEAR_FIRST ? OP_CLEAR1 : OP_LVL;
        break;
    case OP_CLEAR1: reg_write32(IRQ_RAW, TICK | OVF); C.t += T_ACC; C.op = OP_LVL; break;
    case OP_LVL:    C.op = reg_read32(FIFO_LVL) ? OP_SEQ : OP_WORK; C.t += T_ACC; break;
    case OP_SEQ:    C.seq = (uint16_t)reg_read32(EVT_SEQ); C.t += T_ACC; C.op = OP_TS; break;
    case OP_TS: {
        uint32_t ts = reg_read32(EVT_TS);
        if (R->nrec < MAXREC) R->rec[R->nrec++] = (rec_t){C.seq, ts, C.entry_ts, ts_now(), C.entry};
        C.pops++; C.t += T_ACC; C.op = OP_LVL;
        break;
    }
    case OP_WORK:   C.t += sc->work_ns; C.op = sc->policy == CLEAR_LAST ? OP_CLEAR2 : OP_EXIT; break;
    case OP_CLEAR2: reg_write32(IRQ_RAW, TICK | OVF); C.t += T_ACC; C.op = OP_EXIT; break;
    case OP_EXIT:
        if (C.pops == 0) R->empty_entries++;
        if (C.pops >= 2) R->multi_entries++;
        if (H.cnt && !(H.raw & TICK) && R->strand_exits++ == 0) {   /* 불변식: 레코드가 있으면 TICK 도 있어야 */
            R->strand_entry = C.entry; R->strand_seq = H.q[H.head].seq;
        }
        if (irq_line()) { C.op = OP_ENTER; C.t += T_TAIL; } else C.op = OP_IDLE;
        break;
    default: break;
    }
}
static void sim_run(const scen_t *sc, const fault_t *f, run_t *R) {
    memset(&H, 0, sizeof H); memset(&C, 0, sizeof C); memset(R, 0, sizeof *R);
    if (f) H.f = *f;
    H.ts0 = sc->ts0; H.seq_ctr = sc->seq0;
    gen_masks(sc);
    reg_write32(IRQ_EN, TICK | OVF);
    uint32_t k = 0;
    for (;;) {                                      /* 가장 이른 사건부터. 동시각이면 HW tick 먼저 */
        uint64_t tt = k < sc->n_ticks ? sc->first_ns + (uint64_t)k * sc->period_ns : UINT64_MAX;
        uint64_t tc = C.op != OP_IDLE ? C.t : UINT64_MAX;
        if (tt == UINT64_MAX && tc == UINT64_MAX) break;
        if (tt <= tc) { H.now = tt; hw_tick(); k++; cpu_kick(); }
        else          { H.now = tc; cpu_step(sc, R); }
    }
    R->ovf_cnt = reg_read32(OVF_CNT);
    R->end_lvl = reg_read32(FIFO_LVL);              /* 끝난 뒤 FIFO 에 남은 레코드 = stranded */
    R->nacc = H.nacc;
}

/* ================= Part A: latency 와 히스토그램 ================= */
/* 레코드 하나의 latency(ns). 기본은 entry_ts - trig_ts (32-bit wrap 카운터). 이 값이 음수면 레코드가
 * ISR 진입 '뒤에' 도착해 같은 drain 루프에 잡힌 것(in-flight) → pop_ts - trig_ts 로 재고 *inflight=true. */
static int32_t lat_of(const rec_t *r, bool *inflight) {
    /* TODO: s32_of(entry_ts - trig_ts) >= 0 이면 그 값, 아니면 in-flight → pop_ts 기준 */
    (void)r;
    *inflight = false;
    return 0;
}

#define HBINS 64
typedef struct { uint32_t bin_ns, n, cnt[HBINS + 1]; int32_t min, max; int64_t sum; } hist_t;   /* cnt[HBINS] = overflow */

static void hist_init(hist_t *h, uint32_t bin_ns) {
    memset(h, 0, sizeof *h);
    h->bin_ns = bin_ns; h->min = INT32_MAX; h->max = INT32_MIN;
}
/* v 를 bin v/bin_ns 에 넣는다 (음수는 bin 0, HBINS 이상은 overflow bin). n/min/max/sum 도 갱신 */
static void hist_add(hist_t *h, int32_t v) {
    /* TODO: bin 계산(음수 → 0, 넘치면 HBINS) + n/sum/min/max 갱신 */
    (void)h;
    (void)v;
}
/* nearest-rank 분위수 num/den 의 '상한': rank r = ceil(n*num/den) (최소 1) 번째 값이 든 bin 의
 * 위쪽 끝 (b+1)*bin_ns-1 과 max 중 작은 값. overflow bin 이면 max. 비었거나 잘못된 p 면 -1. */
static int32_t hist_quantile(const hist_t *h, uint32_t num, uint32_t den) {
    /* TODO: rank = ceil(n*num/den) (최소 1) → 누적 count 로 bin 찾기 → min(bin 상한, max) */
    (void)h;
    (void)num;
    (void)den;
    return -1;
}

static void hist_print(const hist_t *h) {           /* 주어진 코드: 빈 bin 은 건너뛴다 */
    uint32_t mx = 0;
    for (int b = 0; b <= HBINS; b++) if (h->cnt[b] > mx) mx = h->cnt[b];
    int gap = 0;
    for (int b = 0; b <= HBINS; b++) {
        if (!h->cnt[b]) { gap = 1; continue; }
        if (gap && b) printf("        ...\n");
        gap = 0;
        if (b == HBINS) printf("  >=%5.1f us      |", HBINS * h->bin_ns / 1000.0);
        else printf("  %5.1f-%5.1f us |", b * h->bin_ns / 1000.0, (b + 1) * h->bin_ns / 1000.0);
        int w = 1 + (int)(39ull * h->cnt[b] / mx);
        for (int i = 0; i < w; i++) putchar('#');
        printf(" %u\n", h->cnt[b]);
    }
}

/* ================= Part B: seq 검사기 ================= */
enum { SQ_GAP, SQ_DUP, SQ_REORDER, SQ_STALE, SQ_TAIL, SQ_KINDS };
static const char *const SQ_NAME[] = {"GAP", "DUP", "REORDER", "STALE", "TAIL"};
typedef struct { uint32_t idx; uint16_t got, exp; bool hit; } sqev_t;
typedef struct {
    uint16_t next;              /* 다음에 와야 할 seq                         */
    uint64_t win;               /* bit i = seq (next-1-i) 를 받았음            */
    uint32_t n, lost, dup, reorder, stale, extra;
    sqev_t   first[SQ_KINDS];   /* 종류별 첫 이상 (idx, 받은 값, 그때의 next) */
} seqchk_t;

static void seq_init(seqchk_t *c, uint16_t first) {
    memset(c, 0, sizeof *c);
    c->next = first;
    c->win = ~0ull;             /* first 이전은 '받은 것' 으로 → 그보다 오래된 seq 는 DUP */
}
static void seq_note(seqchk_t *c, int kind, uint32_t idx, uint16_t got) {
    if (!c->first[kind].hit) c->first[kind] = (sqev_t){idx, got, c->next, true};
}
/* seq 하나를 먹인다. d = s - next (16-bit serial 차이):
 *  d >= 0 : d 개를 건너뜀 → lost += d (GAP), 윈도를 d+1 칸 밀고 bit0 세움, next = s+1
 *  d <  0 : i = -d-1 칸 아래. i >= 64 → STALE, 이미 받음 → DUP, 아니면 → REORDER (lost 하나 되돌림) */
static void seq_feed(seqchk_t *c, uint16_t s, uint32_t idx) {
    /* TODO: d = s16_of(s - next) 로 GAP / 윈도 이동 / DUP / REORDER / STALE 분류, seq_note 로 첫 이상 기록 */
    (void)c;
    (void)s;
    (void)idx;
    (void)seq_note;
}
/* 스트림 끝: 와야 했던 마지막 다음 값(end_next)까지 못 받은 것은 lost(TAIL), 넘어선 것은 extra */
static void seq_finish(seqchk_t *c, uint16_t end_next) {
    /* TODO: end_next - next 가 양수면 TAIL 유실, 음수면 extra */
    (void)c;
    (void)end_next;
}

/* ================= 분석 (주어진 코드) ================= */
typedef struct {
    hist_t   h;
    seqchk_t sq;
    uint32_t inflight, late, first_late, ts_bad, first_ts_bad;
} analysis_t;

static uint32_t golden_ts(const scen_t *sc, uint16_t seq) {      /* 스코어보드: seq 의 trigger 시각 */
    uint32_t k = (uint16_t)(seq - sc->seq0);
    return sc->ts0 + sc->first_ns + k * sc->period_ns;
}
static void analyze(const scen_t *sc, const run_t *R, analysis_t *a) {
    memset(a, 0, sizeof *a);
    hist_init(&a->h, 500);
    seq_init(&a->sq, sc->seq0);
    a->first_late = a->first_ts_bad = UINT32_MAX;
    for (uint32_t i = 0; i < R->nrec; i++) {
        const rec_t *r = &R->rec[i];
        bool inf = false;
        int32_t lat = lat_of(r, &inf);
        hist_add(&a->h, lat);
        a->inflight += inf;
        if (lat > (int32_t)sc->budget_ns && a->late++ == 0) a->first_late = i;
        if (r->trig_ts != golden_ts(sc, r->seq) && a->ts_bad++ == 0) a->first_ts_bad = i;
        seq_feed(&a->sq, r->seq, i);
    }
    seq_finish(&a->sq, (uint16_t)(sc->seq0 + sc->n_ticks));
}

/* ================= Part C: 판정 ================= */
enum { V_TS, V_SILENT_LOSS, V_DUP, V_REORDER, V_STRANDED, V_OVERFLOW, V_DEADLINE, V_KINDS };
static const char *const V_NAME[] = {"TS_MISMATCH", "SILENT_LOSS", "DUPLICATE", "REORDER",
                                     "STRANDED", "OVERFLOW", "DEADLINE"};
typedef struct { uint32_t flags; int primary; uint32_t silent; } verdict_t;

/* flags 의 bit k = 원인 k. primary = 번호가 가장 작은(우선순위 높은) 원인, 없으면 -1.
 * 유실 중 '설명되는' 것(OVF_CNT 로 보고된 버림 + 끝까지 FIFO 에 남은 레코드)을 뺀 나머지만 SILENT.
 * DUP 에는 stale·extra 도 포함. STRANDED = strand_exits 또는 end_lvl. */
static void judge(const run_t *R, const analysis_t *a, verdict_t *v) {
    /* TODO: 설명되는 유실(ovf_cnt + end_lvl)을 뺀 silent 계산, 원인별 flags, primary = 가장 작은 번호 */
    (void)R;
    (void)a;
    memset(v, 0, sizeof *v);
    v->primary = -1;
}

/* ================= 리포트 (주어진 코드) ================= */
static void print_sq(int kind, const seqchk_t *c, const run_t *R) {
    const sqev_t *e = &c->first[kind];
    printf("  first: FAIL %s kind=%s rec#%u got=0x%04x exp=0x%04x", IP_NAME, SQ_NAME[kind], e->idx, e->got, e->exp);
    if (e->idx < R->nrec) printf(" entry#%u", R->rec[e->idx].entry);
    printf("\n");
}
static void print_verdict(const char *name, const scen_t *sc, const run_t *R, const analysis_t *a,
                          const verdict_t *v) {
    printf("[%s] %s", name, v->flags ? "FAIL" : "PASS");
    if (v->flags) {
        printf(" cause=%s flags=", V_NAME[v->primary]);
        const char *sep = "";
        for (int k = 0; k < V_KINDS; k++)
            if (v->flags & (1u << k)) { printf("%s%s", sep, V_NAME[k]); sep = "|"; }
    }
    printf("\n  recs=%u entries=%u multi=%u empty=%u lost=%u(silent=%u ovf=%u left=%u) dup=%u reorder=%u"
           " strand=%u late=%u p50<=%d p99<=%d max=%d ns\n",
           R->nrec, R->entries, R->multi_entries, R->empty_entries, a->sq.lost, v->silent, R->ovf_cnt,
           R->end_lvl, a->sq.dup + a->sq.stale + a->sq.extra, a->sq.reorder, R->strand_exits, a->late,
           hist_quantile(&a->h, 1, 2), hist_quantile(&a->h, 99, 100), a->h.max);
    if (!v->flags) return;
    switch (v->primary) {
    case V_TS: {
        const rec_t *r = &R->rec[a->first_ts_bad];
        printf("  first: FAIL %s kind=TS_MISMATCH rec#%u seq=0x%04x trig_ts=0x%08x exp_ts=0x%08x diff=%+d ns\n",
               IP_NAME, a->first_ts_bad, r->seq, r->trig_ts, golden_ts(sc, r->seq),
               s32_of(r->trig_ts - golden_ts(sc, r->seq)));
        break;
    }
    case V_SILENT_LOSS:
        print_sq(a->sq.first[SQ_GAP].hit ? SQ_GAP : SQ_TAIL, &a->sq, R);
        break;
    case V_DUP:
        print_sq(a->sq.first[SQ_DUP].hit ? SQ_DUP : SQ_STALE, &a->sq, R);
        break;
    case V_REORDER: print_sq(SQ_REORDER, &a->sq, R); break;
    case V_STRANDED:
        printf("  first: FAIL %s kind=STRANDED isr_exit entry#%u FIFO_LVL>0 but IRQ_RAW.TICK=0 head_seq=0x%04x"
               " policy=%s\n", IP_NAME, R->strand_entry, R->strand_seq, POLICY_NAME[sc->policy]);
        break;
    case V_OVERFLOW:
        printf("  first: FAIL %s kind=OVERFLOW OVF_CNT=%u (accounted) first_gap rec#%u got=0x%04x exp=0x%04x\n",
               IP_NAME, R->ovf_cnt, a->sq.first[SQ_GAP].idx, a->sq.first[SQ_GAP].got, a->sq.first[SQ_GAP].exp);
        break;
    case V_DEADLINE: {
        const rec_t *r = &R->rec[a->first_late];
        bool inf;
        printf("  first: FAIL %s kind=DEADLINE rec#%u seq=0x%04x lat=%d ns budget=%u ns entry#%u\n",
               IP_NAME, a->first_late, r->seq, lat_of(r, &inf), sc->budget_ns, r->entry);
        break;
    }
    default: break;
    }
}

/* ================= 테스트 ================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static scen_t nominal(void) {                       /* 40 kHz FSYNC, ISR 18 us, 마스크 0.2~9 us */
    scen_t s = {.period_ns = 25000, .n_ticks = MAXT, .first_ns = 10000, .ts0 = 0xFFFFFFFFu - 99999u,
                .seq0 = 0xFFF0, .seed = 0xC0FFEEu, .mask_min_ns = 200, .mask_max_ns = 9000,
                .policy = CLEAR_FIRST, .work_ns = 18000, .budget_ns = 10000};
    return s;
}
static run_t RUN;
static analysis_t AN;
static void run_and_judge(const scen_t *sc, const fault_t *f, verdict_t *v) {
    sim_run(sc, f, &RUN);
    analyze(sc, &RUN, &AN);
    judge(&RUN, &AN, v);
}
static int cmp_i32(const void *a, const void *b) {
    int32_t x = *(const int32_t *)a, y = *(const int32_t *)b;
    return (x > y) - (x < y);
}

int main(void) {
    /* ---- Part A: lat_of — wrap 과 in-flight ---- */
    struct { rec_t r; int32_t lat; bool inf; } lt[] = {
        {{0, 0x000000F0u, 0x00000100u, 0x00000120u, 1}, 16, false},
        {{0, 0xFFFFFFF0u, 0x00000010u, 0x00000030u, 1}, 32, false},       /* 카운터 wrap */
        {{0, 0x00001050u, 0x00001000u, 0x00001100u, 1}, 0xB0, true},      /* 진입 뒤 도착 */
        {{0, 0xFFFFFF00u, 0x00000000u, 0x00000040u, 1}, 256, false},
        {{0, 0x00000010u, 0xFFFFFFF0u, 0x00000020u, 1}, 16, true},        /* 음수 차이 + wrap */
    };
    int lok = 0;
    for (size_t i = 0; i < sizeof lt / sizeof lt[0]; i++) {
        bool inf = !lt[i].inf;
        lok += lat_of(&lt[i].r, &inf) == lt[i].lat && inf == lt[i].inf;
    }
    CHECK(lok == (int)(sizeof lt / sizeof lt[0]));
    CHECK(s32_of(0x80000000u) == INT32_MIN && s32_of(0xFFFFFFFFu) == -1 && s16_of(0x8000u) == -32768);

    /* ---- Part A: 히스토그램 ---- */
    hist_t h;
    hist_init(&h, 100);
    CHECK(hist_quantile(&h, 1, 2) == -1);                        /* 빈 히스토그램 */
    int32_t hv[] = {0, 50, 99, 100, 250, 999, 5000, 10000};
    for (size_t i = 0; i < sizeof hv / sizeof hv[0]; i++) hist_add(&h, hv[i]);
    CHECK(h.n == 8 && h.cnt[0] == 3 && h.cnt[1] == 1 && h.cnt[2] == 1 && h.cnt[9] == 1);
    CHECK(h.cnt[50] == 1 && h.cnt[HBINS] == 1 && h.min == 0 && h.max == 10000 && h.sum == 16498);
    CHECK(hist_quantile(&h, 1, 2) == 199);                       /* rank 4 → bin 1 */
    CHECK(hist_quantile(&h, 7, 8) == 5099);                      /* rank 7 → bin 50 */
    CHECK(hist_quantile(&h, 1, 1) == 10000);                     /* rank 8 → overflow → max */
    CHECK(hist_quantile(&h, 0, 1) == 99 && hist_quantile(&h, 3, 2) == -1);

    /* 무작위 2000 개: bin 상한 q 는 정확한 nearest-rank 값 x 에 대해 x <= q <= x + bin - 1 */
    static int32_t xs[2000];
    uint32_t rs = 12345u;
    hist_init(&h, 500);
    for (int i = 0; i < 2000; i++) {
        xs[i] = (int32_t)(xs32(&rs) % 20 == 0 ? xs32(&rs) % 30000u : 120u + xs32(&rs) % 900u);
        hist_add(&h, xs[i]);
    }
    qsort(xs, 2000, sizeof xs[0], cmp_i32);
    uint32_t qn[] = {1, 9, 99, 999}, qd[] = {2, 10, 100, 1000};
    int qok = 0;
    for (int i = 0; i < 4; i++) {
        uint64_t r = (2000ull * qn[i] + qd[i] - 1) / qd[i];
        int32_t x = xs[r - 1], q = hist_quantile(&h, qn[i], qd[i]);
        qok += x <= q && q <= x + 499;
    }
    CHECK(qok == 4);

    /* ---- Part A: 정상 실리콘 + 정상 ISR 에서 latency 히스토그램 ---- */
    scen_t sc = nominal();
    verdict_t v;
    run_and_judge(&sc, NULL, &v);
    CHECK(RUN.nrec == MAXT && RUN.entries == MAXT && RUN.empty_entries == 0 && RUN.multi_entries == 0);
    CHECK(AN.h.n == MAXT && AN.inflight == 0 && AN.ts_bad == 0 && AN.late == 0);
    CHECK(AN.h.min > 0 && AN.h.min <= (int32_t)T_ENTRY && AN.h.max < (int32_t)sc.budget_ns);
    CHECK(hist_quantile(&AN.h, 1, 2) == 499);                     /* 절반 이상이 '마스크 없음' = 120 ns */
    CHECK(v.flags == 0 && v.primary == -1);
    printf("---- Part A: %s nominal, %u events, P=%u ns, ISR %s ----\n", IP_NAME, sc.n_ticks, sc.period_ns,
           POLICY_NAME[sc.policy]);
    printf("  n=%u min=%d mean=%.0f max=%d ns  p50<=%d p90<=%d p99<=%d p99.9<=%d  bus_acc=%u\n", AN.h.n,
           AN.h.min, AN.h.n ? (double)AN.h.sum / AN.h.n : 0.0, AN.h.max, hist_quantile(&AN.h, 1, 2),
           hist_quantile(&AN.h, 9, 10), hist_quantile(&AN.h, 99, 100), hist_quantile(&AN.h, 999, 1000), RUN.nacc);
    hist_print(&AN.h);

    /* ---- Part B: seq 검사기 표 ---- */
    struct { const char *name; uint16_t first; uint16_t s[8]; int n; uint16_t end;
             uint32_t lost, dup, reorder, stale; int kind; uint32_t idx; } st[] = {
        {"in-order wrap", 0xFFFE, {0xFFFE, 0xFFFF, 0x0000, 0x0001}, 4, 0x0002, 0, 0, 0, 0, -1, 0},
        {"gap",           1, {1, 2, 4, 5}, 4, 6, 1, 0, 0, 0, SQ_GAP, 2},
        {"dup",           1, {1, 2, 2, 3}, 4, 4, 0, 1, 0, 0, SQ_DUP, 2},
        {"reorder",       1, {1, 3, 2, 4}, 4, 5, 0, 0, 1, 0, SQ_REORDER, 2},
        {"reorder wrap",  0xFFFF, {0xFFFF, 0x0001, 0x0000, 0x0002}, 4, 3, 0, 0, 1, 0, SQ_REORDER, 2},
        {"fifo swap x4",  10, {11, 12, 13, 10, 14}, 5, 15, 0, 0, 1, 0, SQ_REORDER, 3},
        {"stale",         1, {1, 100, 2}, 3, 101, 98, 0, 0, 1, SQ_STALE, 2},
        {"older than 1st",10, {9, 10}, 2, 11, 0, 1, 0, 0, SQ_DUP, 0},
        {"tail lost",     1, {1, 2}, 2, 5, 2, 0, 0, 0, SQ_TAIL, 2},
        {"lost first",    0, {1, 2, 3}, 3, 4, 1, 0, 0, 0, SQ_GAP, 0},
    };
    int sok = 0;
    for (size_t i = 0; i < sizeof st / sizeof st[0]; i++) {
        seqchk_t c;
        seq_init(&c, st[i].first);
        for (int j = 0; j < st[i].n; j++) seq_feed(&c, st[i].s[j], (uint32_t)j);
        seq_finish(&c, st[i].end);
        bool ok = c.lost == st[i].lost && c.dup == st[i].dup && c.reorder == st[i].reorder && c.stale == st[i].stale;
        if (st[i].kind >= 0) ok = ok && c.first[st[i].kind].hit && c.first[st[i].kind].idx == st[i].idx;
        else for (int k = 0; k < SQ_KINDS; k++) ok = ok && !c.first[k].hit;
        if (!ok) printf("  seq case '%s': lost=%u dup=%u reorder=%u stale=%u\n", st[i].name, c.lost, c.dup,
                        c.reorder, c.stale);
        sok += ok;
    }
    CHECK(sok == (int)(sizeof st / sizeof st[0]));
    {   /* 첫 GAP 의 서명: 기대 3, 받은 4 */
        seqchk_t c;
        seq_init(&c, 1);
        uint16_t g[] = {1, 2, 4};
        for (int j = 0; j < 3; j++) seq_feed(&c, g[j], (uint32_t)j);
        CHECK(c.first[SQ_GAP].got == 4 && c.first[SQ_GAP].exp == 3 && c.next == 5);
    }

    /* 퍼즈: 0xFF00 부터 5000 개(wrap 포함)에 유실 2%·중복 1%·인접 swap 1% 를 심고, 심은 개수와 일치해야 */
    {
        uint32_t r = 0xBADC0DEu, drops = 0, dups = 0, swaps = 0, m = 0;
        static uint16_t out[6000];
        uint16_t s0 = 0xFF00;
        for (uint32_t i = 0; i < 5000; i++) {
            uint16_t s = (uint16_t)(s0 + i);
            uint32_t x = xs32(&r) % 100;
            if (x < 2 && i > 0) drops++;
            else if (x < 3) { out[m++] = s; out[m++] = s; dups++; }
            else if (x < 4 && i + 1 < 5000) { out[m++] = (uint16_t)(s + 1); out[m++] = s; swaps++; i++; }
            else out[m++] = s;
        }
        seqchk_t c;
        seq_init(&c, s0);
        for (uint32_t j = 0; j < m; j++) seq_feed(&c, out[j], j);
        seq_finish(&c, (uint16_t)(s0 + 5000));
        printf("---- Part B: fuzz planted drop=%u dup=%u swap=%u -> lost=%u dup=%u reorder=%u stale=%u\n",
               drops, dups, swaps, c.lost, c.dup, c.reorder, c.stale);
        CHECK(drops > 0 && dups > 0 && swaps > 0);
        CHECK(c.lost == drops && c.dup == dups && c.reorder == swaps && c.stale == 0 && c.extra == 0);
    }

    /* 시뮬레이션: seq 0xFFFF→0x0000 에서 push 가 사라지는 결함 → GAP @ seq 0x0000 */
    fault_t fw = {.drop_at_wrap = true};
    run_and_judge(&sc, &fw, &v);
    CHECK(AN.sq.lost == 1 && RUN.ovf_cnt == 0 && v.silent == 1 && v.primary == V_SILENT_LOSS);
    CHECK(AN.sq.first[SQ_GAP].idx == 16 && AN.sq.first[SQ_GAP].exp == 0x0000 && AN.sq.first[SQ_GAP].got == 0x0001);

    /* ---- Part C: 결함 주입 캠페인 ---- */
    printf("---- Part C: %s fault campaign (budget %u ns) ----\n", IP_NAME, sc.budget_ns);
    scen_t stress = nominal();
    stress.long_mask_tick = 5000; stress.long_mask_ns = 140000;   /* 140 us 동안 IRQ off (flash erase) */
    scen_t late = nominal();
    late.policy = CLEAR_LAST;
    struct { const char *name; const scen_t *sc; fault_t f; int primary; uint32_t flags; } camp[] = {
        {"good",                    &sc,     {0}, -1, 0},
        {"drop_at_wrap",            &sc,     {.drop_at_wrap = true}, V_SILENT_LOSS, 1u << V_SILENT_LOSS},
        {"pop_stall_every=3000",    &sc,     {.pop_stall_every = 3000}, V_DUP, 1u << V_DUP},
        {"swap_backlog (nominal)",  &sc,     {.swap_backlog = true}, -1, 0},
        {"long_mask 140us",         &stress, {0}, V_OVERFLOW, 1u << V_OVERFLOW | 1u << V_DEADLINE},
        {"swap_backlog + long_mask",&stress, {.swap_backlog = true}, V_REORDER,
                                             1u << V_REORDER | 1u << V_OVERFLOW | 1u << V_DEADLINE},
        {"ts_at_pop",               &sc,     {.ts_at_pop = true}, V_TS, 1u << V_TS},
        {"isr clear-last",          &late,   {0}, V_STRANDED, 1u << V_STRANDED | 1u << V_DEADLINE},
    };
    for (size_t i = 0; i < sizeof camp / sizeof camp[0]; i++) {
        run_and_judge(camp[i].sc, &camp[i].f, &v);
        print_verdict(camp[i].name, camp[i].sc, &RUN, &AN, &v);
        if (v.primary != camp[i].primary || v.flags != camp[i].flags)
            printf("  (expected primary=%d flags=0x%x, got primary=%d flags=0x%x)\n", camp[i].primary,
                   camp[i].flags, v.primary, v.flags);
        CHECK(v.primary == camp[i].primary && v.flags == camp[i].flags);
    }

    /* 서명까지: 각 결함이 '어디서' 잡혔나 */
    fault_t fp = {.pop_stall_every = 3000};
    run_and_judge(&sc, &fp, &v);
    CHECK(AN.sq.dup == 3 && AN.sq.lost == 0 && RUN.nrec == MAXT + 3 && AN.sq.first[SQ_DUP].idx == 3000);
    run_and_judge(&stress, NULL, &v);
    CHECK(RUN.ovf_cnt == 2 && AN.sq.lost == 2 && v.silent == 0 && RUN.multi_entries == 1 && RUN.nrec == MAXT - 2);
    fault_t fs = {.swap_backlog = true};
    run_and_judge(&stress, &fs, &v);
    CHECK(AN.sq.reorder == 1 && AN.sq.lost == 2 && v.silent == 0 && AN.sq.first[SQ_REORDER].got == 0x1378);
    fault_t ft = {.ts_at_pop = true};
    run_and_judge(&sc, &ft, &v);
    CHECK(AN.ts_bad == MAXT && AN.h.max < 200);          /* 계측기가 고장나면 latency 가 '너무 좋게' 나온다 */
    run_and_judge(&late, NULL, &v);
    uint32_t late_strand = RUN.strand_exits, late_multi = RUN.multi_entries;
    CHECK(late_strand > 0 && late_multi == late_strand && RUN.entries + late_multi == MAXT);
    CHECK(AN.sq.lost == RUN.end_lvl && v.silent == 0 && AN.h.max > (int32_t)sc.period_ns);
    printf("---- clear-last latency histogram ----\n");
    hist_print(&AN.h);

    /* ISR 본문 길이 sweep: clear-first 는 언제나 깨끗, clear-last 는 work 가 길수록 합쳐짐 증가 */
    printf("---- sweep: work_ns x policy (same masks) ----\n");
    printf("  work_us  policy       entries  multi  strand  late  max_ns\n");
    uint32_t works[] = {14000, 16000, 18000, 20000, 22000}, prev = 0;
    int first_clean = 0, last_mono = 1;
    for (int w = 0; w < 5; w++)
        for (int p = CLEAR_FIRST; p <= CLEAR_LAST; p++) {
            scen_t s2 = nominal();
            s2.work_ns = works[w]; s2.policy = p;
            run_and_judge(&s2, NULL, &v);
            printf("  %7u  %-11s  %7u  %5u  %6u  %4u  %6d\n", works[w] / 1000, POLICY_NAME[p], RUN.entries,
                   RUN.multi_entries, RUN.strand_exits, AN.late, AN.h.max);
            if (p == CLEAR_FIRST) first_clean += v.flags == 0 && RUN.entries == MAXT;
            else { last_mono = last_mono && RUN.strand_exits >= prev; prev = RUN.strand_exits; }
        }
    CHECK(first_clean == 5 && last_mono && prev > late_strand);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
