/* C-M07 demo — 같은 버튼 디바운스 FSM 을 switch 와 표(함수 포인터)로 각각 구현해 속도 비교
 *
 * 입력: xorshift32(고정 시드) 로 만든 N 개 이벤트 (DOWN / UP / TICK). 두 구현이 같은 결과(press/release
 * 횟수, 최종 상태)를 내는지 먼저 확인하고, 이벤트 1개당 ns 를 잰다. 숫자는 참고용 (CHECK 에 넣지 않는다).
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_bench.c -o demo_bench && ./demo_bench
 */
#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <stdio.h>
#include <time.h>

enum { IDLE = 1, MAYBE_P, PRESSED, MAYBE_R, NS };
enum { EV_DOWN, EV_UP, EV_TIMEOUT, NE };
#define DEB 3u

typedef struct { uint8_t state, timer; uint32_t press, release; } btn_t;

/* ---------- 1) switch-case FSM: 상태별 case 안에서 이벤트를 if 로 ---------- */
__attribute__((noinline)) static void sw_event(btn_t *b, unsigned ev) {
    switch (b->state) {
    case IDLE:
        if (ev == EV_DOWN) { b->state = MAYBE_P; b->timer = DEB; }                 /* entry 를 손으로 */
        break;
    case MAYBE_P:
        if (ev == EV_UP)           { b->timer = 0; b->state = IDLE; }              /* exit 를 손으로 */
        else if (ev == EV_TIMEOUT) { b->timer = 0; b->press++; b->state = PRESSED; }
        break;
    case PRESSED:
        if (ev == EV_UP) { b->state = MAYBE_R; b->timer = DEB; }
        break;
    case MAYBE_R:
        if (ev == EV_DOWN)         { b->timer = 0; b->state = PRESSED; }
        else if (ev == EV_TIMEOUT) { b->timer = 0; b->release++; b->state = IDLE; }
        break;
    default: break;
    }
}

/* ---------- 2) 표 FSM: solution.c 와 같은 모양의 엔진 (큐 생략) ---------- */
typedef void (*act_t)(btn_t *);
typedef struct { uint8_t next; act_t action; } tr_t;
typedef struct { act_t entry, exit; } st_t;
static void t_start(btn_t *b) { b->timer = DEB; }
static void t_stop(btn_t *b)  { b->timer = 0; }
static void e_press(btn_t *b) { b->press++; }
static void e_rel(btn_t *b)   { b->release++; }
static const tr_t tab[NS][NE] = {
    [IDLE]    = { [EV_DOWN] = { MAYBE_P, 0 } },
    [MAYBE_P] = { [EV_UP] = { IDLE, 0 }, [EV_TIMEOUT] = { PRESSED, e_press } },
    [PRESSED] = { [EV_UP] = { MAYBE_R, 0 } },
    [MAYBE_R] = { [EV_DOWN] = { PRESSED, 0 }, [EV_TIMEOUT] = { IDLE, e_rel } },
};
static const st_t sts[NS] = { [MAYBE_P] = { t_start, t_stop }, [MAYBE_R] = { t_start, t_stop } };
__attribute__((noinline)) static void tb_event(btn_t *b, unsigned ev) {
    const tr_t *t = &tab[b->state][ev];
    if (t->next == 0) { if (t->action) t->action(b); return; }
    if (sts[b->state].exit) sts[b->state].exit(b);
    if (t->action) t->action(b);
    b->state = t->next;
    if (sts[b->state].entry) sts[b->state].entry(b);
}

/* tick 은 두 구현 공통 */
typedef void (*ev_fn)(btn_t *, unsigned);
static void tick(btn_t *b, ev_fn f) { if (b->timer && --b->timer == 0) f(b, EV_TIMEOUT); }

static uint32_t rng;
static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static double now(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec + ts.tv_nsec * 1e-9; }

static double run(ev_fn f, btn_t *b, long n) {
    rng = 0x12345678u;
    *b = (btn_t){ IDLE, 0, 0, 0 };
    double t0 = now();
    for (long i = 0; i < n; i++) {
        uint32_t r = rnd() & 7u;                       /* 1/8 DOWN, 1/8 UP, 6/8 TICK */
        if (r == 0) f(b, EV_DOWN); else if (r == 1) f(b, EV_UP); else tick(b, f);
    }
    return (now() - t0) * 1e9 / (double)n;
}
int main(void) {
    const long N = 50000000;
    btn_t a, b;
    for (int rep = 0; rep < 3; rep++) {
        double ns_sw = run(sw_event, &a, N);
        double ns_tb = run(tb_event, &b, N);
        printf("run %d: switch %.2f ns/input   table %.2f ns/input   same=%s (press %u release %u)\n",
               rep, ns_sw, ns_tb,
               (a.state == b.state && a.press == b.press && a.release == b.release) ? "yes" : "NO",
               (unsigned)a.press, (unsigned)a.release);
    }
    return 0;
}
