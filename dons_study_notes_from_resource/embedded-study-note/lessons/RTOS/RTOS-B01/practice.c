/* RTOS-B01 추가 연습 — 한 번에 검증
 *
 *   solution.c 와 같은 시뮬레이션 시계 + 협력형 스케줄러 (자기 완결, 공유 파일 없음).
 *   연습 1  순진한 비교 now >= next 가 tick wrap 에서 하는 일: 일찍 몰아 실행 / 작업이 멈춤
 *   연습 2  next_run = now + period (잘못) vs next_run += period: 10 초 동안의 드리프트
 *   연습 3  지연 상한 "가장 긴 다른 작업 - 1": 위상(offset)을 바꾼 예 + 무작위 300 시나리오
 *   연습 4  워치독(IWDG 20 ms): 메인 루프에서 kick → 최대 kick 간격 = 가장 긴 한 번 호출
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ======================= 시뮬레이션 시계 (solution.c 와 같음, 타임라인 생략) ======================= */
#define MAX_JOBS 4
typedef enum { JOB_DONE = 0, JOB_MORE = 1 } job_ret_t;
typedef job_ret_t (*job_fn)(void *ctx);

typedef struct job {
    const char *name;
    job_fn   fn;
    void    *ctx;
    uint32_t period, next_run;
    bool     in_progress;
    uint32_t runs, skipped, lat_min, lat_max, ivl_min, ivl_max, last_start, max_step, early;
} job_t;

typedef bool (*due_fn)(uint32_t now, uint32_t t);
typedef struct {
    job_t  job[MAX_JOBS];
    int    n;
    bool   skip_missed;
    bool   anchor_now;          /* 연습 2: true 면 next_run = now + period (잘못된 방식) */
    due_fn due;                 /* 연습 1: 비교 함수 교체 */
} sched_t;

static uint32_t g_now;
static void sim_work(uint32_t ms) { g_now += ms; }

static void trace_release(job_t *j, uint32_t release) {
    if ((int32_t)(g_now - release) < 0) j->early++;
    uint32_t lat = g_now - release;
    if (j->runs == 0) { j->lat_min = j->lat_max = lat; j->ivl_min = UINT32_MAX; j->ivl_max = 0; }
    else {
        uint32_t ivl = g_now - j->last_start;
        if (lat < j->lat_min) j->lat_min = lat;
        if (lat > j->lat_max) j->lat_max = lat;
        if (ivl < j->ivl_min) j->ivl_min = ivl;
        if (ivl > j->ivl_max) j->ivl_max = ivl;
    }
    j->last_start = g_now; j->runs++;
}
static job_ret_t job_call(job_t *j) {
    uint32_t t = g_now;
    job_ret_t r = j->fn(j->ctx);
    if (g_now - t > j->max_step) j->max_step = g_now - t;
    return r;
}

typedef struct { uint32_t cost; uint32_t count; } work_t;
static job_ret_t job_work(void *ctx) { work_t *w = ctx; sim_work(w->cost); w->count++; return JOB_DONE; }

#define TELEM_FORMAT_MS 2
#define TELEM_CHUNKS    7
#define TELEM_CHUNK_MS  4
#define TELEM_TOTAL_MS  (TELEM_FORMAT_MS + TELEM_CHUNKS * TELEM_CHUNK_MS)
enum { TS_FORMAT, TS_SEND };
typedef struct { int state; int chunk; uint32_t frames; uint32_t work_ms; } telem_sm_t;

/* ---- solution.c 의 답 (연습용 스위치 2개 추가) ---- */
static bool time_due(uint32_t now, uint32_t t) { return (int32_t)(now - t) >= 0; }
static bool naive_due(uint32_t now, uint32_t t) { return now >= t; }        /* 연습 1 의 나쁜 예 */

static int sched_add(sched_t *s, const char *name, job_fn fn, void *ctx, uint32_t period, uint32_t offset) {
    if (s->n >= MAX_JOBS || period == 0 || !fn) return -1;
    job_t *j = &s->job[s->n];
    memset(j, 0, sizeof *j);
    j->name = name; j->fn = fn; j->ctx = ctx; j->period = period;
    j->next_run = g_now + offset;
    return s->n++;
}
static int sched_dispatch(sched_t *s) {
    due_fn due = s->due ? s->due : time_due;
    for (int i = 0; i < s->n; i++) {
        job_t *j = &s->job[i];
        if (j->in_progress) { j->in_progress = (job_call(j) == JOB_MORE); return i; }
        if (due(g_now, j->next_run)) {
            trace_release(j, j->next_run);
            if (s->anchor_now) j->next_run = g_now + j->period;     /* 연습 2: 늦은 만큼 영원히 밀린다 */
            else               j->next_run += j->period;
            if (s->skip_missed)
                while (due(g_now, j->next_run)) { j->next_run += j->period; j->skipped++; }
            j->in_progress = (job_call(j) == JOB_MORE);
            return i;
        }
    }
    return -1;
}
static job_ret_t telem_sm_step(void *ctx) {
    telem_sm_t *t = ctx;
    switch (t->state) {
    case TS_FORMAT:
        sim_work(TELEM_FORMAT_MS); t->work_ms += TELEM_FORMAT_MS;
        t->chunk = 0; t->state = TS_SEND;
        return JOB_MORE;
    case TS_SEND:
    default:
        sim_work(TELEM_CHUNK_MS); t->work_ms += TELEM_CHUNK_MS;
        if (++t->chunk == TELEM_CHUNKS) { t->frames++; t->state = TS_FORMAT; return JOB_DONE; }
        return JOB_MORE;
    }
}

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

enum { CFG_BASE, CFG_LONG, CFG_SPLIT };
typedef struct {
    int cfg; bool skip, anchor; due_fn due; uint32_t start, duration;
    uint32_t c_sensor, c_control, c_telem;           /* 0 이면 기본값 1 / 4 / (2 or 30) */
    uint32_t p_control, p_telem;                     /* 0 이면 50 / 100 */
    uint32_t off_control, off_telem;
    uint32_t wdt_timeout;                            /* 연습 4: 0 이면 워치독 없음 */
} scen_t;
typedef struct { job_t j[3]; uint32_t wdt_gap_max, wdt_resets; } out_t;

static out_t run(const scen_t *sc) {
    static work_t ws, wc, wt; static telem_sm_t tsm;
    g_now = sc->start;
    ws = (work_t){ sc->c_sensor ? sc->c_sensor : 1, 0 };
    wc = (work_t){ sc->c_control ? sc->c_control : 4, 0 };
    wt = (work_t){ sc->c_telem ? sc->c_telem : (sc->cfg == CFG_LONG ? TELEM_TOTAL_MS : 2), 0 };
    memset(&tsm, 0, sizeof tsm);
    sched_t s; memset(&s, 0, sizeof s);
    s.skip_missed = sc->skip; s.anchor_now = sc->anchor; s.due = sc->due;
    sched_add(&s, "SENSOR", job_work, &ws, 10, 0);
    sched_add(&s, "CONTROL", job_work, &wc, sc->p_control ? sc->p_control : 50, sc->off_control);
    if (sc->cfg == CFG_SPLIT) sched_add(&s, "TELEM", telem_sm_step, &tsm, sc->p_telem ? sc->p_telem : 100, sc->off_telem);
    else                      sched_add(&s, "TELEM", job_work, &wt, sc->p_telem ? sc->p_telem : 100, sc->off_telem);
    out_t o; memset(&o, 0, sizeof o);
    uint32_t last_kick = g_now;
    long guard = 0;
    while (g_now - sc->start < sc->duration && guard++ < 4000000) {
        if (sched_dispatch(&s) < 0) g_now += 1;              /* idle 1 tick */
        if (sc->wdt_timeout) {                               /* 메인 루프 한 바퀴마다 IWDG kick */
            uint32_t gap = g_now - last_kick;
            if (gap > o.wdt_gap_max) o.wdt_gap_max = gap;
            if (gap > sc->wdt_timeout) o.wdt_resets++;       /* 실제 보드라면 여기서 리셋 */
            last_kick = g_now;
        }
    }
    memcpy(o.j, s.job, sizeof o.j);
    return o;
}

static uint32_t xs = 0xC0FFEE11u;
static uint32_t rnd32(void) { xs ^= xs << 13; xs ^= xs >> 17; xs ^= xs << 5; return xs; }
static uint32_t rnd(uint32_t lo, uint32_t hi) { return lo + rnd32() % (hi - lo + 1); }   /* [lo, hi], hi-lo < 2^32-1 */

int main(void) {
    /* ---------- 연습 1: 순진한 now >= next 와 tick wrap ---------- */
    {
        scen_t a = { .cfg = CFG_BASE, .start = 0xFFFFFF00u, .duration = 1000 };   /* 256 ms 뒤 wrap */
        out_t good = run(&a);
        a.due = naive_due;
        out_t bad = run(&a);
        printf("ex1 start 0xFFFFFF00, time_due: S/C/T runs %u/%u/%u early %u/%u/%u\n",
               good.j[0].runs, good.j[1].runs, good.j[2].runs, good.j[0].early, good.j[1].early, good.j[2].early);
        printf("ex1 start 0xFFFFFF00, naive   : S/C/T runs %u/%u/%u early %u/%u/%u, C last start = t0+%u\n",
               bad.j[0].runs, bad.j[1].runs, bad.j[2].runs, bad.j[0].early, bad.j[1].early, bad.j[2].early,
               bad.j[1].last_start - a.start);
        CHECK(good.j[0].runs == 100 && good.j[1].runs == 20 && good.j[2].runs == 10);
        CHECK(good.j[0].early + good.j[1].early + good.j[2].early == 0);
        CHECK(bad.j[0].early == 4 && bad.j[2].early == 20 && bad.j[2].runs == 23);   /* 일찍, 몰아서 */
        CHECK(bad.j[1].runs == 5);                         /* CONTROL 은 wrap 뒤로 멈춤 (2^32 ms ≈ 49.7 일) */
    }

    /* ---------- 연습 2: now + period 의 드리프트 ---------- */
    {
        scen_t a = { .cfg = CFG_LONG, .duration = 10000 };
        out_t good = run(&a);
        a.anchor = true;
        out_t bad = run(&a);
        printf("ex2 10 s long: next+=period S/C/T runs %u/%u/%u | now+period S/C/T runs %u/%u/%u, S ivl %u..%u\n",
               good.j[0].runs, good.j[1].runs, good.j[2].runs, bad.j[0].runs, bad.j[1].runs, bad.j[2].runs,
               bad.j[0].ivl_min, bad.j[0].ivl_max);
        CHECK(good.j[0].runs == 1000 && good.j[1].runs == 200 && good.j[2].runs == 100);
        CHECK(bad.j[0].runs < 1000 && bad.j[1].runs < 200 && bad.j[0].ivl_min == 10);
        scen_t c = { .cfg = CFG_BASE, .duration = 10000, .anchor = true };
        out_t base = run(&c);
        printf("ex2 10 s base: now+period S/C/T runs %u/%u/%u\n",
               base.j[0].runs, base.j[1].runs, base.j[2].runs);
        CHECK(base.j[0].runs == 1000 && base.j[1].runs == 200);       /* 늦음이 매번 같으면 위상만 1 ms 밀림 */
    }

    /* ---------- 연습 3: 지연 상한 = (가장 긴 다른 한 번 호출) - 1 ---------- */
    {
        scen_t a = { .cfg = CFG_LONG, .duration = 1000, .off_telem = 40 };     /* TELEM 40..70 이 CONTROL 50 을 막는다 */
        out_t lo = run(&a);
        a.cfg = CFG_SPLIT;
        out_t sp = run(&a);
        printf("ex3 telem offset 40: long C lat max %u, S lat max %u | split C lat max %u, S lat max %u\n",
               lo.j[1].lat_max, lo.j[0].lat_max, sp.j[1].lat_max, sp.j[0].lat_max);
        CHECK(lo.j[1].lat_max == 24 && sp.j[1].lat_max <= TELEM_CHUNK_MS);   /* 24 = 막힘 21 + SENSOR 몰아치기 3 */

        int viol = 0, tight = 0, short_runs = 0;
        for (int k = 0; k < 300; k++) {
            scen_t r = { .cfg = CFG_LONG, .duration = 2000, .start = rnd32() };      /* 시작 시각도 무작위 → wrap 포함 */
            r.c_sensor = rnd(1, 3);
            r.c_control = rnd(1, 10); r.p_control = rnd(20, 60); r.off_control = rnd(0, r.p_control - 1);
            r.c_telem = rnd(1, 40);   r.p_telem = rnd(100, 200); r.off_telem = rnd(0, r.p_telem - 1);
            out_t o = run(&r);
            uint32_t bound = (r.c_control > r.c_telem ? r.c_control : r.c_telem) - 1;
            if (o.j[0].lat_max > bound) viol++;
            if (o.j[0].runs < 196) short_runs++;          /* 끝 무렵 막힌 릴리스는 창 밖에서 시작 */
            if (o.j[0].lat_max == bound) tight++;
        }
        printf("ex3 random 300: sensor lat max <= maxC_other - 1 violated %d, tight (== bound) %d\n", viol, tight);
        CHECK(viol == 0 && tight > 0 && short_runs == 0);
    }

    /* ---------- 연습 4: 워치독 ---------- */
    {
        scen_t a = { .cfg = CFG_LONG, .duration = 1000, .wdt_timeout = 20 };
        out_t lo = run(&a);
        a.cfg = CFG_SPLIT;
        out_t sp = run(&a);
        printf("ex4 IWDG 20 ms: long gap max %u ms, resets %u | split gap max %u ms, resets %u\n",
               lo.wdt_gap_max, lo.wdt_resets, sp.wdt_gap_max, sp.wdt_resets);
        CHECK(lo.wdt_gap_max == TELEM_TOTAL_MS && lo.wdt_resets == 10);
        CHECK(sp.wdt_gap_max == TELEM_CHUNK_MS && sp.wdt_resets == 0);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
