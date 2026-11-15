/* ARCH-A07 클럭 도메인 교차: 동기화기와 Gray 코드 FIFO
 *
 * 두 클럭 도메인을 ps 단위 이산 사건(discrete-event) 시뮬레이션으로 돌린다.
 *   클럭    주기 T · 위상 · 지터(±J, 고정 시드). 에지를 시간 순서대로 처리 (같은 시각이면 쓰기/소스 도메인 먼저).
 *   와이어  wire_t = 한 도메인 플롭의 출력. 에지 t 에 값이 바뀌면 비트 b 는 t + tcq + skew[b] 에 바뀐다.
 *   메타안정성 *모델*: 다른 도메인 플롭이 에지 t 에 와이어를 샘플할 때 어떤 비트의 변화 시각이
 *           (t - tsu, t + th] 안이면 그 비트는 0/1 중 무작위(xorshift32)로 결정된다. FF2 는 FF1 을 한 주기
 *           뒤에 샘플하므로 "결정된 값" 을 본다고 *가정* 한다 (실제로는 MTBF 만큼의 확률로 틀린다).
 *
 *   Part A  2-FF 동기화기 (TODO sync2_clock) — 단일 비트 레벨 전달, 지연 1~2 dst 주기, 창 적중률 = (tsu+th)/Tdst
 *   Part B  Gray 변환 (TODO bin2gray / gray2bin), Gray full/empty (TODO gray_full / gray_empty),
 *           비동기 FIFO 깊이 2^N · (N+1)비트 포인터: 이진 포인터 bitwise 2-FF vs Gray + 2-FF,
 *           스코어보드(ok/dup/lost/corrupt) + 전지적 시점의 overflow/underflow + 플래그 비관성(pessimism)
 *   Part C  토글 펄스 동기화기 (TODO toggle_src / toggle_dst), 4-phase req/ack 버스 핸드셰이크
 *           (TODO hs_src_step / hs_dst_step) vs "버스 비트마다 2-FF"
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so      (starter.c 도 같은 명령)
 *        ./so trace      (Part A 한 장면의 사건 로그 — md 파형 그림의 원본 숫자)
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; printf("PASS  %s\n", #expr); } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }
static bool chance(uint32_t *rs, double p) { return p >= 1.0 || (double)xs32(rs) < p * 4294967296.0; }

/* ======================= 시뮬레이션 커널 (starter 와 같음) ======================= */

typedef struct { int64_t tsu, th, tcq; } timing_t;
static const timing_t TM = { 150, 100, 200 };      /* ps — 교육용 대표값(가정). 창 폭 T_W = tsu + th */
#define TWIN (TM.tsu + TM.th)

typedef struct { int64_t T, phase, J, k, next; uint32_t rs; } clk_t;
static int64_t clk_jit(clk_t *c) { return c->J ? (int64_t)(xs32(&c->rs) % (uint32_t)(2 * c->J + 1)) - c->J : 0; }
static void clk_init(clk_t *c, int64_t T, int64_t phase, int64_t J, uint32_t seed) {
    c->T = T; c->phase = phase; c->J = J; c->k = 0; c->rs = seed | 1u; c->next = phase + clk_jit(c);
}
static void clk_adv(clk_t *c) { c->k++; c->next = c->phase + c->k * c->T + clk_jit(c); }

#define MAXB 32
typedef struct { uint32_t old, cur; int64_t tc; int nb; int64_t skew[MAXB]; } wire_t;
static void wire_init(wire_t *w, int nb, int64_t skew_step, uint32_t v) {
    memset(w, 0, sizeof *w);
    w->nb = nb; w->old = w->cur = v; w->tc = INT64_MIN / 2;
    for (int b = 0; b < nb; b++) w->skew[b] = b * skew_step;
}
/* 소스 플롭이 에지 t 에 v 를 잡는다 → 출력은 t + tcq (+ 비트별 skew) 에 바뀐다.
 * 직전 변화는 이미 끝났다고 본다 (설정 검사: T_src > tcq + skew_max + tsu + 2J). */
static void wire_drive(wire_t *w, int64_t t, uint32_t v) {
    if (v == w->cur) return;
    w->old = w->cur; w->cur = v; w->tc = t + TM.tcq;
}

typedef struct { uint64_t samples, meta, meta_bits; } sstat_t;
/* 다른 도메인 플롭이 에지 t 에 와이어를 잡은 값. 비트마다: 안정된 새 값 / 아직 옛 값 / 창 안이면 무작위. */
static uint32_t wire_sample(const wire_t *w, int64_t t, uint32_t *rs, sstat_t *st) {
    uint32_t diff = w->old ^ w->cur, v = w->cur & ~diff;
    if (st) st->samples++;
    if (!diff) return w->cur;
    bool meta = false;
    for (int b = 0; b < w->nb; b++) {
        uint32_t m = 1u << b;
        if (!(diff & m)) continue;
        int64_t tcb = w->tc + w->skew[b];
        if (tcb <= t - TM.tsu) v |= w->cur & m;              /* setup 전에 이미 바뀜 → 새 값 */
        else if (tcb > t + TM.th) v |= w->old & m;            /* hold 끝난 뒤에 바뀜 → 옛 값 */
        else { meta = true; if (st) st->meta_bits++; if (xs32(rs) & 1u) v |= m; }   /* 창 안 → 무작위 결정 */
    }
    if (meta && st) st->meta++;
    return v;
}

/* ======================= Part A: 2-FF 동기화기 ======================= */

typedef struct { uint32_t q1, q2; } sync2_t;

/* 목적지 도메인 에지 t 에서 동기화기 한 번 클럭. q1 = 와이어를 직접 잡는 FF1 (메타안정 가능),
 * q2 = FF1 을 한 주기 뒤에 잡는 FF2 (모델에서는 결정된 값). 모든 플롭은 같은 에지에서 동시에 갱신된다. */
static void sync2_clock(sync2_t *s, const wire_t *w, int64_t t, uint32_t *rs, sstat_t *st) {
    /* TODO(Part A-1): 두 플롭을 동시에 갱신하라. 순서에 주의 — q2 는 에지 직전의 q1 을 받는다. */
    s->q2 = s->q1;
    s->q1 = wire_sample(w, t, rs, st);
}

/* ======================= Part B: Gray 코드와 full/empty ======================= */

/* 이진 → Gray: 이웃한 두 수의 Gray 코드는 정확히 1 비트 다르다. */
static uint32_t bin2gray(uint32_t b) {
    /* TODO(Part B-1) */
    return b ^ (b >> 1);
}

/* Gray → 이진: 비트 i = Gray 의 비트 i 와 그 위 모든 비트의 XOR (prefix XOR, 위에서 아래로). */
static uint32_t gray2bin(uint32_t g) {
    /* TODO(Part B-2) */
    uint32_t b = g;
    for (uint32_t s = g >> 1; s; s >>= 1) b ^= s;
    return b;
}

/* 깊이 2^N FIFO, 포인터 N+1 비트 (Gray). wg = 쓰기 도메인의 Gray wptr, rq2 = 동기화된 Gray rptr.
 * full ⇔ 이진 차이 (w - r) mod 2^(N+1) == 2^N. Gray 로는 위 두 비트만 반전된 값과 같을 때. */
static bool gray_full(uint32_t wg, uint32_t rq2, int N) {
    /* TODO(Part B-3) */
    return wg == (rq2 ^ (3u << (N - 1)));
}

/* empty ⇔ 두 포인터가 같다 (Gray 는 일대일이므로 Gray 끼리 비교해도 된다). */
static bool gray_empty(uint32_t rg, uint32_t wq2) {
    /* TODO(Part B-4) */
    return rg == wq2;
}

/* ======================= Part C: 펄스 동기화 · req/ack 핸드셰이크 ======================= */

/* 소스 도메인: 1 주기 펄스를 레벨 토글로 바꾼다 (펄스가 짧아도 레벨은 다음 펄스까지 유지된다). */
static uint32_t toggle_src(uint32_t tog, bool pulse) {
    /* TODO(Part C-1) */
    return tog ^ (pulse ? 1u : 0u);
}

/* 목적지 도메인: 동기화된 토글 q2 와 한 주기 전 값 q3 를 비교해 변화가 있으면 1 주기 펄스. q3 갱신도 여기서. */
static bool toggle_dst(uint32_t q2, uint32_t *q3) {
    /* TODO(Part C-2) */
    bool p = ((q2 ^ *q3) & 1u) != 0;
    *q3 = q2;
    return p;
}

typedef enum { HS_IDLE, HS_WAIT_ACK, HS_WAIT_ACK_LOW } hs_st_t;
typedef struct { hs_st_t st; uint32_t req, data; } hs_src_t;
typedef struct { uint32_t ack; } hs_dst_t;

/* 4-phase 소스 쪽 한 에지. ack_q2 = 소스 도메인으로 동기화된 ack. have = 보낼 단어가 있음.
 * 새 단어를 data 레지스터에 실었으면 1 을 돌려준다. data 는 다음 단어를 실을 때까지 절대 바꾸지 않는다. */
static int hs_src_step(hs_src_t *s, uint32_t ack_q2, bool have, uint32_t word) {
    /* TODO(Part C-3): IDLE → (단어 싣고 req=1) → WAIT_ACK → (ack 보이면 req=0) → WAIT_ACK_LOW → (ack 내려가면) IDLE */
    switch (s->st) {
    case HS_IDLE:
        if (have) { s->data = word; s->req = 1; s->st = HS_WAIT_ACK; return 1; }
        break;
    case HS_WAIT_ACK:
        if (ack_q2) { s->req = 0; s->st = HS_WAIT_ACK_LOW; }
        break;
    case HS_WAIT_ACK_LOW:
        if (!ack_q2) s->st = HS_IDLE;
        break;
    }
    return 0;
}

/* 4-phase 목적지 쪽 한 에지. req_q2 = 목적지 도메인으로 동기화된 req.
 * 이 에지에서 데이터 버스를 잡아야 하면 1 (그 순간 버스는 이미 안정돼 있어야 한다). */
static int hs_dst_step(hs_dst_t *d, uint32_t req_q2) {
    /* TODO(Part C-4): req 가 보이고 아직 ack 안 했으면 잡고 ack=1, req 가 내려갔으면 ack=0 */
    if (req_q2 && !d->ack) { d->ack = 1; return 1; }
    if (!req_q2 && d->ack) d->ack = 0;
    return 0;
}

/* ======================= 하네스 (starter 와 같음) ======================= */

typedef struct { const char *name; int64_t Tw, Tr; } clkcfg_t;
static const clkcfg_t CLK[] = {
    { "100->77 MHz", 10000, 12987 },    /* 기본: 빠름→조금 느림, 비율이 정수비와 거리가 멀다 */
    { "200->31 MHz", 5000, 32258 },     /* 빠름→아주 느림 (6.45:1) */
    { "31->200 MHz", 32258, 5000 },     /* 느림→빠름 */
    { "100->99.97", 10000, 10003 },     /* 거의 같은 주파수: 위상이 3 ps/주기씩 미끄러진다 (plesiochronous) */
};
#define NCLK ((int)(sizeof CLK / sizeof CLK[0]))

static int64_t i64max(int64_t a, int64_t b) { return a > b ? a : b; }

/* ---------- Part A: 단일 비트 레벨을 2-FF 로 넘긴다 ---------- */
typedef struct {
    long sent, recv, extra, max_run; sstat_t st;
    int64_t lat_min, lat_max, t_end; double lat_sum; long hist[4];
} lvl_t;

static lvl_t run_level(int64_t Ts, int64_t Td, int64_t J, uint32_t seed, long ntog) {
    lvl_t r; memset(&r, 0, sizeof r); r.lat_min = INT64_MAX;
    clk_t cs, cd;
    clk_init(&cs, Ts, 1000 + J, J, seed);
    clk_init(&cd, Td, 1000 + J + (int64_t)((seed * 7919u) % (uint32_t)Td), J, seed * 31u + 7u);
    wire_t w; wire_init(&w, 1, 0, 0);
    sync2_t sy = { 0, 0 };
    uint32_t rs = seed * 2654435761u | 1u, lvl = 0, out = 0;
    int64_t pend[64]; long ph = 0, pt = 0;              /* 아직 목적지에 안 보인 소스 변화 시각 (FIFO) */
    long hold_min = (long)((2 * Td + Ts - 1) / Ts) + 1;  /* 레벨을 ≥ 2·Tdst 유지 → 레벨 유실 불가 */
    long hold = 1, run = 0, after = 0; uint64_t meta_mark = 0;
    while (r.sent < ntog || after < 6) {
        if (cs.next <= cd.next) {
            int64_t t = cs.next;
            if (r.sent < ntog && --hold <= 0) {
                run = (r.st.meta != meta_mark) ? run + 1 : 0;      /* 직전 변화가 창에 걸렸나 → 연속 길이 */
                if (run > r.max_run) r.max_run = run;
                meta_mark = r.st.meta;
                lvl ^= 1u; wire_drive(&w, t, lvl); pend[pt++ % 64] = t; r.sent++;
                hold = hold_min + (long)(xs32(&rs) % 4u);
            }
            clk_adv(&cs);
        } else {
            int64_t t = cd.next;
            sync2_clock(&sy, &w, t, &rs, &r.st);
            if ((sy.q2 & 1u) != out) {
                out = sy.q2 & 1u; r.recv++;
                if (ph == pt) r.extra++;
                else {
                    int64_t lat = t - pend[ph++ % 64];
                    if (lat < r.lat_min) r.lat_min = lat;
                    if (lat > r.lat_max) r.lat_max = lat;
                    r.lat_sum += (double)lat;
                    r.hist[lat / Td < 3 ? lat / Td : 3]++;
                }
            }
            if (r.sent >= ntog) after++;
            r.t_end = t;
            clk_adv(&cd);
        }
    }
    return r;
}

static void trace_demo(uint32_t meta_seed) {
    /* 손으로 위상을 맞춘 장면: 쓰기 클럭 100 MHz 위상 1000 ps, 읽기 77 MHz 위상 1250 ps.
     * 1000 ps 에지에서 d: 0→1 → 와이어는 1200 ps 에 바뀜 → 읽기 에지 1250 ps 의 setup 창 (1100,1350] 안. */
    wire_t w; wire_init(&w, 1, 0, 0);
    sync2_t sy = { 0, 0 }; sstat_t st = { 0, 0, 0 };
    uint32_t rs = meta_seed;
    int64_t tw = 1000, tr = 1250, Tr = 12987;
    printf("  t=%6lld ps  W edge : d 0->1 (wire changes at %lld ps)\n", (long long)tw, (long long)(tw + TM.tcq));
    wire_drive(&w, tw, 1);
    for (int k = 0; k < 3; k++, tr += Tr) {
        uint64_t m0 = st.meta;
        sync2_clock(&sy, &w, tr, &rs, &st);
        printf("  t=%6lld ps  R edge : q1=%u q2=%u%s", (long long)tr, (unsigned)sy.q1, (unsigned)sy.q2,
               st.meta != m0 ? "  <- META (change inside (t-tsu, t+th]) resolved randomly" : "");
        if (sy.q2) { printf("  -> out=1, latency %lld ps = %.2f Tr\n", (long long)(tr - tw), (double)(tr - tw) / (double)Tr); break; }
        printf("\n");
    }
}

static void part_a(void) {
    printf("== Part A: 단일 비트 레벨 + 2-FF 동기화기 (tsu=%lld th=%lld tcq=%lld ps, T_W=%lld ps) ==\n",
           (long long)TM.tsu, (long long)TM.th, (long long)TM.tcq, (long long)TWIN);
    printf("  trace (resolves to new):\n"); trace_demo(1u);
    printf("  trace (resolves to old):\n"); trace_demo(2u);
    printf("  %-14s %6s %6s %6s %8s %8s %6s  %-24s %s\n", "clocks", "sent", "recv", "meta", "meta/tx", "T_W/Td",
           "maxrun", "latency min/avg/max (Td)", "hist[1Td,2Td)");
    for (int i = 0; i < NCLK; i++) {
        lvl_t r = run_level(CLK[i].Tw, CLK[i].Tr, 0, 11u + (uint32_t)i, 20000);
        double Td = (double)CLK[i].Tr, rate = (double)r.st.meta / (double)r.sent, expect = (double)TWIN / Td;
        printf("  %-14s %6ld %6ld %6llu %8.4f %8.4f %6ld  %.2f / %.2f / %.2f %10ld/%ld\n", CLK[i].name, r.sent, r.recv,
               (unsigned long long)r.st.meta, rate, expect, r.max_run, r.recv ? (double)r.lat_min / Td : 0.0,
               r.recv ? r.lat_sum / (double)r.recv / Td : 0.0, (double)r.lat_max / Td, r.hist[1], r.hist[2]);
        CHECK(r.recv == r.sent && r.extra == 0);                               /* 레벨은 하나도 안 잃는다 */
        CHECK(r.lat_min >= TM.tcq - TM.th + CLK[i].Tr);                         /* 지연 하한: Td + tcq - th */
        CHECK(r.lat_max < TM.tcq + TM.tsu + 2 * CLK[i].Tr);                     /* 지연 상한: 2Td + tcq + tsu */
        CHECK(rate > 0.75 * expect && rate < 1.25 * expect);                    /* 창 적중률 ≈ T_W / Td */
        if (i == 0) {
            double sec = (double)r.t_end * 1e-12, fdata = (double)r.sent / sec, fclk = 1e12 / Td;
            printf("  [MTBF 분모] 측정 meta/s = %.3e, f_clk*f_data*T_W = %.3e (f_data = %.3e /s)\n",
                   (double)r.st.meta / sec, fclk * fdata * (double)TWIN * 1e-12, fdata);
        }
        if (i == 3) CHECK(r.max_run >= 5);      /* 거의 같은 주파수 → 창 적중이 몰려서(연속으로) 온다 */
    }
}

/* ---------- Part B: 비동기 FIFO ---------- */
#define MAXD 64
typedef struct { int clk, N, burst; bool gray; long edges; uint32_t seed; int64_t J, skew; double pw_fix, pr_fix; } fcfg_t;
typedef struct {
    long writes, reads, overflow, underflow, ok, dup, lost, corrupt, imp_w, imp_r;
    long full_seen, full_true, full_pess, full_unsafe, empty_seen, empty_true, empty_pess, empty_unsafe;
    long redges_main, reads_main;      /* drain 전 구간의 읽기 에지 수 · 읽은 수 (처리량) */
    bool all_delivered; sstat_t sp, sd;
} fres_t;

static fres_t run_fifo(const fcfg_t *c) {
    fres_t r; memset(&r, 0, sizeof r);
    const uint32_t D = 1u << c->N, M = (2u << c->N) - 1u;     /* 깊이, 포인터 마스크 (N+1 비트) */
    const int64_t Tw = CLK[c->clk].Tw, Tr = CLK[c->clk].Tr;
    clk_t cw, cr;
    clk_init(&cw, Tw, 1000 + c->J, c->J, c->seed);
    clk_init(&cr, Tr, 1000 + c->J + (int64_t)((c->seed * 7919u) % (uint32_t)Tr), c->J, c->seed * 31u + 7u);
    uint32_t rs = c->seed * 2654435761u | 1u;
    static wire_t mem[MAXD];
    wire_t wp, rp;                                           /* 포인터 와이어 (Gray 또는 이진) */
    wire_init(&wp, c->N + 1, c->skew, 0); wire_init(&rp, c->N + 1, c->skew, 0);
    for (uint32_t d = 0; d < D; d++) wire_init(&mem[d], 32, 0, 0xFFFFFFFFu);   /* 한 번도 안 쓴 칸 */
    sync2_t wq = { 0, 0 }, rq = { 0, 0 };   /* wq: 읽기 도메인에 있는 wptr 동기화기, rq: 쓰기 도메인에 있는 rptr 동기화기 */
    uint32_t wbin = 0, rbin = 0, wseq = 0, rseq = 0; uint32_t wleft = 0, rleft = 0;
    const double fmin = 1.0 / (double)i64max(Tw, Tr);
    long idle = 0;
    for (long e = 0; e < c->edges + 50000; e++) {          /* 끝에 비우기(drain) 구간 */
        bool drain = e >= c->edges;
        if (drain && idle >= 8) { r.all_delivered = (rseq == wseq && r.ok == (long)wseq); break; }
        bool fill = ((e / 4000) & 1) == 0;                   /* 4000 에지마다 채우기 ↔ 비우기 압력 교대 */
        double pw = drain ? 0.0 : c->pw_fix > 0 ? c->pw_fix : (fill ? 0.9 : 0.5) * fmin * (double)Tw;
        double pr = drain ? 1.0 : c->pr_fix > 0 ? c->pr_fix : (fill ? 0.5 : 0.9) * fmin * (double)Tr;
        if (cw.next <= cr.next) {                             /* ---- 쓰기 도메인 에지 ---- */
            int64_t t = cw.next;
            uint32_t rsb = c->gray ? gray2bin(rq.q2) : rq.q2;          /* 쓰기 쪽이 아는 rptr (이진) */
            bool full = c->gray ? gray_full(bin2gray(wbin), rq.q2, c->N) : (((wbin ^ rsb) & M) == D);
            uint32_t freecnt = (D - ((wbin - rsb) & M)) & M;           /* (N+1)비트 뺄셈: 쓰레기면 감싸 돈다 */
            bool tfull = ((wbin - rbin) & M) == D;                      /* 전지적 시점의 진짜 full */
            r.full_seen += full; r.full_true += tfull;
            r.full_pess += full && !tfull; r.full_unsafe += !full && tfull;
            if (((rbin - rsb) & M) > D) r.imp_w++;                      /* 실제 rptr 보다 앞서거나 DEPTH 넘게 뒤 */
            if (wleft == 0 && chance(&rs, pw))
                wleft = c->burst == 1 ? (full ? 0u : 1u) : (freecnt < (uint32_t)c->burst ? freecnt : (uint32_t)c->burst);
            if (wleft > 0) {
                if (tfull) r.overflow++;
                wire_drive(&mem[wbin & (D - 1)], t, wseq++);
                wbin = (wbin + 1) & M; wleft--; r.writes++;
                wire_drive(&wp, t, c->gray ? bin2gray(wbin) : wbin);
            }
            sync2_clock(&rq, &rp, t, &rs, &r.sp);
            clk_adv(&cw);
        } else {                                              /* ---- 읽기 도메인 에지 ---- */
            int64_t t = cr.next;
            uint32_t wsb = c->gray ? gray2bin(wq.q2) : wq.q2;
            bool empty = c->gray ? gray_empty(bin2gray(rbin), wq.q2) : (rbin == wsb);
            uint32_t level = (wsb - rbin) & M;
            bool tempty = wbin == rbin;
            r.empty_seen += empty; r.empty_true += tempty;
            r.empty_pess += empty && !tempty; r.empty_unsafe += !empty && tempty;
            if (((wbin - wsb) & M) > D) r.imp_r++;
            if (rleft == 0 && chance(&rs, pr))
                rleft = c->burst == 1 ? (empty ? 0u : 1u) : (level < (uint32_t)c->burst ? level : (uint32_t)c->burst);
            if (drain) idle = (rleft == 0 && empty) ? idle + 1 : 0; else r.redges_main++;
            if (rleft > 0) {
                if (tempty) r.underflow++;
                uint32_t slot = rbin & (D - 1);
                uint32_t got = wire_sample(&mem[slot], t, &rs, &r.sd);
                if (got == rseq) { r.ok++; rseq++; }
                else {
                    if (got == 0xFFFFFFFFu || (got & (D - 1)) != slot || got >= wseq) r.corrupt++;
                    else if (got < rseq) r.dup++;
                    else { r.lost += (long)(got - rseq); rseq = got + 1; }
                }
                rbin = (rbin + 1) & M; rleft--; r.reads++; r.reads_main += !drain;
                wire_drive(&rp, t, c->gray ? bin2gray(rbin) : rbin);
            }
            sync2_clock(&wq, &wp, t, &rs, &r.sp);
            clk_adv(&cr);
        }
    }
    return r;
}

static long errs(const fres_t *r) { return r->overflow + r->underflow + r->dup + r->lost + r->corrupt; }

static void part_b(void) {
    printf("== Part B-1: Gray 변환 전수 검사 ==\n");
    bool rt = true, one = true;
    for (uint32_t x = 0; x < 65536u; x++) if (gray2bin(bin2gray(x)) != x || bin2gray(gray2bin(x)) != x) rt = false;
    long pairs = 0;
    for (int w = 1; w <= 16; w++) {
        uint32_t m = (1u << w) - 1u;
        for (uint32_t x = 0; x <= m; x++, pairs++)
            if (__builtin_popcount(bin2gray(x) ^ bin2gray((x + 1) & m)) != 1) one = false;
    }
    printf("  round trip 0..65535, one-bit step incl. wrap for widths 1..16 (%ld pairs)\n", pairs);
    CHECK(rt);
    CHECK(one);
    CHECK(bin2gray(5) == 7 && bin2gray(8) == 12 && gray2bin(12) == 8 && gray2bin(0x8000) == 0xFFFF);

    printf("== Part B-2: Gray full/empty 공식 vs 이진 차이 정의 (전수) ==\n");
    bool fe = true; long cases = 0;
    for (int N = 1; N <= 7; N++) {
        uint32_t D = 1u << N, M = (2u << N) - 1u;
        for (uint32_t w = 0; w <= M; w++)
            for (uint32_t rr = 0; rr <= M; rr++, cases++) {
                bool fdef = ((w - rr) & M) == D, edef = w == rr;
                if (gray_full(bin2gray(w), bin2gray(rr), N) != fdef || gray_empty(bin2gray(rr), bin2gray(w)) != edef) fe = false;
            }
    }
    printf("  N=1..7, all (wptr, rptr) pairs: %ld cases\n", cases);
    CHECK(fe);

    printf("== Part B-3: 비동기 FIFO 8 깊이 (N=3), 400k 에지, 지터 ±40 ps, 포인터 비트 skew 30 ps/bit ==\n");
    printf("  %-12s %-4s %2s %7s %5s %5s %5s %5s %5s %6s %5s %5s %5s %6s %6s\n", "clocks", "ptr", "B", "writes", "ovf",
           "unf", "dup", "lost", "corr", "imp", "meta", "!F&TF", "!E&TE", "F_pess", "E_pess");
    long bin1_err = 0, bin1_imp = 0, bin4_err = 0, bin4_flow = 0, bin4_runs_bad = 0;
    for (int ci = 0; ci < NCLK; ci++)
        for (int v = 0; v < 4; v++) {
            fcfg_t c = { ci, 3, (v & 1) ? 4 : 1, v >= 2, 400000, 3u + (uint32_t)ci, 40, 30, 0, 0 };
            fres_t r = run_fifo(&c);
            printf("  %-12s %-4s %2d %7ld %5ld %5ld %5ld %5ld %5ld %6ld %5llu %5ld %5ld %5.1f%% %5.1f%%\n", CLK[ci].name,
                   c.gray ? "gray" : "bin", c.burst, r.writes, r.overflow, r.underflow, r.dup, r.lost, r.corrupt,
                   r.imp_w + r.imp_r, (unsigned long long)r.sp.meta, r.full_unsafe, r.empty_unsafe,
                   r.full_seen ? 100.0 * (double)r.full_pess / (double)r.full_seen : 0.0,
                   r.empty_seen ? 100.0 * (double)r.empty_pess / (double)r.empty_seen : 0.0);
            if (!c.gray && c.burst == 1) { bin1_err += errs(&r); bin1_imp += r.imp_w + r.imp_r; }
            if (!c.gray && c.burst == 4) { bin4_err += errs(&r); bin4_flow += r.overflow + r.underflow; bin4_runs_bad += errs(&r) > 0; }
        }
    CHECK(bin1_imp > 0);           /* 이진 포인터: 동기화된 값이 '있을 수 없는 값' 이 된다 */
    CHECK(bin1_err == 0);          /* …그러나 한 칸씩 + 플래그만 쓰면 데이터 오류로 이어지지 않는다 (풀이의 증명) */
    CHECK(bin4_flow > 0);          /* 레벨(개수)로 버스트를 정하면 overflow/underflow */
    CHECK(bin4_err > 0 && bin4_runs_bad >= 3);

    printf("== Part B-4: Gray FIFO 장기 랜덤 스트레스 (N=1,2,3,5 × B=1,4 × 클럭 4 × 시드 3) ==\n");
    long runs = 0, bad = 0, unsafe = 0, imp = 0, dmeta = 0, undeliv = 0, pmeta = 0, fpess = 0, epess = 0, ftrue = 0, etrue = 0;
    long w_total = 0;
    static const int NS[] = { 1, 2, 3, 5 };
    for (int ni = 0; ni < 4; ni++)
        for (int b = 0; b < 2; b++)
            for (int ci = 0; ci < NCLK; ci++)
                for (uint32_t s = 1; s <= 3; s++) {
                    fcfg_t c = { ci, NS[ni], b ? 4 : 1, true, 100000, 100u * s + (uint32_t)(ni * 10 + ci), 40, 30, 0, 0 };
                    fres_t r = run_fifo(&c);
                    runs++; bad += errs(&r) > 0; unsafe += r.full_unsafe + r.empty_unsafe; imp += r.imp_w + r.imp_r;
                    dmeta += (long)r.sd.meta; undeliv += !r.all_delivered; pmeta += (long)r.sp.meta;
                    fpess += r.full_pess; epess += r.empty_pess; ftrue += r.full_true; etrue += r.empty_true;
                    w_total += r.writes;
                }
    printf("  runs=%ld writes=%ld | bad runs=%ld unsafe flags=%ld impossible ptr=%ld data-meta=%ld undelivered=%ld\n",
           runs, w_total, bad, unsafe, imp, dmeta, undeliv);
    printf("  coverage: ptr meta events=%ld true-full edges=%ld true-empty edges=%ld | pessimistic full=%ld empty=%ld\n",
           pmeta, ftrue, etrue, fpess, epess);
    CHECK(bad == 0 && undeliv == 0);
    CHECK(unsafe == 0 && imp == 0);
    CHECK(dmeta == 0);             /* 읽는 칸은 포인터가 건너오기 전에 이미 안정 → 데이터 경로는 동기화기가 필요 없다 */
    CHECK(pmeta > 1000 && ftrue > 0 && etrue > 0);   /* 커버리지: 창 적중과 full/empty 가 실제로 일어났다 */
    CHECK(fpess > 0 && epess > 0); /* 플래그는 보수적: 늦게 풀릴 뿐 틀리지 않는다 */
}

/* ---------- Part C: 펄스 · 버스 ---------- */
typedef enum { PM_LEVEL, PM_EDGE, PM_TOGGLE } pmode_t;
typedef struct { long sent, recv; } pres_t;

static pres_t run_pulse(int ci, pmode_t mode, long spacing, long npulse, uint32_t seed) {
    pres_t r = { 0, 0 };
    int64_t Ts = CLK[ci].Tw, Td = CLK[ci].Tr;
    clk_t cs, cd;
    clk_init(&cs, Ts, 1000, 0, seed);
    clk_init(&cd, Td, 1000 + (int64_t)((seed * 7919u) % (uint32_t)Td), 0, seed * 31u + 7u);
    wire_t w; wire_init(&w, 1, 0, 0);
    sync2_t sy = { 0, 0 }; sstat_t st = { 0, 0, 0 };
    uint32_t rs = seed * 2654435761u | 1u, tog = 0, q3 = 0, prev = 0, lvl = 0;
    long cnt = 1, after = 0;
    while (r.sent < npulse || after < 8) {
        if (cs.next <= cd.next) {
            int64_t t = cs.next;
            bool pulse = false;
            if (r.sent < npulse && --cnt <= 0) { pulse = true; r.sent++; cnt = spacing + (long)(xs32(&rs) % 3u); }
            if (mode == PM_TOGGLE) { tog = toggle_src(tog, pulse); wire_drive(&w, t, tog); }
            else { lvl = pulse ? 1u : 0u; wire_drive(&w, t, lvl); }   /* 펄스 = 소스 1 주기 동안만 1 */
            clk_adv(&cs);
        } else {
            int64_t t = cd.next;
            sync2_clock(&sy, &w, t, &rs, &st);
            if (mode == PM_LEVEL) r.recv += sy.q2 & 1u;                      /* q2 를 그대로 펄스로 씀 */
            else if (mode == PM_EDGE) { r.recv += (sy.q2 & ~prev) & 1u; prev = sy.q2; }   /* 상승 에지 */
            else r.recv += toggle_dst(sy.q2, &q3);
            if (r.sent >= npulse) after++;
            clk_adv(&cd);
        }
    }
    return r;
}

typedef struct { long sent, recv, bad, corrupt; sstat_t sd; int64_t t_end; } hres_t;

/* 4-phase req/ack 로 16-bit 단어 n 개를 넘긴다. 데이터 버스에는 동기화기가 없다 (잡는 순간 안정이 보장되므로). */
static hres_t run_hs(int ci, long n, uint32_t seed, int64_t J) {
    hres_t r; memset(&r, 0, sizeof r);
    int64_t Ts = CLK[ci].Tw, Td = CLK[ci].Tr;
    clk_t cs, cd;
    clk_init(&cs, Ts, 1000 + J, J, seed);
    clk_init(&cd, Td, 1000 + J + (int64_t)((seed * 7919u) % (uint32_t)Td), J, seed * 31u + 7u);
    wire_t wd, wreq, wack; wire_init(&wd, 16, 40, 0); wire_init(&wreq, 1, 0, 0); wire_init(&wack, 1, 0, 0);
    sync2_t reqs = { 0, 0 }, acks = { 0, 0 };
    hs_src_t s = { HS_IDLE, 0, 0 }; hs_dst_t d = { 0 };
    uint32_t rs = seed * 2654435761u | 1u, gen = seed | 1u, chk = seed | 1u;
    uint32_t word = xs32(&gen) & 0xFFFFu;                    /* 다음에 보낼 단어 (실을 때마다 새로 만든다) */
    for (long e = 0; e < 200 * n && (r.recv < n || s.st != HS_IDLE); e++) {
        if (cs.next <= cd.next) {
            int64_t t = cs.next;
            if (hs_src_step(&s, acks.q2, r.sent < n, word)) {
                r.sent++; wire_drive(&wd, t, s.data); word = xs32(&gen) & 0xFFFFu;
            }
            wire_drive(&wreq, t, s.req);
            sync2_clock(&acks, &wack, t, &rs, NULL);
            clk_adv(&cs);
        } else {
            int64_t t = cd.next;
            if (hs_dst_step(&d, reqs.q2)) {
                uint32_t got = wire_sample(&wd, t, &rs, &r.sd), want = xs32(&chk) & 0xFFFFu;
                r.recv++; if (got != want) r.bad++;
            }
            wire_drive(&wack, t, d.ack);
            sync2_clock(&reqs, &wreq, t, &rs, NULL);
            r.t_end = t;
            clk_adv(&cd);
        }
    }
    return r;
}

/* 같은 16-bit 버스를 비트마다 2-FF 로만 넘긴다. 소스는 H 주기마다 새 단어. 목적지는 q2 가 바뀔 때마다 값을 받는다. */
static hres_t run_bus_naive(int ci, long n, uint32_t seed, long hold) {
    hres_t r; memset(&r, 0, sizeof r);
    int64_t Ts = CLK[ci].Tw, Td = CLK[ci].Tr;
    clk_t cs, cd;
    clk_init(&cs, Ts, 1000, 0, seed);
    clk_init(&cd, Td, 1000 + (int64_t)((seed * 7919u) % (uint32_t)Td), 0, seed * 31u + 7u);
    wire_t wd; wire_init(&wd, 16, 40, 0);
    sync2_t sy = { 0, 0 };
    uint32_t rs = seed * 2654435761u | 1u, words[64]; long cnt = 1, after = 0;
    uint32_t last = 0; long ri = 0;                         /* ri = 다음에 기대하는 단어 번호 */
    while (r.sent < n || after < 8) {
        if (cs.next <= cd.next) {
            int64_t t = cs.next;
            if (r.sent < n && --cnt <= 0) {
                uint32_t v; do v = xs32(&rs) & 0xFFFFu; while (v == (r.sent ? words[(r.sent - 1) % 64] : 0u));
                words[r.sent % 64] = v; wire_drive(&wd, t, v); r.sent++; cnt = hold;
            }
            clk_adv(&cs);
        } else {
            int64_t t = cd.next;
            sync2_clock(&sy, &wd, t, &rs, &r.sd);
            if (sy.q2 != last) {
                last = sy.q2; r.recv++;
                bool found = false;
                for (long k = ri; k < r.sent && k < ri + 3; k++)
                    if (words[k % 64] == last) { ri = k + 1; found = true; break; }
                if (!found) r.corrupt++;                       /* 보낸 적 없는 단어 = 비트가 섞인 값 */
            }
            if (r.sent >= n) after++;
            clk_adv(&cd);
        }
    }
    return r;
}

static void part_c(void) {
    printf("== Part C-1: 1 주기 펄스 넘기기 — q2 그대로 / q2 상승 에지 / 토글 동기화기 ==\n");
    printf("  %-12s %7s %8s %8s %8s %8s\n", "clocks", "spacing", "sent", "level", "edge", "toggle");
    for (int ci = 0; ci < 3; ci++) {
        long smin = (long)((CLK[ci].Tr + TWIN + CLK[ci].Tw - 1) / CLK[ci].Tw);   /* ⌈(Td + T_W)/Ts⌉ */
        if (smin < 1) smin = 1;
        pres_t a = run_pulse(ci, PM_LEVEL, smin, 5000, 21u + (uint32_t)ci);
        pres_t b = run_pulse(ci, PM_EDGE, smin, 5000, 21u + (uint32_t)ci);
        pres_t c = run_pulse(ci, PM_TOGGLE, smin, 5000, 21u + (uint32_t)ci);
        printf("  %-12s %7ld %8ld %8ld %8ld %8ld\n", CLK[ci].name, smin, c.sent, a.recv, b.recv, c.recv);
        CHECK(c.recv == c.sent);                      /* 토글: 간격만 지키면 하나도 안 잃는다 */
        if (ci == 1) CHECK(b.recv < b.sent / 2);      /* 빠름→느림: 1 주기 펄스는 대부분 사라진다 */
        if (ci == 2) CHECK(a.recv > 3 * a.sent);      /* 느림→빠름: q2 를 그대로 쓰면 한 펄스가 여러 개로 */
    }
    pres_t tight = run_pulse(1, PM_TOGGLE, 2, 5000, 77u);
    printf("  toggle, 200->31 MHz, spacing 2 src cycles (< Td): sent %ld recv %ld (lost %ld)\n",
           tight.sent, tight.recv, tight.sent - tight.recv);
    CHECK(tight.recv < tight.sent);                   /* 간격 규칙을 어기면 토글 둘이 서로 지운다 */

    printf("== Part C-2: 16-bit 버스 — 비트마다 2-FF vs 4-phase req/ack ==\n");
    printf("  %-12s %-20s %7s %7s %7s %7s %10s\n", "clocks", "scheme", "sent", "recv", "corrupt", "d-meta", "ns/word");
    long naive_bad = 0;
    for (int ci = 0; ci < NCLK; ci++) {
        long hold = (long)((3 * CLK[ci].Tr + CLK[ci].Tw - 1) / CLK[ci].Tw) + 1;
        hres_t nv = run_bus_naive(ci, 5000, 31u + (uint32_t)ci, hold);
        hres_t hs = run_hs(ci, 5000, 41u + (uint32_t)ci, 40);
        printf("  %-12s %-20s %7ld %7ld %7ld %7llu %10s\n", CLK[ci].name, "2-FF per bit", nv.sent, nv.recv, nv.corrupt,
               (unsigned long long)nv.sd.meta, "-");
        printf("  %-12s %-20s %7ld %7ld %7ld %7llu %10.1f\n", "", "4-phase req/ack", hs.sent, hs.recv, hs.bad,
               (unsigned long long)hs.sd.meta, hs.recv ? (double)hs.t_end / 1000.0 / (double)hs.recv : 0.0);
        naive_bad += nv.corrupt;
        CHECK(hs.recv == 5000 && hs.bad == 0 && hs.sd.meta == 0);   /* 데이터는 잡을 때 항상 안정 */
    }
    CHECK(naive_bad > 0);                             /* 비트마다 따로 동기화하면 섞인 단어가 나온다 */
}

int main(int argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "trace") == 0) {
        printf("resolves to new:\n"); trace_demo(1u);
        printf("resolves to old:\n"); trace_demo(2u);
        return 0;
    }
    part_a();
    part_b();
    part_c();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
