/* C-M07 demo — Cortex-M4 에서 switch FSM 과 표 FSM 은 어떤 코드가 되나 (크로스 컴파일만, main 없음)
 *
 * clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_target.c -o -
 * clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -Os -ffunction-sections -fdata-sections \
 *       -c demo_target.c -o /tmp/demo_target.o && xcrun llvm-nm -S --size-sort /tmp/demo_target.o
 * (freestanding: <stdint.h> 만 쓴다)
 */
#include <stdint.h>

enum { IDLE = 1, MAYBE_P, PRESSED, MAYBE_R, NS };
enum { EV_DOWN, EV_UP, EV_TIMEOUT, NE };
#define DEB 3u
typedef struct { uint8_t state, timer; uint32_t press, release; } btn_t;

/* ---- switch 버전 ---- */
void sw_event(btn_t *b, unsigned ev) {
    switch (b->state) {
    case IDLE:    if (ev == EV_DOWN) { b->state = MAYBE_P; b->timer = DEB; } break;
    case MAYBE_P: if (ev == EV_UP) { b->timer = 0; b->state = IDLE; }
                  else if (ev == EV_TIMEOUT) { b->timer = 0; b->press++; b->state = PRESSED; } break;
    case PRESSED: if (ev == EV_UP) { b->state = MAYBE_R; b->timer = DEB; } break;
    case MAYBE_R: if (ev == EV_DOWN) { b->timer = 0; b->state = PRESSED; }
                  else if (ev == EV_TIMEOUT) { b->timer = 0; b->release++; b->state = IDLE; } break;
    default: break;
    }
}

/* ---- 표 버전: 엔진(tb_event) + 데이터(tab, sts) + 작은 액션들 ---- */
typedef void (*act_t)(btn_t *);
typedef struct { uint8_t next; act_t action; } tr_t;
typedef struct { act_t entry, exit; } st_t;
static void t_start(btn_t *b) { b->timer = DEB; }
static void t_stop(btn_t *b)  { b->timer = 0; }
static void e_press(btn_t *b) { b->press++; }
static void e_rel(btn_t *b)   { b->release++; }
const tr_t tab[NS][NE] = {
    [IDLE]    = { [EV_DOWN] = { MAYBE_P, 0 } },
    [MAYBE_P] = { [EV_UP] = { IDLE, 0 }, [EV_TIMEOUT] = { PRESSED, e_press } },
    [PRESSED] = { [EV_UP] = { MAYBE_R, 0 } },
    [MAYBE_R] = { [EV_DOWN] = { PRESSED, 0 }, [EV_TIMEOUT] = { IDLE, e_rel } },
};
const st_t sts[NS] = { [MAYBE_P] = { t_start, t_stop }, [MAYBE_R] = { t_start, t_stop } };
void tb_event(btn_t *b, unsigned ev) {
    const tr_t *t = &tab[b->state][ev];
    if (t->next == 0) { if (t->action) t->action(b); return; }
    if (sts[b->state].exit) sts[b->state].exit(b);
    if (t->action) t->action(b);
    b->state = t->next;
    if (sts[b->state].entry) sts[b->state].entry(b);
}
