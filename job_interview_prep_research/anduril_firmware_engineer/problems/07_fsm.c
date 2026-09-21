// 07_fsm.c  —  PRACTICE STUB (여기 빈칸을 채우세요)
// 유한 상태 기계 (Finite State Machine, FSM)  —  Q76~Q85
// ---------------------------------------------------------------------------
// 빌드: cc -std=c11 -Wall -Wextra 07_fsm.c -o /tmp/andb_fsm
// 각 함수의 // TODO 를 구현하고 다시 빌드/실행해 FAIL -> PASS 로 바꾸세요.
// 지금 상태로도 컴파일/실행은 되며(placeholder 반환), 대부분 [FAIL] 로 나옵니다.
//
// 임베디드 힌트:
//   - switch 기반 FSM 은 상태별 case 안에서 입력에 따라 다음 상태를 정한다.
//   - 함수 포인터 테이블: table[state](input) 로 O(1) 디스패치.
//   - 프로토콜 파서: SYNC -> LEN -> DATA(러닝 XOR) -> CHECKSUM 순으로 프레이밍.
//   - Moore 출력=f(상태), Mealy 출력=f(상태,입력). HSM 은 자식->부모 버블링.
// ---------------------------------------------------------------------------
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

// ===========================================================================
// 테스트 하네스 (PASS/FAIL)
// ===========================================================================
static int g_pass = 0;
static int g_fail = 0;
#define T(label, cond) do {                                   \
    if (cond) { printf("  [PASS] %s\n", (label)); g_pass++; } \
    else      { printf("  [FAIL] %s\n", (label)); g_fail++; } \
} while (0)

// ===========================================================================
// Q76. 버튼 디바운싱 FSM (switch 기반)
//   RELEASED --high--> BOUNCE_PRESS --(N tick 연속 high)--> PRESSED
//   PRESSED  --low --> BOUNCE_RELEASE --(N tick 연속 low)--> RELEASED
//   BOUNCE_* 중 반대 입력이 오면 글리치로 보고 원래 상태로 복귀.
//   pressed(bool) 이 디바운스된 출력.
// ===========================================================================
typedef enum {
    BTN_RELEASED,
    BTN_BOUNCE_PRESS,
    BTN_PRESSED,
    BTN_BOUNCE_RELEASE
} btn_state_t;

typedef struct {
    btn_state_t state;
    uint8_t     counter;
    uint8_t     ticks;
    bool        pressed;
} button_fsm_t;

void button_fsm_init(button_fsm_t *fsm, uint8_t debounce_ticks) {
    (void)fsm; (void)debounce_ticks;
    // TODO: state=RELEASED, counter=0, ticks=debounce_ticks, pressed=false (NULL 방어)
}

void button_fsm_update(button_fsm_t *fsm, bool gpio_high) {
    (void)fsm; (void)gpio_high;
    // TODO: 4-상태 switch. BOUNCE_* 에서 ++counter>=ticks 로 확정, 반대 입력이면 취소.
}

// ===========================================================================
// Q77. 함수 포인터 배열 FSM (state table)  — 모터 IDLE/RUN/STOP
//   각 핸들러: 현재 상태에서 입력을 받아 "다음 상태" 반환.
//   IDLE --START--> RUN,  RUN --STOP--> STOP,  STOP --RESET--> IDLE.
// ===========================================================================
typedef enum { M_IDLE, M_RUN, M_STOP, M_COUNT } motor_state_t;
typedef enum { IN_START, IN_STOP, IN_RESET }   motor_input_t;

static motor_state_t h_idle(motor_input_t in) { (void)in; return M_IDLE; /* TODO: START->RUN */ }
static motor_state_t h_run (motor_input_t in) { (void)in; return M_RUN;  /* TODO: STOP->STOP */ }
static motor_state_t h_stop(motor_input_t in) { (void)in; return M_STOP; /* TODO: RESET->IDLE */ }

typedef motor_state_t (*motor_handler_t)(motor_input_t);

typedef struct {
    motor_state_t   state;
    motor_handler_t table[M_COUNT];
} motor_fsm_t;

void motor_fsm_init(motor_fsm_t *fsm) {
    (void)fsm; (void)h_idle; (void)h_run; (void)h_stop;  // (구현 시 table 에 연결)
    // TODO: state=M_IDLE, table[i]=핸들러 지정
}

motor_state_t motor_fsm_dispatch(motor_fsm_t *fsm, motor_input_t in) {
    (void)fsm; (void)in;
    // TODO: fsm->state = fsm->table[fsm->state](in); 반환
    return M_IDLE;   // placeholder
}

// ===========================================================================
// Q78. UART 패킷 파서 FSM  ⭐
//   프레임: [0xAA sync][len(1..MAX)][data x len][checksum = XOR(data)]
//   반환: 이 바이트로 정상 패킷이 방금 완성되면 true.
// ===========================================================================
#define UART_MAX_DATA 16

typedef enum { P_IDLE, P_LEN, P_DATA, P_CKSUM } parser_state_t;

typedef struct {
    parser_state_t state;
    uint8_t  len;
    uint8_t  idx;
    uint8_t  checksum;
    uint8_t  data[UART_MAX_DATA];
    uint8_t  out_data[UART_MAX_DATA];
    uint8_t  out_len;
    uint32_t good_packets;
    uint32_t bad_packets;
} uart_parser_t;

void uart_parser_init(uart_parser_t *p) {
    (void)p;
    // TODO: 전체 0 초기화 후 state=P_IDLE (memset 권장)
}

bool uart_parse_byte(uart_parser_t *p, uint8_t byte) {
    (void)p; (void)byte;
    // TODO: IDLE(0xAA대기)->LEN(1..MAX검증)->DATA(저장+러닝XOR)->CKSUM(비교).
    //       정상 완료 시 out_data/out_len 채우고 good_packets++, true 반환.
    return false;   // placeholder
}

// ===========================================================================
// Q79. 신호등 제어 FSM (타이머 기반)  RED->GREEN->YELLOW->RED
// ===========================================================================
typedef enum { TL_RED, TL_GREEN, TL_YELLOW } tl_state_t;

typedef struct {
    tl_state_t state;
    int32_t timer;
    int32_t red_t, green_t, yellow_t;
} tl_fsm_t;

void tl_init(tl_fsm_t *fsm, int32_t red_t, int32_t green_t, int32_t yellow_t) {
    (void)fsm; (void)red_t; (void)green_t; (void)yellow_t;
    // TODO: state=RED, timer=red_t, 지속시간 저장
}

void tl_tick(tl_fsm_t *fsm) {
    (void)fsm;
    // TODO: --timer<=0 이면 다음 상태로 전이하고 해당 지속시간으로 timer 재장전
}

// ===========================================================================
// Q80. Mealy vs Moore — overlapping "11" 검출기
//   Moore: 출력=f(상태), 상태 3개(S0,S1,S2=검출).
//   Mealy: 출력=f(상태,입력), 상태 2개(S0,S1).
//   두 step 함수 모두 (전이 후 기준) 출력을 반환.
// ===========================================================================
#define MEALY_STATES 2
#define MOORE_STATES 3

typedef enum { MO_S0, MO_S1, MO_S2 } moore_state_t;
typedef struct { moore_state_t state; } moore_fsm_t;

void moore_init(moore_fsm_t *m) { (void)m; /* TODO: state=MO_S0 */ }

int moore_step(moore_fsm_t *m, int bit) {
    (void)m; (void)bit;
    // TODO: S0/S1/S2 전이 후, 출력 = (state==MO_S2)
    return 0;   // placeholder
}

typedef enum { ME_S0, ME_S1 } mealy_state_t;
typedef struct { mealy_state_t state; } mealy_fsm_t;

void mealy_init(mealy_fsm_t *m) { (void)m; /* TODO: state=ME_S0 */ }

int mealy_step(mealy_fsm_t *m, int bit) {
    (void)m; (void)bit;
    // TODO: 출력 = (state==ME_S1 && bit==1). 그 후 전이.
    return 0;   // placeholder
}

// ===========================================================================
// Q81. 계층적 상태 기계 (HSM) — leaf: OFF / (ON 내부) IDLE, RUN
//   leaf 미처리 이벤트는 부모 superstate ON 으로 버블링.
//   EVT_POWER: OFF->IDLE(leaf), IDLE/RUN->OFF(부모).  START/STOP: leaf 특화.
//   반환: 처리 레벨(DISP_LEAF / DISP_PARENT / DISP_UNHANDLED).
// ===========================================================================
typedef enum { ST_OFF, ST_IDLE, ST_RUN } hsm_leaf_t;
typedef enum { EVT_POWER, EVT_START, EVT_STOP } hsm_evt_t;
typedef enum { DISP_UNHANDLED, DISP_LEAF, DISP_PARENT } hsm_disp_t;

typedef struct { hsm_leaf_t state; } hsm_t;

void hsm_init(hsm_t *h) { (void)h; /* TODO: state=ST_OFF */ }

hsm_disp_t hsm_dispatch(hsm_t *h, hsm_evt_t e) {
    (void)h; (void)e;
    // TODO: leaf 먼저 처리 시도, 미처리면 부모 ON(EVT_POWER->OFF) 로 위임.
    return DISP_UNHANDLED;   // placeholder
}

// ===========================================================================
// Q82. entry/exit action FSM
//   전이 시 exit(old)->entry(new) 자동 호출. trace 에 토큰을 남긴다.
//   진입: 대문자(I/R/S), 이탈: 소문자(i/r/s). 자기전이는 무동작.
// ===========================================================================
typedef enum { E_IDLE, E_RUN, E_STOP, E_COUNT } eea_state_t;

typedef struct {
    eea_state_t state;
    char        trace[64];
    size_t      trace_len;
} eea_fsm_t;

// (유틸) trace 에 문자 하나 append — 제공됨.
static void eea_emit(eea_fsm_t *f, char c) {
    if (f->trace_len + 1 < sizeof f->trace) {
        f->trace[f->trace_len++] = c;
        f->trace[f->trace_len]   = '\0';
    }
}
// (유틸) 진입/이탈 토큰 — 제공됨. 구현에서 호출하세요.
static void eea_entry(eea_fsm_t *f, eea_state_t s) {
    eea_emit(f, s == E_IDLE ? 'I' : s == E_RUN ? 'R' : 'S');
}
static void eea_exit(eea_fsm_t *f, eea_state_t s) {
    eea_emit(f, s == E_IDLE ? 'i' : s == E_RUN ? 'r' : 's');
}

void eea_init(eea_fsm_t *f) {
    (void)f; (void)eea_entry;
    // TODO: state=E_IDLE, trace 비우기, 초기 상태 entry 액션 호출
}

void eea_transition(eea_fsm_t *f, eea_state_t next) {
    (void)f; (void)next; (void)eea_exit;
    // TODO: state!=next 일 때만 exit(old)->state=next->entry(new)
}

// ===========================================================================
// Q83. guard condition FSM
//   IDLE 에서 RUN 이벤트: value>0 이면 RUN, 아니면 ERROR. RESET 은 IDLE 로.
//   반환: 전이 후 상태.
// ===========================================================================
typedef enum { G_IDLE, G_RUN, G_ERROR } guard_state_t;
typedef enum { GEV_RUN, GEV_RESET }      guard_evt_t;

typedef struct { guard_state_t state; int32_t value; } guard_fsm_t;

void guard_init(guard_fsm_t *f, int32_t value) {
    (void)f; (void)value;
    // TODO: state=G_IDLE, value 저장
}

guard_state_t guard_update(guard_fsm_t *f, guard_evt_t ev) {
    (void)f; (void)ev;
    // TODO: IDLE+RUN -> guard(value>0)로 RUN/ERROR 분기, RESET -> IDLE
    return G_IDLE;   // placeholder
}

// ===========================================================================
// Q84. event queue FSM (원형 버퍼로 ISR↔FSM 디커플)
// ===========================================================================
#define EVQ_SIZE 8

typedef enum { EV_NONE, EV_START, EV_STOP, EV_ERROR } ev_type_t;
typedef struct { ev_type_t type; int32_t value; } event_t;

typedef struct {
    event_t buffer[EVQ_SIZE];
    int head, tail, count;
} event_queue_t;

void evq_init(event_queue_t *q) {
    (void)q;
    // TODO: head=tail=count=0
}

bool evq_push(event_queue_t *q, event_t ev) {
    (void)q; (void)ev;
    // TODO: full(count==SIZE)이면 false, 아니면 head 에 쓰고 wrap, count++
    return false;   // placeholder
}

bool evq_pop(event_queue_t *q, event_t *out) {
    (void)q; (void)out;
    // TODO: empty(count==0)이면 false, 아니면 tail 에서 읽고 wrap, count--
    return false;   // placeholder
}

typedef enum { Q_IDLE, Q_RUN, Q_ERROR } q_state_t;
typedef struct { q_state_t state; } evq_fsm_t;

void evq_fsm_init(evq_fsm_t *f) { (void)f; /* TODO: state=Q_IDLE */ }

q_state_t evq_fsm_handle(evq_fsm_t *f, const event_t *ev) {
    (void)f; (void)ev;
    // TODO: IDLE:START->RUN, RUN:STOP->IDLE/ERROR->ERROR, ERROR:STOP->IDLE
    return Q_IDLE;   // placeholder
}

// ===========================================================================
// Q85. timeout event FSM
//   ACTIVE 에서 limit tick 무활동 -> TIMEOUT. event=1 활동(리셋), 0 무활동.
//   반환: 전이 후 상태.
// ===========================================================================
typedef enum { TO_IDLE, TO_ACTIVE, TO_TIMEOUT } to_state_t;
typedef struct { to_state_t state; int32_t timer; int32_t limit; } timeout_fsm_t;

void timeout_init(timeout_fsm_t *f, int32_t limit) {
    (void)f; (void)limit;
    // TODO: state=TO_IDLE, timer=0, limit 저장
}

to_state_t timeout_update(timeout_fsm_t *f, int event) {
    (void)f; (void)event;
    // TODO: IDLE+1->ACTIVE. ACTIVE: 0이면 ++timer>=limit->TIMEOUT, 1이면 timer=0.
    //       TIMEOUT+1->ACTIVE(복구).
    return TO_IDLE;   // placeholder
}

// ===========================================================================
// main : 모든 케이스 PASS/FAIL  (솔루션과 동일한 하네스)
// ===========================================================================
int main(void) {
    // -------- Q76 button debounce --------
    printf("== Q76 button debounce FSM ==\n");
    {
        button_fsm_t b;
        button_fsm_init(&b, 2);
        bool seq[]      = {false,true,true,true,true,false,false,false,true,true,false,false,false};
        bool expect[]   = {false,false,false,true,true,true,true,false,false,false,false,false,false};
        int n = (int)(sizeof(seq)/sizeof(seq[0]));
        bool ok = true;
        for (int i = 0; i < n; ++i) {
            button_fsm_update(&b, seq[i]);
            if (b.pressed != expect[i]) ok = false;
        }
        T("debounce output vector matches (glitch rejected)", ok);
        button_fsm_t b2; button_fsm_init(&b2, 3);
        button_fsm_update(&b2, true);
        button_fsm_update(&b2, false);
        T("single spike -> not pressed", b2.pressed == false);
        button_fsm_init(NULL, 3);
        button_fsm_update(NULL, true);
        T("NULL-safe init/update", true);
    }

    // -------- Q77 function-pointer table --------
    printf("== Q77 function-pointer state table ==\n");
    {
        motor_fsm_t m;
        motor_fsm_init(&m);
        T("start IDLE",             m.state == M_IDLE);
        T("IDLE --START--> RUN",    motor_fsm_dispatch(&m, IN_START) == M_RUN);
        T("RUN  --RESET--> RUN (ignored)", motor_fsm_dispatch(&m, IN_RESET) == M_RUN);
        T("RUN  --STOP --> STOP",   motor_fsm_dispatch(&m, IN_STOP)  == M_STOP);
        T("STOP --START--> STOP (ignored)", motor_fsm_dispatch(&m, IN_START) == M_STOP);
        T("STOP --RESET--> IDLE",   motor_fsm_dispatch(&m, IN_RESET) == M_IDLE);
    }

    // -------- Q78 UART packet parser --------
    printf("== Q78 UART packet parser FSM ==\n");
    {
        uart_parser_t p;
        uart_parser_init(&p);
        uint8_t pkt[] = {0xAA, 3, 0x11, 0x22, 0x33, 0x00};
        bool done = false;
        for (size_t i = 0; i < sizeof pkt; ++i) done = uart_parse_byte(&p, pkt[i]);
        T("good packet completes on last byte", done == true);
        T("  good_packets == 1",  p.good_packets == 1);
        T("  out_len == 3",       p.out_len == 3);
        T("  payload == 11 22 33", p.out_data[0]==0x11 && p.out_data[1]==0x22 && p.out_data[2]==0x33);

        uart_parser_init(&p);
        uint8_t bad[] = {0xAA, 2, 0x01, 0x02, 0xFF};
        done = false;
        for (size_t i = 0; i < sizeof bad; ++i) done = uart_parse_byte(&p, bad[i]);
        T("bad checksum -> no completion", done == false);
        T("  bad_packets == 1",   p.bad_packets == 1);
        T("  good_packets == 0",  p.good_packets == 0);

        uart_parser_init(&p);
        uart_parse_byte(&p, 0xAA);
        uart_parse_byte(&p, 0);
        T("len==0 -> back to IDLE", p.state == P_IDLE);

        uart_parser_init(&p);
        uart_parse_byte(&p, 0xAA);
        uart_parse_byte(&p, UART_MAX_DATA + 1);
        T("len>MAX -> back to IDLE", p.state == P_IDLE);

        uart_parser_init(&p);
        uint8_t noisy[] = {0x00, 0x99, 0xAA, 1, 0x55, 0x55};
        done = false;
        for (size_t i = 0; i < sizeof noisy; ++i) done = uart_parse_byte(&p, noisy[i]);
        T("garbage before sync ok", done == true && p.out_len == 1 && p.out_data[0] == 0x55);

        uart_parser_init(&p);
        uint8_t two[] = {0xAA,1,0x0F,0x0F,  0xAA,1,0x0E,0x0E};
        int completed = 0;
        for (size_t i = 0; i < sizeof two; ++i)
            if (uart_parse_byte(&p, two[i])) completed++;
        T("back-to-back: 2 good packets", completed == 2 && p.good_packets == 2);

        uart_parse_byte(NULL, 0x00);
        T("NULL-safe parse", true);
    }

    // -------- Q79 traffic light --------
    printf("== Q79 traffic-light FSM ==\n");
    {
        tl_fsm_t t;
        tl_init(&t, 2, 2, 1);
        tl_state_t exp[] = {TL_RED, TL_GREEN, TL_GREEN, TL_YELLOW, TL_RED, TL_RED, TL_GREEN};
        bool ok = true;
        for (int i = 0; i < 7; ++i) {
            tl_tick(&t);
            if (t.state != exp[i]) ok = false;
        }
        T("R->G->Y->R cycle sequence", ok);
        T("  timer reloaded on entry (RED=2)", t.state == TL_GREEN);
    }

    // -------- Q80 Mealy vs Moore --------
    printf("== Q80 Mealy vs Moore ('11' detector) ==\n");
    {
        int in[]  = {0,1,1,0,1,1,1};
        int exp[] = {0,0,1,0,0,1,1};
        moore_fsm_t mo; moore_init(&mo);
        mealy_fsm_t me; mealy_init(&me);
        bool mo_ok = true, me_ok = true;
        for (int i = 0; i < 7; ++i) {
            if (moore_step(&mo, in[i]) != exp[i]) mo_ok = false;
            if (mealy_step(&me, in[i]) != exp[i]) me_ok = false;
        }
        T("Moore detects overlapping 11", mo_ok);
        T("Mealy detects overlapping 11", me_ok);
        T("Mealy uses fewer states (2 < 3)", MEALY_STATES < MOORE_STATES);
    }

    // -------- Q81 HSM --------
    printf("== Q81 hierarchical state machine ==\n");
    {
        hsm_t h; hsm_init(&h);
        T("start OFF", h.state == ST_OFF);
        T("OFF --POWER--> IDLE (leaf)",  hsm_dispatch(&h, EVT_POWER) == DISP_LEAF && h.state == ST_IDLE);
        T("IDLE --START--> RUN (leaf)",  hsm_dispatch(&h, EVT_START) == DISP_LEAF && h.state == ST_RUN);
        T("RUN --POWER--> OFF (parent bubbling)",
          hsm_dispatch(&h, EVT_POWER) == DISP_PARENT && h.state == ST_OFF);
        T("OFF --START--> unhandled", hsm_dispatch(&h, EVT_START) == DISP_UNHANDLED && h.state == ST_OFF);
        hsm_dispatch(&h, EVT_POWER);
        T("IDLE --STOP--> unhandled",  hsm_dispatch(&h, EVT_STOP) == DISP_UNHANDLED && h.state == ST_IDLE);
    }

    // -------- Q82 entry/exit actions --------
    printf("== Q82 entry/exit actions ==\n");
    {
        eea_fsm_t f; eea_init(&f);
        T("init -> entry IDLE trace 'I'", strcmp(f.trace, "I") == 0);
        eea_transition(&f, E_RUN);
        eea_transition(&f, E_STOP);
        eea_transition(&f, E_IDLE);
        T("trace == 'IiRrSsI'", strcmp(f.trace, "IiRrSsI") == 0);
        size_t before = f.trace_len;
        eea_transition(&f, E_IDLE);
        T("self-transition emits nothing", f.trace_len == before);
    }

    // -------- Q83 guard conditions --------
    printf("== Q83 guard conditions ==\n");
    {
        guard_fsm_t f;
        guard_init(&f, 0);
        T("value=0, RUN evt -> ERROR (guard fail)", guard_update(&f, GEV_RUN) == G_ERROR);
        T("reset -> IDLE",                          guard_update(&f, GEV_RESET) == G_IDLE);
        guard_init(&f, 5);
        T("value=5, RUN evt -> RUN (guard pass)",   guard_update(&f, GEV_RUN) == G_RUN);
        T("reset from RUN -> IDLE",                 guard_update(&f, GEV_RESET) == G_IDLE);
    }

    // -------- Q84 event queue --------
    printf("== Q84 event-queue FSM ==\n");
    {
        event_queue_t q; evq_init(&q);
        evq_fsm_t f;     evq_fsm_init(&f);
        T("push START/ERROR/STOP/START/STOP",
          evq_push(&q,(event_t){EV_START,0}) &&
          evq_push(&q,(event_t){EV_ERROR,42}) &&
          evq_push(&q,(event_t){EV_STOP,0}) &&
          evq_push(&q,(event_t){EV_START,0}) &&
          evq_push(&q,(event_t){EV_STOP,0}));
        event_t ev; q_state_t last = Q_IDLE;
        while (evq_pop(&q, &ev)) last = evq_fsm_handle(&f, &ev);
        T("final state IDLE after sequence", last == Q_IDLE);
        T("queue drained (count 0)", q.count == 0);

        evq_init(&q);
        int ok = 0;
        for (int i = 0; i < EVQ_SIZE; ++i) if (evq_push(&q,(event_t){EV_NONE,i})) ok++;
        T("push 8 ok", ok == EVQ_SIZE);
        T("9th push rejected (full)", evq_push(&q,(event_t){EV_NONE,99}) == false);
        evq_init(&q);
        T("pop empty -> false", evq_pop(&q, &ev) == false);
    }

    // -------- Q85 timeout --------
    printf("== Q85 timeout event FSM ==\n");
    {
        timeout_fsm_t f;
        timeout_init(&f, 3);
        int ev[]           = {0,1,0,0,0,1,0,0,0,0};
        to_state_t exp[]   = {TO_IDLE,TO_ACTIVE,TO_ACTIVE,TO_ACTIVE,TO_TIMEOUT,
                              TO_ACTIVE,TO_ACTIVE,TO_ACTIVE,TO_TIMEOUT,TO_TIMEOUT};
        bool ok = true;
        for (int i = 0; i < 10; ++i)
            if (timeout_update(&f, ev[i]) != exp[i]) ok = false;
        T("timeout + recover sequence", ok);
        timeout_init(&f, 3);
        to_state_t s = TO_IDLE;
        int active[] = {1,0,1,0,1,0,1,0};
        for (int i = 0; i < 8; ++i) s = timeout_update(&f, active[i]);
        T("continuous activity -> never TIMEOUT", s == TO_ACTIVE);
    }

    printf("\n==== %d passed, %d failed ====\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
