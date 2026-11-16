/* OS-A04 CFS 개념 구현: vruntime 과 nice 가중치 — 결정적 단일 CPU 시뮬레이터 (시간 단위 µs)
 *
 *   Part A  nice→weight 표(Linux 6.18.55 core.c 그대로) · 역가중치 고정소수점 calc_delta_fair
 *           · min-heap 런큐(vruntime 최소 선택) · sched_period/sched_slice · update_curr ·
 *           tick 선점 → nice 별 CPU 몫, 공정성 lag 상한
 *   Part B  min_vruntime 추적 · 깨어난 태스크 배치(place_entity, CFS 시절 GENTLE_FAIR_SLEEPERS)
 *           · wakeup 선점 → "오래 잔 태스크가 CPU 독점" 을 배치 有/無 로 비교
 *   Part C  펌웨어/임베디드 리눅스: SCHED_FIFO 폭주 + RT throttling(sched_rt_runtime_us) 과
 *           CFS 워치독, nice 19 로 내린 센서 태스크의 마감 초과
 *
 * 모델은 6.5 이전 CFS(sched_latency / min_granularity / wakeup_granularity) 다.
 * 6.6 부터 Linux 는 pick 을 EEVDF 로 바꿨다 — 개념은 md 5. 풀이, 구현은 practice.c 연습 3~5.
 * 한 µs 마다: ① 끝난 버스트 → 잠 ② 깨어남(release) ③ tick 이면 update_curr + 선점 검사
 *             ④ 필요하면 schedule() ⑤ curr 가 1 µs 실행.  난수·시간 측정 없음 → 결정적.
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so   (starter.c 도 같은 명령)
 */
#define _POSIX_C_SOURCE 200809L
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(e) do { g_total++; if (e) g_pass++; \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #e); } while (0)

/* ---------------- Linux 6.18.55 kernel/sched/core.c:10331 (그대로 복사) ---------------- */
static const int sched_prio_to_weight[40] = {
 /* -20 */     88761,     71755,     56483,     46273,     36291,
 /* -15 */     29154,     23254,     18705,     14949,     11916,
 /* -10 */      9548,      7620,      6100,      4904,      3906,
 /*  -5 */      3121,      2501,      1991,      1586,      1277,
 /*   0 */      1024,       820,       655,       526,       423,
 /*   5 */       335,       272,       215,       172,       137,
 /*  10 */       110,        87,        70,        56,        45,
 /*  15 */        36,        29,        23,        18,        15,
};
/* ---------------- kernel/sched/core.c:10349 — 2^32 / weight (미리 계산) ---------------- */
static const uint32_t sched_prio_to_wmult[40] = {
 /* -20 */     48388,     59856,     76040,     92818,    118348,
 /* -15 */    147320,    184698,    229616,    287308,    360437,
 /* -10 */    449829,    563644,    704093,    875809,   1099582,
 /*  -5 */   1376151,   1717300,   2157191,   2708050,   3363326,
 /*   0 */   4194304,   5237765,   6557202,   8165337,  10153587,
 /*   5 */  12820798,  15790321,  19976592,  24970740,  31350126,
 /*  10 */  39045157,  49367440,  61356676,  76695844,  95443717,
 /*  15 */ 119304647, 148102320, 186737708, 238609294, 286331153,
};
#define NICE_0_LOAD 1024u        /* scale_load_down 해상도. 64-bit 커널 내부는 1<<20 (sched.h:148,173) */
#define WMULT_SHIFT 32           /* fair.c:229 */
#define NS 1000ull               /* µs → ns */

typedef struct { uint32_t weight, inv_weight; } load_weight_t;

enum { POL_NORMAL, POL_FIFO };
enum { BEH_HOG, BEH_PERIODIC, BEH_SLEEP_THEN_HOG };
enum { PLACE_NONE, PLACE_CFS };
enum { ST_RUNNABLE, ST_SLEEPING };

typedef struct {
    int nice, policy, rt_prio, beh;
    uint32_t run_us;     /* PERIODIC: 한 주기에 필요한 CPU */
    uint32_t period_us;  /* PERIODIC: 주기 */
    uint32_t phase_us;   /* 첫 깨어남 시각 (0 이면 t=0 부터 runnable). SLEEP_THEN_HOG 는 이 시각까지 잔다 */
} spec_t;

typedef struct {
    spec_t sp; load_weight_t lw;
    uint64_t vruntime;                    /* ns, u64 — 비교는 항상 부호 있는 차이로 */
    uint64_t sum_exec_us, prev_sum_exec_us, pending_us;
    int state; bool burst_done, woke_pending;
    uint32_t burst_left; uint64_t release;
    uint64_t wait_start, wake_t, run_start;
    uint64_t max_wait_us, max_run_us, max_wake_lat_us, exec_after_us;
    uint32_t nwake, bursts, overruns;
} task_t;

#define MAXTASK 16
typedef struct { int idx[MAXTASK]; int n; } heap_t;    /* curr 는 heap 밖 (커널 트리와 같음) */

typedef struct {
    uint32_t latency_us, min_gran_us, wakeup_gran_us, tick_us;
    int place; uint64_t sim_us, vr0;
    int64_t rt_runtime_us; uint32_t rt_period_us;      /* rt_runtime_us < 0 = RUNTIME_INF */
    bool track_lag; uint64_t stat_from_us;            /* exec_after_us 를 세기 시작하는 시각 */
} cfg_t;

typedef struct {
    cfg_t c; task_t t[MAXTASK]; int n;
    heap_t rq; int curr; uint64_t now;
    uint32_t nr_cfs; uint64_t load_cfs;              /* runnable CFS 태스크(curr 포함) 수 · 가중치 합 */
    uint64_t min_vruntime; bool need_resched;
    uint64_t rt_time_us; bool rt_throttled;
    uint32_t min_vr_back, switches; double max_lag_us; uint64_t max_spread_ns;
} sim_t;

static bool vr_before(uint64_t a, uint64_t b) { return (int64_t)(a - b) < 0; }   /* 래핑 안전 */

/* ======================= Part A ======================= */

/* TODO A1: nice(−20..19) → weight 와 inv_weight. 범위 밖 nice 는 −20 / 19 로 자른다. */
static void set_load_weight(load_weight_t *lw, int nice)
{
    (void)nice; (void)sched_prio_to_weight; (void)sched_prio_to_wmult;
    lw->weight = NICE_0_LOAD;            /* TODO: 표에서 */
    lw->inv_weight = 4194304u;
}

/* TODO A2: delta_ns × NICE_0_LOAD / weight — 나눗셈 없이 inv_weight 곱 + 시프트 (fair.c:260 __calc_delta).
 * weight == NICE_0_LOAD 이면 그대로 (fair.c:290 calc_delta_fair). */
static uint64_t calc_delta_fair(uint64_t delta_ns, const load_weight_t *lw)
{
    (void)lw;
    return delta_ns;                     /* TODO: × 1024 / weight */
}

/* 하네스: a 가 b 보다 먼저인가 (vruntime 작은 쪽, 같으면 번호 작은 쪽) */
static bool entity_before(const task_t *t, int a, int b)
{
    if (t[a].vruntime != t[b].vruntime) return vr_before(t[a].vruntime, t[b].vruntime);
    return a < b;
}

/* TODO A3: 이진 min-heap. push = 끝에 넣고 위로, pop = 루트를 빼고 마지막을 루트로 → 아래로. 비면 −1. */
static void heap_push(heap_t *h, const task_t *t, int i)
{
    (void)h; (void)t; (void)i;           /* TODO */
}

static int heap_pop_min(heap_t *h, const task_t *t)
{
    (void)h; (void)t;
    return -1;                           /* TODO */
}

/* 하네스: heap 불변식 위반 수 (부모가 자식보다 뒤면 위반) */
static int heap_check(const heap_t *h, const task_t *t)
{
    int bad = 0;
    for (int k = 1; k < h->n; k++)
        if (entity_before(t, h->idx[k], h->idx[(k - 1) / 2])) bad++;
    return bad;
}

/* TODO A4: 튜너블 배율 = 1 + ilog2(min(ncpu, 8))  (fair.c:192 get_update_sysctl_factor, LOG 기본) */
static unsigned sysctl_factor(unsigned ncpu)
{
    (void)ncpu;
    return 1;                            /* TODO */
}

/* TODO A4: 주기 = nr ≤ nr_latency(= latency / min_gran) 이면 latency, 넘으면 nr × min_gran (CFS 시절 __sched_period) */
static uint64_t sched_period_us(const cfg_t *c, uint32_t nr)
{
    (void)nr;
    return c->latency_us;                /* TODO: nr 이 많으면 늘린다 */
}

/* TODO A4: 내 몫 = period(nr_cfs) × w_i / Σw  (CFS 시절 sched_slice, 단일 계층) */
static uint64_t sched_slice_us(const sim_t *s, int i)
{
    (void)s; (void)i;
    return 0;                            /* TODO */
}

static void update_min_vruntime(sim_t *s);

/* TODO A5: curr(CFS) 가 쌓아 둔 실행 시간(pending_us)을 vruntime 에 반영 (fair.c:1314
 *   curr->vruntime += calc_delta_fair(delta_exec, curr)) 하고 min_vruntime 갱신. */
static void update_curr(sim_t *s)
{
    (void)s;                             /* TODO */
}

/* TODO A5: tick 선점 검사 (CFS 시절 check_preempt_tick). 바꿔야 하면 true.
 *   ran = 이번에 고른 뒤 실행한 시간. ran > ideal(sched_slice) → true.
 *   ran < min_gran → false.  leftmost 가 있고 curr.vr − left.vr > ideal(ns) → true. */
static bool check_preempt_tick(sim_t *s)
{
    (void)s;
    return false;                        /* TODO */
}

/* ======================= Part B ======================= */

/* TODO B1: min_vruntime = max(min_vruntime, min(curr.vr (CFS·runnable 일 때), leftmost.vr)) — 단조 증가 */
static void update_min_vruntime(sim_t *s)
{
    (void)s;                             /* TODO */
}

/* TODO B2: 깨어난 태스크 배치. PLACE_NONE: 그대로.
 *   PLACE_CFS: vr = min_vruntime − latency/2 (GENTLE_FAIR_SLEEPERS) ; se.vr = max(se.vr, vr) */
static void place_entity(sim_t *s, int i)
{
    (void)s; (void)i;                    /* TODO */
}

/* TODO B3: wakeup 선점 (CFS 시절 wakeup_preempt_entity == 1).
 *   vdiff = curr.vr − woken.vr ; gran = calc_delta_fair(wakeup_gran, woken) ; vdiff > gran 이면 true */
static bool wakeup_preempt(sim_t *s, int w)
{
    (void)s; (void)w;
    return false;                        /* TODO */
}

/* ======================= Part C ======================= */

/* TODO C1: 클래스 순서. throttle 이 아니면 runnable SCHED_FIFO 중 rt_prio 최대(같으면 번호 작은 쪽),
 *   없으면 CFS heap 의 최소 vruntime (heap_pop_min). 아무도 없으면 −1. */
static int pick_next_task(sim_t *s)
{
    (void)s;
    return -1;                           /* TODO */
}

/* TODO C2: RT 대역 회계 (sched_rt_runtime_us / sched_rt_period_us). 매 µs 끝에 부른다.
 *   FIFO 가 돌았으면 rt_time++ ; 제한이 있고(rt_runtime ≥ 0) rt_time ≥ runtime 이면 throttled + 재스케줄.
 *   (now+1) 이 주기 경계면 rt_time = 0, throttled 였으면 풀고 재스케줄. */
static void rt_account(sim_t *s, bool fifo_ran)
{
    (void)s; (void)fifo_ran;             /* TODO */
}

/* ======================= 하네스: 시뮬레이션 엔진 ======================= */

static void stop_running(sim_t *s, int i)
{
    uint64_t r = s->now - s->t[i].run_start;
    if (r > s->t[i].max_run_us) s->t[i].max_run_us = r;
}

static void enqueue(sim_t *s, int i, bool wakeup)
{
    task_t *t = &s->t[i];
    t->state = ST_RUNNABLE; t->wait_start = s->now;
    if (wakeup) { t->wake_t = s->now; t->woke_pending = true; t->nwake++; }
    if (t->sp.beh == BEH_PERIODIC) t->burst_left = t->sp.run_us;
    if (t->sp.policy == POL_FIFO) {
        if (s->curr < 0 || s->t[s->curr].sp.policy == POL_NORMAL ||
            t->sp.rt_prio > s->t[s->curr].sp.rt_prio) s->need_resched = true;
        return;
    }
    if (wakeup) { update_curr(s); place_entity(s, i); }
    heap_push(&s->rq, s->t, i);
    s->nr_cfs++; s->load_cfs += t->lw.weight;
    if (s->curr < 0) s->need_resched = true;
    else if (wakeup && s->t[s->curr].sp.policy == POL_NORMAL && wakeup_preempt(s, i)) s->need_resched = true;
}

static void dequeue_sleep(sim_t *s, int i)
{
    task_t *t = &s->t[i];
    update_curr(s);
    stop_running(s, i);
    t->state = ST_SLEEPING;
    s->curr = -1;
    if (t->sp.policy == POL_NORMAL) { s->nr_cfs--; s->load_cfs -= t->lw.weight; update_min_vruntime(s); }
    s->need_resched = true;
}

static void schedule(sim_t *s)
{
    int prev = s->curr;
    if (prev >= 0) {
        if (s->t[prev].sp.policy == POL_NORMAL) { update_curr(s); heap_push(&s->rq, s->t, prev); }
        s->t[prev].wait_start = s->now;
    }
    s->curr = -1;
    int next = pick_next_task(s);
    if (prev >= 0 && next != prev) stop_running(s, prev);
    s->curr = next;
    if (next >= 0) {
        task_t *t = &s->t[next];
        if (next != prev) {
            s->switches++;
            t->run_start = s->now;
            uint64_t w = s->now - t->wait_start;
            if (w > t->max_wait_us) t->max_wait_us = w;
        }
        if (t->woke_pending) {
            uint64_t l = s->now - t->wake_t;
            if (l > t->max_wake_lat_us) t->max_wake_lat_us = l;
            t->woke_pending = false;
        }
        t->prev_sum_exec_us = t->sum_exec_us;                 /* set_next_entity */
    }
    s->need_resched = false;
}

static void observe_tick(sim_t *s, uint64_t *last_min)
{
    if (vr_before(s->min_vruntime, *last_min)) s->min_vr_back++;
    *last_min = s->min_vruntime;
    if (!s->c.track_lag || s->load_cfs == 0) return;
    uint64_t lo = 0, hi = 0; bool first = true;
    for (int i = 0; i < s->n; i++) {
        const task_t *t = &s->t[i];
        if (t->sp.policy != POL_NORMAL || t->state != ST_RUNNABLE) continue;
        double ideal = (double)s->now * t->lw.weight / (double)s->load_cfs;
        double lag = (double)t->sum_exec_us - ideal;
        if (lag < 0) lag = -lag;
        if (lag > s->max_lag_us) s->max_lag_us = lag;
        uint64_t v = t->vruntime;
        if (first || vr_before(v, lo)) lo = v;
        if (first || vr_before(hi, v)) hi = v;
        first = false;
    }
    if (hi - lo > s->max_spread_ns) s->max_spread_ns = hi - lo;
}

static void sim_init(sim_t *s, const cfg_t *c, const spec_t *sp, int n)
{
    memset(s, 0, sizeof *s);
    s->c = *c; s->n = n; s->curr = -1; s->min_vruntime = c->vr0;
    for (int i = 0; i < n; i++) {
        task_t *t = &s->t[i];
        t->sp = sp[i];
        set_load_weight(&t->lw, sp[i].nice);
        t->vruntime = c->vr0;
        t->release = sp[i].phase_us;
        if (sp[i].phase_us > 0) t->state = ST_SLEEPING;
        else enqueue(s, i, false);
    }
}

static char *g_trace; static uint64_t g_trace_from, g_trace_len;   /* demo_trace.c 용 타임라인 (테스트는 안 씀) */

static void simulate(sim_t *s)
{
    uint64_t last_min = s->min_vruntime;
    for (s->now = 0; s->now < s->c.sim_us; s->now++) {
        for (int i = 0; i < s->n; i++) {                       /* ① 끝난 버스트 */
            task_t *t = &s->t[i];
            if (!t->burst_done) continue;
            t->burst_done = false; t->bursts++;
            t->release += t->sp.period_us;
            if (t->release <= s->now) { t->overruns++; t->burst_left = t->sp.run_us; }
            else dequeue_sleep(s, i);
        }
        for (int i = 0; i < s->n; i++)                         /* ② 깨어남 */
            if (s->t[i].state == ST_SLEEPING && s->t[i].release == s->now) enqueue(s, i, true);
        if (s->now > 0 && s->now % s->c.tick_us == 0) {        /* ③ scheduler tick */
            if (s->curr >= 0 && s->t[s->curr].sp.policy == POL_NORMAL) {
                update_curr(s);
                if (s->nr_cfs > 1 && check_preempt_tick(s)) s->need_resched = true;
            }
            observe_tick(s, &last_min);
        }
        if (s->need_resched || s->curr < 0) schedule(s);       /* ④ */
        bool fifo_ran = false;                                 /* ⑤ 1 µs 실행 */
        if (s->curr >= 0) {
            task_t *t = &s->t[s->curr];
            t->sum_exec_us++;
            if (s->now >= s->c.stat_from_us) t->exec_after_us++;
            if (t->sp.policy == POL_NORMAL) t->pending_us++; else fifo_ran = true;
            if (t->sp.beh == BEH_PERIODIC && --t->burst_left == 0) t->burst_done = true;
        }
        if (g_trace && s->now >= g_trace_from && s->now - g_trace_from < g_trace_len)
            g_trace[s->now - g_trace_from] = s->curr >= 0 ? (char)('A' + s->curr) : '.';
        rt_account(s, fifo_ran);
    }
    if (s->curr >= 0) { update_curr(s); stop_running(s, s->curr); }
}

/* ---------------- 테스트 도우미 ---------------- */
static uint32_t xs = 2463534242u;
static uint32_t xorshift32(void) { xs ^= xs << 13; xs ^= xs >> 17; xs ^= xs << 5; return xs; }

static cfg_t cfs_defaults(void)   /* CFS 시절 기본값, factor 1 (단일 CPU) */
{
    cfg_t c = { .latency_us = 6000, .min_gran_us = 750, .wakeup_gran_us = 1000, .tick_us = 1000,
                .place = PLACE_CFS, .sim_us = 3000000, .vr0 = 0,
                .rt_runtime_us = -1, .rt_period_us = 1000000 };
    return c;
}

static sim_t S;   /* 큰 구조체는 정적으로 */

static double share(const sim_t *s, int i) { return (double)s->t[i].sum_exec_us / (double)s->c.sim_us; }
static double absd(double x) { return x < 0 ? -x : x; }

static bool run_hogs(const int *nices, int n, cfg_t c)
{
    spec_t sp[MAXTASK];
    memset(sp, 0, sizeof sp);
    for (int i = 0; i < n; i++) { sp[i].nice = nices[i]; sp[i].beh = BEH_HOG; }
    c.track_lag = true;
    sim_init(&S, &c, sp, n);
    simulate(&S);
    double W = 0;
    for (int i = 0; i < n; i++) W += S.t[i].lw.weight;
    bool ok = true;
    for (int i = 0; i < n; i++)
        if (absd(share(&S, i) - S.t[i].lw.weight / W) > 0.01) ok = false;
    printf("  nice {");
    for (int i = 0; i < n; i++) printf("%s%d", i ? "," : "", nices[i]);
    printf("}: share");
    for (int i = 0; i < n; i++) printf(" %.4f(ideal %.4f)", share(&S, i), S.t[i].lw.weight / W);
    printf("  max|lag| %.0f us, spread %.2f ms, switches %u\n",
           S.max_lag_us, S.max_spread_ns / 1e6, S.switches);
    return ok;
}

int main(void)
{
    /* ---------- Part A: 가중치 표 ---------- */
    load_weight_t lw;
    set_load_weight(&lw, 0);   CHECK(lw.weight == 1024 && lw.inv_weight == 4194304u);
    set_load_weight(&lw, -20); CHECK(lw.weight == 88761);
    set_load_weight(&lw, 19);  CHECK(lw.weight == 15 && lw.inv_weight == 286331153u);
    set_load_weight(&lw, 5);   CHECK(lw.weight == 335);
    set_load_weight(&lw, 99);  CHECK(lw.weight == 15);               /* 범위 밖 → 19 로 자름 */
    {
        int bad_ratio = 0, bad_inv = 0;
        for (int n = -20; n < 19; n++) {
            load_weight_t a, b; set_load_weight(&a, n); set_load_weight(&b, n + 1);
            double r = (double)a.weight / b.weight;
            if (r < 1.18 || r > 1.32) bad_ratio++;                    /* "nice 1 = 약 1.25 배" */
        }
        for (int n = -20; n <= 19; n++) {
            load_weight_t a; set_load_weight(&a, n);
            uint64_t prod = (uint64_t)a.weight * a.inv_weight, two32 = 1ull << 32;
            uint64_t diff = prod > two32 ? prod - two32 : two32 - prod;
            if (diff >= a.weight) bad_inv++;                          /* inv = floor(2^32 / w) 근사 */
        }
        CHECK(bad_ratio == 0);
        CHECK(bad_inv == 0);
    }

    /* ---------- Part A: calc_delta_fair (역가중치 고정소수점) ---------- */
    set_load_weight(&lw, 0);  CHECK(calc_delta_fair(1234567, &lw) == 1234567);
    set_load_weight(&lw, 5);  CHECK(calc_delta_fair(1000000, &lw) == 3056716);  /* 1e6·1024/335 = 3056716.4 */
    set_load_weight(&lw, -5); CHECK(calc_delta_fair(1000000, &lw) == 328099);   /* 1e6·1024/3121 = 328099.97 */
    {
        int bad = 0;
        const uint64_t deltas[] = { 1, 999, 1000000, 6000000, 123456789, 5000000000ull };
        for (int n = -20; n <= 19; n++)
            for (size_t k = 0; k < sizeof deltas / sizeof deltas[0]; k++) {
                load_weight_t a; set_load_weight(&a, n);
                double exact = (double)deltas[k] * 1024.0 / a.weight;
                double got = (double)calc_delta_fair(deltas[k], &a);
                if (absd(got - exact) > 1.0 + exact * 2e-6) bad++;
            }
        CHECK(bad == 0);
    }

    /* ---------- Part A: min-heap 런큐 단독 테스트 ---------- */
    {
        task_t tk[MAXTASK]; memset(tk, 0, sizeof tk);
        heap_t h = { .n = 0 };
        bool in[MAXTASK] = { false };
        int mism = 0, inv = 0, ops = 0;
        for (int step = 0; step < 20000; step++) {
            int i = (int)(xorshift32() % MAXTASK);
            if (!in[i] && (xorshift32() & 1)) {
                tk[i].vruntime = xorshift32() % 50;                 /* 일부러 동률을 많이 */
                heap_push(&h, tk, i); in[i] = true; ops++;
            } else {
                int want = -1;
                for (int j = 0; j < MAXTASK; j++)
                    if (in[j] && (want < 0 || entity_before(tk, j, want))) want = j;
                int got = heap_pop_min(&h, tk);
                if (got != want) mism++;
                if (got >= 0) in[got] = false;
                ops++;
            }
            inv += heap_check(&h, tk);
        }
        CHECK(ops == 20000 && mism == 0);
        CHECK(inv == 0);
        heap_t e = { .n = 0 };
        CHECK(heap_pop_min(&e, tk) == -1);
        tk[3].vruntime = tk[7].vruntime = 10; tk[5].vruntime = 11;
        heap_push(&e, tk, 7); heap_push(&e, tk, 5); heap_push(&e, tk, 3);
        CHECK(heap_pop_min(&e, tk) == 3 && heap_pop_min(&e, tk) == 7 && heap_pop_min(&e, tk) == 5);
    }

    /* ---------- Part A: 튜너블 배율 · period · slice ---------- */
    CHECK(sysctl_factor(1) == 1 && sysctl_factor(2) == 2 && sysctl_factor(4) == 3 &&
          sysctl_factor(6) == 3 && sysctl_factor(8) == 4 && sysctl_factor(12) == 4);
    CHECK(700u * sysctl_factor(6) == 2100u);         /* 6.18 base_slice: 0.7 ms × 3 (VM 6 CPU) */
    {
        cfg_t c = cfs_defaults();
        CHECK(sched_period_us(&c, 1) == 6000 && sched_period_us(&c, 8) == 6000);
        CHECK(sched_period_us(&c, 9) == 6750 && sched_period_us(&c, 20) == 15000);
        spec_t sp[2] = { { .nice = 0 }, { .nice = 5 } };
        sim_init(&S, &c, sp, 2);
        CHECK(sched_slice_us(&S, 0) == 4520 && sched_slice_us(&S, 1) == 1479);
    }

    /* ---------- Part A: nice 별 CPU 몫 + lag 상한 ---------- */
    {
        cfg_t c = cfs_defaults();
        const int n00[] = { 0, 0 }, n05[] = { 0, 5 }, n3[] = { -5, 0, 5 }, n4[] = { 0, 0, 0, 0 };
        CHECK(run_hogs(n00, 2, c) && absd(share(&S, 0) - 0.5) <= 0.005);
        double lag_bound = (double)sched_period_us(&c, 2) + c.tick_us;
        CHECK(S.max_lag_us <= lag_bound && S.min_vr_back == 0);
        CHECK(run_hogs(n05, 2, c));
        double ratio = share(&S, 0) / share(&S, 1);
        CHECK(ratio > 1024.0 / 335 * 0.97 && ratio < 1024.0 / 335 * 1.03);
        CHECK(S.max_lag_us <= (double)sched_period_us(&c, 2) + c.tick_us && S.min_vr_back == 0);
        CHECK(run_hogs(n3, 3, c));
        CHECK(S.max_lag_us <= (double)sched_period_us(&c, 3) + c.tick_us && S.min_vr_back == 0);
        uint64_t sb = 0;                              /* vruntime 퍼짐 상한: 가장 가벼운 태스크의 (slice+tick) 를 vtime 으로 */
        for (int i = 0; i < 3; i++) {
            uint64_t v = calc_delta_fair((sched_slice_us(&S, i) + 2u * c.tick_us) * NS, &S.t[i].lw);
            if (v > sb) sb = v;
        }
        CHECK(S.max_spread_ns <= sb);
        CHECK(run_hogs(n4, 4, c) && S.max_lag_us <= (double)sched_period_us(&c, 4) + c.tick_us);
    }

    /* ---------- Part B: min_vruntime · place_entity · wakeup 선점 (손으로 만든 상태) ---------- */
    {
        cfg_t c = cfs_defaults();
        spec_t sp[3] = { { .nice = 0 }, { .nice = 0 }, { .nice = 5, .beh = BEH_SLEEP_THEN_HOG, .phase_us = 1 } };
        sim_init(&S, &c, sp, 3);
        S.t[0].vruntime = 50000000; S.t[1].vruntime = 40000000;
        S.rq.n = 0; heap_push(&S.rq, S.t, 0); heap_push(&S.rq, S.t, 1);
        S.min_vruntime = 30000000;
        update_min_vruntime(&S);
        CHECK(S.min_vruntime == 40000000);                 /* curr 없음 → leftmost */
        S.curr = heap_pop_min(&S.rq, S.t);                 /* 1 이 curr, 0 이 leftmost */
        S.t[1].vruntime = 45000000; update_min_vruntime(&S);
        CHECK(S.curr == 1 && S.min_vruntime == 45000000);  /* min(curr 45, left 50) */
        S.t[1].vruntime = 70000000; update_min_vruntime(&S);
        CHECK(S.min_vruntime == 50000000);                 /* min(70, 50) */
        S.min_vruntime = 60000000; update_min_vruntime(&S);
        CHECK(S.min_vruntime == 60000000);                 /* 절대 뒤로 안 감 */

        S.t[2].vruntime = 0; place_entity(&S, 2);
        CHECK(S.t[2].vruntime == 60000000 - 3000000);      /* 오래 잔 태스크 → min_vr − latency/2 */
        S.t[2].vruntime = 59000000; place_entity(&S, 2);
        CHECK(S.t[2].vruntime == 59000000);                /* 이미 앞에 있으면 그대로 (max) */
        S.c.place = PLACE_NONE; S.t[2].vruntime = 0; place_entity(&S, 2);
        CHECK(S.t[2].vruntime == 0);

        S.t[1].vruntime = 70000000;
        S.t[2].vruntime = 70000000 - 3000000;              /* nice 5 의 gran = 1 ms × 1024/335 ≈ 3.06 ms */
        CHECK(!wakeup_preempt(&S, 2));
        S.t[2].vruntime = 70000000 - 3100000;
        CHECK(wakeup_preempt(&S, 2));
        S.t[0].vruntime = 70000000 - 1000000;              /* nice 0: gran 1 ms, vdiff == gran → 선점 안 함 */
        CHECK(!wakeup_preempt(&S, 0));
    }

    /* ---------- Part B: 1 초 잔 태스크 C — 배치 有 vs 無 ---------- */
    {
        spec_t sp[3] = { { .nice = 0 }, { .nice = 0 },
                         { .nice = 0, .beh = BEH_SLEEP_THEN_HOG, .phase_us = 1000000 } };
        cfg_t c = cfs_defaults(); c.sim_us = 2000000; c.stat_from_us = 1000000;
        sim_init(&S, &c, sp, 3); simulate(&S);
        uint64_t runC = S.t[2].max_run_us, waitA = S.t[0].max_wait_us, latC = S.t[2].max_wake_lat_us;
        double shC = S.t[2].exec_after_us / 1e6;
        printf("  place=CFS : C max run %llu us, A max wait %llu us, C wake lat %llu us, C share(1-2s) %.3f\n",
               (unsigned long long)runC, (unsigned long long)waitA, (unsigned long long)latC, shC);
        uint64_t bound = c.latency_us / 2 + sched_period_us(&c, 3) + 2u * c.tick_us;
        CHECK(S.t[2].nwake == 1 && runC > 0 && runC <= bound && waitA <= bound);
        CHECK(absd(shC - 1.0 / 3) < 0.02 && S.min_vr_back == 0);
        CHECK(latC <= c.tick_us);                           /* 깨어나자마자 선점 (wakeup preempt) */

        c.place = PLACE_NONE;
        sim_init(&S, &c, sp, 3); simulate(&S);
        printf("  place=NONE: C max run %llu us, A max wait %llu us, C share(1-2s) %.3f\n",
               (unsigned long long)S.t[2].max_run_us, (unsigned long long)S.t[0].max_wait_us,
               S.t[2].exec_after_us / 1e6);
        CHECK(S.t[2].max_run_us >= 400000 && S.t[0].max_wait_us >= 400000);   /* 독점 · 기아 */
        CHECK(S.t[2].exec_after_us > 600000);               /* 1~2 초 구간의 60 % 이상을 C 가 가져감 */
    }

    /* ---------- Part C: SCHED_FIFO 폭주 + RT throttling + CFS 워치독 ---------- */
    {
        spec_t sp[3] = { { .policy = POL_FIFO, .rt_prio = 50, .beh = BEH_HOG },                 /* 폭주 RT */
                         { .nice = 0, .beh = BEH_PERIODIC, .run_us = 200, .period_us = 100000 },/* 워치독 */
                         { .nice = 0, .beh = BEH_HOG } };                                       /* 로거 */
        cfg_t c = cfs_defaults();
        sim_init(&S, &c, sp, 3); simulate(&S);
        printf("  RT unlimited : fifo share %.3f, watchdog bursts %u\n", share(&S, 0), S.t[1].bursts);
        CHECK(share(&S, 0) > 0.999 && S.t[1].bursts == 0 && S.t[2].sum_exec_us == 0);   /* CFS 전부 굶음 */

        c.rt_runtime_us = 950000; c.rt_period_us = 1000000;            /* VM 의 실제 기본값 */
        sim_init(&S, &c, sp, 3); simulate(&S);
        printf("  RT 950/1000ms: fifo share %.3f, watchdog bursts %u overruns %u max wake lat %llu us\n",
               share(&S, 0), S.t[1].bursts, S.t[1].overruns, (unsigned long long)S.t[1].max_wake_lat_us);
        CHECK(absd(share(&S, 0) - 0.95) < 0.002);
        CHECK(S.t[1].bursts >= 27 && S.t[1].max_wake_lat_us >= 800000);   /* 처리량은 됐지만 0.8 초+ 늦음 */

        c.rt_runtime_us = 95000; c.rt_period_us = 100000;              /* 같은 95 %, 주기만 1/10 */
        sim_init(&S, &c, sp, 3); simulate(&S);
        printf("  RT 95/100ms  : fifo share %.3f, watchdog bursts %u max wake lat %llu us\n",
               share(&S, 0), S.t[1].bursts, (unsigned long long)S.t[1].max_wake_lat_us);
        CHECK(absd(share(&S, 0) - 0.95) < 0.002 && S.t[1].max_wake_lat_us <= 100000 && S.t[1].bursts >= 29);
    }

    /* ---------- Part C: nice 는 "몫" 이다 — 센서 태스크를 nice 19 로 내리면 ---------- */
    {
        spec_t sp[4] = { { .nice = 0, .beh = BEH_PERIODIC, .run_us = 1500, .period_us = 10000 },  /* 센서 15 % */
                         { .nice = 0 }, { .nice = 0 }, { .nice = 10 } };                         /* 로거·압축·OTA */
        cfg_t c = cfs_defaults();
        sim_init(&S, &c, sp, 4); simulate(&S);
        printf("  sensor nice 0 : bursts %u overruns %u max wake lat %llu us share %.3f\n",
               S.t[0].bursts, S.t[0].overruns, (unsigned long long)S.t[0].max_wake_lat_us, share(&S, 0));
        CHECK(S.t[0].overruns == 0 && S.t[0].bursts >= 299);
        sp[0].nice = 19;
        sim_init(&S, &c, sp, 4); simulate(&S);
        printf("  sensor nice 19: bursts %u overruns %u max wake lat %llu us share %.3f\n",
               S.t[0].bursts, S.t[0].overruns, (unsigned long long)S.t[0].max_wake_lat_us, share(&S, 0));
        CHECK(S.t[0].overruns > 0 && share(&S, 0) < 0.15);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
