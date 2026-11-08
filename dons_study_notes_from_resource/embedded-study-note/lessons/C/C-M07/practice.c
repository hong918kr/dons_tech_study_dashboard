/* C-M07 추가 연습 — 한 번에 검증
 *
 * 1. 같은 버튼 FSM 을 switch 로 다시 쓰고, 10,000 개 랜덤 입력(xorshift32 고정 시드)에서 표 버전과 동치 확인
 * 2. fsm_validate(): 범위 밖 next, 도달 불가능한 상태를 init 전에 찾아낸다
 * 3. guard 가 있는 희소(sparse) 전이 리스트 — 동전 개찰구(turnstile)
 * 4. 전이 기록 링 버퍼 (post-mortem 디버깅용, 훅 함수 포인터)
 * 5. 길게 누르기(long press): 기능 추가 = 표에 행 추가
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---------- 미니 엔진 (solution.c 의 fsm_step 규칙 그대로, 큐 생략) + 전이 훅 ---------- */
typedef struct fsm fsm_t;
typedef void (*fsm_action_t)(fsm_t *m);
typedef void (*fsm_hook_t)(fsm_t *m, uint8_t from, uint8_t ev, uint8_t to);
typedef struct { uint8_t next; fsm_action_t action; } fsm_trans_t;
typedef struct { const char *name; fsm_action_t entry, exit; } fsm_state_t;
struct fsm {
    const fsm_trans_t *table; const fsm_state_t *states;
    uint8_t n_states, n_events, state;
    void *ctx;
    fsm_hook_t hook;                       /* 연습 4: 외부 전이마다 호출 (NULL 가능) */
};
#define CELL(s, e, n) ((s) * (n) + (e))
enum { R_EINVAL = -1, R_IGNORED = 0, R_HANDLED = 1, R_TRANSITION = 2 };

static void fsm_init(fsm_t *m, const fsm_trans_t *t, const fsm_state_t *s, uint8_t ns, uint8_t ne,
                     uint8_t init, void *ctx) {
    memset(m, 0, sizeof *m);
    m->table = t; m->states = s; m->n_states = ns; m->n_events = ne; m->state = init; m->ctx = ctx;
    if (s[init].entry) s[init].entry(m);
}
static int fsm_dispatch(fsm_t *m, unsigned ev) {
    if (ev >= m->n_events || m->state == 0 || m->state >= m->n_states) return R_EINVAL;
    const fsm_trans_t *t = &m->table[CELL(m->state, ev, m->n_events)];
    if (t->next == 0) {
        if (!t->action) return R_IGNORED;
        t->action(m);
        return R_HANDLED;
    }
    uint8_t from = m->state;
    if (m->states[from].exit) m->states[from].exit(m);
    if (t->action) t->action(m);
    m->state = t->next;
    if (m->states[m->state].entry) m->states[m->state].entry(m);
    if (m->hook) m->hook(m, from, (uint8_t)ev, m->state);
    return R_TRANSITION;
}

/* ---------- 버튼 (표 버전) — 연습 1·2·4 공용 ---------- */
enum { IDLE = 1, MAYBE_P, PRESSED, MAYBE_R, LONG_HELD, N_ST };   /* LONG_HELD 는 연습 5 용 */
enum { EV_DOWN, EV_UP, EV_TIMEOUT, N_EV };
#define DEB  3u
#define LONG 5u
#define B(s, e) CELL(s, e, N_EV)
typedef struct { uint8_t timer; uint32_t press, release, longp; char out[32]; } bctx_t;
static void out_add(bctx_t *b, char c) { size_t n = strlen(b->out); if (n + 1 < sizeof b->out) { b->out[n] = c; b->out[n + 1] = 0; } }
static void t_deb(fsm_t *m)   { ((bctx_t *)m->ctx)->timer = DEB; }
static void t_long(fsm_t *m)  { ((bctx_t *)m->ctx)->timer = LONG; }
static void t_stop(fsm_t *m)  { ((bctx_t *)m->ctx)->timer = 0; }
static void e_press(fsm_t *m) { bctx_t *b = m->ctx; b->press++;   out_add(b, 'P'); }
static void e_rel(fsm_t *m)   { bctx_t *b = m->ctx; b->release++; out_add(b, 'R'); }
static void e_long(fsm_t *m)  { bctx_t *b = m->ctx; b->longp++;   out_add(b, 'L'); }

static const fsm_trans_t btn_tab[N_ST * N_EV] = {
    [B(IDLE, EV_DOWN)]       = { MAYBE_P, NULL },
    [B(MAYBE_P, EV_UP)]      = { IDLE, NULL },
    [B(MAYBE_P, EV_TIMEOUT)] = { PRESSED, e_press },
    [B(PRESSED, EV_UP)]      = { MAYBE_R, NULL },
    [B(MAYBE_R, EV_DOWN)]    = { PRESSED, NULL },
    [B(MAYBE_R, EV_TIMEOUT)] = { IDLE, e_rel },
};
static const fsm_state_t btn_st[N_ST] = {
    [IDLE] = { "IDLE", NULL, NULL },       [MAYBE_P] = { "MAYBE_P", t_deb, t_stop },
    [PRESSED] = { "PRESSED", NULL, NULL }, [MAYBE_R] = { "MAYBE_R", t_deb, t_stop },
    [LONG_HELD] = { "LONG_HELD", NULL, NULL },
};
static void btn_tick(fsm_t *m) {
    bctx_t *b = m->ctx;
    if (b->timer > 0 && --b->timer == 0) (void)fsm_dispatch(m, EV_TIMEOUT);
}

/* ====================== 연습 1: switch 버전 ====================== */
typedef struct { uint8_t state, timer; uint32_t press, release; } sw_btn_t;
static void sw_event(sw_btn_t *b, unsigned ev) {
    switch (b->state) {
    case IDLE:
        if (ev == EV_DOWN) { b->state = MAYBE_P; b->timer = DEB; }          /* entry(MAYBE_P) 를 손으로 */
        break;
    case MAYBE_P:
        if (ev == EV_UP)           { b->timer = 0; b->state = IDLE; }       /* exit(MAYBE_P) 를 손으로 */
        else if (ev == EV_TIMEOUT) { b->timer = 0; b->press++; b->state = PRESSED; }
        break;
    case PRESSED:
        if (ev == EV_UP) { b->state = MAYBE_R; b->timer = DEB; }
        break;
    case MAYBE_R:
        if (ev == EV_DOWN)         { b->timer = 0; b->state = PRESSED; }
        else if (ev == EV_TIMEOUT) { b->timer = 0; b->release++; b->state = IDLE; }
        break;
    default:
        break;                                                              /* 깨진 상태 → 무시 */
    }
}
static void sw_tick(sw_btn_t *b) { if (b->timer > 0 && --b->timer == 0) sw_event(b, EV_TIMEOUT); }

static uint32_t g_rng = 2463534242u;
static uint32_t rnd(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }

/* ====================== 연습 2: 표 검증기 ====================== */
enum { V_BAD_NEXT = 1, V_UNREACHABLE = 2 };
static int fsm_validate(const fsm_trans_t *tab, uint8_t ns, uint8_t ne, uint8_t init, uint32_t *unreach) {
    int r = 0;
    uint32_t seen = 1u << init;
    for (unsigned s = 1; s < ns; s++)
        for (unsigned e = 0; e < ne; e++)
            if (tab[CELL(s, e, ne)].next >= ns) r |= V_BAD_NEXT;
    for (bool changed = true; changed; ) {                   /* 고정점까지 반복 = 그래프 도달성 */
        changed = false;
        for (unsigned s = 1; s < ns; s++) {
            if (!(seen & (1u << s))) continue;
            for (unsigned e = 0; e < ne; e++) {
                uint8_t nx = tab[CELL(s, e, ne)].next;
                if (nx != 0 && nx < ns && !(seen & (1u << nx))) { seen |= 1u << nx; changed = true; }
            }
        }
    }
    uint32_t all = ((1u << ns) - 1u) & ~1u;                   /* 상태 0 은 예약 → 제외 */
    *unreach = all & ~seen;
    if (*unreach) r |= V_UNREACHABLE;
    return r;
}
static const fsm_trans_t broken_tab[N_ST * N_EV] = {          /* 일부러 망가뜨린 표 */
    [B(IDLE, EV_DOWN)]       = { MAYBE_P, NULL },
    [B(MAYBE_P, EV_TIMEOUT)] = { PRESSED, NULL },
    [B(PRESSED, EV_UP)]      = { 9, NULL },                    /* 존재하지 않는 상태 */
    [B(MAYBE_R, EV_TIMEOUT)] = { IDLE, NULL },                 /* MAYBE_R 로 들어오는 칸이 없다 */
};

/* ====================== 연습 3: guard 가 있는 희소 전이 리스트 ====================== */
enum { TS_LOCKED = 1, TS_UNLOCKED };
enum { TS_COIN, TS_PUSH };
#define PRICE 50u
typedef struct { uint8_t state; unsigned credit, coin, unlocks, alarms; } ts_t;
typedef struct {
    uint8_t state, ev;
    bool  (*guard)(const ts_t *);       /* NULL = 항상 참 */
    uint8_t next;                       /* 0 = 상태 유지 (내부 전이) */
    void  (*action)(ts_t *);
} ts_row_t;
static bool enough(const ts_t *t)  { return t->credit + t->coin >= PRICE; }
static void add_credit(ts_t *t)    { t->credit += t->coin; }
static void take_fare(ts_t *t)     { t->credit = t->credit + t->coin - PRICE; t->unlocks++; }
static void alarm_(ts_t *t)        { t->alarms++; }
static const ts_row_t ts_rows[] = {                 /* 같은 (state, ev) 는 위에서부터 — guard 있는 행이 먼저 */
    { TS_LOCKED,   TS_COIN, enough, TS_UNLOCKED, take_fare },
    { TS_LOCKED,   TS_COIN, NULL,   0,           add_credit },
    { TS_LOCKED,   TS_PUSH, NULL,   0,           alarm_ },
    { TS_UNLOCKED, TS_COIN, NULL,   0,           add_credit },
    { TS_UNLOCKED, TS_PUSH, NULL,   TS_LOCKED,   NULL },
};
static int ts_dispatch(ts_t *t, uint8_t ev, unsigned coin) {     /* 반환: 실행한 행 번호, 없으면 -1 */
    t->coin = coin;
    for (size_t i = 0; i < sizeof ts_rows / sizeof ts_rows[0]; i++) {
        const ts_row_t *r = &ts_rows[i];
        if (r->state != t->state || r->ev != ev) continue;
        if (r->guard && !r->guard(t)) continue;
        if (r->action) r->action(t);
        if (r->next) t->state = r->next;
        return (int)i;
    }
    return -1;
}

/* ====================== 연습 4: 전이 기록 링 버퍼 ====================== */
#define HIST_N 8u
static struct { uint8_t from, ev, to; } g_hist[HIST_N];
static uint32_t g_hist_count;                                   /* 지금까지 총 전이 수 */
static void hist_hook(fsm_t *m, uint8_t from, uint8_t ev, uint8_t to) {
    (void)m;
    g_hist[g_hist_count % HIST_N].from = from;
    g_hist[g_hist_count % HIST_N].ev = ev;
    g_hist[g_hist_count % HIST_N].to = to;
    g_hist_count++;
}
static void hist_dump(char *out, size_t cap) {                   /* 오래된 것 → 최신 */
    static const char L[] = "?IdPuL";
    uint32_t n = g_hist_count < HIST_N ? g_hist_count : HIST_N;
    uint32_t first = g_hist_count - n;
    out[0] = '\0';
    for (uint32_t k = 0; k < n; k++) {
        size_t len = strlen(out);
        const uint32_t i = (first + k) % HIST_N;
        snprintf(out + len, cap - len, "%s%c>%c", k ? " " : "", L[g_hist[i].from], L[g_hist[i].to]);
    }
}

/* ====================== 연습 5: long press = 표에 행 추가 ====================== */
static const fsm_trans_t long_tab[N_ST * N_EV] = {
    [B(IDLE, EV_DOWN)]         = { MAYBE_P, NULL },
    [B(MAYBE_P, EV_UP)]        = { IDLE, NULL },
    [B(MAYBE_P, EV_TIMEOUT)]   = { PRESSED, e_press },
    [B(PRESSED, EV_UP)]        = { MAYBE_R, NULL },
    [B(PRESSED, EV_TIMEOUT)]   = { LONG_HELD, e_long },        /* 새 행 */
    [B(LONG_HELD, EV_UP)]      = { MAYBE_R, NULL },            /* 새 행 */
    [B(MAYBE_R, EV_DOWN)]      = { PRESSED, NULL },
    [B(MAYBE_R, EV_TIMEOUT)]   = { IDLE, e_rel },
};
static const fsm_state_t long_st[N_ST] = {
    [IDLE] = { "IDLE", NULL, NULL },           [MAYBE_P] = { "MAYBE_P", t_deb, t_stop },
    [PRESSED] = { "PRESSED", t_long, t_stop }, [MAYBE_R] = { "MAYBE_R", t_deb, t_stop },  /* PRESSED 에 타이머 */
    [LONG_HELD] = { "LONG_HELD", NULL, NULL },
};

/* ---------------- 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static void run(fsm_t *m, bctx_t *b, const fsm_trans_t *tab, const fsm_state_t *sts, const char *in, char *st) {
    static const char L[] = "?IdPuL";
    memset(b, 0, sizeof *b);
    fsm_init(m, tab, sts, N_ST, N_EV, IDLE, b);
    size_t k = 0;
    for (const char *p = in; *p; p++) {
        if (*p == 'D') (void)fsm_dispatch(m, EV_DOWN);
        else if (*p == 'U') (void)fsm_dispatch(m, EV_UP);
        else if (*p == 't') btn_tick(m);
        st[k++] = *p == ' ' ? ' ' : L[m->state];
    }
    st[k] = '\0';
}

int main(void) {
    fsm_t m; bctx_t b; char st[96];

    /* 연습 1 */
    {
        sw_btn_t s = { IDLE, 0, 0, 0 };
        memset(&b, 0, sizeof b);
        fsm_init(&m, btn_tab, btn_st, N_ST, N_EV, IDLE, &b);
        int mismatch = 0;
        for (int i = 0; i < 10000; i++) {
            uint32_t r = rnd() % 8u;
            if (r == 0)      { (void)fsm_dispatch(&m, EV_DOWN); sw_event(&s, EV_DOWN); }
            else if (r == 1) { (void)fsm_dispatch(&m, EV_UP);   sw_event(&s, EV_UP); }
            else             { btn_tick(&m);                     sw_tick(&s); }
            if (s.state != m.state || s.timer != b.timer || s.press != b.press || s.release != b.release)
                mismatch++;
        }
        CHECK(mismatch == 0);
        CHECK(b.press > 100 && b.release > 100);        /* 실제로 많은 전이를 거쳤는가 */
    }

    /* 연습 2 */
    {
        uint32_t un = 0;
        CHECK(fsm_validate(btn_tab, LONG_HELD, N_EV, IDLE, &un) == 0 && un == 0);   /* LONG_HELD 빼고 4상태 */
        CHECK(fsm_validate(btn_tab, N_ST, N_EV, IDLE, &un) == V_UNREACHABLE && un == (1u << LONG_HELD));
        CHECK(fsm_validate(broken_tab, LONG_HELD, N_EV, IDLE, &un) == (V_BAD_NEXT | V_UNREACHABLE));
        CHECK(un == (1u << MAYBE_R));
        CHECK(fsm_validate(long_tab, N_ST, N_EV, IDLE, &un) == 0 && un == 0);
    }

    /* 연습 3 */
    {
        ts_t t = { TS_LOCKED, 0, 0, 0, 0 };
        CHECK(ts_dispatch(&t, TS_PUSH, 0) == 2 && t.alarms == 1 && t.state == TS_LOCKED);
        CHECK(ts_dispatch(&t, TS_COIN, 25) == 1 && t.state == TS_LOCKED && t.credit == 25);
        CHECK(ts_dispatch(&t, TS_COIN, 25) == 0 && t.state == TS_UNLOCKED && t.credit == 0);
        CHECK(ts_dispatch(&t, TS_PUSH, 0) == 4 && t.state == TS_LOCKED && t.unlocks == 1);
        unsigned coins[] = { 10, 10, 10, 25 };
        for (int i = 0; i < 4; i++) (void)ts_dispatch(&t, TS_COIN, coins[i]);
        CHECK(t.state == TS_UNLOCKED && t.credit == 5 && t.unlocks == 2);       /* 55 - 50 = 5 남음 */
        CHECK(ts_dispatch(&t, TS_COIN, 100) == 3 && t.credit == 105 && t.state == TS_UNLOCKED);
    }

    /* 연습 4 */
    {
        g_hist_count = 0;
        memset(&b, 0, sizeof b);
        run(&m, &b, btn_tab, btn_st, "", st);
        m.hook = hist_hook;
        const char *in = "DtttUtttDtttUtttDttt";                /* 전이 10번 */
        for (const char *p = in; *p; p++) {
            if (*p == 'D') (void)fsm_dispatch(&m, EV_DOWN);
            else if (*p == 'U') (void)fsm_dispatch(&m, EV_UP);
            else btn_tick(&m);
        }
        char h[128];
        hist_dump(h, sizeof h);
        CHECK(g_hist_count == 10);
        CHECK(strcmp(h, "P>u u>I I>d d>P P>u u>I I>d d>P") == 0);  /* 마지막 8개, 오래된 것부터 */
    }

    /* 연습 5 */
    run(&m, &b, long_tab, long_st, "Dttt ttttt Uttt", st);
    CHECK(strcmp(st, "dddP PPPPL uuuI") == 0 && strcmp(b.out, "PLR") == 0);
    run(&m, &b, long_tab, long_st, "Dttt tttt Uttt", st);         /* 4 tick 만 → long 아님 */
    CHECK(strcmp(st, "dddP PPPP uuuI") == 0 && strcmp(b.out, "PR") == 0);
    run(&m, &b, long_tab, long_st, "Dttt tt UtD ttt", st);       /* 뗄 때 튀면 long 타이머 새로 */
    CHECK(strcmp(st, "dddP PP uuP PPP") == 0 && strcmp(b.out, "P") == 0 && b.timer == 2);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
