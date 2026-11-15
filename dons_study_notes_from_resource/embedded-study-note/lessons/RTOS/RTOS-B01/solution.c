/* RTOS-B01 슈퍼루프에서 협력형 스케줄러로
 *
 *   시뮬레이션 시계 (하네스 제공): g_now = ms tick (uint32_t, wrap 가능). 실제 시간은 쓰지 않는다
 *   → 모든 실행이 결정적. 작업(job)은 sim_work(ms) 로 "CPU 를 ms 만큼 쓴다".
 *   작업 3개 (드론/로봇 제어 보드 풍):
 *       SENSOR  10 ms 주기, 1 ms  (IMU 읽기)
 *       CONTROL 50 ms 주기, 4 ms  (PID 제어)
 *       TELEM  100 ms 주기, 2 ms  (DMA UART) 또는 30 ms (블로킹 UART printf)  ← "긴 작업"
 *   Part A  wrap-safe 시간 비교 time_due, 슈퍼루프(카운터 + delay), 타임스탬프 기반
 *           run-to-completion 협력형 스케줄러 (작업 테이블: fn, period, next_run,
 *           next_run += period, 테이블 순서 = 우선순위)
 *   Part B  tick wrap (시계를 0xFFFFFF00 에서 시작해도 결과가 같다), 밀린 실행 정책
 *           (catch-up vs skip), 10 초 동안 드리프트 0
 *   Part C  긴 작업(30 ms 블로킹)이 지터를 얼마나 키우나 → 상태 기계로 쪼개서(2 + 7×4 ms) 해결
 *
 * 체크는 시뮬레이션 결과만 본다 (실제 시간 측정 없음). 틀린 코드도 반복 상한에서 멈춘다.
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o st && ./st      (solution.c 도 같은 명령)
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ======================= 시뮬레이션 시계 + 트레이스 (하네스 제공) ======================= */
#define MAX_JOBS 4
#define TL_LEN   50                 /* 타임라인은 실행 시작부터 50 tick 만 기록 */

typedef enum { JOB_DONE = 0, JOB_MORE = 1 } job_ret_t;  /* JOB_MORE = 아직 안 끝남, 곧 다시 불러 달라 */
typedef job_ret_t (*job_fn)(void *ctx);

typedef struct job {
    const char *name;
    job_fn   fn;
    void    *ctx;
    uint32_t period;                /* ms */
    uint32_t next_run;              /* 다음 릴리스 시각 (절대 tick) */
    bool     in_progress;           /* JOB_MORE 를 돌려준 뒤 아직 안 끝남 */
    int      row;                   /* 타임라인 줄 = 테이블 인덱스 */
    /* ---- 통계: trace_release / job_call 이 채운다 ---- */
    uint32_t runs;                  /* 시작한 릴리스 수 */
    uint32_t skipped;               /* 건너뛴 릴리스 수 (skip 정책) */
    uint32_t lat_min, lat_max;      /* 지연 = 시작 시각 - 릴리스 시각 */
    uint32_t ivl_min, ivl_max;      /* 시작 → 다음 시작 간격 */
    uint32_t last_start, last_end;
    uint32_t steps, max_step;       /* job_call 횟수, 한 번 호출이 CPU 를 쥔 최대 ms */
    uint32_t early;                 /* 릴리스 시각 전에 시작한 횟수 (버그 감지) */
} job_t;

typedef struct {
    job_t job[MAX_JOBS];            /* 테이블 순서 = 우선순위 (0 이 가장 높다) */
    int   n;
    bool  skip_missed;              /* false: catch-up (밀린 릴리스를 연달아 실행) / true: 건너뜀 */
} sched_t;

static uint32_t g_now;              /* 현재 시각 ms. 실제 MCU 의 HAL_GetTick() / xTaskGetTickCount() 자리 */
static uint32_t g_t0;               /* 이번 실행의 시작 시각 (타임라인 0 번 칸) */
static job_t   *g_cur;              /* 지금 CPU 를 쥔 작업 (NULL = idle) */
static char     g_tl[MAX_JOBS][TL_LEN + 1];
static uint32_t g_busy_ms, g_idle_ms, g_delay_ms;

static void tl_mark(int row, uint32_t from, uint32_t to, char c) {   /* [from, to) 칸을 c 로 ('R' 은 유지) */
    for (uint32_t t = from; t != to; t++) {
        uint32_t k = t - g_t0;
        if (k >= TL_LEN) break;
        if (row >= 0 && row < MAX_JOBS && g_tl[row][k] != 'R') g_tl[row][k] = c;
    }
}
static void sim_work(uint32_t ms) {               /* 지금 작업이 CPU 를 ms 동안 쓴다 */
    if (g_cur) tl_mark(g_cur->row, g_now, g_now + ms, 'R');
    g_now += ms; g_busy_ms += ms;
}
static void sim_idle(uint32_t ms)  { g_now += ms; g_idle_ms += ms; }   /* WFI: 다음 SysTick 까지 잠 */
static void sim_delay(uint32_t ms) { g_now += ms; g_delay_ms += ms; }  /* HAL_Delay: 바쁜 대기, 아무 일도 못 함 */

/* 새 릴리스를 시작할 때 학생 코드가 부른다 (FreeRTOS 의 traceTASK_SWITCHED_IN 같은 트레이스 훅) */
static void trace_release(job_t *j, uint32_t release) {
    if ((int32_t)(g_now - release) < 0) j->early++;
    uint32_t lat = g_now - release;               /* unsigned 차: wrap 에도 맞다 */
    if (j->runs == 0) {
        j->lat_min = j->lat_max = lat;
        j->ivl_min = UINT32_MAX; j->ivl_max = 0;
    } else {
        uint32_t ivl = g_now - j->last_start;
        if (lat < j->lat_min) j->lat_min = lat;
        if (lat > j->lat_max) j->lat_max = lat;
        if (ivl < j->ivl_min) j->ivl_min = ivl;
        if (ivl > j->ivl_max) j->ivl_max = ivl;
    }
    j->last_start = g_now; j->runs++;
    tl_mark(j->row, release, g_now, 'r');
}
/* 작업 함수는 반드시 이것으로 부른다: 조각 사이 'r' 표시 + 한 번 호출의 길이 측정 */
static job_ret_t job_call(job_t *j) {
    if (j->in_progress) tl_mark(j->row, j->last_end, g_now, 'r');
    uint32_t t = g_now;
    g_cur = j;
    job_ret_t r = j->fn(j->ctx);
    g_cur = NULL;
    j->steps++;
    if (g_now - t > j->max_step) j->max_step = g_now - t;
    j->last_end = g_now;
    return r;
}
/* 슈퍼루프용: k 번째 실행의 "이상적인" 릴리스 = 실행 시작 + k × period (슈퍼루프엔 next_run 이 없다) */
static void superloop_call(job_t *j) {
    trace_release(j, g_t0 + j->runs * j->period);
    (void)job_call(j);                             /* 슈퍼루프는 조각 개념 없이 한 번에 끝까지 */
}

/* ---- 작업 본문 (하네스 제공) ---- */
typedef struct { uint32_t cost; uint32_t count; } work_t;
static job_ret_t job_work(void *ctx) { work_t *w = ctx; sim_work(w->cost); w->count++; return JOB_DONE; }

#define TELEM_FORMAT_MS 2           /* 프레임 만들기 (센서값 → 문자열) */
#define TELEM_CHUNKS    7           /* UART 로 7 조각 */
#define TELEM_CHUNK_MS  4           /* 조각 하나 = 46 B @115200 ≈ 4 ms (블로킹 전송 가정) */
#define TELEM_TOTAL_MS  (TELEM_FORMAT_MS + TELEM_CHUNKS * TELEM_CHUNK_MS)    /* = 30 ms */
enum { TS_FORMAT, TS_SEND };
typedef struct { int state; int chunk; uint32_t frames; uint32_t work_ms; } telem_sm_t;

/* ======================= TODO A1 — wrap-safe 시간 비교 ======================= */
/* now 가 t 에 도달했거나 지났으면 true. 두 시각의 실제 거리가 2^31 ms 미만이면 wrap 과 무관하게 맞아야 한다. */
static bool time_due(uint32_t now, uint32_t t) {
    return (int32_t)(now - t) >= 0;               /* 부호 있는 차: 2^31 미만 앞/뒤를 구분 */
}

/* ======================= TODO A2 — 슈퍼루프 ======================= */
/* while(1) { 매 패스 SENSOR; 5 패스마다 CONTROL; 10 패스마다 TELEM; HAL_Delay(10); } 를 duration ms 동안.
 * 작업은 superloop_call(&J[i]) 로 부르고, 대기는 sim_delay(LOOP_BASE_MS). */
#define LOOP_BASE_MS 10
static void superloop_run(job_t J[3], uint32_t duration) {
    uint32_t start = g_now, pass = 0;
    while (g_now - start < duration) {            /* 경과 시간도 unsigned 차 → wrap 안전 */
        for (int i = 0; i < 3; i++)
            if (pass % (J[i].period / LOOP_BASE_MS) == 0) superloop_call(&J[i]);
        pass++;
        sim_delay(LOOP_BASE_MS);                  /* 작업 시간 + 10 ms → 주기가 늘어진다 */
    }
}

/* ======================= TODO A3 — 작업 테이블에 등록 ======================= */
/* 0 으로 지우고 name/fn/ctx/period 설정, next_run = 지금 + offset, row = 인덱스. 꽉 찼거나 period 0 이면 -1. */
static int sched_add(sched_t *s, const char *name, job_fn fn, void *ctx, uint32_t period, uint32_t offset) {
    if (s->n >= MAX_JOBS || period == 0 || !fn) return -1;
    job_t *j = &s->job[s->n];
    memset(j, 0, sizeof *j);
    j->name = name; j->fn = fn; j->ctx = ctx; j->period = period;
    j->next_run = g_now + offset;
    j->row = s->n;
    return s->n++;
}

/* ======================= TODO A4 — 디스패치 한 번 ======================= */
/* 테이블을 앞(우선순위 높음)부터 보고 "할 일이 있는" 첫 작업 하나만 실행, 그 인덱스를 돌려준다. 없으면 -1.
 *   - in_progress 인 작업: 이어서 job_call (릴리스 아님)
 *   - time_due(g_now, next_run) 인 작업: 새 릴리스 → trace_release(j, next_run),
 *     next_run += period (now + period 아님!), skip_missed 면 이미 지난 릴리스는 건너뛰고 skipped++,
 *     그다음 job_call. JOB_MORE 를 돌려주면 in_progress = true. */
static int sched_dispatch(sched_t *s) {
    for (int i = 0; i < s->n; i++) {
        job_t *j = &s->job[i];
        if (j->in_progress) {
            j->in_progress = (job_call(j) == JOB_MORE);
            return i;
        }
        if (time_due(g_now, j->next_run)) {
            trace_release(j, j->next_run);
            j->next_run += j->period;             /* 기준은 "원래 릴리스 시각" → 드리프트 0 */
            if (s->skip_missed)
                while (time_due(g_now, j->next_run)) { j->next_run += j->period; j->skipped++; }
            j->in_progress = (job_call(j) == JOB_MORE);
            return i;
        }
    }
    return -1;
}

/* ======================= TODO C1 — 텔레메트리를 상태 기계로 쪼개기 ======================= */
/* 한 번 불릴 때 한 조각만: TS_FORMAT → sim_work(FORMAT) 후 JOB_MORE,
 * TS_SEND → sim_work(CHUNK), 7번째 조각이면 frames++ 하고 TS_FORMAT 로 돌아가 JOB_DONE, 아니면 JOB_MORE.
 * work_ms 에 쓴 시간을 더한다. 합계는 블로킹 버전과 같은 30 ms. */
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
static const char *CFG_NAME[] = { "base(telem 2ms)", "long(telem 30ms)", "split(2+7x4ms)" };

typedef struct { uint32_t runs, skipped, lat_min, lat_max, ivl_min, ivl_max, max_step, early; } js_t;
typedef struct {
    js_t j[3];
    uint32_t busy, idle, delay, frames, telem_work;
    char tl[3][TL_LEN + 1];
} result_t;

static work_t     w_sensor, w_control, w_telem;
static telem_sm_t g_tsm;

static void clock_reset(uint32_t start) {
    g_now = g_t0 = start; g_cur = NULL;
    g_busy_ms = g_idle_ms = g_delay_ms = 0;
    for (int r = 0; r < MAX_JOBS; r++) { memset(g_tl[r], '.', TL_LEN); g_tl[r][TL_LEN] = '\0'; }
    w_sensor  = (work_t){ 1, 0 };
    w_control = (work_t){ 4, 0 };
    w_telem   = (work_t){ 2, 0 };
    memset(&g_tsm, 0, sizeof g_tsm);
}
static void telem_choice(int cfg, job_fn *fn, void **ctx) {
    if (cfg == CFG_SPLIT) { *fn = telem_sm_step; *ctx = &g_tsm; }
    else { w_telem.cost = (cfg == CFG_LONG) ? TELEM_TOTAL_MS : 2; *fn = job_work; *ctx = &w_telem; }
}
static result_t collect(job_t *J) {
    result_t r;
    memset(&r, 0, sizeof r);
    for (int i = 0; i < 3; i++) {
        r.j[i] = (js_t){ J[i].runs, J[i].skipped, J[i].lat_min, J[i].lat_max,
                         J[i].runs > 1 ? J[i].ivl_min : 0, J[i].ivl_max, J[i].max_step, J[i].early };
        memcpy(r.tl[i], g_tl[i], TL_LEN + 1);
    }
    r.busy = g_busy_ms; r.idle = g_idle_ms; r.delay = g_delay_ms;
    r.frames = g_tsm.frames + w_telem.count;
    r.telem_work = g_tsm.work_ms + w_telem.count * w_telem.cost;
    return r;
}
static result_t run_sched(int cfg, bool skip, uint32_t start, uint32_t duration) {
    clock_reset(start);
    sched_t s;
    memset(&s, 0, sizeof s);
    s.skip_missed = skip;
    job_fn tf; void *tc;
    telem_choice(cfg, &tf, &tc);
    sched_add(&s, "SENSOR", job_work, &w_sensor, 10, 0);
    sched_add(&s, "CONTROL", job_work, &w_control, 50, 0);
    sched_add(&s, "TELEM", tf, tc, 100, 0);
    long guard = 0;
    while (g_now - start < duration && guard++ < 2000000)     /* 메인 루프: 할 일 없으면 1 tick 잠 */
        if (sched_dispatch(&s) < 0) sim_idle(1);
    if (s.n < 3) { job_t z[3]; memset(z, 0, sizeof z); return collect(z); }
    return collect(s.job);
}
static result_t run_loop(int cfg, uint32_t start, uint32_t duration) {
    clock_reset(start);
    job_t J[3];
    memset(J, 0, sizeof J);
    job_fn tf; void *tc;
    telem_choice(cfg, &tf, &tc);
    J[0] = (job_t){ .name = "SENSOR",  .fn = job_work, .ctx = &w_sensor,  .period = 10,  .row = 0 };
    J[1] = (job_t){ .name = "CONTROL", .fn = job_work, .ctx = &w_control, .period = 50,  .row = 1 };
    J[2] = (job_t){ .name = "TELEM",   .fn = tf,       .ctx = tc,         .period = 100, .row = 2 };
    superloop_run(J, duration);
    return collect(J);
}

static const char *ROW[3] = { "SENSOR ", "CONTROL", "TELEM  " };
static void print_result(const char *label, const result_t *r, uint32_t duration, bool timeline) {
    static const uint32_t P[3] = { 10, 50, 100 };
    printf("%s\n", label);
    for (int i = 0; i < 3; i++) {
        const js_t *j = &r->j[i];
        printf("  %s runs %3u/%-3u skip %2u  lat max %3u  jitter %3u  ivl %2u..%-3u  step max %2u\n",
               ROW[i], j->runs, duration / P[i], j->skipped, j->lat_max, j->lat_max - j->lat_min,
               j->ivl_min, j->ivl_max, j->max_step);
    }
    printf("  busy %u ms, idle %u ms, delay %u ms\n", r->busy, r->idle, r->delay);
    if (!timeline) return;
    printf("  tick   : ");
    for (int t = 0; t < TL_LEN; t++) printf("%d ", t % 10);
    printf("\n");
    for (int i = 0; i < 3; i++) {
        printf("  %s: ", ROW[i]);
        for (int t = 0; t < TL_LEN; t++) printf("%c ", r->tl[i][t]);
        printf("\n");
    }
}

int main(void) {
    /* ---------------- Part A1: time_due ---------------- */
    CHECK(time_due(5, 5) && time_due(6, 5) && !time_due(4, 5));
    CHECK(time_due(0x00000002u, 0xFFFFFFF8u));        /* now 는 wrap 해서 지나갔다 → due */
    CHECK(!time_due(0xFFFFFFF8u, 0x00000002u));       /* 마감이 wrap 너머에 있다 → 아직 */
    CHECK(time_due(1000u + 0x7FFFFFFFu, 1000u) && !time_due(1000u + 0x80000000u, 1000u));  /* 한계 2^31 */
    {   /* 무작위 1e5 쌍: 순수 unsigned 버전 (now - t) < 2^31 과 항상 같다 */
        uint32_t x = 2463534242u; int bad = 0;
        for (int k = 0; k < 100000; k++) {
            x ^= x << 13; x ^= x >> 17; x ^= x << 5; uint32_t a = x;
            x ^= x << 13; x ^= x >> 17; x ^= x << 5; uint32_t b = (k & 1) ? a + (x & 0xFFFF) : x;
            if (time_due(a, b) != ((uint32_t)(a - b) < 0x80000000u)) bad++;
        }
        CHECK(bad == 0);
    }

    /* ---------------- Part A3: sched_add ---------------- */
    {
        sched_t s; memset(&s, 0, sizeof s); work_t w = { 1, 0 };
        clock_reset(1234);
        CHECK(sched_add(&s, "a", job_work, &w, 10, 3) == 0 && s.job[0].next_run == 1237 && s.job[0].row == 0);
        CHECK(sched_add(&s, "z", job_work, &w, 0, 0) == -1 && s.n == 1);           /* period 0 거부 */
        sched_add(&s, "b", job_work, &w, 10, 0); sched_add(&s, "c", job_work, &w, 10, 0);
        CHECK(sched_add(&s, "d", job_work, &w, 10, 0) == 3 && sched_add(&s, "e", job_work, &w, 10, 0) == -1);
        /* 우선순위: 셋 다 due 면 테이블 앞쪽부터 */
        clock_reset(0); memset(&s, 0, sizeof s);
        sched_add(&s, "a", job_work, &w, 10, 0); sched_add(&s, "b", job_work, &w, 10, 0);
        int first = sched_dispatch(&s), second = sched_dispatch(&s), third = sched_dispatch(&s);
        CHECK(first == 0 && second == 1 && third == -1 && g_now == 2);
    }

    /* ---------------- Part A: 슈퍼루프 vs 스케줄러 (긴 작업 없음) ---------------- */
    const uint32_t D = 1000;
    result_t lb = run_loop(CFG_BASE, 0, D);
    print_result("[A] superloop, base", &lb, D, true);
    result_t sb = run_sched(CFG_BASE, false, 0, D);
    print_result("[A] scheduler, base", &sb, D, true);
    CHECK(sb.j[0].runs == 100 && sb.j[1].runs == 20 && sb.j[2].runs == 10);
    CHECK(sb.j[0].lat_max == 0 && sb.j[1].lat_max == 1 && sb.j[2].lat_max == 5);
    CHECK(sb.j[0].ivl_min == 10 && sb.j[0].ivl_max == 10);       /* 센서 간격이 정확히 10 ms */
    CHECK(lb.j[0].runs > 0 && lb.j[0].runs < 90 && lb.j[0].lat_max > 100);   /* 슈퍼루프: 덜 돌고 계속 밀린다 */
    CHECK(lb.j[0].ivl_min >= 11 && lb.j[0].early == 0);

    /* ---------------- Part B: wrap · catch-up vs skip · 드리프트 ---------------- */
    result_t sl = run_sched(CFG_LONG, false, 0, D);
    result_t slw = run_sched(CFG_LONG, false, 0xFFFFFF00u, D);    /* 256 ms 뒤 wrap */
    result_t sbw = run_sched(CFG_BASE, false, 0xFFFFFF00u, D);
    result_t lbw = run_loop(CFG_BASE, 0xFFFFFF00u, D);
    printf("[B1] start 0xFFFFFF00: sched long identical=%d, sched base identical=%d, loop identical=%d\n",
           memcmp(&sl, &slw, sizeof sl) == 0, memcmp(&sb, &sbw, sizeof sb) == 0, memcmp(&lb, &lbw, sizeof lb) == 0);
    CHECK(sl.j[0].runs == 100 && memcmp(&sl, &slw, sizeof sl) == 0);
    CHECK(memcmp(&sb, &sbw, sizeof sb) == 0 && memcmp(&lb, &lbw, sizeof lb) == 0);

    result_t ss = run_sched(CFG_LONG, true, 0, D);
    print_result("[B2] scheduler, long, catch-up", &sl, D, false);
    print_result("[B2] scheduler, long, skip", &ss, D, false);
    CHECK(sl.j[0].runs == 100 && sl.j[0].skipped == 0 && sl.j[0].ivl_min == 1);  /* catch-up: 몰아서 1 ms 간격 */
    CHECK(ss.j[0].runs + ss.j[0].skipped == 100 && ss.j[0].skipped > 0 && ss.j[0].ivl_min == 5);
    CHECK(sl.j[0].lat_max == ss.j[0].lat_max);                    /* 첫 지연은 정책과 무관 */

    result_t l10 = run_sched(CFG_LONG, false, 0x7FFFFF00u, 10000);
    result_t s10 = run_sched(CFG_LONG, true, 0x7FFFFF00u, 10000);
    printf("[B3] 10 s long: catch-up runs %u/%u/%u, skip runs %u + skipped %u\n",
           l10.j[0].runs, l10.j[1].runs, l10.j[2].runs, s10.j[0].runs, s10.j[0].skipped);
    CHECK(l10.j[0].runs == 1000 && l10.j[1].runs == 200 && l10.j[2].runs == 100);
    CHECK(s10.j[0].runs + s10.j[0].skipped == 1000);

    /* ---------------- Part C: 긴 작업 → 상태 기계 ---------------- */
    result_t ll = run_loop(CFG_LONG, 0, D);
    print_result("[C] superloop, long", &ll, D, true);
    print_result("[C] scheduler, long (catch-up)", &sl, D, true);
    result_t sp = run_sched(CFG_SPLIT, false, 0, D);
    print_result("[C] scheduler, split", &sp, D, true);
    CHECK(ll.j[0].runs < lb.j[0].runs && ll.j[0].ivl_max >= 40);   /* 슈퍼루프: 긴 작업이 모두를 민다 */
    CHECK(sl.j[0].lat_max == 25 && sl.j[0].lat_max <= TELEM_TOTAL_MS - 1);  /* 지연 ≤ 가장 긴 작업 - 1 */
    CHECK(sl.j[1].lat_max == 1);
    CHECK(sp.j[2].max_step == TELEM_CHUNK_MS && sp.j[2].runs == 10 && sp.frames == 10);
    CHECK(sp.telem_work == 10 * TELEM_TOTAL_MS);                  /* 일의 총량은 그대로 300 ms */
    CHECK(sp.j[0].lat_max <= TELEM_CHUNK_MS - 1 && sp.j[0].runs == 100);
    CHECK(sp.j[1].lat_max <= TELEM_CHUNK_MS);

    printf("\nsummary (1000 ms): sensor lat max / jitter\n");
    const result_t *all[] = { &lb, &ll, &sb, &sl, &ss, &sp };
    const char *nm[] = { "superloop base", "superloop long", "sched base", "sched long catch-up",
                         "sched long skip", "sched split" };
    for (int k = 0; k < 6; k++)
        printf("  %-20s S %3u/%3u  C %3u/%3u  T %3u/%3u  (runs S %u C %u T %u)\n", nm[k],
               all[k]->j[0].lat_max, all[k]->j[0].lat_max - all[k]->j[0].lat_min,
               all[k]->j[1].lat_max, all[k]->j[1].lat_max - all[k]->j[1].lat_min,
               all[k]->j[2].lat_max, all[k]->j[2].lat_max - all[k]->j[2].lat_min,
               all[k]->j[0].runs, all[k]->j[1].runs, all[k]->j[2].runs);
    (void)CFG_NAME;

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
