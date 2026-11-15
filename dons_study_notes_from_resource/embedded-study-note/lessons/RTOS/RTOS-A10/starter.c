/* RTOS-A10 RTOS 시스템 설계: 모터 제어 + 통신 + 로깅
 *
 * 면접형 설계 문제 + 핵심 경로 구현. 단일 코어 Cortex-M4F (STM32G4 급) 모터 컨트롤러:
 *   - 10 kHz 전류 루프   : ADC 변환 완료 ISR (NVIC 1, configMAX_SYSCALL 위 → 커널 API 금지, BASEPRI 에 안 가려짐)
 *   - 1 kHz 속도 루프    : 전류 ISR 이 10번째 주기마다 SWI "브리지" ISR(NVIC 6)을 펜딩 → vTaskNotifyGiveFromISR
 *   - CAN 명령 수신      : CAN RX ISR(NVIC 5) → 큐 → can_cmd 태스크
 *   - 플래시 로깅        : 로그 레코드 링 → 최하위 flush 태스크가 SPI NOR 에 페이지 쓰기 / 섹터 지우기
 *   - 워치독             : 태스크 생존 비트마스크가 다 찼을 때만 모니터 태스크가 IWDG 를 킥
 * 시간 단위는 µs, 전부 정수, 시뮬레이션 시간 (실제 시계 없음) → 결과는 매번 같다.
 * 우선순위: 태스크는 클수록 급함 (FreeRTOS), NVIC 는 작을수록 급함 (Cortex-M).
 *
 * TODO (8개)
 *  Part A — 분석
 *   1. blocking_term()   : 커널 임계 구역(BASEPRI) 때문에 i 가 기다릴 수 있는 최대 시간 B_i
 *   2. rta()             : w = C + B + Σ_{hp} ceil((w + J_j)/T_j)·C_j,  R = w + J  (RTOS-A04 확장 RTA 축약)
 *   3. can_backlog()     : CAN 버스트 도착 vs 소비 태스크 최소 서비스 → 큐 깊이, 버스트 처리 지연 상한
 *   4. log_ring_needed() : 플래시가 못 쓰는 동안(섹터 지우기) 쌓일 레코드 수 → 링 크기 (+ 지속 가능성 검사)
 *  Part B — 미니 커널 핵심 경로
 *   5. notify_give_from_isr() / notify_take() : 카운팅 태스크 알림 + ISR 우선순위 가드
 *   6. queue_send_from_isr() / queue_try_receive() : 고정 깊이 복사 큐, 가득 차면 버리고 센다
 *   7. log_push() / log_pop() : 레코드 링 (가득 차면 새 레코드를 버리고 센다)
 *   8. wdg_checkin() / wdg_monitor() : 모두 체크인했을 때만 킥, 빠진 비트를 돌려준다
 * 주어진 코드: 설계 표, 스택 예산 (RTOS-M08 방법), 1 µs 스텝 시뮬레이터 (ISR 중첩, BASEPRI, tick, 태스크).
 *
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution [-v] [-t]   (starter.c 도 같다)
 *   -v : 분석 표 + 시나리오별 통계,  -t : 정상 실행의 한 구간을 rtos-trace v1 형식으로 출력 (RTOS-A06 포맷)
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_pass, g_total, g_verbose, g_trace;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

/* ===================== 설계 상수 (µs) ===================== */
#define T_CUR        100u   /* 10 kHz 전류 루프 */
#define C_CUR         25u   /* FOC 1회 (Clarke/Park, PI 2개, SVPWM) — 추정 WCET */
#define D_CUR         60u   /* PWM 섀도 레지스터 로드 전에 듀티를 써야 한다 */
#define A_CAN        111u   /* 1 Mbit/s 표준 ID DLC 8 최단 프레임: 44 + 64 + 3(IFS) = 111 비트 (COMM-M04) */
#define C_CANISR       4u
#define HWFIFO         3u   /* bxCAN RX FIFO 깊이 */
#define D_CANISR     (HWFIFO * A_CAN)   /* 하드웨어 FIFO 가 넘치기 전에 하나는 꺼내야 한다 */
#define C_BRIDGE       2u
#define T_TICK      1000u   /* configTICK_RATE_HZ = 1000 */
#define C_TICK         3u
#define T_SPEED     1000u
#define C_SPEED      150u   /* 속도 PI + 관측기 + 로그 기록 (임계 구역 2 µs 포함) */
#define C_MON         10u
#define MON_TICKS     20u   /* 모니터 주기 20 ms */
#define WDG_US    100000u   /* IWDG 타임아웃 100 ms */
#define C_FRAME       30u   /* CAN 명령 1개 처리 */
#define CAN_TMO_TICKS 10u   /* 명령이 없어도 10 ms 마다 깨어 체크인 */
#define N_BURST       16u   /* 요구사항: 호스트는 최대 16 프레임을 연달아 보낸다 */
#define T_BURST    50000u   /* 버스트 사이 최소 50 ms */
#define LOG_DECIM     10u   /* 속도 루프 10번에 1번 로그 = 100 Hz */
#define REC_BYTES     16u
#define REC_PER_PAGE  16u   /* 256 B 페이지 */
#define PAGES_PER_SECTOR 16u /* 4 KB 섹터 */
#define CHUNK          4u   /* flush 가 임계 구역 한 번에 꺼내는 레코드 수 */
#define C_CHUNK        4u
#define C_LOGPUSH      2u
#define ERASE_MAX 400000u   /* SPI NOR 4 KB 섹터 지우기 최대 (데이터시트 대표값, 부품별 확인 필요) */
#define PROG_MAX    3000u   /* 페이지 쓰기 최대 */
#define NVIC_SYSCALL   5    /* configMAX_SYSCALL_INTERRUPT_PRIORITY = 5 << (8 - 4) */
#define CAN_QDEPTH     3u   /* can_backlog() 결과 (Part A) */
#define LOG_RING     224u   /* log_ring_needed() 218 → 32 의 배수로 올림 */
#define QMAX          64u
#define RING_MAX    1024u

/* ===================== Part A: 분석 ===================== */
#define MAXE 12
typedef struct {
    const char *name;
    int isr;              /* 1 = ISR, 0 = 태스크 */
    int nvic;             /* ISR: NVIC 우선순위 (작을수록 급함) */
    int prio;             /* 태스크: FreeRTOS 우선순위 (클수록 급함) */
    uint32_t C, T, D, J;  /* D = 0 → 데드라인 없음 (배경 / 별도 분석) */
    uint32_t crit;        /* 이 개체 안의 가장 긴 커널 임계 구역 (BASEPRI 올림) */
} ent_t;
typedef struct { ent_t e[MAXE]; int n; } sys_t;
typedef struct { int ok; uint32_t R, B, iters; } rta_t;

/* 하나의 축으로 줄 세우기: ISR 은 모든 태스크보다 급하다 */
static int level(const ent_t *e) { return e->isr ? 1000 - e->nvic : e->prio; }
static int maskable(const ent_t *e) { return e->isr && e->nvic >= NVIC_SYSCALL; }
static uint32_t ceil_div(uint64_t a, uint32_t b) { return (uint32_t)((a + b - 1) / b); }

/* TODO 1: B_i — i 가 준비됐는데 덜 급한 누군가의 커널 임계 구역 때문에 못 도는 최대 시간.
 *   - configMAX_SYSCALL 위의 ISR (BASEPRI 에 안 가려짐) → 0
 *   - BASEPRI 에 가려지는 ISR → 모든 태스크 + 덜 급한(nvic 큰) ISR 의 crit 중 최댓값
 *     (FromISR API 의 임계 구역도 BASEPRI 를 configMAX_SYSCALL 까지 올린다)
 *   - 태스크 → 우선순위가 더 낮은 태스크의 crit 중 최댓값 (임계 구역 안에서는 PendSV 도 가려진다)
 *   하나만 기다린다(최댓값, 합 아님): 단일 코어에서 임계 구역은 중첩돼도 한 번에 하나만 열려 있다. */
static uint32_t blocking_term(const sys_t *s, int i) {
    (void)s; (void)i; (void)maskable;
    return 0;   /* TODO 1 */
}

/* TODO 2: 확장 RTA (RTOS-A04 축약). hp(i) = level 이 i 이상인 다른 개체 (같은 level 도 보수적으로 포함).
 *   w0 = C + B, w_{k+1} = C + B + Σ ceil((w_k + J_j)/T_j)·C_j, 수렴하면 R = w + J_i.
 *   w + J_i > D 가 되는 순간 멈추고 ok = 0. D = 0 이면 한도 10 s. iters = 반복 횟수. */
static rta_t rta(const sys_t *s, int i) {
    rta_t r = {0, 0, 0, 0};
    (void)s; (void)i;   /* TODO 2 */
    return r;
}

/* TODO 3: CAN 큐 깊이와 버스트 지연 (네트워크 캘큘러스식 상한).
 *   도착: 프레임 k (0 ≤ k < n) 가 k·a 에 도착 → A(t) = min(n, t/a + 1).
 *   서비스 하한 β(Δ) = served_lb(): 소비자 i 가 계속 일감이 있을 때 길이 Δ 창에서 끝내는 프레임 수
 *     floor(max(0, Δ − B_i − I(Δ)) / c),  I(Δ) = Σ_{hp(i)} ceil((Δ + J_j)/T_j)·C_j
 *   처리 완료 수 하한 D(t) = min( β(t), min_{k=1..n} [ k + β(t − s_k) ], n )
 *     s_k = A 가 k 로 머무는 마지막 시각 = min(t, k·a − 1)  (k = n 이면 t). (k−1)·a > t 인 k 는 아직 없다.
 *   depth = max_t (A(t) − D(t)),  latency = D(t) ≥ n 이 되는 가장 작은 t. limit 안에 없으면 ok = 0. */
typedef struct { int ok; uint32_t depth, latency; } backlog_t;
/* 주어진 도우미: β(Δ) */
static uint32_t served_lb(const sys_t *s, int i, uint32_t w, uint32_t B, uint32_t c) {
    uint64_t I = 0;
    for (int j = 0; j < s->n; j++) {
        if (j == i || level(&s->e[j]) < level(&s->e[i])) continue;
        I += (uint64_t)ceil_div((uint64_t)w + s->e[j].J, s->e[j].T) * s->e[j].C;
    }
    if ((uint64_t)w <= I + B) return 0;
    return (uint32_t)(((uint64_t)w - I - B) / c);
}
static backlog_t can_backlog(const sys_t *s, int i, uint32_t n, uint32_t a, uint32_t c, uint32_t limit) {
    backlog_t r = {0, 0, 0};
    (void)s; (void)i; (void)n; (void)a; (void)c; (void)limit; (void)served_lb;   /* TODO 3 */
    return r;
}

/* TODO 4: 로그 링 크기 (레코드 수).
 *   생산자 k 는 최소 간격 T_k 마다 burst_k 개. 길이 w 창에 최대 (ceil(w/T_k) + 1)·burst_k 개 (창 양 끝 걸침).
 *   flush 는 링에 페이지 1장(rec_per_page)이 차야 시작하고, 그 뒤 stall_us 동안 아무것도 못 꺼낸다:
 *     needed = rec_per_page + Σ_k arrivals_k(stall_us)
 *   지속 가능성: 섹터 한 바퀴(cycle_us = 지우기 + 페이지들 쓰기) 동안 도착이 섹터 용량(sector_recs) 이하여야
 *   한다. 아니면 링을 아무리 키워도 결국 넘친다 → 0 을 돌려준다. */
typedef struct { uint32_t T, burst; } producer_t;
/* 주어진 도우미: 길이 w 창의 최대 도착 수 */
static uint32_t arrivals(const producer_t *p, int np, uint32_t w) {
    uint32_t a = 0;
    for (int k = 0; k < np; k++) a += (ceil_div(w, p[k].T) + 1) * p[k].burst;
    return a;
}
static uint32_t log_ring_needed(const producer_t *p, int np, uint32_t stall_us, uint32_t rec_per_page,
                                uint32_t cycle_us, uint32_t sector_recs) {
    (void)p; (void)np; (void)stall_us; (void)rec_per_page; (void)cycle_us; (void)sector_recs; (void)arrivals;
    return 0;   /* TODO 4 */
}

/* ---------------- 주어진 코드: 설계 표 ---------------- */
enum { E_CUR, E_CAN, E_BRIDGE, E_TICK, E_WDG, E_SPEED, E_CANCMD, E_LOG, NE };
static void build_design(sys_t *s) {
    static const ent_t d[NE] = {
        /* name        isr nvic prio  C        T        D          J  crit */
        {"cur_isr",     1,  1,  -1, C_CUR,    T_CUR,   D_CUR,      0, 0},
        {"can_isr",     1,  5,  -1, C_CANISR, A_CAN,   D_CANISR,   0, 1},
        {"bridge_swi",  1,  6,  -1, C_BRIDGE, T_SPEED, T_SPEED,    0, 1},
        {"systick",     1, 15,  -1, C_TICK,   T_TICK,  T_TICK,     0, 2},
        {"wdg_mon",     0, -1,   5, C_MON,    MON_TICKS * T_TICK, MON_TICKS * T_TICK, 0, 2},
        {"speed",       0, -1,   4, C_SPEED,  T_SPEED, T_SPEED,    0, C_LOGPUSH},
        {"can_cmd",     0, -1,   3, C_FRAME,  A_CAN,   0,          0, C_LOGPUSH},
        {"log_flush",   0, -1,   1, C_CHUNK,  1000,    0,          0, C_CHUNK},
    };
    memset(s, 0, sizeof *s);
    for (int i = 0; i < NE; i++) s->e[i] = d[i];
    s->n = NE;
}
/* 지터 전파: 브리지는 전류 ISR 이 끝나야 펜딩된다 → J_bridge = R_cur.
 * 속도 태스크는 브리지가 알림을 줘야 READY → J_speed = R_bridge (명목 출시 = 1 ms 경계). */
static void propagate_jitter(sys_t *s) {
    rta_t rc = rta(s, E_CUR);
    s->e[E_BRIDGE].J = rc.R;
    rta_t rb = rta(s, E_BRIDGE);
    s->e[E_SPEED].J = rb.R;
}

/* ---------------- 주어진 코드: 스택 예산 (RTOS-M08 방법, 경로 바이트는 추정치) ---------------- */
typedef struct { const char *name; int path; int fpu; } stk_t;
static int ctx_bytes(int fpu) {                 /* 예외 프레임(+정렬 4) + FreeRTOS ARM_CM4F PendSV 소프트웨어 저장 */
    return (fpu ? 26 : 8) * 4 + 4 + (9 + (fpu ? 16 : 0)) * 4;    /* FPU 208 B, 정수 72 B */
}
static int with_margin(int raw) {               /* +25 %, 최소 64 B, 8 B 정렬 */
    int add = (raw * 25 + 99) / 100;
    if (add < 64) add = 64;
    return (raw + add + 7) / 8 * 8;
}

/* ===================== Part B: 미니 커널 (1 µs 스텝 결정적 시뮬레이터) ===================== */
enum { I_CUR, I_CAN, I_BRIDGE, I_TICK, NISR };
enum { TK_IDLE, TK_LOG, TK_CAN, TK_SPEED, TK_WDG, NTASK };
#define BIT_SPEED 1u
#define BIT_CAN   2u
#define BIT_LOG   4u
#define WDG_ALL   (BIT_SPEED | BIT_CAN | BIT_LOG)

typedef enum { TASK_READY, TASK_RUNNING, TASK_BLOCKED, TASK_SUSPENDED } task_state_t;
typedef enum { W_NONE, W_NOTIFY, W_QUEUE, W_DELAY } wait_t;

typedef struct {
    const char *name; int nvic; uint32_t C;
    int pending, active; uint32_t left, trig_t;
    uint32_t count, lost;
} isr_t;
typedef struct tcb {
    const char *name; uint8_t prio; task_state_t state;
    wait_t wait; uint32_t wake_tick; int has_tmo;
    int pc; uint32_t left; int in_crit;      /* 지금 세그먼트: left µs 남음, in_crit = 커널 임계 구역 */
    uint32_t notify;                         /* 태스크 알림 값 (카운팅) */
} tcb_t;
typedef struct { uint32_t burst, idx, t_arr; } can_msg_t;
typedef struct { uint32_t seq, t; } logrec_t;
typedef struct { can_msg_t buf[QMAX]; uint32_t head, count, len, drops, max_count; } queue_t;
typedef struct { logrec_t buf[RING_MAX]; uint32_t head, count, cap, drops, max_count, pushed; } ring_t;

typedef struct {
    uint32_t horizon, q_depth, ring_cap, erase_us, prog_us;
    int flush_naive;        /* 플래시 완료 대기를 taskENTER_CRITICAL 안에서 바쁘게 폴링 (나쁜 드라이버) */
    int kick_in_tick;       /* SysTick 훅에서 무조건 킥 (나쁜 워치독) */
    int cur_direct_notify;  /* 전류 ISR(NVIC 1)에서 바로 FromISR API 호출 (금지) */
    int stall_task; uint32_t stall_at; int stall_busy;   /* stall_task < 0 이면 고장 없음 */
    uint32_t trace_from, trace_to;
} cfg_t;
typedef struct {
    uint32_t cur_jobs, cur_miss, cur_max;
    uint32_t spd_jobs, spd_miss, spd_merged, spd_max, spd_jit_max;
    uint32_t frames, can_done, hw_overrun, q_drops, q_max, canisr_max, burst_lat_max;
    uint32_t log_pushed, log_drops, ring_max, pages, erases, log_written;
    uint32_t kicks, bite, bite_t, last_missing, asserts, tick_lost, bridge_lost, ctx_sw, idle_us, end_t;
} stats_t;

static struct {
    uint32_t t, tick;
    isr_t isr[NISR]; int stk[NISR]; int nst; int in_isr;
    tcb_t task[NTASK]; int cur;
    queue_t q; ring_t ring;
    uint32_t alive, wdg_deadline, mon_last;
    uint32_t spd_nominal, spd_rel, spd_iter, spd_served, log_seq;
    can_msg_t hw[HWFIFO]; uint32_t hw_head, hw_n, burst_k, frame_i, next_frame_t;
    can_msg_t cmsg;
    logrec_t page[REC_PER_PAGE]; uint32_t copied, sector_pages, flash_busy_until; int need_erase;
    cfg_t cfg; stats_t st;
} K;

static void trace(uint32_t t, const char *ev, const char *name, long arg) {   /* RTOS-A06 포맷 한 줄 */
    if (!g_trace || t < K.cfg.trace_from || t > K.cfg.trace_to) return;
    if (arg >= 0) printf("%u %s %s %ld\n", t, ev, name, arg);
    else printf("%u %s %s\n", t, ev, name);
}
static int running_prio(void) { return K.task[K.cur].prio; }
static void make_ready(tcb_t *t) { t->state = TASK_READY; t->wait = W_NONE; t->has_tmo = 0; }
static void block(tcb_t *t, wait_t w, int has_tmo, uint32_t wake_tick) {
    t->state = TASK_BLOCKED; t->wait = w; t->has_tmo = has_tmo; t->wake_tick = wake_tick;
}
static void seg(tcb_t *t, uint32_t us, int crit) { t->left = us; t->in_crit = crit; }
static void wdg_kick(void) { K.wdg_deadline = K.t + WDG_US; K.st.kicks++; }

/* TODO 5: FreeRTOS vTaskNotifyGiveFromISR / ulTaskNotifyTake(pdTRUE, ...) 모델.
 *   give: ① 지금 ISR(K.in_isr) 의 NVIC 가 NVIC_SYSCALL 보다 급하면(숫자가 작으면) 커널 API 금지 —
 *          portASSERT_IF_INTERRUPT_PRIORITY_INVALID 처럼 K.st.asserts++ 하고 아무것도 하지 않는다.
 *         ② 알림 값 ++, 알림을 기다리며 BLOCKED 면 READY 로. 깨운 태스크가 지금 돌던 태스크보다
 *          급하면 *woken = 1 (portYIELD_FROM_ISR 가 PendSV 를 건다).
 *   take: 값을 돌려주고 0 으로 지운다 (clear on exit). 0 이면 호출자가 블록한다. */
static void notify_give_from_isr(tcb_t *t, int *woken) {
    (void)t; (void)woken; (void)running_prio;   /* TODO 5 */
}
static uint32_t notify_take(tcb_t *t) {
    (void)t;
    return 0;   /* TODO 5 */
}

/* TODO 6: 복사 큐 (xQueueSendFromISR / xQueueReceive(…, 0)).
 *   send: 가득 차면 drops++ 하고 0 (errQUEUE_FULL — ISR 은 기다릴 수 없다). 아니면 꼬리에 복사, count++,
 *         max_count 갱신, 큐에서 기다리던 can_cmd 태스크가 있으면 READY + (더 급하면) *woken = 1. 1 반환.
 *   try_receive: 비었으면 0, 아니면 머리에서 꺼내 *out 에 복사하고 1. */
static int queue_send_from_isr(queue_t *q, const can_msg_t *m, int *woken) {
    (void)m; (void)woken;
    q->drops++;
    return 0;   /* TODO 6 */
}
static int queue_try_receive(queue_t *q, can_msg_t *out) {
    (void)q; (void)out;
    return 0;   /* TODO 6 */
}

/* TODO 7: 로그 링. push: 가득 차면 drops++ 후 0 (생산자는 절대 기다리지 않는다 — 속도 루프가 로그 때문에
 *   늦으면 안 된다). 아니면 꼬리에 넣고 count, pushed, max_count 갱신, 1.
 *   pop: 최대 max 개를 머리에서 out[] 로 꺼내고 꺼낸 개수를 돌려준다. */
static int log_push(ring_t *r, const logrec_t *rec) {
    (void)rec;
    r->drops++;
    return 0;   /* TODO 7 */
}
static uint32_t log_pop(ring_t *r, logrec_t *out, uint32_t max) {
    (void)r; (void)out; (void)max;
    return 0;   /* TODO 7 */
}

/* TODO 8: 태스크 생존 비트마스크 워치독.
 *   checkin: K.alive 에 bit 를 OR (실제 칩에서는 임계 구역 또는 원자적 OR).
 *   monitor: missing = WDG_ALL 중 체크인 안 한 비트. missing == 0 일 때만 wdg_kick().
 *            판정 후 K.alive = 0 (다음 창은 새로 증명해야 한다). missing 을 돌려준다. */
static void wdg_checkin(uint32_t bit) { (void)bit; /* TODO 8 */ }
static uint32_t wdg_monitor(void) {
    return WDG_ALL;   /* TODO 8: 지금은 아무도 체크인 안 한 것으로 본다 → 킥 없음 */
}

/* ---------------- 주어진 코드: 하드웨어 + 커널 ---------------- */
static void pend(int i, uint32_t trig) {
    isr_t *x = &K.isr[i];
    if (x->pending) x->lost++;               /* NVIC 펜딩 비트는 하나 — 겹치면 사라진다 */
    x->pending = 1; x->trig_t = trig;
}
static uint32_t burst_start(uint32_t k) { return k * T_BURST + (27u + (k - 1u) * 389u) % 1000u; }
static void next_frame(void) {
    if (++K.frame_i == N_BURST) { K.frame_i = 0; K.burst_k++; }
    K.next_frame_t = burst_start(K.burst_k) + K.frame_i * A_CAN;
}
static void kernel_tick(void) {
    K.tick++;
    for (int i = 0; i < NTASK; i++) {
        tcb_t *t = &K.task[i];
        if (t->state == TASK_BLOCKED && t->has_tmo && (int32_t)(K.tick - t->wake_tick) >= 0) make_ready(t);
    }
    if (K.cfg.kick_in_tick) wdg_kick();
}
static void isr_end(int i) {
    isr_t *x = &K.isr[i];
    uint32_t now = K.t + 1;
    int woken = 0;
    x->active = 0; K.nst--; x->count++;
    K.in_isr = i;
    trace(now, "ISR_EXIT", x->name, -1);
    if (i == I_CUR) {
        uint32_t resp = now - x->trig_t;
        K.st.cur_jobs++;
        if (resp > K.st.cur_max) K.st.cur_max = resp;
        if (resp > D_CUR) K.st.cur_miss++;
        if (x->trig_t % T_SPEED == 0) {                  /* 10번째 주기마다 */
            if (K.cfg.cur_direct_notify) notify_give_from_isr(&K.task[TK_SPEED], &woken);
            else pend(I_BRIDGE, x->trig_t);              /* NVIC STIR 로 SWI 펜딩 */
        }
    } else if (i == I_BRIDGE) {
        uint32_t jit = now - x->trig_t;
        if (jit > K.st.spd_jit_max) K.st.spd_jit_max = jit;
        K.spd_nominal = x->trig_t;
        notify_give_from_isr(&K.task[TK_SPEED], &woken);
        if (K.task[TK_SPEED].state == TASK_READY) trace(now, "RELEASE", "speed", (long)x->trig_t);
    } else if (i == I_CAN) {
        can_msg_t m = K.hw[K.hw_head];
        K.hw_head = (K.hw_head + 1) % HWFIFO; K.hw_n--;
        if (now - m.t_arr > K.st.canisr_max) K.st.canisr_max = now - m.t_arr;
        queue_send_from_isr(&K.q, &m, &woken);
        if (K.hw_n > 0) x->pending = 1;                  /* 레벨 트리거: FIFO 가 남아 있으면 다시 */
    } else if (i == I_TICK) {
        kernel_tick();
    }
    K.in_isr = -1;
    (void)woken;   /* 스레드 모드로 돌아가는 즉시 스케줄러가 다시 고른다 = portYIELD_FROM_ISR(woken) */
}
static int crit_active(void) { const tcb_t *c = &K.task[K.cur]; return c->in_crit && c->left > 0; }
static void isr_dispatch(void) {
    for (;;) {
        int best = -1, top = K.nst ? K.isr[K.stk[K.nst - 1]].nvic : 256;
        for (int i = 0; i < NISR; i++) {
            const isr_t *x = &K.isr[i];
            if (!x->pending || x->active) continue;
            if (x->nvic >= NVIC_SYSCALL && crit_active()) continue;   /* BASEPRI 에 가려짐 */
            if (x->nvic >= top) continue;                              /* 같거나 덜 급하면 중첩 불가 */
            if (best < 0 || x->nvic < K.isr[best].nvic) best = i;
        }
        if (best < 0) return;
        isr_t *x = &K.isr[best];
        x->pending = 0; x->active = 1; x->left = (best == I_CAN) ? C_CANISR : x->C;
        K.stk[K.nst++] = best;
        trace(K.t, "ISR_ENTER", x->name, -1);
    }
}

static int stall_here(tcb_t *c, int id) {
    if (K.cfg.stall_task != id || K.t < K.cfg.stall_at || c->pc == 99) return c->pc == 99;
    if (K.cfg.stall_busy) seg(c, 0x7fffffffu, 0);       /* 폭주 루프: 자기 우선순위로 CPU 를 먹는다 */
    else c->state = TASK_SUSPENDED;                      /* 영원히 블록 (데드락, 안 오는 이벤트) */
    c->pc = 99;
    return 1;
}
static void prog_wdg(tcb_t *c) {
    for (;;) switch (c->pc) {
    case 0: {                                            /* xTaskDelayUntil(&last, 20) */
        K.mon_last += MON_TICKS;
        c->pc = 1;
        if ((int32_t)(K.mon_last - K.tick) > 0) { block(c, W_DELAY, 1, K.mon_last); return; }
        continue;
    }
    case 1: seg(c, C_MON - 2, 0); c->pc = 2; return;
    case 2: {
        uint32_t missing = wdg_monitor();
        if (missing) K.st.last_missing = missing;
        seg(c, 2, 1); c->pc = 0; return;
    }
    default: return;
    }
}
static void prog_speed(tcb_t *c) {
    for (;;) switch (c->pc) {
    case 0: {
        uint32_t n = notify_take(c);
        if (n == 0) { block(c, W_NOTIFY, 0, 0); return; }
        if (n > 1) K.st.spd_merged += n - 1;
        K.spd_rel = K.spd_nominal - (n - 1) * T_SPEED;
        seg(c, C_SPEED - C_LOGPUSH, 0); c->pc = 1; return;
    }
    case 1:
        if (++K.spd_iter % LOG_DECIM == 0) {
            logrec_t r = {K.log_seq++, K.t};
            log_push(&K.ring, &r);
        }
        wdg_checkin(BIT_SPEED);
        seg(c, C_LOGPUSH, 1); c->pc = 2; return;
    case 2: {
        uint32_t resp = K.t - K.spd_rel;
        K.st.spd_jobs++;
        if (resp > K.st.spd_max) K.st.spd_max = resp;
        K.spd_served = K.spd_nominal / T_SPEED + 1;      /* 명목 출시 0, 1000, … 중 몇 개를 끝냈나 */
        c->pc = 0; continue;
    }
    default: return;
    }
}
static void prog_can(tcb_t *c) {
    for (;;) switch (c->pc) {
    case 0:
        wdg_checkin(BIT_CAN);
        if (queue_try_receive(&K.q, &K.cmsg)) { seg(c, C_FRAME - C_LOGPUSH, 0); c->pc = 1; return; }
        block(c, W_QUEUE, 1, K.tick + CAN_TMO_TICKS); return;
    case 1: {
        logrec_t r = {K.log_seq++, K.t};
        log_push(&K.ring, &r);
        seg(c, C_LOGPUSH, 1); c->pc = 2; return;
    }
    case 2:
        K.st.can_done++;
        if (K.cmsg.idx == N_BURST - 1) {
            uint32_t lat = K.t - burst_start(K.cmsg.burst);
            if (lat > K.st.burst_lat_max) K.st.burst_lat_max = lat;
        }
        c->pc = 0; continue;
    default: return;
    }
}
static void prog_log(tcb_t *c) {
    for (;;) switch (c->pc) {
    case 0:
        wdg_checkin(BIT_LOG);
        if (K.ring.count < REC_PER_PAGE) { block(c, W_DELAY, 1, K.tick + 1); return; }
        if (K.need_erase) {
            K.need_erase = 0; K.st.erases++;
            if (K.cfg.flush_naive) { seg(c, K.cfg.erase_us, 1); c->pc = 3; return; }
            K.flash_busy_until = K.t + K.cfg.erase_us; c->pc = 2; continue;
        }
        c->pc = 3; continue;
    case 2:                                              /* 지우기 중: 1 tick 마다 상태 폴링 */
        if (K.t < K.flash_busy_until) { wdg_checkin(BIT_LOG); block(c, W_DELAY, 1, K.tick + 1); return; }
        c->pc = 3; continue;
    case 3:
        if (K.cfg.flush_naive) {                         /* 페이지 통째로 한 임계 구역에서 복사 */
            K.copied += log_pop(&K.ring, K.page + K.copied, REC_PER_PAGE - K.copied);
            seg(c, REC_PER_PAGE, 1); c->pc = 4; return;
        }
        K.copied += log_pop(&K.ring, K.page + K.copied, CHUNK);
        seg(c, C_CHUNK, 1); c->pc = (K.copied >= REC_PER_PAGE) ? 4 : 3; return;
    case 4:
        if (K.cfg.flush_naive) { seg(c, K.cfg.prog_us, 1); c->pc = 6; return; }
        K.flash_busy_until = K.t + K.cfg.prog_us; c->pc = 5; continue;
    case 5:
        if (K.t < K.flash_busy_until) { wdg_checkin(BIT_LOG); block(c, W_DELAY, 1, K.tick + 1); return; }
        c->pc = 6; continue;
    case 6:
        K.st.pages++; K.st.log_written += K.copied; K.copied = 0;
        if (++K.sector_pages == PAGES_PER_SECTOR) { K.sector_pages = 0; K.need_erase = 1; }
        c->pc = 0; continue;
    default: return;
    }
}
static void task_program(int id) {
    tcb_t *c = &K.task[id];
    if (stall_here(c, id)) return;                      /* 고장 주입: 어느 지점에서든 멈춘다 */
    switch (id) {
    case TK_WDG: prog_wdg(c); break;
    case TK_SPEED: prog_speed(c); break;
    case TK_CAN: prog_can(c); break;
    case TK_LOG: prog_log(c); break;
    default: seg(c, 0x7fffffffu, 0); break;              /* idle */
    }
}
static void schedule(void) {
    tcb_t *c = &K.task[K.cur];
    if (c->state == TASK_RUNNING && crit_active()) return;    /* 임계 구역 안: PendSV 도 가려진다 */
    int best = -1;
    for (int i = 0; i < NTASK; i++) {
        const tcb_t *t = &K.task[i];
        if ((t->state == TASK_READY || t->state == TASK_RUNNING) && (best < 0 || t->prio > K.task[best].prio))
            best = i;
    }
    if (best == K.cur && c->state == TASK_RUNNING) return;
    int changed = (best != K.cur);
    if (c->state == TASK_RUNNING) c->state = TASK_READY;
    if (changed) { trace(K.t, "SWITCH_OUT", c->name, -1); K.st.ctx_sw++; }
    K.cur = best;
    K.task[best].state = TASK_RUNNING;
    if (changed) trace(K.t, "SWITCH_IN", K.task[best].name, -1);
}
static void run_thread_1us(void) {
    for (int guard = 0; guard < 100; guard++) {
        tcb_t *c = &K.task[K.cur];
        if (c->state == TASK_RUNNING && c->left == 0) { c->in_crit = 0; task_program(K.cur); }
        schedule();
        c = &K.task[K.cur];
        if (c->state == TASK_RUNNING && c->left > 0) break;
    }
    tcb_t *c = &K.task[K.cur];
    if (K.cur == TK_IDLE) K.st.idle_us++;
    if (c->left > 0) c->left--;
}

static void sim_init(const cfg_t *cfg) {
    static const char *tn[NTASK] = {"IDLE", "log_flush", "can_cmd", "speed", "wdg_mon"};
    static const uint8_t tp[NTASK] = {0, 1, 3, 4, 5};
    static const char *in[NISR] = {"cur_isr", "can_isr", "bridge_swi", "systick"};
    static const int iv[NISR] = {1, 5, 6, 15};
    static const uint32_t ic[NISR] = {C_CUR, C_CANISR, C_BRIDGE, C_TICK};
    memset(&K, 0, sizeof K);
    K.cfg = *cfg;
    for (int i = 0; i < NTASK; i++) { K.task[i].name = tn[i]; K.task[i].prio = tp[i]; K.task[i].state = TASK_READY; }
    for (int i = 0; i < NISR; i++) { K.isr[i].name = in[i]; K.isr[i].nvic = iv[i]; K.isr[i].C = ic[i]; }
    K.cur = TK_IDLE; K.task[TK_IDLE].state = TASK_RUNNING;
    K.in_isr = -1;
    K.q.len = cfg->q_depth;
    K.ring.cap = cfg->ring_cap;
    K.need_erase = 1;                                    /* 첫 섹터는 지우고 시작 */
    K.wdg_deadline = WDG_US;                             /* 부팅 때 한 번 킥된 상태 */
    K.burst_k = 1; K.frame_i = 0; K.next_frame_t = burst_start(1);
}
static void hw_events(void) {
    uint32_t t = K.t;
    if (t % T_CUR == 0) pend(I_CUR, t);
    if (t % T_TICK == 0 && t > 0) pend(I_TICK, t);          /* 첫 tick 은 한 주기 뒤 (SysTick) */
    if (t % T_SPEED == 0 && t > 0 && K.spd_served < t / T_SPEED) {   /* t − 1000 에 출시된 작업의 데드라인 = t */
        K.st.spd_miss++;
        trace(K.t, "DEADLINE_MISS", "speed", (long)(t - T_SPEED));
    }
    while (K.next_frame_t == t) {
        K.st.frames++;
        if (K.hw_n == HWFIFO) K.st.hw_overrun++;
        else {
            can_msg_t m = {K.burst_k, K.frame_i, t};
            K.hw[(K.hw_head + K.hw_n) % HWFIFO] = m; K.hw_n++;
            if (!K.isr[I_CAN].active) K.isr[I_CAN].pending = 1;
        }
        next_frame();
    }
    if (t >= K.wdg_deadline) { K.st.bite = 1; K.st.bite_t = t; }
}
static stats_t sim_run(const cfg_t *cfg) {
    sim_init(cfg);
    if (g_trace) printf("# rtos-trace v1 unit=us\n");
    for (K.t = 0; K.t < cfg->horizon; K.t++) {
        hw_events();
        if (K.st.bite) break;
        isr_dispatch();
        if (K.nst) {
            int i = K.stk[K.nst - 1];
            if (--K.isr[i].left == 0) isr_end(i);
        } else run_thread_1us();
    }
    K.st.end_t = K.t;
    K.st.q_drops = K.q.drops; K.st.q_max = K.q.max_count;
    K.st.log_pushed = K.ring.pushed; K.st.log_drops = K.ring.drops; K.st.ring_max = K.ring.max_count;
    K.st.tick_lost = K.isr[I_TICK].lost; K.st.bridge_lost = K.isr[I_BRIDGE].lost;
    return K.st;
}
static cfg_t design_cfg(uint32_t horizon) {
    cfg_t c;
    memset(&c, 0, sizeof c);
    c.horizon = horizon; c.q_depth = CAN_QDEPTH; c.ring_cap = LOG_RING;
    c.erase_us = ERASE_MAX; c.prog_us = PROG_MAX;
    c.stall_task = -1;
    return c;
}
static void print_stats(const char *name, const stats_t *s) {
    if (!g_verbose) return;
    printf("  %-22s spd %u jobs miss %u max %u jit %u merged %u | cur max %u miss %u | can %u/%u hw_ovr %u q_drop %u q_max %u isr_max %u burst %u\n",
           name, s->spd_jobs, s->spd_miss, s->spd_max, s->spd_jit_max, s->spd_merged, s->cur_max, s->cur_miss,
           s->can_done, s->frames, s->hw_overrun, s->q_drops, s->q_max, s->canisr_max, s->burst_lat_max);
    printf("  %-22s log push %u drop %u ring_max %u pages %u erases %u | wdg kicks %u bite %u@%u missing 0x%x | tick_lost %u ctx %u idle %u%% end %u\n",
           "", s->log_pushed, s->log_drops, s->ring_max, s->pages, s->erases, s->kicks, s->bite, s->bite_t,
           s->last_missing, s->tick_lost, s->ctx_sw, s->end_t ? (unsigned)((uint64_t)s->idle_us * 100 / s->end_t) : 0u, s->end_t);
}

int main(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-v")) g_verbose = 1;
        if (!strcmp(argv[i], "-t")) g_trace = 1;
    }
    sys_t s;

    /* ---------- Part A: 분석 ---------- */
    build_design(&s);
    CHECK(blocking_term(&s, E_CUR) == 0);                 /* configMAX_SYSCALL 위 → 안 가려짐 */
    CHECK(blocking_term(&s, E_CAN) == 4);                 /* flush 의 4 µs 임계 구역 */
    CHECK(blocking_term(&s, E_TICK) == 4);
    CHECK(blocking_term(&s, E_WDG) == 4);
    CHECK(blocking_term(&s, E_SPEED) == 4);
    CHECK(blocking_term(&s, E_LOG) == 0);                 /* 최하위 태스크는 안 막힌다 */
    {   /* 교과서 세트로 RTA 자체 확인 (RTOS-M11 예): R = 20, 60, 240 */
        sys_t m; memset(&m, 0, sizeof m); m.n = 3;
        ent_t a = {"t1", 0, -1, 3, 20, 100, 100, 0, 0}, b = {"t2", 0, -1, 2, 40, 150, 150, 0, 0},
              c = {"t3", 0, -1, 1, 100, 350, 350, 0, 0};
        m.e[0] = a; m.e[1] = b; m.e[2] = c;
        CHECK(rta(&m, 0).ok && rta(&m, 0).R == 20);
        CHECK(rta(&m, 1).ok && rta(&m, 1).R == 60);
        CHECK(rta(&m, 2).ok && rta(&m, 2).R == 240);
        m.e[1].J = 70;                                     /* 상위 지터는 하위 창에 작업을 하나 더 넣을 수 있다 */
        CHECK(rta(&m, 1).R == 130 && rta(&m, 2).R == 280);
    }
    propagate_jitter(&s);
    rta_t R[NE];
    for (int i = 0; i < NE; i++) R[i] = rta(&s, i);
    CHECK(R[E_CUR].ok && R[E_CUR].R == 25);
    CHECK(R[E_CAN].ok && R[E_CAN].R == 33);
    CHECK(s.e[E_BRIDGE].J == 25 && R[E_BRIDGE].ok && R[E_BRIDGE].R == 60);
    CHECK(s.e[E_SPEED].J == 60 && R[E_SPEED].ok && R[E_SPEED].R == 316);
    CHECK(R[E_TICK].ok && R[E_WDG].ok);
    int all_ok = 1;
    for (int i = 0; i < NE; i++) if (s.e[i].D && !R[i].ok) all_ok = 0;
    CHECK(all_ok);
    {   /* 나쁜 설계 1: 드라이버가 섹터 지우기 완료를 임계 구역 안에서 기다림 → B = 400 ms */
        sys_t bad = s; bad.e[E_LOG].crit = ERASE_MAX;
        CHECK(blocking_term(&bad, E_CUR) == 0 && rta(&bad, E_CUR).ok);   /* 전류 루프만 살아남는다 */
        CHECK(!rta(&bad, E_SPEED).ok && !rta(&bad, E_CAN).ok);
        /* 나쁜 설계 2: 전류 루프를 BASEPRI 아래(NVIC 5)로 → 임계 구역 4 µs 만큼 지터 */
        sys_t low = s; low.e[E_CUR].nvic = 5;
        CHECK(blocking_term(&low, E_CUR) == 4 && rta(&low, E_CUR).R == 29 + 4);
    }
    backlog_t bl = can_backlog(&s, E_CANCMD, N_BURST, A_CAN, C_FRAME, 100000);
    CHECK(bl.ok && bl.depth == 3);
    CHECK(bl.latency == 1950);
    {   /* 속도 루프가 두 배 무거우면 큐도 깊어져야 한다 */
        sys_t heavy = s; heavy.e[E_SPEED].C = 300;
        backlog_t hb = can_backlog(&heavy, E_CANCMD, N_BURST, A_CAN, C_FRAME, 100000);
        CHECK(hb.ok && hb.depth == 8 && hb.latency == 2439);
    }
    producer_t prod[2] = {{T_SPEED * LOG_DECIM, 1}, {T_BURST, N_BURST}};
    uint32_t stall = ERASE_MAX + PROG_MAX + 2 * T_TICK;
    uint32_t cycle = ERASE_MAX + PAGES_PER_SECTOR * (PROG_MAX + T_TICK);
    uint32_t need = log_ring_needed(prod, 2, stall, REC_PER_PAGE, cycle, REC_PER_PAGE * PAGES_PER_SECTOR);
    CHECK(need == 218);
    {   /* 1 kHz 전부 로깅 + 20 ms 마다 버스트 → 최악 지우기 시간으로는 지속 불가 */
        producer_t hot[2] = {{T_SPEED, 1}, {20000, N_BURST}};
        CHECK(log_ring_needed(hot, 2, stall, REC_PER_PAGE, cycle, REC_PER_PAGE * PAGES_PER_SECTOR) == 0);
    }
    {   /* 스택 예산 (경로 바이트는 추정치, 방법은 RTOS-M08) */
        stk_t st[] = {{"wdg_mon", 96, 0}, {"speed", 320, 1}, {"can_cmd", 208, 1}, {"log_flush", 176, 0}, {"IDLE", 48, 0}};
        int b[5], tot = 0;
        for (int i = 0; i < 5; i++) { b[i] = with_margin(st[i].path + ctx_bytes(st[i].fpu)); tot += b[i]; }
        CHECK(ctx_bytes(1) == 208 && ctx_bytes(0) == 72);
        CHECK(b[1] == 664 && b[0] == 232 && tot == 1912);
        /* MSP: SysTick(44) ← bridge(40) ← can(64) ← cur(120) 중첩, 끼어들 때마다 정수 프레임 32 B */
        int msp = with_margin(44 + 32 + 40 + 32 + 64 + 32 + 120);
        CHECK(msp == 456);
        if (g_verbose) {
            printf("== stack budget (bytes, path = estimate)\n");
            for (int i = 0; i < 5; i++)
                printf("  %-9s path %3d fpu %d ctx %3d -> %3d B (%d words)\n", st[i].name, st[i].path, st[i].fpu,
                       ctx_bytes(st[i].fpu), b[i], b[i] / 4);
            printf("  tasks total %d B, MSP %d B, CAN queue %u B, log ring %u B, page buf %u B\n",
                   tot, msp, CAN_QDEPTH * 16u, LOG_RING * REC_BYTES, REC_PER_PAGE * REC_BYTES);
        }
    }
    if (g_verbose) {
        uint32_t U = 0;
        printf("== RTA (us)        kind  nvic/prio     C      T      D     J   B      R  ok iters\n");
        for (int i = 0; i < NE; i++) {
            const ent_t *e = &s.e[i];
            if (e->D) U += (uint32_t)((uint64_t)e->C * 10000 / e->T);
            if (e->D) printf("  %-14s %-5s %9d %5u %6u %6u %5u %3u %6u %3d %5u\n", e->name, e->isr ? "ISR" : "task",
                         e->isr ? e->nvic : e->prio, e->C, e->T, e->D, e->J, R[i].B, R[i].R, R[i].ok, R[i].iters);
        else printf("  %-14s %-5s %9d %5u %6u      - %5u %3u      -   -     - (no deadline: backlog / background)\n",
                    e->name, e->isr ? "ISR" : "task", e->isr ? e->nvic : e->prio, e->C, e->T, e->J, R[i].B);
        }
        printf("  U(periodic, x1e-4) = %u   CAN backlog: depth %u, burst latency %u us   log ring need %u rec\n",
               U, bl.depth, bl.latency, need);
    }

    /* ---------- Part B: 정상 설계의 핵심 경로 ---------- */
    cfg_t cfg = design_cfg(1000000);
    if (g_trace) { cfg.trace_from = 50000; cfg.trace_to = 50620; }
    stats_t h = sim_run(&cfg);
    g_trace = 0;
    if (g_verbose) printf("== simulation (us)\n");
    print_stats("design", &h);
    CHECK(h.cur_jobs == 10000 && h.cur_miss == 0 && h.cur_max == C_CUR);    /* NVIC 1 은 아무것도 못 막는다 */
    CHECK(h.spd_jobs >= 999 && h.spd_miss == 0 && h.spd_merged == 0);
    CHECK(h.spd_max <= R[E_SPEED].R && h.spd_jit_max <= s.e[E_SPEED].J);    /* 분석이 관측을 덮는다 */
    CHECK(h.canisr_max <= R[E_CAN].R && h.hw_overrun == 0);
    CHECK(h.frames == 19 * N_BURST && h.can_done == h.frames && h.q_drops == 0);
    CHECK(h.q_max >= 2 && h.q_max <= bl.depth && h.burst_lat_max <= bl.latency);
    CHECK(h.log_drops == 0 && h.ring_max <= need && h.erases == 2);
    CHECK(h.log_pushed == h.log_written + K.ring.count + K.copied);       /* 레코드 보존 */
    CHECK(!h.bite && h.kicks >= 49 && h.tick_lost == 0 && h.asserts == 0);
    {   /* 가드: 전류 ISR(NVIC 1)이 FromISR API 를 직접 부르면 assert, 알림은 안 간다 */
        cfg_t c = design_cfg(5000); c.cur_direct_notify = 1;
        stats_t d = sim_run(&c);
        print_stats("cur_direct_notify", &d);
        CHECK(d.asserts == 5 && d.spd_jobs == 0);
    }

    /* ---------- Part C: 실패 모드 ---------- */
    {   cfg_t c = design_cfg(500000); c.flush_naive = 1;
        stats_t d = sim_run(&c);
        print_stats("naive flush (crit)", &d);
        CHECK(d.spd_miss > 0 && d.hw_overrun > 0 && d.tick_lost > 0);
        CHECK(d.cur_miss == 0);                                             /* ISR 전류 루프는 여전히 정시 */
        CHECK(d.bite && d.bite_t < 200000);                                 /* 현장에서는 리셋 루프 */
    }
    {   cfg_t c = design_cfg(1000000); c.q_depth = 1;
        stats_t d = sim_run(&c);
        print_stats("can queue depth 1", &d);
        CHECK(d.q_drops > 0 && d.hw_overrun == 0);                          /* ISR 은 제때 — 소프트웨어 큐가 병목 */
    }
    {   cfg_t c = design_cfg(1000000); c.ring_cap = need / 2;
        stats_t d = sim_run(&c);
        print_stats("log ring need/2", &d);
        CHECK(d.log_drops > 0 && d.spd_miss == 0);                          /* 로그만 잃고 제어는 멀쩡 */
    }
    {   /* 고장 주입: 각 태스크가 300 ms 에 멈춘다 → 상한 = 모니터 주기 + R_mon + IWDG 안에 리셋 */
        uint32_t bound = MON_TICKS * T_TICK + R[E_WDG].R + WDG_US;
        static const int victims[3] = {TK_SPEED, TK_CAN, TK_LOG};
        static const uint32_t bits[3] = {BIT_SPEED, BIT_CAN, BIT_LOG};
        for (int v = 0; v < 3; v++) {
            cfg_t c = design_cfg(600000); c.stall_task = victims[v]; c.stall_at = 300300;
            stats_t d = sim_run(&c);
            print_stats(K.task[victims[v]].name, &d);
            CHECK(d.bite && d.bite_t > c.stall_at && d.bite_t - c.stall_at <= bound && d.last_missing == bits[v]);
        }
        cfg_t c = design_cfg(600000); c.stall_task = TK_SPEED; c.stall_at = 300300; c.stall_busy = 1;
        stats_t d = sim_run(&c);
        print_stats("speed busy-loop", &d);
        CHECK(d.bite && d.bite_t - c.stall_at <= bound && d.last_missing == WDG_ALL);
        CHECK(d.cur_miss == 0 && d.cur_jobs >= 3000);                       /* 그동안 전류 루프는 계속 돈다 */
        c.stall_busy = 0; c.kick_in_tick = 1;                               /* 나쁜 워치독 */
        d = sim_run(&c);
        print_stats("kick in tick hook", &d);
        CHECK(!d.bite && d.spd_jobs < 310);                                 /* 속도 루프가 죽었는데 리셋 없음 */
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
