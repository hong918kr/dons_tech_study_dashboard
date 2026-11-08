/* C-M07 테이블 기반 상태 머신: State × Event
 *
 * 작은 FSM 엔진 하나(fsm_step / fsm_dispatch)를 세 머신이 같이 쓴다.
 *   Part A  버튼 디바운스       IDLE → MAYBE_PRESSED → PRESSED → MAYBE_RELEASED  (EXTI 엣지 + SysTick)
 *   Part B  링크 감시 머신      엔진의 가장자리 규칙: 무시 / 내부 전이 / 자기 전이 / 잘못된 입력 / 재진입
 *   Part C  UART 프레임 수신기  SYNC LEN PAYLOAD CRC8 을 RX ISR 처럼 1바이트씩 먹인다
 * 머신 = 데이터(전이 표 + 상태 표) + 작은 액션 함수들. 엔진은 어떤 머신인지 모른다.
 * 함수 포인터 typedef · 콜백 + context 기초는 C-M06 참고.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ============================ 공용 FSM 엔진 ============================ */
typedef struct fsm fsm_t;
typedef void (*fsm_action_t)(fsm_t *m);

typedef struct {               /* 전이 표의 칸 하나: state × event → {next, action}          */
    uint8_t      next;         /* 다음 상태. FSM_NO_STATE(0) 이면 상태를 바꾸지 않는다       */
    fsm_action_t action;       /* 전이 액션 (NULL 가능)                                     */
} fsm_trans_t;                 /* {0, NULL} = 빈 칸 = 무시     {0, act} = 내부 전이          */

typedef struct {
    const char  *name;
    fsm_action_t entry;        /* 이 상태로 들어올 때 (NULL 가능) */
    fsm_action_t exit;         /* 이 상태에서 나갈 때 (NULL 가능) */
} fsm_state_t;

#define FSM_NO_STATE 0u        /* 상태 번호 0 은 예약 → 0 으로 채워진 칸이 저절로 "무시" 가 된다 */
#define FSM_QLEN     4u        /* 액션 안에서 발생한 이벤트를 잠시 담는 큐 */
#define FSM_CELL(s, e, n_ev) ((s) * (n_ev) + (e))      /* 1차원 표에서 [s][e] 의 위치 */

enum { FSM_TRANSITION = 1,     /* 외부 전이 exit → action → entry (자기 전이 포함) */
       FSM_HANDLED    = 2,     /* 내부 전이: action 만                               */
       FSM_IGNORED    = 3,     /* 빈 칸                                              */
       FSM_QUEUED     = 4,     /* dispatch 도중에 들어온 이벤트 → 큐에 넣음          */
       FSM_EINVAL     = -1,    /* 범위 밖 이벤트 / 깨진 상태 값                      */
       FSM_EFULL      = -2 };  /* 큐가 가득 참                                       */

struct fsm {
    const fsm_trans_t *table;  /* n_states * n_events 칸, 행 = 상태 (행 0 은 안 씀) */
    const fsm_state_t *states; /* n_states 개 (0번은 예약)                          */
    uint8_t  n_states, n_events;
    uint8_t  state;            /* 현재 상태                                         */
    bool     busy;             /* fsm_dispatch 실행 중인가 (재진입 감지)            */
    uint8_t  q[FSM_QLEN], q_head, q_count;
    void    *ctx;              /* 머신별 데이터 (버튼 / 링크 / 수신기)              */
};

static void fsm_init(fsm_t *m, const fsm_trans_t *table, const fsm_state_t *states,
                     uint8_t n_states, uint8_t n_events, uint8_t initial, void *ctx) {
    memset(m, 0, sizeof *m);
    m->table = table;  m->states = states;
    m->n_states = n_states;  m->n_events = n_events;
    m->ctx = ctx;
    m->state = initial;
    if (states[initial].entry) states[initial].entry(m);    /* 초기 상태의 entry 도 실행 */
}

/* ---- Part A: 이벤트 1개를 표대로 처리 (ev 와 state 는 이미 검사되었다고 가정) ----
 * 빈 칸 {0,NULL}  → FSM_IGNORED (아무것도 안 함)
 * 내부 전이 {0,a} → a() 만 → FSM_HANDLED
 * 외부 전이 {s,a} → exit(현재) → a() → state = s → entry(s) → FSM_TRANSITION (s == 현재여도 동일) */
static int fsm_step(fsm_t *m, unsigned ev) {
    const fsm_trans_t *t = &m->table[FSM_CELL(m->state, ev, m->n_events)];
    if (t->next == FSM_NO_STATE) {
        if (t->action == NULL) return FSM_IGNORED;             /* 빈 칸 */
        t->action(m);                                          /* 내부 전이: exit/entry 없음 */
        return FSM_HANDLED;
    }
    if (m->states[m->state].exit) m->states[m->state].exit(m); /* ① 나가는 상태의 exit */
    if (t->action) t->action(m);                               /* ② 전이 액션 (state 는 아직 이전 값) */
    m->state = t->next;                                        /* ③ 상태 변경 */
    if (m->states[m->state].entry) m->states[m->state].entry(m); /* ④ 들어온 상태의 entry */
    return FSM_TRANSITION;
}

/* ---- Part B: 외부에서 부르는 입구 ----
 * ① ev >= n_events, state == 0, state >= n_states → FSM_EINVAL (표를 읽기 전에)
 * ② 이미 dispatch 중(액션이 다시 부름) → 큐에 넣고 FSM_QUEUED, 큐가 차 있으면 FSM_EFULL
 * ③ 아니면 fsm_step 하고, 큐가 빌 때까지 하나씩 마저 처리 (run-to-completion). 반환 = 첫 이벤트의 결과 */
static int fsm_dispatch(fsm_t *m, unsigned ev) {
    if (ev >= m->n_events || m->state == FSM_NO_STATE || m->state >= m->n_states)
        return FSM_EINVAL;                                     /* 표 밖을 읽기 전에 거른다 */
    if (m->busy) {                                             /* 재진입: 지금 처리 중인 전이를 끝낸 뒤에 */
        if (m->q_count == FSM_QLEN) return FSM_EFULL;
        m->q[(m->q_head + m->q_count) % FSM_QLEN] = (uint8_t)ev;
        m->q_count++;
        return FSM_QUEUED;
    }
    m->busy = true;
    int r = fsm_step(m, ev);
    while (m->q_count > 0) {                                   /* 한 이벤트가 끝나야 다음 이벤트 */
        uint8_t e = m->q[m->q_head];
        m->q_head = (uint8_t)((m->q_head + 1u) % FSM_QLEN);
        m->q_count--;
        (void)fsm_step(m, e);
    }
    m->busy = false;
    return r;
}

/* ===================== Part A: 버튼 디바운스 머신 ===================== */
enum { BTN_IDLE = 1, BTN_MAYBE_PRESSED, BTN_PRESSED, BTN_MAYBE_RELEASED, BTN_N_STATES };
enum { BTN_EV_DOWN, BTN_EV_UP, BTN_EV_TIMEOUT, BTN_N_EVENTS };
#define DEBOUNCE_TICKS 3u
#define BT(s, e) FSM_CELL(s, e, BTN_N_EVENTS)

typedef struct {
    uint8_t timer;             /* 디바운스 타이머: 남은 tick 수, 0 = 멈춤   */
    char    out[32];           /* 앱에 알린 이벤트: 'P' = press, 'R' = release */
} btn_ctx_t;

static void out_add(char *s, size_t cap, char c) {
    size_t n = strlen(s);
    if (n + 1 < cap) { s[n] = c; s[n + 1] = '\0'; }
}
static void btn_timer_start(fsm_t *m)  { ((btn_ctx_t *)m->ctx)->timer = DEBOUNCE_TICKS; }
static void btn_timer_stop(fsm_t *m)   { ((btn_ctx_t *)m->ctx)->timer = 0; }
static void btn_emit_press(fsm_t *m)   { btn_ctx_t *b = m->ctx; out_add(b->out, sizeof b->out, 'P'); }
static void btn_emit_release(fsm_t *m) { btn_ctx_t *b = m->ctx; out_add(b->out, sizeof b->out, 'R'); }

/* 전이 표 — 적지 않은 칸은 0 = 무시 */
static const fsm_trans_t btn_table[BTN_N_STATES * BTN_N_EVENTS] = {
    [BT(BTN_IDLE, BTN_EV_DOWN)]              = { BTN_MAYBE_PRESSED, NULL },
    [BT(BTN_MAYBE_PRESSED, BTN_EV_UP)]       = { BTN_IDLE, NULL },               /* 튐 → 없던 일 */
    [BT(BTN_MAYBE_PRESSED, BTN_EV_TIMEOUT)]  = { BTN_PRESSED, btn_emit_press },
    [BT(BTN_PRESSED, BTN_EV_UP)]             = { BTN_MAYBE_RELEASED, NULL },
    [BT(BTN_MAYBE_RELEASED, BTN_EV_DOWN)]    = { BTN_PRESSED, NULL },            /* 튐 → 계속 눌림 */
    [BT(BTN_MAYBE_RELEASED, BTN_EV_TIMEOUT)] = { BTN_IDLE, btn_emit_release },
};
/* 상태 표 — 이름, entry, exit */
static const fsm_state_t btn_states[BTN_N_STATES] = {
    [BTN_IDLE]           = { "IDLE",           NULL, NULL },
    [BTN_MAYBE_PRESSED]  = { "MAYBE_PRESSED",  btn_timer_start, btn_timer_stop },
    [BTN_PRESSED]        = { "PRESSED",        NULL, NULL },
    [BTN_MAYBE_RELEASED] = { "MAYBE_RELEASED", btn_timer_start, btn_timer_stop },
};

/* (주어진 코드) EXTI 엣지 인터럽트: 핀 레벨이 바뀔 때마다 */
static void btn_edge(fsm_t *m, bool pressed) {
    (void)fsm_dispatch(m, pressed ? BTN_EV_DOWN : BTN_EV_UP);
}
/* SysTick(1 ms) 마다: 타이머가 돌고 있으면 1 줄이고, 0 이 되는 순간 TIMEOUT 이벤트 1번 */
static void btn_tick(fsm_t *m) {
    btn_ctx_t *b = m->ctx;
    if (b->timer > 0 && --b->timer == 0)
        (void)fsm_dispatch(m, BTN_EV_TIMEOUT);
}

/* ============ Part B: 링크 감시 머신 (주어진 코드 — 엔진 규칙 시험용) ============ */
enum { LINK_DOWN = 1, LINK_UP, LINK_N_STATES };
enum { LINK_EV_HB, LINK_EV_DATA, LINK_EV_TIMEOUT, LINK_N_EVENTS };
#define LINK_WDT 5
#define LT(s, e) FSM_CELL(s, e, LINK_N_EVENTS)

typedef struct {
    char trace[128];           /* 실행된 entry/exit/action 기록: "xDOWN aUP eUP" */
    int  wdt;                  /* 링크 워치독 남은 tick (UP 의 entry 가 시작)    */
    int  rx, pending, queued, dropped;
} link_ctx_t;

static void tr_add(char *s, size_t cap, const char *tok) {
    size_t n = strlen(s);
    snprintf(s + n, cap - n, "%s%s", n ? " " : "", tok);
}
#define LTR(m, tok) tr_add(((link_ctx_t *)(m)->ctx)->trace, sizeof ((link_ctx_t *)0)->trace, (tok))

static void link_enter_down(fsm_t *m) { LTR(m, "eDOWN"); }
static void link_exit_down(fsm_t *m)  { LTR(m, "xDOWN"); }
static void link_enter_up(fsm_t *m)   { LTR(m, "eUP");  ((link_ctx_t *)m->ctx)->wdt = LINK_WDT; }
static void link_exit_up(fsm_t *m)    { LTR(m, "xUP");  ((link_ctx_t *)m->ctx)->wdt = 0; }
static void link_on_hb(fsm_t *m)      { LTR(m, "aHB"); }
static void link_on_lost(fsm_t *m)    { LTR(m, "aLOST"); }
static void link_on_data(fsm_t *m)    { LTR(m, "aDATA"); ((link_ctx_t *)m->ctx)->rx++; }
static void link_on_up(fsm_t *m) {    /* 링크가 올라오는 순간, DOWN 동안 쌓인 데이터를 처리해 달라고 요청 */
    link_ctx_t *L = m->ctx;
    LTR(m, "aUP");
    for (; L->pending > 0; L->pending--) {
        int r = fsm_dispatch(m, LINK_EV_DATA);                /* 액션 안에서 dispatch = 재진입 */
        if (r == FSM_QUEUED) L->queued++;
        else if (r == FSM_EFULL) L->dropped++;
    }
}
static const fsm_trans_t link_table[LINK_N_STATES * LINK_N_EVENTS] = {
    [LT(LINK_DOWN, LINK_EV_HB)]      = { LINK_UP, link_on_up },          /* 외부 전이          */
    [LT(LINK_UP, LINK_EV_HB)]        = { LINK_UP, link_on_hb },          /* 자기 전이 → wdt 재시작 */
    [LT(LINK_UP, LINK_EV_DATA)]      = { FSM_NO_STATE, link_on_data },   /* 내부 전이 → wdt 그대로 */
    [LT(LINK_UP, LINK_EV_TIMEOUT)]   = { LINK_DOWN, link_on_lost },
    /* DOWN × DATA, DOWN × TIMEOUT : 빈 칸 = 무시 */
};
static const fsm_state_t link_states[LINK_N_STATES] = {
    [LINK_DOWN] = { "DOWN", link_enter_down, link_exit_down },
    [LINK_UP]   = { "UP",   link_enter_up,   link_exit_up },
};

/* =================== Part C: UART 프레임 수신기 =================== */
/* 프레임: [SYNC=0xA5] [LEN 0..16] [PAYLOAD × LEN] [CRC8(LEN, PAYLOAD)]  — 바이트가 1개씩 RX ISR 로 온다 */
#define RX_SYNC          0xA5u
#define RX_MAX_LEN       16u
#define RX_TIMEOUT_TICKS 3u     /* 프레임 도중 이만큼 tick 동안 바이트가 없으면 버린다 */
enum { RX_HUNT = 1, RX_LEN, RX_PAYLOAD, RX_CRC, RX_N_STATES };
enum { RX_EV_SYNC,      /* 받은 바이트 == 0xA5            (rx_isr 가 분류) */
       RX_EV_BYTE,      /* 받은 바이트 != 0xA5            (rx_isr 가 분류) */
       RX_EV_TIMEOUT,   /* 바이트 사이 간격 초과          (rx_tick)        */
       RX_EV_DONE,      /* 필드 완료 / CRC 일치           (액션이 발생)    */
       RX_EV_EMPTY,     /* LEN == 0 → 페이로드 건너뜀     (액션이 발생)    */
       RX_EV_ERROR,     /* LEN 초과 / CRC 불일치          (액션이 발생)    */
       RX_N_EVENTS };
#define RT(s, e) FSM_CELL(s, e, RX_N_EVENTS)

typedef struct {
    uint8_t byte;                      /* 지금 처리 중인 바이트 (이벤트의 "인자") */
    uint8_t len, idx, crc, idle;
    uint8_t buf[RX_MAX_LEN];
    int     frames, bad_len, bad_crc, timeouts;
    uint8_t last[RX_MAX_LEN], last_len; /* 마지막으로 전달된 프레임 */
} rx_ctx_t;

/* (주어진 코드) CRC-8, 다항식 0x07, 초기값 0 (CRC-8/SMBUS): crc8("123456789") == 0xF4 */
static uint8_t crc8_update(uint8_t crc, uint8_t b) {
    crc ^= b;
    for (int i = 0; i < 8; i++)
        crc = (crc & 0x80u) ? (uint8_t)((crc << 1) ^ 0x07u) : (uint8_t)(crc << 1);
    return crc;
}
static void rx_on_start(fsm_t *m) {    /* LEN 의 entry: 새 프레임 시작 */
    rx_ctx_t *r = m->ctx;
    r->len = r->idx = r->crc = 0;
}
static void rx_deliver(fsm_t *m) {
    rx_ctx_t *r = m->ctx;
    r->frames++;
    memcpy(r->last, r->buf, r->len);
    r->last_len = r->len;
}
static void rx_err_len(fsm_t *m)     { ((rx_ctx_t *)m->ctx)->bad_len++; }
static void rx_err_crc(fsm_t *m)     { ((rx_ctx_t *)m->ctx)->bad_crc++; }
static void rx_err_timeout(fsm_t *m) { ((rx_ctx_t *)m->ctx)->timeouts++; }

/* LEN 상태에서 바이트 1개: 길이 저장 + CRC 갱신 → 판정 이벤트(ERROR / EMPTY / DONE) 발생 */
static void rx_on_len(fsm_t *m) {
    rx_ctx_t *r = m->ctx;
    r->len = r->byte;
    r->crc = crc8_update(r->crc, r->byte);
    if (r->len > RX_MAX_LEN)  (void)fsm_dispatch(m, RX_EV_ERROR);
    else if (r->len == 0)     (void)fsm_dispatch(m, RX_EV_EMPTY);
    else                      (void)fsm_dispatch(m, RX_EV_DONE);
}
/* PAYLOAD 상태에서 바이트 1개: 저장 + CRC 갱신, 다 모이면 DONE */
static void rx_on_data(fsm_t *m) {
    rx_ctx_t *r = m->ctx;
    r->buf[r->idx++] = r->byte;
    r->crc = crc8_update(r->crc, r->byte);
    if (r->idx == r->len) (void)fsm_dispatch(m, RX_EV_DONE);
}
/* CRC 상태에서 바이트 1개: 계산한 CRC 와 비교 → DONE 또는 ERROR */
static void rx_on_crc(fsm_t *m) {
    rx_ctx_t *r = m->ctx;
    (void)fsm_dispatch(m, r->byte == r->crc ? RX_EV_DONE : RX_EV_ERROR);
}

static const fsm_trans_t rx_table[RX_N_STATES * RX_N_EVENTS] = {
    [RT(RX_HUNT, RX_EV_SYNC)]       = { RX_LEN, NULL },             /* HUNT 의 BYTE/TIMEOUT 은 무시 */
    [RT(RX_LEN, RX_EV_SYNC)]        = { FSM_NO_STATE, rx_on_len },  /* 0xA5 도 길이 값일 뿐 (→ 초과) */
    [RT(RX_LEN, RX_EV_BYTE)]        = { FSM_NO_STATE, rx_on_len },
    [RT(RX_LEN, RX_EV_DONE)]        = { RX_PAYLOAD, NULL },
    [RT(RX_LEN, RX_EV_EMPTY)]       = { RX_CRC, NULL },
    [RT(RX_LEN, RX_EV_ERROR)]       = { RX_HUNT, rx_err_len },
    [RT(RX_LEN, RX_EV_TIMEOUT)]     = { RX_HUNT, rx_err_timeout },
    [RT(RX_PAYLOAD, RX_EV_SYNC)]    = { FSM_NO_STATE, rx_on_data }, /* 페이로드 안의 0xA5 는 데이터 */
    [RT(RX_PAYLOAD, RX_EV_BYTE)]    = { FSM_NO_STATE, rx_on_data },
    [RT(RX_PAYLOAD, RX_EV_DONE)]    = { RX_CRC, NULL },
    [RT(RX_PAYLOAD, RX_EV_TIMEOUT)] = { RX_HUNT, rx_err_timeout },
    [RT(RX_CRC, RX_EV_SYNC)]        = { FSM_NO_STATE, rx_on_crc },
    [RT(RX_CRC, RX_EV_BYTE)]        = { FSM_NO_STATE, rx_on_crc },
    [RT(RX_CRC, RX_EV_DONE)]        = { RX_HUNT, rx_deliver },
    [RT(RX_CRC, RX_EV_ERROR)]       = { RX_HUNT, rx_err_crc },
    [RT(RX_CRC, RX_EV_TIMEOUT)]     = { RX_HUNT, rx_err_timeout },
};
static const fsm_state_t rx_states[RX_N_STATES] = {
    [RX_HUNT]    = { "HUNT",    NULL, NULL },
    [RX_LEN]     = { "LEN",     rx_on_start, NULL },
    [RX_PAYLOAD] = { "PAYLOAD", NULL, NULL },
    [RX_CRC]     = { "CRC",     NULL, NULL },
};
/* (주어진 코드) UART RX ISR: DR 에서 읽은 바이트 1개를 분류해서 넘긴다 */
static void rx_isr(fsm_t *m, uint8_t byte) {
    rx_ctx_t *r = m->ctx;
    r->byte = byte;
    r->idle = 0;
    (void)fsm_dispatch(m, byte == RX_SYNC ? RX_EV_SYNC : RX_EV_BYTE);
}
/* (주어진 코드) SysTick: 마지막 바이트 이후 RX_TIMEOUT_TICKS 가 되는 순간 TIMEOUT 1번 (HUNT 에선 무시됨) */
static void rx_tick(fsm_t *m) {
    rx_ctx_t *r = m->ctx;
    if (r->idle < UINT8_MAX) r->idle++;
    if (r->idle == RX_TIMEOUT_TICKS) (void)fsm_dispatch(m, RX_EV_TIMEOUT);
}

/* ======================= 디버그 도구: 표 덤프 ======================= */
static const struct { fsm_action_t fn; const char *name; } act_names[] = {
    { btn_timer_start, "timer_start" }, { btn_timer_stop, "timer_stop" },
    { btn_emit_press, "emit_press" },   { btn_emit_release, "emit_release" },
    { rx_on_start, "on_start" }, { rx_on_len, "on_len" }, { rx_on_data, "on_data" },
    { rx_on_crc, "on_crc" },     { rx_deliver, "deliver" }, { rx_err_len, "err_len" },
    { rx_err_crc, "err_crc" },   { rx_err_timeout, "err_timeout" },
};
static const char *act_name(fsm_action_t fn) {
    if (fn == NULL) return "-";
    for (size_t i = 0; i < sizeof act_names / sizeof act_names[0]; i++)
        if (act_names[i].fn == fn) return act_names[i].name;
    return "?";
}
static void fsm_dump(const fsm_t *m) {          /* 테스트가 실패하면 표를 사람이 읽는 모양으로 출력 */
    printf("  -- table dump (non-empty cells) --\n");
    for (unsigned s = 1; s < m->n_states; s++) {
        const fsm_state_t *st = &m->states[s];
        printf("  %-14s entry=%-11s exit=%s\n", st->name, act_name(st->entry), act_name(st->exit));
        for (unsigned e = 0; e < m->n_events; e++) {
            const fsm_trans_t *t = &m->table[FSM_CELL(s, e, m->n_events)];
            if (t->next == FSM_NO_STATE && t->action == NULL) continue;
            const char *to = t->next == FSM_NO_STATE ? "(internal)"
                           : t->next < m->n_states ? m->states[t->next].name : "(BAD)";
            printf("      ev%u -> %-14s %s\n", e, to, act_name(t->action));
        }
    }
}

/* ============================ 테스트 하네스 ============================ */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

/* 버튼: 입력 문자열 D=눌림 엣지, U=놓임 엣지, t=tick, ' '=구분 → 매 입력 뒤 상태 글자 */
static const char BTN_LETTER[] = "?IdPu";   /* IDLE, d=MAYBE_PRESSED, PRESSED, u=MAYBE_RELEASED */
static void btn_run(fsm_t *m, btn_ctx_t *b, const char *in, char *st, size_t cap) {
    memset(b, 0, sizeof *b);
    fsm_init(m, btn_table, btn_states, BTN_N_STATES, BTN_N_EVENTS, BTN_IDLE, b);
    size_t k = 0;
    for (const char *p = in; *p && k + 1 < cap; p++) {
        if (*p == 'D') btn_edge(m, true);
        else if (*p == 'U') btn_edge(m, false);
        else if (*p == 't') btn_tick(m);
        st[k++] = *p == ' ' ? ' ' : BTN_LETTER[m->state < BTN_N_STATES ? m->state : 0];
    }
    st[k] = '\0';
}
static const struct { const char *in, *states, *out; } btn_cases[] = {
    { "ttt",                 "III",                 ""     },  /* 가만히 있으면 아무 일 없음      */
    { "Dttt",                "dddP",                "P"    },  /* 3 tick 유지 → press 1번         */
    { "Dttt Uttt",           "dddP uuuI",           "PR"   },  /* 깨끗한 누름·뗌                  */
    { "DtU DtU Dttt",        "ddI ddI dddP",        "P"    },  /* 누를 때 튐: 타이머가 매번 새로  */
    { "DttU ttt",            "dddI III",            ""     },  /* 2 tick 짜리 글리치 → 무시       */
    { "Dttt UtD ttt Uttt",   "dddP uuP PPP uuuI",   "PR"   },  /* 뗄 때 튐 → release 1번만        */
    { "Dttt D U U ttt",      "dddP P u u uuI",      "PR"   },  /* 중복 엣지는 빈 칸 → 무시        */
    { "Dttt Uttt Dttt Uttt", "dddP uuuI dddP uuuI", "PRPR" },  /* 두 번 누르기                    */
};

/* 링크 */
static fsm_t g_link;
static link_ctx_t g_lc;
static void link_reset(void) {
    memset(&g_lc, 0, sizeof g_lc);
    fsm_init(&g_link, link_table, link_states, LINK_N_STATES, LINK_N_EVENTS, LINK_DOWN, &g_lc);
}
static bool link_trace_is(const char *s) { bool ok = strcmp(g_lc.trace, s) == 0; g_lc.trace[0] = '\0'; return ok; }

/* 수신기 */
static const char RX_LETTER[] = "?HLPC";
static void rx_reset(fsm_t *m, rx_ctx_t *r) {
    memset(r, 0, sizeof *r);
    fsm_init(m, rx_table, rx_states, RX_N_STATES, RX_N_EVENTS, RX_HUNT, r);
}
static void rx_feed(fsm_t *m, const uint8_t *b, size_t n, char *st) {   /* st: 바이트마다 상태 글자 (NULL 가능) */
    for (size_t i = 0; i < n; i++) {
        rx_isr(m, b[i]);
        if (st) st[i] = RX_LETTER[m->state < RX_N_STATES ? m->state : 0];
    }
    if (st) st[n] = '\0';
}
static size_t make_frame(uint8_t *out, const uint8_t *pl, uint8_t len) {
    uint8_t crc = crc8_update(0, len);
    out[0] = RX_SYNC; out[1] = len;
    for (uint8_t i = 0; i < len; i++) { out[2 + i] = pl[i]; crc = crc8_update(crc, pl[i]); }
    out[2 + len] = crc;
    return (size_t)len + 3u;
}
static uint32_t g_rng = 0xC0FFEE07u;          /* xorshift32, 고정 시드 → 매번 같은 입력 */
static uint32_t rnd(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }

int main(void) {
    fsm_t btn; btn_ctx_t bc; char st[64];
    int f0;
    setvbuf(stdout, NULL, _IONBF, 0);   /* 중간에 죽어도 그때까지의 출력은 보이게 */

    /* ---------------- Part A: 버튼 디바운스 ---------------- */
    f0 = g_total - g_pass;
    for (size_t i = 0; i < sizeof btn_cases / sizeof btn_cases[0]; i++) {
        btn_run(&btn, &bc, btn_cases[i].in, st, sizeof st);
        CHECK(strcmp(st, btn_cases[i].states) == 0 && strcmp(bc.out, btn_cases[i].out) == 0);
        if (strcmp(st, btn_cases[i].states) != 0 || strcmp(bc.out, btn_cases[i].out) != 0)
            printf("     in \"%s\": states \"%s\" (want \"%s\"), out \"%s\" (want \"%s\")\n",
                   btn_cases[i].in, st, btn_cases[i].states, bc.out, btn_cases[i].out);
    }
    btn_run(&btn, &bc, "D", st, sizeof st);
    CHECK(bc.timer == DEBOUNCE_TICKS);                 /* MAYBE_PRESSED 의 entry 가 타이머 시작 */
    btn_run(&btn, &bc, "DU", st, sizeof st);
    CHECK(bc.timer == 0);                              /* exit 가 타이머를 멈춘다 */
    btn_run(&btn, &bc, "", st, sizeof st);
    CHECK(fsm_dispatch(&btn, BTN_EV_UP) == FSM_IGNORED && btn.state == BTN_IDLE);
    CHECK(fsm_dispatch(&btn, BTN_EV_DOWN) == FSM_TRANSITION && btn.state == BTN_MAYBE_PRESSED);
    if (g_total - g_pass != f0) fsm_dump(&btn);

    /* ---------------- Part B: 엔진 가장자리 규칙 (링크 머신) ---------------- */
    link_reset();
    CHECK(g_link.state == LINK_DOWN && link_trace_is("eDOWN"));          /* init 도 entry 실행 */
    CHECK(fsm_dispatch(&g_link, LINK_EV_DATA) == FSM_IGNORED);          /* ① 빈 칸 */
    CHECK(g_link.state == LINK_DOWN && link_trace_is(""));
    CHECK(fsm_dispatch(&g_link, LINK_EV_HB) == FSM_TRANSITION);         /* 외부 전이 순서 */
    CHECK(link_trace_is("xDOWN aUP eUP") && g_link.state == LINK_UP && g_lc.wdt == LINK_WDT);
    g_lc.wdt = 2;                                                        /* 시간이 좀 흘렀다고 치자 */
    CHECK(fsm_dispatch(&g_link, LINK_EV_DATA) == FSM_HANDLED);          /* ② 내부 전이 */
    CHECK(link_trace_is("aDATA") && g_lc.wdt == 2 && g_lc.rx == 1);      /*   exit/entry 없음 → wdt 그대로 */
    CHECK(fsm_dispatch(&g_link, LINK_EV_HB) == FSM_TRANSITION);         /* ③ 자기 전이 */
    CHECK(link_trace_is("xUP aHB eUP") && g_lc.wdt == LINK_WDT);         /*   exit+entry → wdt 재시작 */
    CHECK(fsm_dispatch(&g_link, 7) == FSM_EINVAL);                      /* ④ 범위 밖 이벤트 */
    CHECK(fsm_dispatch(&g_link, LINK_N_EVENTS) == FSM_EINVAL);          /*   경계값 */
    CHECK(g_link.state == LINK_UP && link_trace_is(""));
    g_link.state = 0;                                                    /*   깨진 상태 값 (메모리 손상) */
    CHECK(fsm_dispatch(&g_link, LINK_EV_HB) == FSM_EINVAL);
    g_link.state = 200;
    CHECK(fsm_dispatch(&g_link, LINK_EV_HB) == FSM_EINVAL && link_trace_is(""));
    link_reset(); g_lc.trace[0] = '\0';
    g_lc.pending = 2;                                                    /* ⑤ 액션 안에서 dispatch */
    CHECK(fsm_dispatch(&g_link, LINK_EV_HB) == FSM_TRANSITION);
    CHECK(link_trace_is("xDOWN aUP eUP aDATA aDATA"));                   /*   전이를 끝낸 뒤 UP 에서 처리 */
    CHECK(g_lc.rx == 2 && g_lc.queued == 2 && g_link.state == LINK_UP && !g_link.busy);
    link_reset(); g_lc.trace[0] = '\0';
    g_lc.pending = 6;                                                    /* ⑥ 큐 넘침 (FSM_QLEN = 4) */
    CHECK(fsm_dispatch(&g_link, LINK_EV_HB) == FSM_TRANSITION);
    CHECK(g_lc.rx == 4 && g_lc.queued == 4 && g_lc.dropped == 2);
    g_lc.trace[0] = '\0';
    CHECK(fsm_dispatch(&g_link, LINK_EV_TIMEOUT) == FSM_TRANSITION);    /* 큐를 다 비운 뒤엔 정상 동작 */
    CHECK(link_trace_is("xUP aLOST eDOWN") && g_link.state == LINK_DOWN && g_lc.wdt == 0);

    /* ---------------- Part C: UART 프레임 수신기 ---------------- */
    fsm_t rx; rx_ctx_t rc;
    uint8_t fr[64];
    size_t n;
    f0 = g_total - g_pass;
    CHECK(crc8_update(crc8_update(crc8_update(crc8_update(crc8_update(crc8_update(crc8_update(
          crc8_update(crc8_update(0, '1'), '2'), '3'), '4'), '5'), '6'), '7'), '8'), '9') == 0xF4);
    {   /* 고정 벡터 + 바이트마다 상태 */
        const uint8_t f1[] = { 0xA5, 0x03, 0x11, 0x22, 0x33, 0xEE };
        rx_reset(&rx, &rc);
        rx_feed(&rx, f1, sizeof f1, st);
        CHECK(strcmp(st, "LPPPCH") == 0);
        CHECK(rc.frames == 1 && rc.last_len == 3 && memcmp(rc.last, f1 + 2, 3) == 0);
    }
    {   /* 앞쪽 잡음 + 페이로드 속 0xA5 */
        const uint8_t pl[] = { 0xA5, 0xA5 };
        const uint8_t noise[] = { 0x00, 0xFF, 0x12 };
        rx_reset(&rx, &rc);
        rx_feed(&rx, noise, sizeof noise, st);
        CHECK(strcmp(st, "HHH") == 0);
        n = make_frame(fr, pl, 2);
        rx_feed(&rx, fr, n, st);
        CHECK(strcmp(st, "LPPCH") == 0 && rc.frames == 1 && rc.last_len == 2 && memcmp(rc.last, pl, 2) == 0);
    }
    {   /* LEN == 0 → 페이로드 건너뜀 */
        const uint8_t f0b[] = { 0xA5, 0x00, 0x00 };
        rx_reset(&rx, &rc);
        rx_feed(&rx, f0b, sizeof f0b, st);
        CHECK(strcmp(st, "LCH") == 0 && rc.frames == 1 && rc.last_len == 0);
    }
    {   /* LEN 경계: 16 OK, 17 거절, 그다음 프레임은 정상 */
        uint8_t pl[RX_MAX_LEN];
        for (int i = 0; i < (int)RX_MAX_LEN; i++) pl[i] = (uint8_t)(i * 7);
        rx_reset(&rx, &rc);
        n = make_frame(fr, pl, RX_MAX_LEN);
        rx_feed(&rx, fr, n, NULL);
        CHECK(rc.frames == 1 && rc.last_len == RX_MAX_LEN && memcmp(rc.last, pl, RX_MAX_LEN) == 0);
        const uint8_t bad[] = { 0xA5, 17, 0x01, 0x02 };
        rx_feed(&rx, bad, sizeof bad, st);
        CHECK(strcmp(st, "LHHH") == 0 && rc.bad_len == 1);
        const uint8_t bad2[] = { 0xA5, 0xA5 };                     /* 길이 자리에 SYNC → 165 > 16 */
        rx_feed(&rx, bad2, sizeof bad2, st);
        CHECK(strcmp(st, "LH") == 0 && rc.bad_len == 2);
        n = make_frame(fr, pl, 4);
        rx_feed(&rx, fr, n, NULL);
        CHECK(rc.frames == 2 && rc.last_len == 4);
    }
    {   /* CRC 불일치 → 버리고 다음 프레임은 정상 */
        const uint8_t pl[] = { 1, 2, 3 };
        rx_reset(&rx, &rc);
        n = make_frame(fr, pl, 3);
        fr[n - 1] ^= 0x01u;
        rx_feed(&rx, fr, n, st);
        CHECK(strcmp(st, "LPPPCH") == 0 && rc.frames == 0 && rc.bad_crc == 1);
        fr[n - 1] ^= 0x01u;
        rx_feed(&rx, fr, n, NULL);
        CHECK(rc.frames == 1 && rc.bad_crc == 1);
    }
    {   /* 타임아웃: 프레임 도중 3 tick 침묵 → HUNT, 2 tick 은 괜찮다, HUNT 에선 안 센다 */
        const uint8_t pl[] = { 9, 8, 7 };
        rx_reset(&rx, &rc);
        for (int i = 0; i < 10; i++) rx_tick(&rx);
        CHECK(rc.timeouts == 0 && rx.state == RX_HUNT);
        n = make_frame(fr, pl, 3);
        rx_feed(&rx, fr, 3, NULL);                                  /* A5 03 09 까지만 */
        rx_tick(&rx); rx_tick(&rx);
        CHECK(rx.state == RX_PAYLOAD && rc.timeouts == 0);
        rx_tick(&rx);
        CHECK(rx.state == RX_HUNT && rc.timeouts == 1);
        for (size_t i = 0; i < n; i++) { rx_isr(&rx, fr[i]); rx_tick(&rx); rx_tick(&rx); }   /* 느리지만 제때 */
        CHECK(rc.frames == 1 && rc.timeouts == 1 && memcmp(rc.last, pl, 3) == 0);
    }
    {   /* 퍼즈: 300 프레임, 사이에 잡음 0~3 바이트(0xA5 제외), 바이트 사이 0~2 tick,
           7번째마다 페이로드 1바이트 손상 → 정확히 그 프레임만 CRC 에러 */
        rx_reset(&rx, &rc);
        int want_frames = 0, want_bad = 0, mismatch = 0;
        for (int k = 0; k < 300; k++) {
            uint8_t pl[RX_MAX_LEN];
            uint8_t len = (uint8_t)(rnd() % (RX_MAX_LEN + 1u));
            for (uint8_t i = 0; i < len; i++) pl[i] = (uint8_t)rnd();
            for (uint32_t z = rnd() % 4u; z > 0; z--) {
                uint8_t nb = (uint8_t)rnd();
                rx_isr(&rx, nb == RX_SYNC ? 0x00 : nb);
            }
            n = make_frame(fr, pl, len);
            bool corrupt = (k % 7 == 6) && len > 0;
            if (corrupt) fr[2 + rnd() % len] ^= (uint8_t)(1u + rnd() % 255u);
            for (size_t i = 0; i < n; i++) {
                rx_isr(&rx, fr[i]);
                for (uint32_t t = rnd() % 3u; t > 0; t--) rx_tick(&rx);
            }
            if (corrupt) want_bad++;
            else {
                want_frames++;
                if (rc.last_len != len || memcmp(rc.last, pl, len) != 0) mismatch++;
            }
        }
        CHECK(rc.frames == want_frames && rc.bad_crc == want_bad);
        CHECK(mismatch == 0 && rc.bad_len == 0 && rc.timeouts == 0 && rx.state == RX_HUNT);
    }
    if (g_total - g_pass != f0) fsm_dump(&rx);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
