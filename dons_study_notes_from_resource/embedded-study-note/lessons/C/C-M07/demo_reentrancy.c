/* C-M07 demo — 액션 안에서 dispatch 를 다시 부르면? (재진입 3가지 방식 비교)
 *
 * 링크 머신 (solution.c Part B 와 같은 표): DOWN × HB → UP, 액션 aUP 가 DOWN 동안 쌓인 DATA 2개를
 * fsm_dispatch(DATA) 로 "다시" 넣는다. 셋 중 무엇이 맞는지 trace 로 본다.
 *   A) 그냥 재귀 호출, 상태는 액션 "뒤" 에 바꿈 (solution 의 fsm_step 순서)
 *   B) 그냥 재귀 호출, 상태를 액션 "앞" 에 바꿈 (흔한 변형)
 *   C) run-to-completion: busy 면 큐에 넣고, 현재 전이가 끝난 뒤 처리 (solution 의 fsm_dispatch)
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_reentrancy.c -o demo_reentrancy && ./demo_reentrancy
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum { DOWN = 1, UP, NS };
enum { HB, DATA, TMO, NE };
typedef struct m m_t;
typedef void (*act_t)(m_t *);
typedef struct { uint8_t next; act_t action; } tr_t;
typedef struct { const char *name; act_t entry, exit; } st_t;
struct m {
    uint8_t state; int mode;            /* mode: 'A', 'B', 'C' */
    bool busy; uint8_t q[4], qh, qn;
    char trace[128]; int rx, pending;
};
static void T(m_t *m, const char *s) {
    size_t n = strlen(m->trace);
    snprintf(m->trace + n, sizeof m->trace - n, "%s%s", n ? " " : "", s);
}
static int dispatch(m_t *m, unsigned ev);
static void e_down(m_t *m) { T(m, "eDOWN"); }
static void x_down(m_t *m) { T(m, "xDOWN"); }
static void e_up(m_t *m)   { T(m, "eUP"); }
static void x_up(m_t *m)   { T(m, "xUP"); }
static void a_data(m_t *m) { T(m, "aDATA"); m->rx++; }
static void a_up(m_t *m) {
    T(m, "aUP");
    for (; m->pending > 0; m->pending--) (void)dispatch(m, DATA);   /* 재진입 */
}
static const tr_t tab[NS][NE] = {
    [DOWN] = { [HB] = { UP, a_up } },
    [UP]   = { [DATA] = { 0, a_data } },
};
static const st_t sts[NS] = { [DOWN] = { "DOWN", e_down, x_down }, [UP] = { "UP", e_up, x_up } };

static int step(m_t *m, unsigned ev) {
    const tr_t *t = &tab[m->state][ev];
    if (t->next == 0) { if (!t->action) return 0; t->action(m); return 1; }
    sts[m->state].exit(m);
    if (m->mode == 'B') {               /* B: 상태 먼저 바꾸고 액션 */
        m->state = t->next;
        if (t->action) t->action(m);
    } else {                            /* A, C: exit → action → state → entry */
        if (t->action) t->action(m);
        m->state = t->next;
    }
    sts[m->state].entry(m);
    return 2;
}
static int dispatch(m_t *m, unsigned ev) {
    if (m->mode != 'C') return step(m, ev);           /* A, B: 그냥 재귀 */
    if (m->busy) { m->q[(m->qh + m->qn++) % 4] = (uint8_t)ev; return 3; }
    m->busy = true;
    int r = step(m, ev);
    while (m->qn) { uint8_t e = m->q[m->qh]; m->qh = (uint8_t)((m->qh + 1) % 4); m->qn--; (void)step(m, e); }
    m->busy = false;
    return r;
}
int main(void) {
    const char *desc[] = { "A) 재귀, 액션 뒤 상태 변경", "B) 재귀, 액션 앞 상태 변경", "C) RTC 큐" };
    for (int i = 0; i < 3; i++) {
        m_t m;
        memset(&m, 0, sizeof m);
        m.mode = "ABC"[i];
        m.state = DOWN;
        m.pending = 2;
        (void)dispatch(&m, HB);
        printf("%-30s trace: %-32s rx=%d state=%s\n", desc[i], m.trace, m.rx, sts[m.state].name);
    }
    return 0;
}
