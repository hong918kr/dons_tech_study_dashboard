/* 14_debounce_fsm.c — 버튼 디바운스 상태기계 (solution)
 *
 * 기계식 접점은 누를 때·뗄 때 수 ms 동안 수십 번 튄다(contact bounce).
 * GPIO를 그대로 읽으면 한 번 누른 것이 여러 번 눌린 것으로 보인다.
 * 여기서는 "같은 값이 DEB_STABLE_MS 이상 유지되면 그 값을 받아들인다"는
 * 시간 기반 디바운서를 상태기계로 만들고, 그 위에 롱프레스를 얹는다.
 *
 * 실행 환경 가정: 주기 타이머 ISR 또는 태스크가 button_poll()을 규칙적으로
 * 부른다(예: 5~10 ms). GPIO 값과 tick은 호출자가 인자로 넘긴다. 벤더 헤더를
 * 쓰지 않기 위해 테스트가 직접 raw 값과 tick을 만든다(예시용 가짜 입력).
 *
 * 시간 비교는 전부 unsigned 뺄셈(now - then)이다. uint32_t 뺄셈은 2^32 모듈로
 * 연산이라 tick이 wrap해도 "지난 시간"은 그대로 맞는다. 절대 시각 비교
 * (deadline < now)는 wrap에서 깨지므로 쓰지 않는다 — 그쪽은 17번 주제다.
 *
 * build: cc -std=c11 -Wall -Wextra -O2 14_debounce_fsm.c -o sol_14
 */

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* ------------------------------------------------------------------ */
/* 파라미터                                                             */
/* ------------------------------------------------------------------ */

#define DEB_STABLE_MS  20u    /* 이만큼 값이 유지되면 확정한다 */
#define DEB_LONG_MS   800u    /* 물리적 누름부터 이만큼 지나면 롱프레스 */

/* ------------------------------------------------------------------ */
/* 타입                                                                 */
/* ------------------------------------------------------------------ */

typedef enum {
    BTN_IDLE,          /* 뗀 상태로 안정 */
    BTN_WAIT_PRESS,    /* 누름 후보. 아직 확정 전 */
    BTN_DOWN,          /* 누른 상태로 안정 */
    BTN_WAIT_RELEASE   /* 뗌 후보. 아직 확정 전 */
} btn_state_t;

typedef enum {
    BTN_EV_NONE = 0,
    BTN_EV_PRESS,        /* 누름 확정 */
    BTN_EV_LONG_PRESS,   /* 누른 채 DEB_LONG_MS 경과. 누름 1회당 1번만 */
    BTN_EV_RELEASE       /* 뗌 확정 */
} btn_event_t;

typedef struct {
    btn_state_t st;
    uint32_t t_edge;      /* raw가 마지막으로 바뀐 tick (후보 시작 시각) */
    uint32_t t_press;     /* 물리적 누름 시각 = 누름 후보가 시작된 tick */
    bool     long_fired;  /* 이번 누름에서 롱프레스를 이미 냈는가 */
    uint32_t n_press;     /* 확정된 누름 횟수 */
    uint32_t n_long;      /* 롱프레스 횟수 */
    uint32_t n_release;   /* 확정된 뗌 횟수 */
    uint32_t n_glitch;    /* 확정 전에 되돌아간 후보(= 걸러낸 바운스) 횟수 */
} button_t;

/* ------------------------------------------------------------------ */
/* 구현                                                                 */
/* ------------------------------------------------------------------ */

void button_init(button_t *b, uint32_t now_ms)
{
    b->st         = BTN_IDLE;
    b->t_edge     = now_ms;
    b->t_press    = now_ms;
    b->long_fired = false;
    b->n_press    = 0u;
    b->n_long     = 0u;
    b->n_release  = 0u;
    b->n_glitch   = 0u;
}

/* 디바운스된 논리 상태. WAIT_RELEASE는 아직 "눌린 것"으로 본다. */
bool button_is_down(const button_t *b)
{
    return (b->st == BTN_DOWN) || (b->st == BTN_WAIT_RELEASE);
}

const char *btn_event_name(btn_event_t e)
{
    switch (e) {
    case BTN_EV_NONE:       return "NONE";
    case BTN_EV_PRESS:      return "PRESS";
    case BTN_EV_LONG_PRESS: return "LONG";
    case BTN_EV_RELEASE:    return "RELEASE";
    default:                return "?";
    }
}

/* raw_pressed: GPIO를 읽어 "지금 눌려 있다"로 해석한 값(active-low 변환은
 * 호출자 책임). 한 번의 호출은 이벤트를 최대 하나만 낸다. */
btn_event_t button_poll(button_t *b, bool raw_pressed, uint32_t now_ms)
{
    switch (b->st) {
    case BTN_IDLE:
        if (raw_pressed) {
            /* 누름 후보 시작. 아직 아무 이벤트도 내지 않는다. */
            b->t_edge = now_ms;
            b->st     = BTN_WAIT_PRESS;
        }
        break;

    case BTN_WAIT_PRESS:
        if (!raw_pressed) {
            /* 안정 시간을 못 채우고 되돌아갔다 = 바운스. 조용히 버린다. */
            b->n_glitch++;
            b->st = BTN_IDLE;
        } else if ((uint32_t)(now_ms - b->t_edge) >= DEB_STABLE_MS) {
            /* 누름 확정. 롱프레스는 '물리적 누름 시각'부터 재므로 t_edge를 쓴다. */
            b->t_press    = b->t_edge;
            b->long_fired = false;
            b->st         = BTN_DOWN;
            b->n_press++;
            return BTN_EV_PRESS;
        }
        break;

    case BTN_DOWN:
        if (!raw_pressed) {
            b->t_edge = now_ms;
            b->st     = BTN_WAIT_RELEASE;
        } else if (!b->long_fired &&
                   (uint32_t)(now_ms - b->t_press) >= DEB_LONG_MS) {
            b->long_fired = true;
            b->n_long++;
            return BTN_EV_LONG_PRESS;
        }
        break;

    case BTN_WAIT_RELEASE:
        if (raw_pressed) {
            /* 뗌 후보가 취소됐다. t_press는 건드리지 않으므로 롱프레스
             * 타이머는 계속 원래 누름 시각부터 흐른다. */
            b->n_glitch++;
            b->st = BTN_DOWN;
        } else if ((uint32_t)(now_ms - b->t_edge) >= DEB_STABLE_MS) {
            b->st = BTN_IDLE;
            b->n_release++;
            return BTN_EV_RELEASE;
        }
        break;
    }

    return BTN_EV_NONE;
}

/* ================================================================== */
/* 여기부터는 테스트 코드                                                */
/* ================================================================== */

/* raw 패턴을 문자열로 먹인다. '1' = 눌림, '0' = 뗌, 그 외 문자는 무시.
 * 한 글자가 step_ms만큼의 시간 진행에 해당한다.
 * 관찰된 이벤트 이름을 log에 누적한다(예: "P", "L", "R"). */
static void feed(button_t *b, const char *pattern, uint32_t *t, uint32_t step_ms,
                 char *log, size_t logsz)
{
    size_t ln = 0u;
    while (log != NULL && log[ln] != '\0') { ln++; }
    for (const char *p = pattern; *p != '\0'; p++) {
        if (*p != '0' && *p != '1') { continue; }
        btn_event_t e = button_poll(b, (*p == '1'), *t);
        if (log != NULL && e != BTN_EV_NONE && ln + 2u < logsz) {
            log[ln++] = (e == BTN_EV_PRESS) ? 'P' : (e == BTN_EV_LONG_PRESS) ? 'L' : 'R';
            log[ln]   = '\0';
        }
        *t += step_ms;
    }
}

/* raw를 hold_ms 동안 같은 값으로 유지하며 step_ms마다 폴링한다. */
static void hold(button_t *b, bool raw, uint32_t hold_ms, uint32_t *t,
                 uint32_t step_ms, char *log, size_t logsz)
{
    size_t ln = 0u;
    while (log != NULL && log[ln] != '\0') { ln++; }
    for (uint32_t elapsed = 0u; elapsed < hold_ms; elapsed += step_ms) {
        btn_event_t e = button_poll(b, raw, *t);
        if (log != NULL && e != BTN_EV_NONE && ln + 2u < logsz) {
            log[ln++] = (e == BTN_EV_PRESS) ? 'P' : (e == BTN_EV_LONG_PRESS) ? 'L' : 'R';
            log[ln]   = '\0';
        }
        *t += step_ms;
    }
}

static bool streq(const char *a, const char *b)
{
    while (*a != '\0' && *a == *b) { a++; b++; }
    return *a == *b;
}

static void test_clean_press_release(void)
{
    button_t b;
    uint32_t t = 1000u;
    char log[32] = "";

    button_init(&b, t);
    /* 5 ms 폴링. 100 ms 누르고 100 ms 뗀다. */
    hold(&b, true,  100u, &t, 5u, log, sizeof log);
    hold(&b, false, 100u, &t, 5u, log, sizeof log);

    assert(streq(log, "PR"));
    assert(b.n_press == 1u && b.n_release == 1u);
    assert(b.n_long == 0u && b.n_glitch == 0u);
    assert(b.st == BTN_IDLE && !button_is_down(&b));
    printf("  ok  clean 100 ms press -> PRESS, RELEASE, no long, no glitch\n");
}

static void test_stable_boundary(void)
{
    button_t b;
    uint32_t t;
    char log[32];

    /* 정확히 20 ms 유지: 확정된다(>= 비교).
     * t=0 에서 첫 '1'(후보 시작), t=20 에서 두 번째 폴링에 확정. */
    button_init(&b, 0u);
    t = 0u; log[0] = '\0';
    feed(&b, "1", &t, 20u, log, sizeof log);     /* t: 0 -> 20 */
    assert(streq(log, ""));
    feed(&b, "1", &t, 20u, log, sizeof log);     /* t=20, 경과 20 ms */
    assert(streq(log, "P"));
    assert(b.n_press == 1u);

    /* 19 ms 만 유지하고 뗀다: 확정되지 않는다. */
    button_init(&b, 0u);
    t = 0u; log[0] = '\0';
    feed(&b, "1", &t, 19u, log, sizeof log);     /* t: 0 -> 19 */
    feed(&b, "0", &t, 1u, log, sizeof log);      /* t=19에서 뗌 = 후보 취소 */
    assert(streq(log, ""));
    assert(b.n_press == 0u && b.n_glitch == 1u);
    printf("  ok  boundary: 20 ms stable confirms, 19 ms is rejected as a glitch\n");
}

static void test_bounce_on_press(void)
{
    button_t b;
    uint32_t t = 0u;
    char log[32] = "";

    button_init(&b, 0u);
    /* 1 ms 폴링. 접점이 12 ms 동안 튀고(101010101010) 그 뒤 안정. */
    feed(&b, "101010101010", &t, 1u, log, sizeof log);
    assert(streq(log, ""));            /* 바운스 구간에서는 이벤트 0개 */
    assert(b.n_press == 0u);
    assert(b.n_glitch == 6u);          /* '0'을 만난 횟수만큼 후보가 취소됐다 */

    hold(&b, true, 40u, &t, 1u, log, sizeof log);
    assert(streq(log, "P"));           /* 안정된 뒤 딱 한 번 */
    assert(b.n_press == 1u);
    printf("  ok  12 ms of contact chatter -> exactly one PRESS (glitch=6)\n");
}

static void test_bounce_on_release(void)
{
    button_t b;
    uint32_t t = 0u;
    char log[32] = "";

    button_init(&b, 0u);
    hold(&b, true, 60u, &t, 1u, log, sizeof log);      /* 누름 확정 */
    assert(streq(log, "P"));
    feed(&b, "010101", &t, 1u, log, sizeof log);       /* 뗄 때 바운스 */
    assert(streq(log, "P"));                           /* 아직 RELEASE 없음 */
    assert(button_is_down(&b) || b.st == BTN_DOWN);
    hold(&b, false, 40u, &t, 1u, log, sizeof log);
    assert(streq(log, "PR"));
    assert(b.n_release == 1u);
    printf("  ok  release-side chatter -> exactly one RELEASE\n");
}

static void test_long_press(void)
{
    button_t b;
    uint32_t t = 0u;
    char log[32] = "";

    button_init(&b, 0u);
    hold(&b, true, 1000u, &t, 5u, log, sizeof log);
    assert(streq(log, "PL"));                 /* PRESS 뒤 LONG 한 번만 */
    assert(b.n_long == 1u);
    hold(&b, true, 2000u, &t, 5u, log, sizeof log);
    assert(streq(log, "PL"));                 /* 계속 눌러도 추가 LONG 없음 */
    assert(b.n_long == 1u);
    hold(&b, false, 40u, &t, 5u, log, sizeof log);
    assert(streq(log, "PLR"));
    printf("  ok  long press fires once, repeats suppressed, then RELEASE\n");
}

static void test_long_press_boundary(void)
{
    button_t b;
    uint32_t t;
    char log[32];

    /* 롱프레스 기준은 '물리적 누름 시각'(t_edge)부터다.
     * t=0에 누르기 시작 -> t=800에 폴링하면 정확히 800 ms 경과 -> LONG. */
    button_init(&b, 0u);
    t = 0u; log[0] = '\0';
    feed(&b, "1", &t, DEB_LONG_MS, log, sizeof log);   /* t: 0 -> 800 */
    assert(streq(log, ""));
    feed(&b, "1", &t, 1u, log, sizeof log);            /* t=800: PRESS 먼저 */
    assert(streq(log, "P"));
    feed(&b, "1", &t, 1u, log, sizeof log);            /* t=801: LONG */
    assert(streq(log, "PL"));

    /* 799 ms 만 누르고 떼면 LONG이 없다. */
    button_init(&b, 0u);
    t = 0u; log[0] = '\0';
    hold(&b, true, DEB_LONG_MS - 1u, &t, 1u, log, sizeof log);
    hold(&b, false, 40u, &t, 1u, log, sizeof log);
    assert(streq(log, "PR"));
    assert(b.n_long == 0u);
    printf("  ok  long boundary: held 800 ms -> LONG, 799 ms -> short click only\n");
}

static void test_glitch_during_hold_keeps_long_timer(void)
{
    /* 누른 채로 한 샘플만 튀어도 롱프레스 타이머가 리셋되지 않아야 한다. */
    button_t b;
    uint32_t t = 0u;
    char log[32] = "";

    button_init(&b, 0u);
    hold(&b, true, 400u, &t, 1u, log, sizeof log);
    feed(&b, "0", &t, 1u, log, sizeof log);        /* 1 ms 짜리 튐 */
    feed(&b, "1", &t, 1u, log, sizeof log);        /* 바로 복귀 */
    assert(streq(log, "P"));
    assert(b.n_glitch == 1u);
    /* 누름 시각으로부터 800 ms가 되면 LONG. 리셋됐다면 1200 ms까지 안 나온다. */
    hold(&b, true, 400u, &t, 1u, log, sizeof log); /* 누적 ~802 ms */
    assert(streq(log, "PL"));
    assert(b.n_press == 1u);
    printf("  ok  1 ms glitch mid-hold: no extra PRESS, long timer not restarted\n");
}

static void test_slow_polling(void)
{
    /* 폴링 주기가 안정 시간보다 길면(50 ms) 연속 두 샘플로 확정된다. */
    button_t b;
    uint32_t t = 0u;
    char log[32] = "";

    button_init(&b, 0u);
    feed(&b, "1", &t, 50u, log, sizeof log);   /* 후보 */
    assert(streq(log, ""));
    feed(&b, "1", &t, 50u, log, sizeof log);   /* 50 ms >= 20 ms -> 확정 */
    assert(streq(log, "P"));
    feed(&b, "0", &t, 50u, log, sizeof log);
    feed(&b, "0", &t, 50u, log, sizeof log);
    assert(streq(log, "PR"));
    /* 대가: 50 ms 짧은 펄스는 폴링 사이에 통째로 사라질 수 있다. */
    printf("  ok  slow 50 ms polling still works (two consecutive samples)\n");
}

static void test_multiple_clicks(void)
{
    button_t b;
    uint32_t t = 0u;
    char log[64] = "";

    button_init(&b, 0u);
    for (int k = 0; k < 3; k++) {
        hold(&b, true,  60u, &t, 5u, log, sizeof log);
        hold(&b, false, 60u, &t, 5u, log, sizeof log);
    }
    assert(streq(log, "PRPRPR"));
    assert(b.n_press == 3u && b.n_release == 3u && b.n_long == 0u);
    printf("  ok  three short clicks -> PRPRPR, counters 3/3/0\n");
}

static void test_tick_wraparound(void)
{
    /* tick이 2^32에서 wrap하는 구간에 걸쳐 누름·롱프레스·뗌이 모두 정상. */
    button_t b;
    uint32_t t = 0xFFFFFF00u;      /* wrap까지 256 ms 남았다 */
    char log[32] = "";

    button_init(&b, t);
    hold(&b, true, 1000u, &t, 5u, log, sizeof log);   /* wrap을 관통 */
    assert(streq(log, "PL"));
    hold(&b, false, 40u, &t, 5u, log, sizeof log);
    assert(streq(log, "PLR"));
    assert(b.n_press == 1u && b.n_long == 1u && b.n_release == 1u);
    assert(b.n_glitch == 0u);
    printf("  ok  tick wrap at 2^32 during hold: PRESS/LONG/RELEASE all correct\n");
}

static void test_idle_is_quiet(void)
{
    /* 아무도 안 누르는 링크에서 이벤트나 카운터가 움직이면 안 된다. */
    button_t b;
    uint32_t t = 12345u;
    button_init(&b, t);
    for (int i = 0; i < 1000; i++) {
        assert(button_poll(&b, false, t) == BTN_EV_NONE);
        t += 7u;
    }
    assert(b.n_press == 0u && b.n_release == 0u);
    assert(b.n_long == 0u && b.n_glitch == 0u);
    assert(!button_is_down(&b));
    printf("  ok  idle: 1000 polls with button up produce nothing\n");
}

static void test_pressed_at_boot(void)
{
    /* 부팅 순간 이미 눌려 있으면 그것도 한 번의 누름으로 확정되어야 한다. */
    button_t b;
    uint32_t t = 0u;
    char log[32] = "";

    button_init(&b, 0u);
    hold(&b, true, 40u, &t, 5u, log, sizeof log);
    assert(streq(log, "P"));
    assert(b.n_press == 1u);
    printf("  ok  button already held at init -> one PRESS after stable time\n");
}

static void test_event_names(void)
{
    assert(streq(btn_event_name(BTN_EV_NONE), "NONE"));
    assert(streq(btn_event_name(BTN_EV_PRESS), "PRESS"));
    assert(streq(btn_event_name(BTN_EV_LONG_PRESS), "LONG"));
    assert(streq(btn_event_name(BTN_EV_RELEASE), "RELEASE"));
    printf("  ok  btn_event_name covers every enumerator\n");
}

int main(void)
{
    printf("14_debounce_fsm (solution)\n");
    test_clean_press_release();
    test_stable_boundary();
    test_bounce_on_press();
    test_bounce_on_release();
    test_long_press();
    test_long_press_boundary();
    test_glitch_during_hold_keeps_long_timer();
    test_slow_polling();
    test_multiple_clicks();
    test_tick_wraparound();
    test_idle_is_quiet();
    test_pressed_at_boot();
    test_event_names();
    printf("ALL TESTS PASSED\n");
    return 0;
}
