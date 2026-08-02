// 07_fsm.c  —  REFERENCE SOLUTION
// 유한 상태 기계 (Finite State Machine, FSM)  —  Q76~Q85
// ---------------------------------------------------------------------------
// 빌드: cc -std=c11 -Wall -Wextra 07_fsm.c -o /tmp/andb_fsm
// 임베디드 관점 핵심:
//   - switch 기반 FSM: 가장 단순·명료. 버튼 디바운스/타임아웃/신호등의 기본형.
//   - 함수 포인터 테이블: 상태가 많아질 때 확장성·유지보수성 ↑ (O(1) 디스패치).
//   - 프로토콜 파서 FSM(SYNC/LEN/DATA/CHECKSUM): 드론 텔레메트리/링크의 심장.
//   - Mealy vs Moore: 출력이 (상태)만이냐 (상태+입력)이냐 — 지연/상태수 트레이드오프.
//   - HSM(계층적 상태): 부모 superstate 가 공통 이벤트를 처리, 자식은 특화 이벤트만.
//   - entry/exit action, guard, event queue, timeout: 실무 FSM 프레임워크의 4대 요소.
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
// ---------------------------------------------------------------------------
// 실제 버튼 접점은 누를/뗄 때 수 ms 동안 채터링(bounce)한다. 4-상태 FSM 으로
// "확정된 press/release" 만 output(pressed) 으로 내보낸다.
//   RELEASED --high--> BOUNCE_PRESS --(N tick 연속 high)--> PRESSED
//   PRESSED  --low --> BOUNCE_RELEASE --(N tick 연속 low)--> RELEASED
// BOUNCE_* 중 반대 입력이 오면 글리치로 보고 원래 상태로 되돌아간다.
// ===========================================================================
typedef enum {
    BTN_RELEASED,
    BTN_BOUNCE_PRESS,
    BTN_PRESSED,
    BTN_BOUNCE_RELEASE
} btn_state_t;

typedef struct {
    btn_state_t state;
    uint8_t     counter;   // 연속 동일 샘플 카운트
    uint8_t     ticks;     // 확정에 필요한 연속 샘플 수
    bool        pressed;   // 디바운스된 출력 (확정 눌림)
} button_fsm_t;

void button_fsm_init(button_fsm_t *fsm, uint8_t debounce_ticks) {
    if (!fsm) return;
    fsm->state   = BTN_RELEASED;
    fsm->counter = 0;
    fsm->ticks   = debounce_ticks;
    fsm->pressed = false;
}

// 매 샘플 틱마다 호출. gpio_high = 현재 원시 입력(디바운스 전).
void button_fsm_update(button_fsm_t *fsm, bool gpio_high) {
    if (!fsm) return;
    switch (fsm->state) {
        case BTN_RELEASED:
            if (gpio_high) { fsm->state = BTN_BOUNCE_PRESS; fsm->counter = 0; }
            break;
        case BTN_BOUNCE_PRESS:
            if (gpio_high) {
                if (++fsm->counter >= fsm->ticks) {   // N번 연속 high -> 확정
                    fsm->state = BTN_PRESSED;
                    fsm->pressed = true;
                }
            } else {
                fsm->state = BTN_RELEASED;            // 글리치 -> 취소
            }
            break;
        case BTN_PRESSED:
            if (!gpio_high) { fsm->state = BTN_BOUNCE_RELEASE; fsm->counter = 0; }
            break;
        case BTN_BOUNCE_RELEASE:
            if (!gpio_high) {
                if (++fsm->counter >= fsm->ticks) {   // N번 연속 low -> 확정
                    fsm->state = BTN_RELEASED;
                    fsm->pressed = false;
                }
            } else {
                fsm->state = BTN_PRESSED;             // 글리치 -> 취소
            }
            break;
    }
}

// ===========================================================================
// Q77. 함수 포인터 배열 FSM (state table)
// ---------------------------------------------------------------------------
// 상태별 핸들러를 배열에 담아 table[state](input) 로 O(1) 디스패치.
// 상태 추가 = 핸들러 추가 + 테이블 한 줄. switch 폭발 없이 확장.
// 예: 모터 컨트롤러 IDLE/RUN/STOP.
// ===========================================================================
typedef enum { M_IDLE, M_RUN, M_STOP, M_COUNT } motor_state_t;
typedef enum { IN_START, IN_STOP, IN_RESET }   motor_input_t;

// 각 핸들러: 현재 상태에서 입력을 받아 "다음 상태" 를 반환.
static motor_state_t h_idle(motor_input_t in) { return (in == IN_START) ? M_RUN  : M_IDLE; }
static motor_state_t h_run (motor_input_t in) { return (in == IN_STOP)  ? M_STOP : M_RUN;  }
static motor_state_t h_stop(motor_input_t in) { return (in == IN_RESET) ? M_IDLE : M_STOP; }

typedef motor_state_t (*motor_handler_t)(motor_input_t);

typedef struct {
    motor_state_t   state;
    motor_handler_t table[M_COUNT];
} motor_fsm_t;

void motor_fsm_init(motor_fsm_t *fsm) {
    if (!fsm) return;
    fsm->state           = M_IDLE;
    fsm->table[M_IDLE]   = h_idle;
    fsm->table[M_RUN]    = h_run;
    fsm->table[M_STOP]   = h_stop;
}

// 테이블 디스패치. 새 상태를 반환.
motor_state_t motor_fsm_dispatch(motor_fsm_t *fsm, motor_input_t in) {
    if (!fsm) return M_IDLE;
    fsm->state = fsm->table[fsm->state](in);
    return fsm->state;
}

// ===========================================================================
// Q78. UART 패킷 파서 FSM  (핵심: 드론 링크/텔레메트리 프레이밍)  ⭐
// ---------------------------------------------------------------------------
// 프레임: [0xAA sync][len(1..MAX)][data x len][checksum = XOR(data)]
//   IDLE  : 0xAA 를 만나면 LEN 으로.  그 외 바이트는 버림(재동기화).
//   LEN   : 길이 판독. 1..MAX 면 DATA 로, 아니면 IDLE 로 폐기.
//   DATA  : data 바이트 저장 + 러닝 XOR. len 개 다 받으면 CKSUM 으로.
//   CKSUM : 수신 checksum == 계산 XOR 이면 정상 패킷 완료(good++), 아니면 bad++.
//           어느 쪽이든 IDLE 로 복귀.
// 반환: 이 바이트로 "정상 패킷이 방금 완성" 되면 true.
// ===========================================================================
#define UART_MAX_DATA 16

typedef enum { P_IDLE, P_LEN, P_DATA, P_CKSUM } parser_state_t;

typedef struct {
    parser_state_t state;
    uint8_t  len;                       // 이번 패킷 길이
    uint8_t  idx;                       // 지금까지 받은 data 수
    uint8_t  checksum;                  // data 의 러닝 XOR
    uint8_t  data[UART_MAX_DATA];       // 조립 중 버퍼
    // 마지막으로 성공한 패킷 (호출자가 소비)
    uint8_t  out_data[UART_MAX_DATA];
    uint8_t  out_len;
    uint32_t good_packets;
    uint32_t bad_packets;               // checksum 실패 수
} uart_parser_t;

void uart_parser_init(uart_parser_t *p) {
    if (!p) return;
    memset(p, 0, sizeof *p);
    p->state = P_IDLE;
}

bool uart_parse_byte(uart_parser_t *p, uint8_t byte) {
    if (!p) return false;
    switch (p->state) {
        case P_IDLE:
            if (byte == 0xAA) p->state = P_LEN;   // sync
            break;
        case P_LEN:
            if (byte >= 1 && byte <= UART_MAX_DATA) {
                p->len      = byte;
                p->idx      = 0;
                p->checksum = 0;
                p->state    = P_DATA;
            } else {
                p->state = P_IDLE;                // 잘못된 길이 -> 폐기
            }
            break;
        case P_DATA:
            p->data[p->idx++] = byte;
            p->checksum ^= byte;
            if (p->idx >= p->len) p->state = P_CKSUM;
            break;
        case P_CKSUM:
            p->state = P_IDLE;                    // 항상 IDLE 로 복귀
            if (byte == p->checksum) {            // 무결성 OK
                memcpy(p->out_data, p->data, p->len);
                p->out_len = p->len;
                p->good_packets++;
                return true;                      // 정상 패킷 완성!
            }
            p->bad_packets++;                     // checksum 불일치
            break;
    }
    return false;
}

// ===========================================================================
// Q79. 신호등 제어 FSM (타이머 기반)
// ---------------------------------------------------------------------------
// RED -> GREEN -> YELLOW -> RED ...  각 상태는 자기 지속시간(tick) 을 가진다.
// tl_tick() 이 매 tick 카운트다운, 0 에 도달하면 다음 상태로 전이 + 타이머 재장전.
// ===========================================================================
typedef enum { TL_RED, TL_GREEN, TL_YELLOW } tl_state_t;

typedef struct {
    tl_state_t state;
    int32_t timer;
    int32_t red_t, green_t, yellow_t;
} tl_fsm_t;

void tl_init(tl_fsm_t *fsm, int32_t red_t, int32_t green_t, int32_t yellow_t) {
    if (!fsm) return;
    fsm->state    = TL_RED;
    fsm->timer    = red_t;
    fsm->red_t    = red_t;
    fsm->green_t  = green_t;
    fsm->yellow_t = yellow_t;
}

void tl_tick(tl_fsm_t *fsm) {
    if (!fsm) return;
    if (--fsm->timer <= 0) {
        switch (fsm->state) {
            case TL_RED:    fsm->state = TL_GREEN;  fsm->timer = fsm->green_t;  break;
            case TL_GREEN:  fsm->state = TL_YELLOW; fsm->timer = fsm->yellow_t; break;
            case TL_YELLOW: fsm->state = TL_RED;    fsm->timer = fsm->red_t;    break;
            default:        fsm->state = TL_RED;    fsm->timer = fsm->red_t;    break;
        }
    }
}

// ===========================================================================
// Q80. Mealy vs Moore — "11" 시퀀스 검출기 (개념 비교)
// ---------------------------------------------------------------------------
// Moore : 출력이 (현재 상태)만의 함수. 별도 "검출됨" 상태 필요 -> 상태 3개.
//         출력이 레지스터드(클럭 동기) -> 글리치 없음, 1클럭 지연 가능.
// Mealy : 출력이 (상태 + 입력)의 함수. 상태 2개로 충분, 조합 출력 -> 더 빠름.
// 둘 다 겹침 허용(overlapping) "11" 을 검출. 여기선 전이 후 출력을 읽어 벡터 비교.
// ===========================================================================
#define MEALY_STATES 2
#define MOORE_STATES 3

// --- Moore: S0(0개)->S1(1 하나)->S2(11 검출, 출력=1) ---
typedef enum { MO_S0, MO_S1, MO_S2 } moore_state_t;
typedef struct { moore_state_t state; } moore_fsm_t;

void moore_init(moore_fsm_t *m) { if (m) m->state = MO_S0; }

// 입력 비트를 처리하고 (전이 후) 출력(=상태가 S2인가)을 반환.
int moore_step(moore_fsm_t *m, int bit) {
    if (!m) return 0;
    switch (m->state) {
        case MO_S0: m->state = bit ? MO_S1 : MO_S0; break;
        case MO_S1: m->state = bit ? MO_S2 : MO_S0; break;
        case MO_S2: m->state = bit ? MO_S2 : MO_S0; break;  // 겹침 허용
    }
    return (m->state == MO_S2) ? 1 : 0;   // 출력은 상태만의 함수
}

// --- Mealy: S0->S1, 출력은 (S1 && bit==1) 일 때 1 ---
typedef enum { ME_S0, ME_S1 } mealy_state_t;
typedef struct { mealy_state_t state; } mealy_fsm_t;

void mealy_init(mealy_fsm_t *m) { if (m) m->state = ME_S0; }

// 출력은 (상태 + 입력)의 함수 -> 전이 계산 시점에 결정.
int mealy_step(mealy_fsm_t *m, int bit) {
    if (!m) return 0;
    int out = 0;
    switch (m->state) {
        case ME_S0:
            m->state = bit ? ME_S1 : ME_S0;
            out = 0;
            break;
        case ME_S1:
            out = bit ? 1 : 0;                 // 상태 S1 에서 입력 1 -> 즉시 검출
            m->state = bit ? ME_S1 : ME_S0;    // 겹침 허용
            break;
    }
    return out;
}

// ===========================================================================
// Q81. 계층적 상태 기계 (Hierarchical State Machine, HSM) — 개념
// ---------------------------------------------------------------------------
// leaf 상태: OFF, (ON superstate 안의) IDLE, RUN.
//   - 자식(leaf) 이 이벤트를 처리하지 못하면 부모(superstate ON) 로 "버블링".
//   - EVT_POWER 는 ON 안 어디서든 부모가 잡아 OFF 로. (공통 동작을 한 곳에)
//   - EVT_START/STOP 는 leaf 특화.
// 반환값으로 "어느 레벨에서 처리됐는지" 를 노출해 버블링을 검증한다.
// ===========================================================================
typedef enum { ST_OFF, ST_IDLE, ST_RUN } hsm_leaf_t;     // IDLE/RUN 은 superstate ON 내부
typedef enum { EVT_POWER, EVT_START, EVT_STOP } hsm_evt_t;
typedef enum { DISP_UNHANDLED, DISP_LEAF, DISP_PARENT } hsm_disp_t;

typedef struct { hsm_leaf_t state; } hsm_t;

void hsm_init(hsm_t *h) { if (h) h->state = ST_OFF; }

// 이벤트를 leaf 에 먼저, 없으면 부모 ON 에 위임. 처리 레벨을 반환.
hsm_disp_t hsm_dispatch(hsm_t *h, hsm_evt_t e) {
    if (!h) return DISP_UNHANDLED;
    // 1) leaf 핸들러
    switch (h->state) {
        case ST_OFF:
            if (e == EVT_POWER) { h->state = ST_IDLE; return DISP_LEAF; }
            return DISP_UNHANDLED;                 // OFF 는 START/STOP 무시
        case ST_IDLE:
            if (e == EVT_START) { h->state = ST_RUN; return DISP_LEAF; }
            break;                                 // 나머지는 부모로 버블링
        case ST_RUN:
            if (e == EVT_STOP)  { h->state = ST_IDLE; return DISP_LEAF; }
            break;                                 // 나머지는 부모로 버블링
    }
    // 2) 부모(superstate ON) 핸들러: IDLE/RUN 공통
    if (h->state == ST_IDLE || h->state == ST_RUN) {
        if (e == EVT_POWER) { h->state = ST_OFF; return DISP_PARENT; }
    }
    return DISP_UNHANDLED;
}

// ===========================================================================
// Q82. entry/exit action FSM
// ---------------------------------------------------------------------------
// 상태 진입 시 entry, 이탈 시 exit 액션을 자동 호출. 리소스 init/cleanup 을
// 전이 로직과 분리 -> 상태 추가 시 누락 없이 대칭적으로 관리.
// 여기선 액션이 trace 문자열에 토큰을 남겨 검증 가능하게 한다.
//   진입: 대문자(I/R/S), 이탈: 소문자(i/r/s).
// ===========================================================================
typedef enum { E_IDLE, E_RUN, E_STOP, E_COUNT } eea_state_t;

typedef struct {
    eea_state_t state;
    char        trace[64];
    size_t      trace_len;
} eea_fsm_t;

static void eea_emit(eea_fsm_t *f, char c) {
    if (f->trace_len + 1 < sizeof f->trace) {
        f->trace[f->trace_len++] = c;
        f->trace[f->trace_len]   = '\0';
    }
}
static void eea_entry(eea_fsm_t *f, eea_state_t s) {
    eea_emit(f, s == E_IDLE ? 'I' : s == E_RUN ? 'R' : 'S');
}
static void eea_exit(eea_fsm_t *f, eea_state_t s) {
    eea_emit(f, s == E_IDLE ? 'i' : s == E_RUN ? 'r' : 's');
}

void eea_init(eea_fsm_t *f) {
    if (!f) return;
    f->state     = E_IDLE;
    f->trace[0]  = '\0';
    f->trace_len = 0;
    eea_entry(f, E_IDLE);          // 초기 상태 진입 액션
}

// 상태가 실제로 바뀔 때만 exit(old) -> entry(new). 자기전이는 무동작.
void eea_transition(eea_fsm_t *f, eea_state_t next) {
    if (!f || next >= E_COUNT) return;
    if (f->state != next) {
        eea_exit(f, f->state);
        f->state = next;
        eea_entry(f, next);
    }
}

// ===========================================================================
// Q83. guard condition FSM
// ---------------------------------------------------------------------------
// 같은 이벤트라도 조건(guard)에 따라 다른 전이. IDLE 에서 "run 시도" 시
// value>0 이면 RUN, 아니면 ERROR. reset 이벤트는 어디서든 IDLE 로.
// 반환: 전이 후 상태.
// ===========================================================================
typedef enum { G_IDLE, G_RUN, G_ERROR } guard_state_t;
typedef enum { GEV_RUN, GEV_RESET }      guard_evt_t;

typedef struct { guard_state_t state; int32_t value; } guard_fsm_t;

void guard_init(guard_fsm_t *f, int32_t value) {
    if (!f) return;
    f->state = G_IDLE;
    f->value = value;
}

guard_state_t guard_update(guard_fsm_t *f, guard_evt_t ev) {
    if (!f) return G_IDLE;
    switch (f->state) {
        case G_IDLE:
            if (ev == GEV_RUN) {
                f->state = (f->value > 0) ? G_RUN : G_ERROR;   // guard 로 분기
            }
            break;
        case G_RUN:
        case G_ERROR:
            if (ev == GEV_RESET) f->state = G_IDLE;
            break;
    }
    return f->state;
}

// ===========================================================================
// Q84. event queue FSM (원형 버퍼로 ISR↔FSM 디커플)
// ---------------------------------------------------------------------------
// ISR 이 이벤트를 큐에 push, main loop 가 pop 해서 FSM 에 주입.
// FSM 을 ISR 문맥에서 떼어내 (짧은 ISR + 결정적 처리) 안전하게 만든다.
// ===========================================================================
#define EVQ_SIZE 8

typedef enum { EV_NONE, EV_START, EV_STOP, EV_ERROR } ev_type_t;
typedef struct { ev_type_t type; int32_t value; } event_t;

typedef struct {
    event_t buffer[EVQ_SIZE];
    int head, tail, count;
} event_queue_t;

void evq_init(event_queue_t *q) { if (q) { q->head = q->tail = q->count = 0; } }

bool evq_push(event_queue_t *q, event_t ev) {
    if (!q || q->count == EVQ_SIZE) return false;      // full -> 거부
    q->buffer[q->head] = ev;
    q->head = (q->head + 1) % EVQ_SIZE;
    q->count++;
    return true;
}

bool evq_pop(event_queue_t *q, event_t *out) {
    if (!q || !out || q->count == 0) return false;     // empty
    *out = q->buffer[q->tail];
    q->tail = (q->tail + 1) % EVQ_SIZE;
    q->count--;
    return true;
}

typedef enum { Q_IDLE, Q_RUN, Q_ERROR } q_state_t;
typedef struct { q_state_t state; } evq_fsm_t;

void evq_fsm_init(evq_fsm_t *f) { if (f) f->state = Q_IDLE; }

q_state_t evq_fsm_handle(evq_fsm_t *f, const event_t *ev) {
    if (!f || !ev) return Q_IDLE;
    switch (f->state) {
        case Q_IDLE:
            if (ev->type == EV_START) f->state = Q_RUN;
            break;
        case Q_RUN:
            if      (ev->type == EV_STOP)  f->state = Q_IDLE;
            else if (ev->type == EV_ERROR) f->state = Q_ERROR;
            break;
        case Q_ERROR:
            if (ev->type == EV_STOP) f->state = Q_IDLE;   // 복구
            break;
    }
    return f->state;
}

// ===========================================================================
// Q85. timeout event FSM
// ---------------------------------------------------------------------------
// ACTIVE 상태에서 일정 tick 동안 활동(event)이 없으면 TIMEOUT 으로 전이 ->
// 무응답 감지 & 복구 로직. 워치독/링크 유실 감지의 전형.
//   event: 1 = 활동 있음(타이머 리셋), 0 = 무활동 tick.
// ===========================================================================
typedef enum { TO_IDLE, TO_ACTIVE, TO_TIMEOUT } to_state_t;
typedef struct { to_state_t state; int32_t timer; int32_t limit; } timeout_fsm_t;

void timeout_init(timeout_fsm_t *f, int32_t limit) {
    if (!f) return;
    f->state = TO_IDLE;
    f->timer = 0;
    f->limit = limit;
}

to_state_t timeout_update(timeout_fsm_t *f, int event) {
    if (!f) return TO_IDLE;
    switch (f->state) {
        case TO_IDLE:
            if (event == 1) { f->state = TO_ACTIVE; f->timer = 0; }
            break;
        case TO_ACTIVE:
            if (event == 0) {
                if (++f->timer >= f->limit) f->state = TO_TIMEOUT;  // 무응답 만료
            } else {
                f->timer = 0;                                       // 활동 -> 리셋
            }
            break;
        case TO_TIMEOUT:
            if (event == 1) { f->state = TO_ACTIVE; f->timer = 0; } // 복구
            break;
    }
    return f->state;
}

// ===========================================================================
// main : 모든 케이스 PASS/FAIL
// ===========================================================================
int main(void) {
    // -------- Q76 button debounce --------
    printf("== Q76 button debounce FSM ==\n");
    {
        button_fsm_t b;
        button_fsm_init(&b, 2);   // 2 연속 샘플로 확정
        //                 idx: 0 1 2 3 4 5 6 7 8 9 10 11 12
        bool seq[]      = {false,true,true,true,true,false,false,false,true,true,false,false,false};
        bool expect[]   = {false,false,false,true,true,true,true,false,false,false,false,false,false};
        int n = (int)(sizeof(seq)/sizeof(seq[0]));
        bool ok = true;
        for (int i = 0; i < n; ++i) {
            button_fsm_update(&b, seq[i]);
            if (b.pressed != expect[i]) ok = false;
        }
        T("debounce output vector matches (glitch rejected)", ok);
        // 단일 스파이크는 무시
        button_fsm_t b2; button_fsm_init(&b2, 3);
        button_fsm_update(&b2, true);   // 1틱만 high
        button_fsm_update(&b2, false);
        T("single spike -> not pressed", b2.pressed == false);
        button_fsm_init(NULL, 3);       // NULL 안전
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
        // 정상: sync, len=3, 0x11 0x22 0x33, cksum = 0x11^0x22^0x33 = 0x00
        uint8_t pkt[] = {0xAA, 3, 0x11, 0x22, 0x33, 0x00};
        bool done = false;
        for (size_t i = 0; i < sizeof pkt; ++i) done = uart_parse_byte(&p, pkt[i]);
        T("good packet completes on last byte", done == true);
        T("  good_packets == 1",  p.good_packets == 1);
        T("  out_len == 3",       p.out_len == 3);
        T("  payload == 11 22 33", p.out_data[0]==0x11 && p.out_data[1]==0x22 && p.out_data[2]==0x33);

        // 나쁜 checksum: len=2, data 01 02 (cksum=0x03) 인데 0xFF 제공
        uart_parser_init(&p);
        uint8_t bad[] = {0xAA, 2, 0x01, 0x02, 0xFF};
        done = false;
        for (size_t i = 0; i < sizeof bad; ++i) done = uart_parse_byte(&p, bad[i]);
        T("bad checksum -> no completion", done == false);
        T("  bad_packets == 1",   p.bad_packets == 1);
        T("  good_packets == 0",  p.good_packets == 0);

        // len == 0 폐기 후 재동기화
        uart_parser_init(&p);
        uart_parse_byte(&p, 0xAA);
        uart_parse_byte(&p, 0);            // 잘못된 길이 -> IDLE
        T("len==0 -> back to IDLE", p.state == P_IDLE);

        // len 초과(> MAX) 폐기
        uart_parser_init(&p);
        uart_parse_byte(&p, 0xAA);
        uart_parse_byte(&p, UART_MAX_DATA + 1);
        T("len>MAX -> back to IDLE", p.state == P_IDLE);

        // sync 앞 쓰레기 -> 정상 파싱, 단일 바이트 payload
        uart_parser_init(&p);
        uint8_t noisy[] = {0x00, 0x99, 0xAA, 1, 0x55, 0x55}; // cksum(0x55)=0x55
        done = false;
        for (size_t i = 0; i < sizeof noisy; ++i) done = uart_parse_byte(&p, noisy[i]);
        T("garbage before sync ok", done == true && p.out_len == 1 && p.out_data[0] == 0x55);

        // 연속 두 패킷 (back-to-back)
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
        tl_init(&t, 2, 2, 1);   // red2 green2 yellow1
        tl_state_t exp[] = {TL_RED, TL_GREEN, TL_GREEN, TL_YELLOW, TL_RED, TL_RED, TL_GREEN};
        bool ok = true;
        for (int i = 0; i < 7; ++i) {
            tl_tick(&t);
            if (t.state != exp[i]) ok = false;
        }
        T("R->G->Y->R cycle sequence", ok);
        T("  timer reloaded on entry (RED=2)", t.state == TL_GREEN); // 마지막 상태 확인
    }

    // -------- Q80 Mealy vs Moore --------
    printf("== Q80 Mealy vs Moore ('11' detector) ==\n");
    {
        int in[]  = {0,1,1,0,1,1,1};
        int exp[] = {0,0,1,0,0,1,1};    // overlapping "11" 검출
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
        // RUN 은 POWER 를 직접 처리 못함 -> 부모 ON 이 처리(버블링) -> OFF
        T("RUN --POWER--> OFF (parent bubbling)",
          hsm_dispatch(&h, EVT_POWER) == DISP_PARENT && h.state == ST_OFF);
        // OFF 에서 START 는 아무도 처리 안 함
        T("OFF --START--> unhandled", hsm_dispatch(&h, EVT_START) == DISP_UNHANDLED && h.state == ST_OFF);
        // IDLE 에서 STOP 은 leaf/부모 모두 처리 안 함
        hsm_dispatch(&h, EVT_POWER); // -> IDLE
        T("IDLE --STOP--> unhandled",  hsm_dispatch(&h, EVT_STOP) == DISP_UNHANDLED && h.state == ST_IDLE);
    }

    // -------- Q82 entry/exit actions --------
    printf("== Q82 entry/exit actions ==\n");
    {
        eea_fsm_t f; eea_init(&f);
        T("init -> entry IDLE trace 'I'", strcmp(f.trace, "I") == 0);
        eea_transition(&f, E_RUN);     // i R
        eea_transition(&f, E_STOP);    // r S
        eea_transition(&f, E_IDLE);    // s I
        T("trace == 'IiRrSsI'", strcmp(f.trace, "IiRrSsI") == 0);
        size_t before = f.trace_len;
        eea_transition(&f, E_IDLE);    // 자기전이 -> 무동작
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
        // START->RUN, ERROR->ERROR, STOP->IDLE, START->RUN, STOP->IDLE
        T("final state IDLE after sequence", last == Q_IDLE);
        T("queue drained (count 0)", q.count == 0);

        // overflow: 8칸 채우고 9번째 거부
        evq_init(&q);
        int ok = 0;
        for (int i = 0; i < EVQ_SIZE; ++i) if (evq_push(&q,(event_t){EV_NONE,i})) ok++;
        T("push 8 ok", ok == EVQ_SIZE);
        T("9th push rejected (full)", evq_push(&q,(event_t){EV_NONE,99}) == false);
        // pop empty
        evq_init(&q);
        T("pop empty -> false", evq_pop(&q, &ev) == false);
    }

    // -------- Q85 timeout --------
    printf("== Q85 timeout event FSM ==\n");
    {
        timeout_fsm_t f;
        timeout_init(&f, 3);   // 3 tick 무활동 -> TIMEOUT
        int ev[]           = {0,1,0,0,0,1,0,0,0,0};
        to_state_t exp[]   = {TO_IDLE,TO_ACTIVE,TO_ACTIVE,TO_ACTIVE,TO_TIMEOUT,
                              TO_ACTIVE,TO_ACTIVE,TO_ACTIVE,TO_TIMEOUT,TO_TIMEOUT};
        bool ok = true;
        for (int i = 0; i < 10; ++i)
            if (timeout_update(&f, ev[i]) != exp[i]) ok = false;
        T("timeout + recover sequence", ok);
        // 활동이 계속되면 타임아웃 없음
        timeout_init(&f, 3);
        to_state_t s = TO_IDLE;
        int active[] = {1,0,1,0,1,0,1,0};   // 매번 리셋
        for (int i = 0; i < 8; ++i) s = timeout_update(&f, active[i]);
        T("continuous activity -> never TIMEOUT", s == TO_ACTIVE);
    }

    printf("\n==== %d passed, %d failed ====\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
