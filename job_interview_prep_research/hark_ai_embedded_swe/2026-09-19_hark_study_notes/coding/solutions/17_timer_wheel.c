/* 17_timer_wheel.c — 소프트웨어 타이머 목록 + wrap-safe 시간 비교 (solution)
 *
 * 하드웨어 타이머는 2~4개인데 펌웨어가 원하는 타이머는 LED 블링크, 센서
 * 폴링, 통신 타임아웃, 워치독 킥, 디바운스... 열 개가 넘는다. 그래서 1 ms
 * 틱 하나만 하드웨어로 만들고 나머지는 소프트웨어 타이머로 얹는다.
 *
 * 이 파일의 핵심은 자료구조가 아니라 시간 비교다.
 *
 *   틀림:  if (now >= t->expiry)          <- tick이 2^32에서 wrap하면 깨진다
 *   맞음:  if ((int32_t)(now - t->expiry) >= 0)
 *
 * unsigned 뺄셈은 2^32 모듈로 연산이라 wrap을 건너도 '차이'가 정확하다.
 * 그 차이를 int32_t로 보면 부호가 곧 '앞/뒤'가 된다. 대신 표현할 수 있는
 * 범위가 ±2^31로 줄어들므로 지연은 TW_MAX_DELAY(2^31-1) 이하여야 한다.
 *
 * 실행 환경 가정: tick ISR이 카운터를 올리고, 메인 루프가 tw_tick()을 부른다.
 * 콜백은 메인 루프 문맥에서 실행된다(ISR 문맥이 아니다). 동적 할당 없음.
 * tick 값은 호출자가 넘긴다(벤더 헤더를 쓰지 않기 위한 가짜 tick).
 *
 * build: cc -std=c11 -Wall -Wextra -O2 17_timer_wheel.c -o sol_17
 */

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* ------------------------------------------------------------------ */
/* 상수와 타입                                                          */
/* ------------------------------------------------------------------ */

#define TW_MAX_TIMERS  8u
#define TW_MAX_DELAY   0x7FFFFFFFu   /* signed 차이로 비교하므로 절반이 한계 */
#define TW_INVALID_ID  (-1)

typedef void (*tw_cb_t)(void *ctx);

typedef struct {
    bool     active;
    uint32_t expiry;   /* 절대 tick. 상대 남은 시간이 아니다 */
    uint32_t period;   /* 0 = one-shot */
    tw_cb_t  cb;
    void    *ctx;
} tw_timer_t;

typedef struct {
    tw_timer_t t[TW_MAX_TIMERS];
    uint32_t   now;       /* 마지막으로 본 tick */
    uint32_t   n_fired;   /* 실행된 콜백 수 */
    uint32_t   n_missed;  /* 늦어서 건너뛴 주기 수 */
} tw_t;

/* ------------------------------------------------------------------ */
/* 1. wrap-safe 비교 — 이 파일의 심장                                     */
/* ------------------------------------------------------------------ */

/* now가 deadline에 도달했거나 지났는가.
 *
 * (now - deadline)은 uint32_t 연산이라 2^32 모듈로로 '차이'가 정확하다.
 * 그 32비트 패턴을 int32_t로 재해석하면
 *     0 .. 2^31-1   -> 양수: now가 deadline 이후 (도달)
 *     2^31 .. 2^32-1 -> 음수: now가 deadline 이전 (미도달)
 * 가 된다. 즉 "차이가 2^31보다 작다"는 가정 아래 부호 하나로 순서가 나온다.
 *
 * 한계: 실제 차이가 2^31을 넘어가면 부호가 뒤집힌다. 1 ms tick이면
 * 2^31 ms = 약 24.8일이다. 그래서 지연을 TW_MAX_DELAY로 막는다. */
bool tw_reached(uint32_t now, uint32_t deadline)
{
    return (int32_t)(now - deadline) >= 0;
}

/* ------------------------------------------------------------------ */
/* 2. 생성 / 취소                                                       */
/* ------------------------------------------------------------------ */

void tw_init(tw_t *w, uint32_t now)
{
    for (unsigned i = 0u; i < TW_MAX_TIMERS; i++) {
        w->t[i].active = false;
        w->t[i].expiry = 0u;
        w->t[i].period = 0u;
        w->t[i].cb     = NULL;
        w->t[i].ctx    = NULL;
    }
    w->now      = now;
    w->n_fired  = 0u;
    w->n_missed = 0u;
}

/* delay_ms 뒤에 처음 만료. period_ms가 0이 아니면 그 간격으로 반복한다.
 * 성공하면 타이머 id(0 이상), 실패하면 TW_INVALID_ID.
 *
 * delay_ms == 0은 '다음 tw_tick()에서 즉시'를 뜻한다(같은 tick 값이어도 된다).
 * 검증을 먼저 다 하고 나서 슬롯을 건드린다 — 실패했을 때 상태가 반쯤
 * 바뀌어 있으면 안 된다. */
int tw_start(tw_t *w, uint32_t delay_ms, uint32_t period_ms, tw_cb_t cb, void *ctx)
{
    if (cb == NULL) { return TW_INVALID_ID; }
    if (delay_ms > TW_MAX_DELAY) { return TW_INVALID_ID; }
    if (period_ms > TW_MAX_DELAY) { return TW_INVALID_ID; }

    for (unsigned i = 0u; i < TW_MAX_TIMERS; i++) {
        if (!w->t[i].active) {
            w->t[i].expiry = w->now + delay_ms;   /* wrap해도 그대로 맞다 */
            w->t[i].period = period_ms;
            w->t[i].cb     = cb;
            w->t[i].ctx    = ctx;
            w->t[i].active = true;                /* 마지막에 켠다 */
            return (int)i;
        }
    }
    return TW_INVALID_ID;                          /* 슬롯 부족 */
}

bool tw_cancel(tw_t *w, int id)
{
    if (id < 0 || (unsigned)id >= TW_MAX_TIMERS) { return false; }
    if (!w->t[id].active) { return false; }
    w->t[id].active = false;
    return true;
}

/* 남은 시간(ms). 이미 만료 시각을 지났으면 0. 비활성이면 false. */
bool tw_remaining(const tw_t *w, int id, uint32_t *out_ms)
{
    if (id < 0 || (unsigned)id >= TW_MAX_TIMERS) { return false; }
    if (!w->t[id].active) { return false; }
    if (tw_reached(w->now, w->t[id].expiry)) {
        *out_ms = 0u;
    } else {
        *out_ms = w->t[id].expiry - w->now;        /* wrap-safe unsigned 차 */
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* 3. 틱 처리                                                          */
/* ------------------------------------------------------------------ */

/* now까지 만료된 타이머를 모두 실행하고 실행한 개수를 돌려준다.
 *
 * 정책을 명세로 못박는다:
 *  (a) 주기 타이머는 tw_tick() 한 번당 최대 한 번만 실행된다.
 *  (b) 재장전은 expiry += period 다(now + period가 아니다). 그래야 호출이
 *      늦어도 위상이 밀리지 않는다 — drift-free.
 *  (c) 너무 늦어 다음 만료도 이미 지났으면 그 주기는 '놓친 것'으로 버리고
 *      n_missed를 올린다. 콜백을 몰아서 여러 번 부르지 않는다.
 *  (d) 만료 목록은 콜백 실행 전에 한 번 확정한다. 그래서 콜백 안에서
 *      새 타이머를 만들어도 그것이 같은 tw_tick()에서 실행되지 않는다.
 *  (e) 콜백이 아직 실행되지 않은 다른 타이머를 취소하면 그 콜백은 실행되지
 *      않는다. 재시작했으면 새 만료 시각을 존중한다.
 *  (f) 재장전/비활성화를 콜백 '전에' 끝낸다. 콜백이 자기 자신을 취소하거나
 *      재시작할 수 있어야 하기 때문이다. */
unsigned tw_tick(tw_t *w, uint32_t now)
{
    unsigned pending[TW_MAX_TIMERS];
    unsigned n_pending = 0u;

    w->now = now;

    /* (d) 먼저 스냅샷 */
    for (unsigned i = 0u; i < TW_MAX_TIMERS; i++) {
        if (w->t[i].active && tw_reached(now, w->t[i].expiry)) {
            pending[n_pending++] = i;
        }
    }

    unsigned fired = 0u;
    for (unsigned k = 0u; k < n_pending; k++) {
        tw_timer_t *t = &w->t[pending[k]];

        /* (e) 앞선 콜백이 이 타이머를 취소했거나 미래로 재시작했으면 건너뛴다 */
        if (!t->active || !tw_reached(now, t->expiry)) { continue; }

        tw_cb_t cb  = t->cb;
        void   *ctx = t->ctx;

        /* (f) 상태를 먼저 정리한다 */
        if (t->period == 0u) {
            t->active = false;                    /* one-shot */
        } else {
            t->expiry += t->period;               /* (b) drift-free */
            while (tw_reached(now, t->expiry)) {  /* (c) 놓친 주기 버리기 */
                t->expiry += t->period;
                w->n_missed++;
            }
        }

        w->n_fired++;
        fired++;
        cb(ctx);
    }
    return fired;
}

/* ================================================================== */
/* 여기부터는 테스트 코드                                                */
/* ================================================================== */

typedef struct {
    unsigned calls;
    uint32_t last_tick;
    tw_t    *w;
    int      victim;      /* 콜백이 취소할 다른 타이머 id */
    int      self;        /* 자기 자신의 id */
} probe_t;

static void cb_count(void *ctx)
{
    probe_t *p = (probe_t *)ctx;
    p->calls++;
    p->last_tick = p->w->now;
}

static void cb_cancel_victim(void *ctx)
{
    probe_t *p = (probe_t *)ctx;
    p->calls++;
    p->last_tick = p->w->now;
    (void)tw_cancel(p->w, p->victim);
}

static void cb_restart_self(void *ctx)
{
    /* one-shot이 자기 자신을 다시 걸어 주기 타이머처럼 행동하게 만든다 */
    probe_t *p = (probe_t *)ctx;
    p->calls++;
    p->last_tick = p->w->now;
    if (p->calls < 3u) {
        p->self = tw_start(p->w, 10u, 0u, cb_restart_self, p);
    }
}

static probe_t mk(tw_t *w) { probe_t p = { 0u, 0u, w, TW_INVALID_ID, TW_INVALID_ID }; return p; }

/* ---------------------------------------------------------------- */

static void test_wrap_safe_compare(void)
{
    /* 같은 값: 도달했다고 본다(>= 0) */
    assert(tw_reached(0u, 0u));
    assert(tw_reached(1000u, 1000u));

    /* 평범한 경우 */
    assert(tw_reached(1001u, 1000u));
    assert(!tw_reached(999u, 1000u));

    /* wrap을 건너뛴 경우: now=0은 deadline=0xFFFFFFFF보다 1 ms '뒤'다 */
    assert(tw_reached(0u, 0xFFFFFFFFu));
    assert(tw_reached(5u, 0xFFFFFFFBu));
    assert(!tw_reached(0xFFFFFFFFu, 0u));       /* 반대 방향은 미도달 */
    assert(!tw_reached(0xFFFFFFFBu, 5u));

    /* 순진한 비교가 어디서 틀리는지 같은 자리에서 보여준다 */
    assert(!(0u >= 0xFFFFFFFFu));               /* now >= expiry 는 여기서 false */
    assert(tw_reached(0u, 0xFFFFFFFFu));        /* 올바른 답은 true */

    /* ±2^31 경계: 여기가 이 기법의 한계다 */
    assert(tw_reached(0x7FFFFFFFu, 0u));        /* 차이 2^31-1: 도달 */
    assert(!tw_reached(0x80000000u, 0u));       /* 차이 2^31: 부호가 뒤집힌다 */
    printf("  ok  wrap-safe compare: correct across 2^32, limit is exactly 2^31\n");
}

static void test_one_shot(void)
{
    tw_t w;
    tw_init(&w, 1000u);
    probe_t p = mk(&w);

    int id = tw_start(&w, 50u, 0u, cb_count, &p);
    assert(id >= 0);

    assert(tw_tick(&w, 1049u) == 0u);      /* 1 ms 이르다 */
    assert(p.calls == 0u);
    assert(tw_tick(&w, 1050u) == 1u);      /* 정확히 만료 시각 */
    assert(p.calls == 1u && p.last_tick == 1050u);

    /* one-shot은 스스로 비활성화된다 */
    assert(tw_tick(&w, 5000u) == 0u);
    assert(p.calls == 1u);
    uint32_t rem;
    assert(!tw_remaining(&w, id, &rem));
    printf("  ok  one-shot: fires at exactly expiry, then deactivates itself\n");
}

static void test_zero_delay(void)
{
    tw_t w;
    tw_init(&w, 777u);
    probe_t p = mk(&w);

    assert(tw_start(&w, 0u, 0u, cb_count, &p) >= 0);
    /* 같은 tick 값으로 불러도 실행된다 */
    assert(tw_tick(&w, 777u) == 1u);
    assert(p.calls == 1u);
    printf("  ok  delay 0 fires on the next tw_tick, even at the same tick value\n");
}

static void test_periodic_no_drift(void)
{
    tw_t w;
    tw_init(&w, 0u);
    probe_t p = mk(&w);

    assert(tw_start(&w, 100u, 100u, cb_count, &p) >= 0);

    /* 1 ms 씩 촘촘히 돌린다: 100, 200, ... 에서만 실행되어야 한다 */
    for (uint32_t t = 1u; t <= 1000u; t++) {
        unsigned f = tw_tick(&w, t);
        assert(f == ((t % 100u == 0u) ? 1u : 0u));
    }
    assert(p.calls == 10u);
    assert(w.n_missed == 0u);
    printf("  ok  periodic 100 ms over 1000 ticks: exactly 10 calls, no drift\n");
}

static void test_periodic_late_call_keeps_phase(void)
{
    /* tw_tick()이 늦게 불려도 위상은 원래 격자에 붙어 있어야 한다.
     * expiry = now + period 로 재장전했다면 여기서 위상이 밀린다. */
    tw_t w;
    tw_init(&w, 0u);
    probe_t p = mk(&w);

    assert(tw_start(&w, 100u, 100u, cb_count, &p) >= 0);

    assert(tw_tick(&w, 137u) == 1u);       /* 37 ms 늦게 불렸다 */
    assert(p.calls == 1u);
    uint32_t rem;
    int id = 0;
    assert(tw_remaining(&w, id, &rem));
    assert(rem == 63u);                    /* 다음 만료는 200, 즉 63 ms 뒤 */

    assert(tw_tick(&w, 199u) == 0u);
    assert(tw_tick(&w, 200u) == 1u);       /* 원래 격자 그대로 */
    assert(p.calls == 2u);
    printf("  ok  periodic: late tick keeps the 100/200/300 grid (drift-free)\n");
}

static void test_periodic_missed_periods(void)
{
    /* 아주 오래 tw_tick()을 못 불렀으면 놓친 주기를 버리고 한 번만 실행한다.
     * 콜백을 몰아서 부르면 메인 루프가 더 밀려 악순환이 된다. */
    tw_t w;
    tw_init(&w, 0u);
    probe_t p = mk(&w);

    assert(tw_start(&w, 10u, 10u, cb_count, &p) >= 0);
    assert(tw_tick(&w, 1000u) == 1u);      /* 만료가 100번 지났다 */
    assert(p.calls == 1u);
    assert(w.n_missed == 99u);             /* 10..990 중 990을 실행, 나머지 99개 버림 */

    uint32_t rem;
    assert(tw_remaining(&w, 0, &rem));
    assert(rem == 10u);                    /* 다음 만료는 1010 */
    printf("  ok  1000 ms gap on a 10 ms timer: 1 call, n_missed=99, phase kept\n");
}

static void test_capacity_and_bad_args(void)
{
    tw_t w;
    tw_init(&w, 0u);
    probe_t p = mk(&w);

    for (unsigned i = 0u; i < TW_MAX_TIMERS; i++) {
        assert(tw_start(&w, 10u, 0u, cb_count, &p) == (int)i);
    }
    assert(tw_start(&w, 10u, 0u, cb_count, &p) == TW_INVALID_ID);   /* 만원 */

    /* 하나 취소하면 그 슬롯이 재사용된다 */
    assert(tw_cancel(&w, 3));
    assert(!tw_cancel(&w, 3));              /* 두 번 취소는 false */
    assert(tw_start(&w, 10u, 0u, cb_count, &p) == 3);

    /* 잘못된 인자 */
    assert(tw_cancel(&w, -1) == false);
    assert(tw_cancel(&w, (int)TW_MAX_TIMERS) == false);
    tw_init(&w, 0u);
    assert(tw_start(&w, 10u, 0u, NULL, &p) == TW_INVALID_ID);
    assert(tw_start(&w, TW_MAX_DELAY + 1u, 0u, cb_count, &p) == TW_INVALID_ID);
    assert(tw_start(&w, 10u, TW_MAX_DELAY + 1u, cb_count, &p) == TW_INVALID_ID);
    assert(tw_start(&w, TW_MAX_DELAY, 0u, cb_count, &p) == 0);     /* 경계는 허용 */
    printf("  ok  capacity %u, slot reuse, NULL cb and delay > 2^31-1 rejected\n",
           (unsigned)TW_MAX_TIMERS);
}

static void test_many_timers_same_tick(void)
{
    tw_t w;
    tw_init(&w, 0u);
    probe_t p[4];
    for (unsigned i = 0u; i < 4u; i++) { p[i] = mk(&w); }

    /* 네 개가 같은 tick에 만료된다: 한 번의 tw_tick에서 넷 다 실행 */
    for (unsigned i = 0u; i < 4u; i++) {
        assert(tw_start(&w, 40u, 0u, cb_count, &p[i]) == (int)i);
    }
    assert(tw_tick(&w, 40u) == 4u);
    for (unsigned i = 0u; i < 4u; i++) { assert(p[i].calls == 1u); }
    assert(w.n_fired == 4u);
    printf("  ok  four timers expiring on the same tick all fire in one tw_tick\n");
}

static void test_callback_cancels_another(void)
{
    tw_t w;
    tw_init(&w, 0u);
    probe_t killer = mk(&w);
    probe_t victim = mk(&w);

    int id_k = tw_start(&w, 10u, 0u, cb_cancel_victim, &killer);
    int id_v = tw_start(&w, 10u, 0u, cb_count, &victim);
    assert(id_k == 0 && id_v == 1);
    killer.victim = id_v;

    /* 둘 다 만료됐지만 앞 콜백이 뒤 타이머를 죽이므로 뒤는 실행되지 않는다 */
    assert(tw_tick(&w, 10u) == 1u);
    assert(killer.calls == 1u);
    assert(victim.calls == 0u);
    printf("  ok  a callback can cancel a not-yet-fired timer in the same tick\n");
}

static void test_callback_starts_timer_not_run_this_tick(void)
{
    tw_t w;
    tw_init(&w, 0u);
    probe_t p = mk(&w);

    p.self = tw_start(&w, 10u, 0u, cb_restart_self, &p);
    assert(p.self >= 0);

    /* 콜백이 만드는 새 타이머는 같은 tw_tick에서 실행되지 않는다 */
    assert(tw_tick(&w, 10u) == 1u);
    assert(p.calls == 1u);
    assert(tw_tick(&w, 19u) == 0u);
    assert(tw_tick(&w, 20u) == 1u);
    assert(p.calls == 2u);
    assert(tw_tick(&w, 30u) == 1u);
    assert(p.calls == 3u);
    assert(tw_tick(&w, 100u) == 0u);      /* 세 번째 콜백은 재시작하지 않았다 */
    assert(p.calls == 3u);
    printf("  ok  a timer started inside a callback waits for the next tw_tick\n");
}

static void test_remaining(void)
{
    tw_t w;
    tw_init(&w, 500u);
    probe_t p = mk(&w);
    uint32_t rem = 0xDEADu;

    int id = tw_start(&w, 250u, 0u, cb_count, &p);
    assert(tw_remaining(&w, id, &rem) && rem == 250u);
    (void)tw_tick(&w, 600u);
    assert(tw_remaining(&w, id, &rem) && rem == 150u);
    (void)tw_tick(&w, 750u);                       /* 만료 실행 */
    assert(!tw_remaining(&w, id, &rem));           /* 비활성 */

    /* 활성인데 이미 만료 시각을 지난 상태(주기 타이머를 늦게 본 경우)는 0 */
    tw_init(&w, 0u);
    id = tw_start(&w, 10u, 0u, cb_count, &p);
    w.now = 50u;                                   /* tw_tick을 아직 안 불렀다 */
    assert(tw_remaining(&w, id, &rem) && rem == 0u);
    assert(!tw_remaining(&w, 99, &rem));
    printf("  ok  tw_remaining: counts down, 0 when overdue, false when inactive\n");
}

static void test_wrap_crossing_full_run(void)
{
    /* tick을 wrap 직전에서 시작해 관통시킨다. 30 ms 주기 타이머와
     * wrap 이후에 만료되는 one-shot을 동시에 돌린다. */
    tw_t w;
    const uint32_t start = 0xFFFFFF00u;   /* wrap까지 256 ms */
    tw_init(&w, start);

    probe_t per = mk(&w);
    probe_t one = mk(&w);

    assert(tw_start(&w, 30u, 30u, cb_count, &per) == 0);
    assert(tw_start(&w, 300u, 0u, cb_count, &one) == 1);   /* wrap 뒤 44 ms */

    /* 1 ms 씩 600 ms 진행. uint32_t가 자연스럽게 wrap한다. */
    uint32_t t = start;
    unsigned total = 0u;
    for (unsigned k = 0u; k < 600u; k++) {
        t++;                                  /* 0xFFFFFFFF -> 0x00000000 */
        total += tw_tick(&w, t);
    }
    /* 주기 타이머: 30, 60, ... 600 -> 20번 */
    assert(per.calls == 20u);
    assert(one.calls == 1u);
    assert(total == 21u);
    assert(w.n_missed == 0u);
    /* one-shot이 wrap '이후'의 tick에서 실행됐음을 확인 */
    assert(one.last_tick == (uint32_t)(start + 300u));
    assert(one.last_tick == 0x0000002Cu);   /* 0xFFFFFF00 + 300 = 0x2C */
    printf("  ok  600 ms run across the 2^32 wrap: 20 periodic + 1 one-shot, exact\n");
}

static void test_wrap_crossing_coarse_ticks(void)
{
    /* 성긴 tick(7 ms 간격)으로도 wrap을 건너 정확해야 한다.
     * 만료 시각이 tick 값과 정확히 일치하지 않는 일반적인 경우다. */
    tw_t w;
    const uint32_t start = 0xFFFFFFF0u;   /* wrap까지 16 ms */
    tw_init(&w, start);

    probe_t p = mk(&w);
    assert(tw_start(&w, 25u, 25u, cb_count, &p) == 0);

    uint32_t t = start;
    for (unsigned k = 0u; k < 100u; k++) {
        t += 7u;
        (void)tw_tick(&w, t);
    }
    /* 700 ms 동안 25 ms 주기 -> 28번(700/25). 마지막 700은 t=start+700에서 실행 */
    assert(p.calls == 28u);
    assert(w.n_missed == 0u);
    printf("  ok  7 ms coarse ticks across wrap: 28 periodic calls in 700 ms\n");
}

static void test_naive_compare_would_break(void)
{
    /* 순진한 비교 `now >= expiry`가 wrap에서 어떻게 틀리는지 숫자로 못박는다.
     * (라이브러리 코드가 아니라 교육용 대조군이다) */
    const uint32_t start = 0xFFFFFF00u;   /* wrap까지 256 ms */

    /* (a) 300 ms one-shot: 만료 시각이 wrap을 넘어간다(0x0000002C).
     *     순진한 비교는 첫 tick에서 이미 "지났다"고 판정한다 -> 299 ms 빠르게
     *     터진다. 현장에서 "타임아웃이 즉시 발생한다"로 보이는 그 버그다. */
    uint32_t expiry = start + 300u;
    uint32_t t = start + 1u;
    assert(expiry == 0x0000002Cu);
    assert(t >= expiry);                  /* 틀린 비교: 벌써 만료 */
    assert(!tw_reached(t, expiry));        /* 올바른 비교: 아직 아니다 */

    /* (b) 30 ms 주기를 600 ms 돌린다. 정답은 20번.
     *     순진한 비교는 expiry가 먼저 wrap하는 순간부터 매 tick 터져
     *     한 번에 몰아서 23번이 된다(빈도와 위상이 둘 다 망가진다). */
    unsigned naive = 0u, safe = 0u;
    uint32_t en = start + 30u, es = start + 30u;
    t = start;
    for (unsigned k = 0u; k < 600u; k++) {
        t++;
        if (t >= en) { naive++; en += 30u; }          /* 틀린 비교 */
        if (tw_reached(t, es)) { safe++; es += 30u; } /* 올바른 비교 */
    }
    assert(safe == 20u);
    assert(naive != safe);
    printf("  ok  naive `now >= expiry`: one-shot 299 ms early, periodic %u vs %u\n",
           naive, safe);
}

int main(void)
{
    printf("17_timer_wheel (solution)\n");
    test_wrap_safe_compare();
    test_one_shot();
    test_zero_delay();
    test_periodic_no_drift();
    test_periodic_late_call_keeps_phase();
    test_periodic_missed_periods();
    test_capacity_and_bad_args();
    test_many_timers_same_tick();
    test_callback_cancels_another();
    test_callback_starts_timer_not_run_this_tick();
    test_remaining();
    test_wrap_crossing_full_run();
    test_wrap_crossing_coarse_ticks();
    test_naive_compare_would_break();
    printf("ALL TESTS PASSED\n");
    return 0;
}
