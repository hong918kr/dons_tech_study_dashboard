/* RTOS-B11 메시지 큐: 생산자-소비자, 타임아웃, 복사 vs 포인터
 *
 * 호스트 모델: 단일 OS 스레드에서 도는 "틱 단위 결정적 미니 커널" (RTOS-B08 과 같은 틀).
 *   - 태스크 = 한 틱에 한 걸음씩 실행되는 상태 기계 (step 함수). 항상 우선순위가 가장 높은 READY 태스크가 돈다.
 *   - 한 틱의 순서: ① 틱 ISR(타임아웃·지연 만료) → ② 스케줄러 선택 → ③ (있으면) 장치 ISR → ④ 태스크 한 걸음.
 *     ④ 에서 걸음 도중 막히거나 더 높은 태스크를 깨우면(g_resched), 이 틱에 아직 안 돈 다음 태스크가 이어 받는다.
 *   - 큐 = 복사 큐 (FreeRTOS 와 같다): send 는 item_size 바이트를 큐 저장소로 memcpy, receive 는 밖으로 memcpy.
 *     큐마다 대기자 칸은 방향별 1개 (생산자 1 · 소비자 1). 여러 대기자의 우선순위 순 대기 리스트는 RTOS-M06.
 * 진짜 FreeRTOS 가 같은 순서·반환값을 내는지는 demo_freertos_queue.c / demo_freertos_pool.c 가 확인한다.
 *
 * 대응: kq_send = xQueueSend(xQueueSendToBack), kq_receive = xQueueReceive, kq_overwrite = xQueueOverwrite,
 *       kq_send_from_isr = xQueueSendFromISR, Q_FULL/Q_EMPTY = errQUEUE_FULL/errQUEUE_EMPTY, FOREVER = portMAX_DELAY.
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ======================= 미니 커널 (주어진 코드) ======================= */
enum { MAX_TASKS = 4, FOREVER = -1, TL = 64 };            /* TL = 타임라인에 기록할 틱 수 */
typedef enum { ST_READY, ST_BLOCKED } tstate_t;
typedef enum { WAKE_NONE, WAKE_DATA, WAKE_TIMEOUT } wake_t;   /* WAKE_DATA = 상대편의 send/receive 가 깨움 */
typedef enum { Q_OK, Q_FULL, Q_EMPTY, Q_BLOCKED } q_rc_t;
typedef enum { DIR_NONE, DIR_SEND, DIR_RECV } qdir_t;

typedef struct kqueue {
    unsigned char *buf;       /* len * item_size 바이트 저장소 (호출자가 준 정적 배열) */
    size_t item_size;         /* 항목 1개 크기 = send/receive 마다 memcpy 하는 바이트 수 (uxItemSize) */
    unsigned len;             /* 깊이 = 최대 항목 수 (uxLength) */
    unsigned count;           /* 지금 들어 있는 항목 수 (uxMessagesWaiting) */
    unsigned rd, wr;          /* 다음에 읽을 칸 / 다음에 쓸 칸 (칸 번호, 0..len-1) */
    int tx_waiter;            /* 꽉 차서 send 를 기다리는 태스크 (-1 = 없음)   ≈ xTasksWaitingToSend */
    int rx_waiter;            /* 비어서 receive 를 기다리는 태스크 (-1 = 없음) ≈ xTasksWaitingToReceive */
    unsigned overwritten;     /* kq_overwrite 가 아직 안 읽힌 값을 덮어쓴 횟수 */
} kqueue_t;

typedef struct {
    const char *name;
    int prio;                 /* 클수록 높다 (FreeRTOS 와 같은 방향) */
    tstate_t st;
    int deadline;             /* BLOCKED 일 때 타임아웃/지연으로 깨어날 틱 (FOREVER = 없음) */
    kqueue_t *wait_q;         /* 기다리는 큐 (NULL = 큐가 아니라 지연(delay) 중이거나 영원히 잠듦) */
    qdir_t wait_dir;
    wake_t wake;              /* 마지막으로 깨어난 이유 */
} task_t;

static task_t g_task[MAX_TASKS];
static int g_ntask;
static int g_cur = -1;        /* 지금 CPU 를 가진 태스크 (-1 = idle) */
static int g_tick;
static bool g_resched;        /* 더 높은 태스크를 깨웠다 → 이 틱 안에서 다시 고른다 (queueYIELD_IF_USING_PREEMPTION) */

static void kernel_reset(void) {
    memset(g_task, 0, sizeof g_task);
    g_ntask = 0; g_cur = -1; g_tick = 0; g_resched = false;
}
static int task_add(const char *name, int prio) {
    task_t *t = &g_task[g_ntask];
    t->name = name; t->prio = prio; t->st = ST_READY;
    t->deadline = FOREVER; t->wait_q = NULL; t->wait_dir = DIR_NONE; t->wake = WAKE_NONE;
    return g_ntask++;
}
static int pick_next(const bool *ran) {      /* 이 틱에 아직 안 돈 READY 중 가장 높은 우선순위 (같으면 먼저 만든 것) */
    int best = -1;
    for (int i = 0; i < g_ntask; i++)
        if (g_task[i].st == ST_READY && !(ran && ran[i]) && (best < 0 || g_task[i].prio > g_task[best].prio)) best = i;
    return best;
}
static void task_wake(int id, wake_t why) {  /* BLOCKED → READY */
    g_task[id].st = ST_READY;
    g_task[id].wait_q = NULL; g_task[id].wait_dir = DIR_NONE;
    g_task[id].deadline = FOREVER;
    g_task[id].wake = why;
}
/* self 를 q 의 dir 쪽 대기자로 막는다. q == NULL 이면 단순 지연. timeout 은 지금부터의 틱 수. */
static void task_block(int self, kqueue_t *q, qdir_t dir, int timeout) {
    task_t *t = &g_task[self];
    t->st = ST_BLOCKED;
    t->wait_q = q; t->wait_dir = dir;
    t->deadline = (timeout == FOREVER) ? FOREVER : g_tick + timeout;
    t->wake = WAKE_NONE;
    if (q) { if (dir == DIR_SEND) q->tx_waiter = self; else q->rx_waiter = self; }
}
static void task_sleep_until(int self, int tick) { task_block(self, NULL, DIR_NONE, tick - g_tick); }  /* vTaskDelayUntil */
static void kernel_tick(void) {              /* 틱 ISR: 만료된 타임아웃/지연을 깨운다 */
    for (int i = 0; i < g_ntask; i++) {
        task_t *t = &g_task[i];
        if (t->st == ST_BLOCKED && t->deadline != FOREVER && t->deadline <= g_tick) {
            kqueue_t *q = t->wait_q;
            if (q && q->tx_waiter == i) q->tx_waiter = -1;
            if (q && q->rx_waiter == i) q->rx_waiter = -1;
            task_wake(i, WAKE_TIMEOUT);
        }
    }
}
/* q 의 dir 쪽(DIR_SEND = 보내려고 기다리는 쪽, DIR_RECV = 받으려고 기다리는 쪽) 대기자를 깨운다.
 * 깨운 태스크가 지금 실행 중인 태스크(g_cur)보다 높으면(또는 idle 이면) true. */
static bool wake_waiter(kqueue_t *q, qdir_t dir) {
    int *slot = (dir == DIR_SEND) ? &q->tx_waiter : &q->rx_waiter;
    if (*slot < 0) return false;
    int w = *slot;
    *slot = -1;
    task_wake(w, WAKE_DATA);
    return g_cur < 0 || g_task[w].prio > g_task[g_cur].prio;
}
static void port_yield_from_isr(bool woken) { if (woken) g_resched = true; }

/* xQueueCreateStatic(len, item_size, storage, &ctrl) — 저장소는 호출자가 준다 (len * item_size 바이트) */
static bool kq_init(kqueue_t *q, void *storage, unsigned len, size_t item_size) {
    if (q == NULL || storage == NULL || len == 0 || item_size == 0) return false;
    q->buf = storage; q->item_size = item_size; q->len = len;
    q->count = q->rd = q->wr = 0;
    q->tx_waiter = q->rx_waiter = -1;
    q->overwritten = 0;
    return true;
}

/* ---- Part A: xQueueSend (= xQueueSendToBack) — 복사해서 넣는다 ----
 * 자리가 있으면: item 의 item_size 바이트를 wr 칸에 memcpy, wr 전진(끝이면 0 으로), count++,
 *   받으려고 기다리는 태스크가 있으면 깨우고(wake_waiter) 그 태스크가 더 높으면 g_resched = true. → Q_OK
 * 꽉 찼고 timeout == 0 → Q_FULL (아무것도 안 바꾼다).  꽉 찼고 timeout != 0 → task_block(DIR_SEND) 후 Q_BLOCKED. */
static q_rc_t kq_send(kqueue_t *q, const void *item, int self, int timeout) {
    /* TODO */
    (void)q; (void)item; (void)self; (void)timeout;
    (void)wake_waiter;                             /* (TODO 를 채우면 지운다: -Wunused-function 방지) */
    return Q_FULL;
}

/* ---- Part A: xQueueReceive — 복사해서 꺼낸다 ----
 * 항목이 있으면: rd 칸의 item_size 바이트를 out 으로 memcpy, rd 전진, count--,
 *   보내려고 기다리는 태스크가 있으면 깨우고 더 높으면 g_resched = true. → Q_OK
 * 비었고 timeout == 0 → Q_EMPTY (out 은 건드리지 않는다).  비었고 timeout != 0 → task_block(DIR_RECV), Q_BLOCKED. */
static q_rc_t kq_receive(kqueue_t *q, void *out, int self, int timeout) {
    /* TODO */
    (void)q; (void)out; (void)self; (void)timeout;
    return Q_EMPTY;
}

/* ---- Part B: xQueueOverwrite — 길이 1 큐 전용 "최신 값 우편함(mailbox)" ----
 * len != 1 이면 거부: Q_FULL 을 돌려주고 아무것도 안 바꾼다 (FreeRTOS 는 configASSERT 로 멈춘다).
 * len == 1: 비었으면 넣고 count = 1. 이미 차 있으면 그 값을 덮어쓰고 overwritten++ (count 는 1 그대로).
 * 절대 막히지 않는다. 받으려고 기다리는 태스크가 있으면 깨우고 더 높으면 g_resched = true. → Q_OK */
static q_rc_t kq_overwrite(kqueue_t *q, const void *item) {
    /* TODO */
    (void)q; (void)item;
    return Q_FULL;
}

/* ---- Part A: xQueueSendFromISR — ISR 에서. 절대 막히지 않는다 ----
 * 자리가 있으면 kq_send 와 같이 넣고 Q_OK. 깨운 태스크가 끼어든 태스크(g_cur)보다 높으면 *woken = true
 *   (g_resched 는 건드리지 않는다 — 전환은 ISR 끝의 port_yield_from_isr(woken) 이 결정). false 로 되돌리지 않는다.
 * 꽉 찼으면 즉시 Q_FULL. */
static q_rc_t kq_send_from_isr(kqueue_t *q, const void *item, bool *woken) {
    /* TODO */
    (void)q; (void)item; (void)woken;
    return Q_FULL;
}

/* ---- 깨어난 뒤 다시 시도하기 (주어진 코드) — FreeRTOS xQueueGenericSend/xQueueReceive 의 for(;;) 루프 ----
 * 처음 호출: 절대 마감 틱을 기억하고 시도. 막혔다가 깨어나면: WAKE_DATA 면 남은 시간으로 다시,
 * WAKE_TIMEOUT 이면 timeout 0 으로 "마지막 한 번" (FreeRTOS 도 루프 맨 위에서 자리를 다시 본 뒤에야 errQUEUE_FULL). */
typedef struct { bool pending; int deadline; } op_t;
static int op_remaining(int self, op_t *op, int timeout) {
    if (!op->pending) { op->deadline = (timeout == FOREVER) ? FOREVER : g_tick + timeout; return timeout; }
    wake_t w = g_task[self].wake;
    g_task[self].wake = WAKE_NONE;
    if (w == WAKE_TIMEOUT || op->deadline == FOREVER) return w == WAKE_TIMEOUT ? 0 : FOREVER;
    return op->deadline - g_tick;
}
static q_rc_t op_send(int self, kqueue_t *q, const void *item, int timeout, op_t *op) {
    q_rc_t rc = kq_send(q, item, self, op_remaining(self, op, timeout));
    op->pending = (rc == Q_BLOCKED);
    return rc;
}
static q_rc_t op_recv(int self, kqueue_t *q, void *out, int timeout, op_t *op) {
    q_rc_t rc = kq_receive(q, out, self, op_remaining(self, op, timeout));
    op->pending = (rc == Q_BLOCKED);
    return rc;
}

/* ---- 시뮬레이터 루프 (주어진 코드) ---- */
typedef void (*step_fn)(int id);
typedef void (*isr_fn)(void);
static char g_tl_task[MAX_TASKS][TL + 1], g_tl_q[2][TL + 1];   /* 타임라인: 틱마다 한 글자 */
static kqueue_t *g_tlq[2];                                      /* 타임라인에 개수를 찍을 큐 (틱 끝의 count) */
static const char *g_tlq_name[2];                               /* 마지막 실행에서 찍은 큐 이름 (NULL = 없음) */

static void sim_run(int ticks, step_fn step, isr_fn isr) {
    memset(g_tl_task, 0, sizeof g_tl_task);
    memset(g_tl_q, 0, sizeof g_tl_q);
    for (int k = 0; k < 2; k++) if (!g_tlq[k]) g_tlq_name[k] = NULL;
    for (g_tick = 0; g_tick < ticks; g_tick++) {
        bool ran[MAX_TASKS] = { false };
        kernel_tick();                                       /* ① 틱 ISR */
        g_cur = pick_next(ran);                              /* ② 스케줄러 */
        if (isr) {                                           /* ③ 장치 ISR */
            g_resched = false;
            isr();
            if (g_resched) g_cur = pick_next(ran);           /*    portYIELD_FROM_ISR → 즉시 전환 */
        }
        while (g_cur >= 0) {                                 /* ④ 이번 틱의 CPU */
            ran[g_cur] = true;
            g_resched = false;
            step(g_cur);
            if (g_task[g_cur].st == ST_READY && !g_resched) break;   /* 이 틱은 이 태스크가 썼다 */
            g_cur = pick_next(ran);                          /* 막혔거나 더 높은 태스크를 깨웠다 → 이어 받기 */
        }
        if (g_tick < TL) {
            for (int i = 0; i < g_ntask; i++)
                g_tl_task[i][g_tick] = ran[i] ? 'R' : g_task[i].st == ST_BLOCKED ? 'B' : 'r';
            for (int k = 0; k < 2; k++)
                if (g_tlq[k]) g_tl_q[k][g_tick] = (char)('0' + (g_tlq[k]->count > 9 ? 9 : g_tlq[k]->count));
        }
    }
}
static void print_timeline(const char *title, int from, int to) {
    if (to > TL) to = TL;
    printf("  -- %s\n  tick : ", title);
    for (int t = from; t < to; t++) printf("%d", t % 10);
    for (int i = 0; i < g_ntask; i++) {
        printf("\n  %-5s: ", g_task[i].name);
        for (int t = from; t < to; t++) printf("%c", g_tl_task[i][t]);
    }
    for (int k = 0; k < 2; k++) {
        if (!g_tlq_name[k]) continue;
        printf("\n  %-5s: ", g_tlq_name[k]);
        for (int t = from; t < to; t++) printf("%c", g_tl_q[k][t]);
    }
    printf("\n");
}

/* ======================= Part B 시나리오: 센서 → 처리 → 로깅 파이프라인 ======================= */
typedef struct { uint32_t seq; int32_t t_rel; int16_t raw[4]; } sample_t;   /* 16 B: IMU 축 3개 + 온도 */
typedef struct { uint32_t seq; int32_t t_rel; int32_t value; } logrec_t;    /* 12 B */
typedef enum { POL_BLOCK, POL_DROP, POL_TIMEOUT, POL_OVERWRITE } policy_t;

typedef struct {
    int n, period;            /* 센서 샘플 수, 주기(틱) */
    int proc, log_cost;       /* 처리 태스크 / 로깅 태스크의 항목당 CPU 틱 */
    int stall_seq, stall_cost;/* 이 seq 의 로깅만 stall_cost 틱 (플래시 페이지 지우기 같은 가끔 긴 일). -1 = 없음 */
    unsigned raw_depth, log_depth;
    policy_t pol; int pol_to; /* 가득 참 정책, POL_TIMEOUT 의 타임아웃 */
    int ticks;
} cfg_t;
typedef struct {
    unsigned produced, s_sent, s_drop, p_drop, logged;
    int s_maxlate;            /* 센서: 실제로 큐에 넣은 틱 - 원래 릴리즈 틱 의 최댓값 (backpressure 의 비용) */
    int max_e2e;              /* 로깅 완료 틱 - 릴리즈 틱 의 최댓값 */
    uint32_t log_seq[32];
    bool order_ok, data_ok;
} pres_t;

static cfg_t C;
static pres_t R;
static kqueue_t g_qraw, g_qlog;
static sample_t g_qraw_buf[16];
static logrec_t g_qlog_buf[16];
static int g_s, g_p, g_l;
static struct { int phase, release; uint32_t seq; sample_t smp; op_t op; } S;   /* 센서 */
static struct { int phase, done; sample_t in; logrec_t out; op_t op; } P;      /* 처리 */
static struct { int phase, done; logrec_t in; op_t op; } L;                    /* 로깅 */
enum { PH_RECV, PH_WORK, PH_SEND };

static int16_t raw_of(uint32_t seq, int k) { return (int16_t)(seq * 10u + (unsigned)k); }
static int32_t value_of(uint32_t seq) { int32_t v = 0; for (int k = 0; k < 4; k++) v += raw_of(seq, k); return v; }
static int send_timeout(void) {
    return C.pol == POL_DROP ? 0 : C.pol == POL_TIMEOUT ? C.pol_to : FOREVER;
}

static void sensor_step(int id) {             /* prio 3: period 마다 깨어나 샘플 1개를 q_raw 로 */
    if (S.phase == PH_RECV) {                 /* (센서에게 PH_RECV = "릴리즈 시각이 됐다, 샘플을 읽어라") */
        S.smp.seq = S.seq; S.smp.t_rel = S.release;
        for (int k = 0; k < 4; k++) S.smp.raw[k] = raw_of(S.seq, k);
        S.phase = PH_SEND;
    }
    q_rc_t rc = op_send(id, &g_qraw, &S.smp, C.pol == POL_OVERWRITE ? FOREVER : send_timeout(), &S.op);
    if (rc == Q_BLOCKED) return;
    if (rc == Q_OK) {
        R.s_sent++;
        if (g_tick - S.release > R.s_maxlate) R.s_maxlate = g_tick - S.release;
        memset(S.smp.raw, 0xEE, sizeof S.smp.raw);    /* 보낸 뒤 내 버퍼를 바로 재사용(낙서) — 복사 큐라 안전 */
    } else {
        R.s_drop++;                                    /* errQUEUE_FULL: 이 샘플은 버린다 (drop-newest) */
    }
    R.produced++;
    S.seq++; S.release += C.period; S.phase = PH_RECV;
    if ((int)S.seq >= C.n) task_block(id, NULL, DIR_NONE, FOREVER);       /* 끝: 영원히 잠든다 */
    else if (S.release > g_tick) task_sleep_until(id, S.release);         /* vTaskDelayUntil(&last, period) */
    /* else: 이미 늦었다 → 막히지 않고 다음 틱에 바로 다음 샘플 (xTaskDelayUntil 이 곧바로 돌아오는 경우) */
}
static void process_step(int id) {            /* prio 2: 받아서 proc 틱 동안 계산, 결과를 q_log 로 */
    for (;;) {
        if (P.phase == PH_RECV) {
            if (op_recv(id, &g_qraw, &P.in, FOREVER, &P.op) != Q_OK) return;   /* 막혔다 */
            P.phase = PH_WORK; P.done = 0;
        }
        if (P.phase == PH_WORK) {
            if (P.done < C.proc) { P.done++; return; }     /* 이 틱을 계산에 썼다 */
            P.out.seq = P.in.seq; P.out.t_rel = P.in.t_rel; P.out.value = 0;
            for (int k = 0; k < 4; k++) P.out.value += P.in.raw[k];
            P.phase = PH_SEND;
        }
        if (C.pol == POL_OVERWRITE) {
            kq_overwrite(&g_qlog, &P.out);
        } else {
            q_rc_t rc = op_send(id, &g_qlog, &P.out, send_timeout(), &P.op);
            if (rc == Q_BLOCKED) return;
            if (rc != Q_OK) R.p_drop++;
        }
        P.phase = PH_RECV;
    }
}
static void log_step(int id) {                /* prio 1: 받아서 log_cost 틱 (UART/플래시 쓰기) */
    for (;;) {
        if (L.phase == PH_RECV) {
            if (op_recv(id, &g_qlog, &L.in, FOREVER, &L.op) != Q_OK) return;
            L.phase = PH_WORK; L.done = 0;
        }
        int cost = ((int)L.in.seq == C.stall_seq) ? C.stall_cost : C.log_cost;
        if (L.done < cost) { L.done++; return; }
        if (R.logged > 0 && L.in.seq <= R.log_seq[R.logged - 1]) R.order_ok = false;   /* FIFO: seq 는 늘기만 */
        if (L.in.value != value_of(L.in.seq)) R.data_ok = false;                         /* 복사가 깨졌나 */
        if (R.logged < 32) R.log_seq[R.logged] = L.in.seq;
        R.logged++;
        if (g_tick - L.in.t_rel > R.max_e2e) R.max_e2e = g_tick - L.in.t_rel;
        L.phase = PH_RECV;
    }
}
static void pipe_step(int id) {
    if (id == g_s) sensor_step(id); else if (id == g_p) process_step(id); else log_step(id);
}
static pres_t run_pipeline(cfg_t cfg) {
    C = cfg;
    kernel_reset();
    memset(&R, 0, sizeof R); R.order_ok = R.data_ok = true;
    memset(&S, 0, sizeof S); memset(&P, 0, sizeof P); memset(&L, 0, sizeof L);
    kq_init(&g_qraw, g_qraw_buf, C.raw_depth, sizeof(sample_t));
    kq_init(&g_qlog, g_qlog_buf, C.log_depth, sizeof(logrec_t));
    g_tlq[0] = &g_qraw; g_tlq[1] = &g_qlog; g_tlq_name[0] = "qRaw"; g_tlq_name[1] = "qLog";
    g_s = task_add("S", 3); g_p = task_add("P", 2); g_l = task_add("L", 1);
    sim_run(C.ticks, pipe_step, NULL);
    g_tlq[0] = g_tlq[1] = NULL;
    return R;
}

/* ======================= Part C: 큰 버퍼는 포인터 + 고정 블록 풀 ======================= */
#define BLK_SIZE 256u                          /* 예: IMU FIFO 한 번 읽기 / 오디오 프레임 */
#define POOL_N   4u
typedef struct blk {
    struct blk *next;                          /* 자유 리스트 링크 (풀 안에 있을 때만 의미) */
    uint32_t seq;
    uint8_t data[BLK_SIZE];
} blk_t;
typedef struct {
    blk_t blocks[POOL_N];
    blk_t *free_head;                          /* 자유 리스트 (LIFO) */
    unsigned nfree, min_free;                  /* 남은 블록 수, 그 최솟값(최악 사용량 = POOL_N - min_free) */
    bool in_use[POOL_N];                       /* 블록별 "누가 갖고 있다" — 이중 해제·남의 포인터 탐지용 */
} pool_t;

static void pool_init(pool_t *p) {
    p->free_head = NULL;
    for (unsigned i = POOL_N; i-- > 0;) { p->blocks[i].next = p->free_head; p->free_head = &p->blocks[i]; p->in_use[i] = false; }
    p->nfree = p->min_free = POOL_N;
}

/* ---- Part C: 블록 하나 빌리기 ----
 * 자유 리스트 머리에서 하나 떼어 in_use 표시, nfree--, min_free 갱신 후 돌려준다. 비었으면 NULL (막히지 않는다). */
static blk_t *pool_alloc(pool_t *p) {
    /* TODO */
    (void)p;
    return NULL;
}

/* ---- Part C: 블록 돌려주기 ----
 * b 가 이 풀의 블록이 아니거나(NULL 포함) 이미 자유 상태면 false (이중 해제) — 아무것도 바꾸지 않는다.
 * 아니면 in_use 해제, 자유 리스트 머리에 붙이고 nfree++, true.
 * (어느 블록인지는 == 비교로 찾는다: 다른 객체를 가리킬 수 있는 포인터에 < > 는 정의되지 않은 동작) */
static bool pool_free(pool_t *p, blk_t *b) {
    /* TODO */
    (void)p; (void)b;
    return false;
}

/* ---- Part C 시나리오: DMA 프레임 생산 태스크 → 포인터 큐 → 소비 태스크 ---- */
typedef struct { unsigned produced, sent, drop_nomem, drop_full, consumed, bad, leaked_free_fail; uint32_t last_seq; bool order_ok; } cres_t;
static pool_t g_pool;
static kqueue_t g_qptr;
static blk_t *g_qptr_buf[8];                   /* 큐에는 포인터(8 B)만 들어간다 */
static cres_t CR;
static int g_cp, g_cc, g_cn, g_cperiod, g_ccost, g_ctimeout;
static struct { int release; uint32_t seq; op_t op; } CP;
static struct { int phase, done; blk_t *b; op_t op; } CC;

static uint8_t pat(uint32_t seq, unsigned i) { return (uint8_t)(seq * 31u + i); }

/* ---- Part C: 프레임 하나를 넘기기 (소유권 이전) ----
 * 1) pool_alloc — 없으면 drop_nomem++ 하고 false.  2) seq 와 data[i] = pat(seq, i) 로 채운다.
 * 3) 포인터를 g_qptr 로 send (timeout = g_ctimeout). Q_OK → sent++ 하고 true: 이제 블록은 소비자 것.
 *    Q_FULL → 소유권이 안 넘어갔다! 생산자가 스스로 pool_free 하고 drop_full++, false.
 * (이 시나리오에서는 g_ctimeout == 0 이라 Q_BLOCKED 는 안 나온다.) */
static bool submit_frame(int self, uint32_t seq) {
    /* TODO */
    (void)self; (void)seq;
    return false;
}

static void cprod_step(int id) {               /* prio 3: g_cperiod 마다 프레임 1개 */
    submit_frame(id, CP.seq);
    CR.produced++;
    CP.seq++; CP.release += g_cperiod;
    if ((int)CP.seq >= g_cn) task_block(id, NULL, DIR_NONE, FOREVER);
    else task_sleep_until(id, CP.release);
}
static void ccons_step(int id) {               /* prio 1: 받아서 g_ccost 틱 처리(예: FFT, 압축), 검사, 반납 */
    for (;;) {
        if (CC.phase == PH_RECV) {
            if (op_recv(id, &g_qptr, &CC.b, FOREVER, &CC.op) != Q_OK) return;
            CC.phase = PH_WORK; CC.done = 0;
        }
        if (CC.done < g_ccost) { CC.done++; return; }
        bool ok = true;
        for (unsigned i = 0; i < BLK_SIZE; i++) if (CC.b->data[i] != pat(CC.b->seq, i)) ok = false;
        if (!ok) CR.bad++;
        if (CR.consumed > 0 && CC.b->seq <= CR.last_seq) CR.order_ok = false;
        CR.last_seq = CC.b->seq;
        CR.consumed++;
        if (!pool_free(&g_pool, CC.b)) CR.leaked_free_fail++;   /* 다 쓴 소비자가 반납 */
        CC.b = NULL;
        CC.phase = PH_RECV;
    }
}
static void cpipe_step(int id) { if (id == g_cp) cprod_step(id); else ccons_step(id); }
static cres_t run_ptr_pipeline(int n, int period, int cost, unsigned qdepth, int ticks) {
    kernel_reset();
    memset(&CR, 0, sizeof CR); CR.order_ok = true;
    memset(&CP, 0, sizeof CP); memset(&CC, 0, sizeof CC);
    pool_init(&g_pool);
    kq_init(&g_qptr, g_qptr_buf, qdepth, sizeof(blk_t *));
    g_cn = n; g_cperiod = period; g_ccost = cost; g_ctimeout = 0;
    g_tlq[0] = &g_qptr; g_tlq_name[0] = "qPtr";
    g_cp = task_add("prod", 3); g_cc = task_add("cons", 1);
    sim_run(ticks, cpipe_step, NULL);
    g_tlq[0] = NULL;
    return CR;
}

/* ======================= ISR 변형: 타이머 ISR → 큐 (Part A 의 kq_send_from_isr) ======================= */
static kqueue_t g_qisr;
static uint32_t g_qisr_buf[4];
static int g_ib, g_ic;                         /* 받는 태스크(prio 2), 배경 태스크(prio 1) */
static unsigned g_isr_n, g_isr_ok, g_isr_full, g_isr_woken, g_isr_got;
static int g_first_rx = -1;
static void timer_isr(void) {                  /* 2..2+g_isr_n-1 틱에 1번씩 (예: 1 kHz ADC 완료 ISR) */
    if (g_tick < 2 || (unsigned)g_tick >= 2u + g_isr_n) return;
    uint32_t v = (uint32_t)g_tick - 2u;
    bool woken = false;                        /* BaseType_t xHigherPriorityTaskWoken = pdFALSE; */
    if (kq_send_from_isr(&g_qisr, &v, &woken) == Q_OK) g_isr_ok++; else g_isr_full++;
    if (woken) g_isr_woken++;
    port_yield_from_isr(woken);
}
static struct { int phase, done; uint32_t v; op_t op; } IB;
static void isr_step(int id) {
    if (id == g_ic) return;                    /* 배경: 늘 READY, 늘 할 일 있음 */
    for (;;) {
        if (IB.phase == PH_RECV) {
            if (op_recv(id, &g_qisr, &IB.v, FOREVER, &IB.op) != Q_OK) return;
            if (g_first_rx < 0) g_first_rx = g_tick;
            IB.phase = PH_WORK; IB.done = 0;
        }
        if (IB.done < 3) { IB.done++; return; }  /* 항목당 3 틱: ISR(1/틱)보다 느리다 */
        g_isr_got++;
        IB.phase = PH_RECV;
    }
}

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main(void) {
    kqueue_t q;
    sample_t st[3], a, b;
    printf("[Part A] 복사 큐: send/receive, FIFO, 가득 참/빔, 블로킹, ISR\n");
    CHECK(!kq_init(&q, st, 0, sizeof(sample_t)));             /* 깊이 0 은 생성 실패 */
    CHECK(kq_init(&q, st, 3, sizeof(sample_t)) && q.count == 0u);
    kernel_reset();
    int me = task_add("me", 2);
    memset(&a, 0, sizeof a); a.seq = 1; a.raw[0] = 100;
    CHECK(kq_send(&q, &a, me, 0) == Q_OK && q.count == 1u);
    a.seq = 99; a.raw[0] = -1;                                  /* 보낸 뒤 원본을 바꾼다 */
    CHECK(q.count == 1u && st[0].seq == 1u && st[0].raw[0] == 100);   /* 큐 안의 사본은 그대로 (복사 의미론) */
    a.seq = 2; CHECK(kq_send(&q, &a, me, 0) == Q_OK);
    a.seq = 3; CHECK(kq_send(&q, &a, me, 0) == Q_OK && q.count == 3u);
    a.seq = 4; CHECK(kq_send(&q, &a, me, 0) == Q_FULL && q.count == 3u);   /* 꽉 참 + timeout 0 → 즉시 실패 */
    CHECK(kq_receive(&q, &b, me, 0) == Q_OK && b.seq == 1u && b.raw[0] == 100);   /* FIFO + 보낸 그때의 값 */
    CHECK(kq_receive(&q, &b, me, 0) == Q_OK && b.seq == 2u);
    CHECK(kq_receive(&q, &b, me, 0) == Q_OK && b.seq == 3u && q.count == 0u);
    b.seq = 777;
    CHECK(kq_receive(&q, &b, me, 0) == Q_EMPTY && b.seq == 777u);   /* 빔 + timeout 0: out 은 건드리지 않는다 */
    bool fifo = true; uint32_t nxt = 10, want = 10;                 /* 링 끝을 여러 번 넘기며 순서 확인 */
    for (int round = 0; round < 20; round++) {
        int ns = 1 + round % 3, nr = 1 + (round * 7) % 3;
        for (int k = 0; k < ns && q.count < q.len; k++) { a.seq = nxt++; kq_send(&q, &a, me, 0); }
        for (int k = 0; k < nr && q.count > 0; k++) { kq_receive(&q, &b, me, 0); if (b.seq != want++) fifo = false; }
    }
    CHECK(fifo && q.rd < q.len && q.wr < q.len);
    /* 블로킹과 타임아웃 */
    kernel_reset();
    int hi = task_add("H", 3), lo = task_add("L", 1);
    kq_init(&q, st, 2, sizeof(sample_t));
    g_tick = 10;
    CHECK(kq_receive(&q, &b, hi, 5) == Q_BLOCKED && g_task[hi].st == ST_BLOCKED && q.rx_waiter == hi);
    CHECK(g_task[hi].deadline == 15);
    g_tick = 15; kernel_tick();
    CHECK(g_task[hi].st == ST_READY && g_task[hi].wake == WAKE_TIMEOUT && q.rx_waiter == -1);
    CHECK(kq_receive(&q, &b, hi, FOREVER) == Q_BLOCKED && g_task[hi].deadline == FOREVER);
    g_cur = lo; g_resched = false; a.seq = 42;
    CHECK(kq_send(&q, &a, lo, 0) == Q_OK && g_task[hi].st == ST_READY && g_task[hi].wake == WAKE_DATA);
    CHECK(g_resched && q.rx_waiter == -1);                     /* L 이 H 를 깨웠다 → 즉시 전환 요청 */
    a.seq = 43; kq_send(&q, &a, lo, 0);                         /* 이제 꽉 참 (2/2) */
    CHECK(kq_send(&q, &a, lo, 4) == Q_BLOCKED && q.tx_waiter == lo && g_task[lo].deadline == 19);
    g_cur = hi; g_resched = false;
    CHECK(kq_receive(&q, &b, hi, 0) == Q_OK && b.seq == 42u);  /* 자리가 났다 → 보내려던 L 을 깨운다 */
    CHECK(g_task[lo].st == ST_READY && g_task[lo].wake == WAKE_DATA && q.tx_waiter == -1 && !g_resched);  /* L 이 낮다 */
    /* ISR 판: 절대 막히지 않고, woken 은 깨운 태스크가 끼어든 태스크보다 높을 때만 */
    kernel_reset();
    hi = task_add("H", 3); lo = task_add("L", 1);
    uint32_t iv = 7, ib[2];
    kq_init(&q, ib, 2, sizeof(uint32_t));
    g_cur = hi; kq_receive(&q, &iv, hi, FOREVER);               /* H 가 비어 있는 큐에서 기다린다 */
    g_cur = lo; bool w = false;
    CHECK(kq_send_from_isr(&q, &iv, &w) == Q_OK && w && g_task[hi].st == ST_READY && !g_resched);
    CHECK(kq_send_from_isr(&q, &iv, &w) == Q_OK && w);         /* 대기자 없음: woken 을 false 로 안 되돌림 */
    w = false;
    CHECK(kq_send_from_isr(&q, &iv, &w) == Q_FULL && !w && q.count == 2u && g_task[lo].st == ST_READY);   /* 꽉 참: 즉시 실패 */

    printf("[Part B] 파이프라인: 깊이 · 타임아웃 · 가득 참 정책\n");
    kqueue_t mb; logrec_t mbs[1], m1 = { 1, 0, 11 }, m2 = { 2, 0, 22 }, mo;
    kernel_reset(); me = task_add("me", 2);
    kq_init(&mb, mbs, 1, sizeof(logrec_t));
    CHECK(kq_overwrite(&mb, &m1) == Q_OK && mb.count == 1u && mb.overwritten == 0u);
    CHECK(kq_overwrite(&mb, &m2) == Q_OK && mb.count == 1u && mb.overwritten == 1u);   /* 덮어쓰기, 개수 그대로 */
    CHECK(kq_receive(&mb, &mo, me, 0) == Q_OK && mo.seq == 2u && mb.count == 0u);        /* 늘 최신 값 */
    CHECK(kq_overwrite(&q, &iv) == Q_FULL && q.count == 2u);   /* 길이 2 큐에는 거부 (FreeRTOS: configASSERT) */

    /* 기준: 12 샘플, 주기 3, 처리 1, 로깅 1 — 여유 있음 */
    cfg_t base = { 12, 3, 1, 1, -1, 0, 4, 2, POL_BLOCK, 0, 60 };
    pres_t r = run_pipeline(base);
    print_timeline("BLOCK, no stall (period 3, proc 1, log 1)", 0, 20);
    CHECK(r.produced == 12u && r.logged == 12u && r.s_drop == 0u && r.p_drop == 0u);
    CHECK(r.order_ok && r.data_ok && r.s_maxlate == 0 && r.log_seq[11] == 11u);
    /* 가끔 긴 로깅: seq 2 의 로깅이 12 틱 (플래시 지우기) */
    cfg_t stall = base; stall.stall_seq = 2; stall.stall_cost = 12;
    r = run_pipeline(stall);
    print_timeline("BLOCK, log stall 12 ticks on seq 2 (raw depth 4, log depth 2)", 0, 44);
    CHECK(r.logged == 12u && r.s_drop == 0u && r.p_drop == 0u && r.order_ok && r.data_ok);   /* 안 잃는다 */
    CHECK(r.s_maxlate == 0);                                    /* 깊이 4+2 가 stall 을 흡수: 센서는 안 늦었다 */
    cfg_t shallow = stall; shallow.raw_depth = 1; shallow.log_depth = 1;
    r = run_pipeline(shallow);
    print_timeline("BLOCK, same stall, depth 1/1 → backpressure", 0, 44);
    CHECK(r.logged == 12u && r.s_drop == 0u && r.p_drop == 0u && r.order_ok);   /* 여전히 안 잃는다 */
    CHECK(r.s_maxlate == 3);                                    /* 대신 센서가 3 틱(= 한 주기) 늦게 넣었다 */
    cfg_t drop = shallow; drop.pol = POL_DROP;
    r = run_pipeline(drop);
    print_timeline("DROP (timeout 0), depth 1/1", 0, 44);
    CHECK(r.s_maxlate == 0 && r.p_drop == 5u && r.s_drop == 0u && r.logged == 7u);   /* 센서는 제때, 5개 버림 */
    CHECK(r.order_ok && r.data_ok && r.log_seq[3] == 3u && r.log_seq[4] == 9u);       /* 버린 건 4..8 (새로 온 것들) */
    cfg_t tmo = shallow; tmo.pol = POL_TIMEOUT; tmo.pol_to = 2;
    r = run_pipeline(tmo);
    CHECK(r.p_drop + r.s_drop == 4u && r.logged == 8u && r.s_maxlate == 0);   /* 2 틱 기다려 1개를 더 살렸다 */
    CHECK(r.log_seq[4] == 8u && r.order_ok);
    cfg_t ow = shallow; ow.pol = POL_OVERWRITE;
    r = run_pipeline(ow);
    print_timeline("OVERWRITE (log queue length 1)", 0, 44);
    CHECK(r.logged == 7u && g_qlog.overwritten == 5u && r.logged + g_qlog.overwritten == 12u);
    CHECK(r.log_seq[2] == 2u && r.log_seq[3] == 8u && r.order_ok && r.s_maxlate == 0);   /* 3..7 을 덮고 최신 8 을 남겼다 */
    unsigned drops[7];                                          /* 깊이 스윕 (DROP 정책): log 큐 깊이 1..7 */
    for (unsigned d = 1; d <= 7; d++) { cfg_t c = drop; c.log_depth = d; drops[d - 1] = run_pipeline(c).p_drop; }
    printf("  log depth : 1 2 3 4 5 6 7\n  drops     :");
    for (unsigned d = 0; d < 7; d++) printf(" %u", drops[d]);
    printf("\n");
    bool lin = true;                                            /* 깊이 1 늘 때마다 1개씩 덜 버린다: max(0, 6 - d) */
    for (unsigned d = 1; d <= 7; d++) if (drops[d - 1] != (d < 6 ? 6 - d : 0)) lin = false;
    CHECK(lin);

    printf("[Part C] 큰 버퍼: 포인터 + 고정 블록 풀\n");
    pool_t *pl = &g_pool;
    pool_init(pl);
    blk_t *p1 = pool_alloc(pl), *p2 = pool_alloc(pl);
    CHECK(p1 && p2 && p1 != p2 && pl->nfree == POOL_N - 2);
    CHECK(pool_free(pl, p1) && pl->nfree == POOL_N - 1);
    CHECK(!pool_free(pl, p1) && pl->nfree == POOL_N - 1);       /* 이중 해제는 거부 */
    blk_t outsider;
    CHECK(!pool_free(pl, &outsider) && !pool_free(pl, NULL));   /* 남의 포인터·NULL 거부 */
    blk_t *all[POOL_N + 1]; unsigned got = 0;
    pool_init(pl);
    while ((all[got] = pool_alloc(pl)) != NULL && got < POOL_N) got++;
    CHECK(got == POOL_N && pl->nfree == 0u && pool_alloc(pl) == NULL);   /* 다 쓰면 NULL (막히지 않는다) */
    for (unsigned i = 0; i < got; i++) pool_free(pl, all[i]);
    CHECK(pl->nfree == POOL_N && pl->min_free == 0u);
    /* 파이프라인: 소비자가 충분히 빠르면 다 전달, 데이터 그대로, 블록 전부 반납 */
    cres_t c = run_ptr_pipeline(10, 3, 2, 2, 40);
    CHECK(c.sent == 10u && c.consumed == 10u && c.bad == 0u && c.order_ok);
    CHECK(g_pool.nfree == POOL_N && c.leaked_free_fail == 0u);
    CHECK(g_qptr.item_size == sizeof(blk_t *));                 /* 큐가 복사하는 건 포인터 8 B 뿐 */
    /* 소비자가 느리면(처리 7 > 주기 3): 큐 깊이 4, 풀 4 → 풀이 먼저 마른다 */
    c = run_ptr_pipeline(10, 3, 7, 4, 80);
    print_timeline("pointer queue, slow consumer (period 3, cost 7, pool 4, qdepth 4)", 0, 40);
    printf("  qdepth 4: produced %u sent %u drop_nomem %u drop_full %u consumed %u min_free %u\n",
           c.produced, c.sent, c.drop_nomem, c.drop_full, c.consumed, g_pool.min_free);
    CHECK(c.produced == 10u && c.sent == c.consumed && c.sent + c.drop_nomem + c.drop_full == 10u);
    CHECK(c.drop_nomem == 3u && c.drop_full == 0u && g_pool.min_free == 0u);  /* 풀(4) ≤ 큐(4)+1 → 풀이 병목 */
    CHECK(g_pool.nfree == POOL_N && c.bad == 0u && c.order_ok);               /* 그래도 새는 블록 없음 */
    /* 큐가 더 얕으면(깊이 1): 큐가 먼저 찬다 → 생산자가 직접 반납해야 새지 않는다 */
    c = run_ptr_pipeline(10, 3, 7, 1, 80);
    printf("  qdepth 1: produced %u sent %u drop_nomem %u drop_full %u consumed %u min_free %u\n",
           c.produced, c.sent, c.drop_nomem, c.drop_full, c.consumed, g_pool.min_free);
    CHECK(c.sent == 5u && c.drop_full == 5u && c.drop_nomem == 0u && g_pool.min_free == 1u);   /* 쓰는 블록 최대 3 */
    CHECK(g_pool.nfree == POOL_N && c.leaked_free_fail == 0u);  /* send 실패 경로에서도 반납했다 */
    /* ISR → 큐: ISR 은 기다릴 수 없다 → 꽉 차면 버리고 센다 */
    kernel_reset();
    kq_init(&g_qisr, g_qisr_buf, 4, sizeof(uint32_t));
    g_ib = task_add("rx", 2); g_ic = task_add("bg", 1);
    g_isr_n = 12; g_isr_ok = g_isr_full = g_isr_woken = g_isr_got = 0; g_first_rx = -1;
    memset(&IB, 0, sizeof IB);
    g_tlq[0] = &g_qisr; g_tlq_name[0] = "q";
    sim_run(60, isr_step, timer_isr);
    g_tlq[0] = NULL;
    print_timeline("ISR sends every tick 2..13, task needs 3 ticks/item, depth 4", 0, 26);
    printf("  isr ok %u full %u woken %u, task got %u\n", g_isr_ok, g_isr_full, g_isr_woken, g_isr_got);
    CHECK(g_isr_ok + g_isr_full == 12u && g_isr_got == g_isr_ok && g_first_rx == 2);
    CHECK(g_isr_full == 4u && g_isr_woken == 1u);           /* woken 은 rx 가 막혀 있던 첫 ISR 에서만 */

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
