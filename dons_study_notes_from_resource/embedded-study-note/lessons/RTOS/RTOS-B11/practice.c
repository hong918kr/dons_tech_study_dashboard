/* RTOS-B11 추가 연습 — 한 번에 검증
 *
 * 미니 커널·복사 큐(kq_*)·시뮬레이터는 solution.c 와 같은 코드 (TODO 를 채운 판).
 *   연습 1 — drop-oldest 정책 (FreeRTOS 에 없는 것: receive 로 하나 버리고 send)
 *   연습 2 — 버스트 흡수에 필요한 최소 큐 깊이 공식 vs 시뮬레이션 (B 1..20, C 1..6 전수)
 *   연습 3 — 최신 값 우편함: xQueueOverwrite + xQueuePeek, 읽는 쪽 여럿
 *   연습 4 — 포인터 큐의 use-after-send: 우선순위에 따라 보였다 안 보였다 → 소유권 태그로 항상 잡기
 *   연습 5 — 풀을 "빈 블록 포인터의 큐" 로 (ISR 에서도 쓰는 FreeRTOS 관용구), 연결 리스트 풀과 같은 결과
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
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
    if (q->count < q->len) {
        memcpy(q->buf + (size_t)q->wr * q->item_size, item, q->item_size);
        q->wr = (q->wr + 1 == q->len) ? 0 : q->wr + 1;
        q->count++;
        if (wake_waiter(q, DIR_RECV)) g_resched = true;
        return Q_OK;
    }
    if (timeout == 0) return Q_FULL;
    task_block(self, q, DIR_SEND, timeout);
    return Q_BLOCKED;
}

/* ---- Part A: xQueueReceive — 복사해서 꺼낸다 ----
 * 항목이 있으면: rd 칸의 item_size 바이트를 out 으로 memcpy, rd 전진, count--,
 *   보내려고 기다리는 태스크가 있으면 깨우고 더 높으면 g_resched = true. → Q_OK
 * 비었고 timeout == 0 → Q_EMPTY (out 은 건드리지 않는다).  비었고 timeout != 0 → task_block(DIR_RECV), Q_BLOCKED. */
static q_rc_t kq_receive(kqueue_t *q, void *out, int self, int timeout) {
    if (q->count > 0) {
        memcpy(out, q->buf + (size_t)q->rd * q->item_size, q->item_size);
        q->rd = (q->rd + 1 == q->len) ? 0 : q->rd + 1;
        q->count--;
        if (wake_waiter(q, DIR_SEND)) g_resched = true;
        return Q_OK;
    }
    if (timeout == 0) return Q_EMPTY;
    task_block(self, q, DIR_RECV, timeout);
    return Q_BLOCKED;
}

/* ---- Part B: xQueueOverwrite — 길이 1 큐 전용 "최신 값 우편함(mailbox)" ----
 * len != 1 이면 거부: Q_FULL 을 돌려주고 아무것도 안 바꾼다 (FreeRTOS 는 configASSERT 로 멈춘다).
 * len == 1: 비었으면 넣고 count = 1. 이미 차 있으면 그 값을 덮어쓰고 overwritten++ (count 는 1 그대로).
 * 절대 막히지 않는다. 받으려고 기다리는 태스크가 있으면 깨우고 더 높으면 g_resched = true. → Q_OK */
static q_rc_t kq_overwrite(kqueue_t *q, const void *item) {
    if (q->len != 1) return Q_FULL;
    if (q->count == 1) q->overwritten++;
    memcpy(q->buf, item, q->item_size);
    q->count = 1; q->rd = q->wr = 0;
    if (wake_waiter(q, DIR_RECV)) g_resched = true;
    return Q_OK;
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


/* ======================= 연습 1 — drop-oldest ======================= */
/* 꽉 차 있으면 가장 오래된 항목 1개를 scratch 로 꺼내 버리고(*dropped++) 새 항목을 넣는다. 절대 막히지 않는다.
 * (FreeRTOS 로는 xQueueReceive(q, scratch, 0) 다음 xQueueSend(q, item, 0) — 두 호출 사이에 다른 생산자가
 *  끼어들 수 있으므로 생산자가 하나일 때만 "항상 성공" 이 보장된다. 연습 1 의 답 참고.) */
static q_rc_t kq_send_drop_oldest(kqueue_t *q, const void *item, void *scratch, unsigned *dropped, int self) {
    if (q->count == q->len) {
        kq_receive(q, scratch, self, 0);
        (*dropped)++;
    }
    return kq_send(q, item, self, 0);
}

/* ======================= 연습 2 — 버스트 흡수 깊이 ======================= */
/* 0..B-1 틱에 1개씩 도착(send 먼저), 소비자는 한가하면 같은 틱에 꺼내고 C 틱 동안 바쁘다. 깊이 d 로 버린 개수. */
static unsigned burst_drops(unsigned B, unsigned C, unsigned d) {
    static uint32_t store[32];
    kqueue_t q;
    kq_init(&q, store, d, sizeof(uint32_t));
    kernel_reset();
    int me = task_add("x", 1);
    unsigned drops = 0, busy_until = 0;
    for (unsigned t = 0; t < B + B * C + 1; t++) {
        uint32_t v = t, out;
        if (t < B && kq_send(&q, &v, me, 0) != Q_OK) drops++;
        if (t >= busy_until && kq_receive(&q, &out, me, 0) == Q_OK) busy_until = t + C;
    }
    return drops;
}
/* 공식: 틱 t 의 send 직후 개수 = (t+1) - ceil(t/C) (그 전까지 꺼낸 수), t 에 대해 줄지 않으므로 t = B-1 에서 최대. */
static unsigned min_depth_formula(unsigned B, unsigned C) { return B - (B - 1 + C - 1) / C; }

/* ======================= 연습 3 — 우편함 + peek ======================= */
/* 지우지 않고 맨 앞 항목을 복사해 본다 (xQueuePeek, timeout 0). 비었으면 Q_EMPTY. */
static q_rc_t kq_peek(const kqueue_t *q, void *out) {
    if (q->count == 0) return Q_EMPTY;
    memcpy(out, q->buf + (size_t)q->rd * q->item_size, q->item_size);
    return Q_OK;
}

/* ======================= 연습 4, 5 — 포인터 + 풀 (solution 의 Part C + 소유권 태그 + free-queue 풀) ======================= */
#define BLK_SIZE 256u
#define POOL_N   4u
enum { OWNER_POOL = -1 };
typedef struct blk { struct blk *next; uint32_t seq; int owner; uint8_t data[BLK_SIZE]; } blk_t;
typedef struct { blk_t blocks[POOL_N]; blk_t *free_head; unsigned nfree, min_free; bool in_use[POOL_N]; } pool_t;

static void pool_init(pool_t *p) {
    p->free_head = NULL;
    for (unsigned i = POOL_N; i-- > 0;) {
        p->blocks[i].next = p->free_head; p->free_head = &p->blocks[i];
        p->in_use[i] = false; p->blocks[i].owner = OWNER_POOL;
    }
    p->nfree = p->min_free = POOL_N;
}
static blk_t *pool_alloc(pool_t *p) {
    blk_t *b = p->free_head;
    if (b == NULL) return NULL;
    p->free_head = b->next; b->next = NULL;
    p->in_use[b - p->blocks] = true;
    if (--p->nfree < p->min_free) p->min_free = p->nfree;
    return b;
}
static bool pool_free(pool_t *p, blk_t *b) {
    unsigned i = 0;
    while (i < POOL_N && b != &p->blocks[i]) i++;
    if (i == POOL_N || !p->in_use[i]) return false;
    p->in_use[i] = false; b->owner = OWNER_POOL;
    b->next = p->free_head; p->free_head = b; p->nfree++;
    return true;
}

/* 연습 4: 소유권 태그. 블록을 만질 때마다 "내 것인가" 를 확인한다 (디버그 빌드의 assert 자리). */
static unsigned g_violations;
static void blk_touch(blk_t *b, int me) { if (b->owner != me) g_violations++; }

/* 연습 5: 풀 = 빈 블록 포인터의 큐. alloc = receive(0), free = send(0). */
static kqueue_t g_freeq;
static blk_t *g_freeq_buf[POOL_N];
static void fq_init(pool_t *p) {
    kq_init(&g_freeq, g_freeq_buf, POOL_N, sizeof(blk_t *));
    for (unsigned i = 0; i < POOL_N; i++) { blk_t *b = &p->blocks[i]; b->owner = OWNER_POOL; kq_send(&g_freeq, &b, -1, 0); }
}
static blk_t *fq_alloc(int self) { blk_t *b; return kq_receive(&g_freeq, &b, self, 0) == Q_OK ? b : NULL; }
static bool fq_free(blk_t *b, int self) { b->owner = OWNER_POOL; return kq_send(&g_freeq, &b, self, 0) == Q_OK; }

typedef struct { unsigned produced, sent, drop_nomem, drop_full, consumed, bad; } cres_t;
static pool_t g_pool;
static kqueue_t g_qptr;
static blk_t *g_qptr_buf[8];
static cres_t CR;
static int g_cp, g_cc, g_cn, g_cperiod, g_ccost;
static bool g_bug, g_use_fq;
static struct { int release; uint32_t seq; bool post; blk_t *last; } CP;
static struct { int phase, done; blk_t *b; op_t op; } CC;
enum { PH_RECV, PH_WORK };
static uint8_t pat(uint32_t seq, unsigned i) { return (uint8_t)(seq * 31u + i); }
static blk_t *p_alloc(int self) { return g_use_fq ? fq_alloc(self) : pool_alloc(&g_pool); }
static void p_free(blk_t *b, int self) { if (g_use_fq) fq_free(b, self); else pool_free(&g_pool, b); }
static unsigned p_nfree(void) { return g_use_fq ? g_freeq.count : g_pool.nfree; }

static void cprod_step(int id) {
    if (CP.post) {                                        /* send 직후의 "다음 줄" — 더 높은 소비자가 있으면 그게 먼저 돌았다 */
        blk_touch(CP.last, id);                           /* 버그: 보낸 뒤에 쓴다 */
        CP.last->data[0] ^= 0xFF;
        CP.post = false;
    } else {
        blk_t *b = p_alloc(id);
        if (b == NULL) CR.drop_nomem++;
        else {
            b->owner = id;                                /* 빌린 순간 내 것 */
            b->seq = CP.seq;
            for (unsigned i = 0; i < BLK_SIZE; i++) b->data[i] = pat(CP.seq, i);
            b->owner = g_cc;                              /* send 직전에 소유권을 넘긴다고 표시 */
            if (kq_send(&g_qptr, &b, id, 0) == Q_OK) {
                CR.sent++;
                /* 모델의 한 걸음은 쪼개지지 않는다. FreeRTOS 는 send 안에서 즉시 선점하므로(더 높은 소비자를 깨우면)
                 * 버그 줄을 다음 걸음으로 미뤄 같은 순서를 만든다. */
                if (g_bug) { CP.post = true; CP.last = b; return; }
            } else {
                b->owner = id;                            /* 안 넘어갔다 → 다시 내 것, 내가 반납 */
                p_free(b, id);
                CR.drop_full++;
            }
        }
    }
    CR.produced++;
    CP.seq++; CP.release += g_cperiod;
    if ((int)CP.seq >= g_cn) task_block(id, NULL, DIR_NONE, FOREVER);
    else task_sleep_until(id, CP.release);
}
static void ccons_step(int id) {
    for (;;) {
        if (CC.phase == PH_RECV) {
            if (op_recv(id, &g_qptr, &CC.b, FOREVER, &CC.op) != Q_OK) return;
            CC.phase = PH_WORK; CC.done = 0;
        }
        if (CC.done < g_ccost) { CC.done++; return; }
        blk_touch(CC.b, id);
        bool ok = true;
        for (unsigned i = 0; i < BLK_SIZE; i++) if (CC.b->data[i] != pat(CC.b->seq, i)) ok = false;
        if (!ok) CR.bad++;
        CR.consumed++;
        p_free(CC.b, id);
        CC.b = NULL; CC.phase = PH_RECV;
    }
}
static void cpipe_step(int id) { if (id == g_cp) cprod_step(id); else ccons_step(id); }
static cres_t run_ptr(int n, int period, int cost, unsigned qdepth, int pprio, int cprio, bool bug, bool use_fq) {
    kernel_reset();
    memset(&CR, 0, sizeof CR); memset(&CP, 0, sizeof CP); memset(&CC, 0, sizeof CC);
    g_violations = 0; g_bug = bug; g_use_fq = use_fq;
    pool_init(&g_pool);
    if (use_fq) fq_init(&g_pool);
    kq_init(&g_qptr, g_qptr_buf, qdepth, sizeof(blk_t *));
    g_cn = n; g_cperiod = period; g_ccost = cost;
    g_cp = task_add("prod", pprio); g_cc = task_add("cons", cprio);
    sim_run(n * period + 10 * cost, cpipe_step, NULL);
    return CR;
}

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main(void) {
    printf("[연습 1] drop-oldest vs drop-newest\n");
    uint32_t st[3], v, scratch, out[3];
    kqueue_t q;
    kernel_reset();
    int me = task_add("me", 1);
    unsigned dropped = 0, full = 0;
    kq_init(&q, st, 3, sizeof(uint32_t));
    for (v = 1; v <= 6; v++) if (kq_send(&q, &v, me, 0) == Q_FULL) full++;          /* drop-newest */
    for (int i = 0; i < 3; i++) kq_receive(&q, &out[i], me, 0);
    CHECK(full == 3u && out[0] == 1u && out[1] == 2u && out[2] == 3u);               /* 오래된 1,2,3 이 남는다 */
    for (v = 1; v <= 6; v++) kq_send_drop_oldest(&q, &v, &scratch, &dropped, me);   /* drop-oldest */
    for (int i = 0; i < 3; i++) kq_receive(&q, &out[i], me, 0);
    CHECK(dropped == 3u && out[0] == 4u && out[1] == 5u && out[2] == 6u);            /* 최신 4,5,6 이 남는다 */
    kqueue_t one; uint32_t one_s[1], a1, a2;
    kq_init(&one, one_s, 1, sizeof(uint32_t));
    dropped = 0;
    for (v = 1; v <= 5; v++) kq_send_drop_oldest(&one, &v, &scratch, &dropped, me);
    kq_receive(&one, &a1, me, 0);
    for (v = 1; v <= 5; v++) kq_overwrite(&one, &v);
    kq_receive(&one, &a2, me, 0);
    CHECK(a1 == 5u && a2 == 5u && dropped == 4u && one.overwritten == 4u);          /* 깊이 1 이면 overwrite 와 같다 */

    printf("[연습 2] 버스트 흡수 최소 깊이: B - ceil((B-1)/C)\n");
    bool all_ok = true;
    for (unsigned B = 1; B <= 20; B++)
        for (unsigned C = 1; C <= 6; C++) {
            unsigned d = 1;
            while (burst_drops(B, C, d) != 0) d++;                                   /* 시뮬레이션으로 찾은 최소 깊이 */
            if (d != min_depth_formula(B, C)) all_ok = false;
        }
    CHECK(all_ok);
    CHECK(min_depth_formula(10, 3) == 7u && burst_drops(10, 3, 7) == 0u && burst_drops(10, 3, 6) == 1u);
    CHECK(min_depth_formula(16, 1) == 1u && min_depth_formula(16, 16) == 15u);       /* C=1: 들어오는 족족 / C≥B: 거의 다 쌓임 */

    printf("[연습 3] 우편함: overwrite + peek (읽는 쪽 여럿)\n");
    kqueue_t mb; uint32_t mbs[1], ra = 0, rb = 0;
    kq_init(&mb, mbs, 1, sizeof(uint32_t));
    CHECK(kq_peek(&mb, &ra) == Q_EMPTY);
    v = 5; kq_overwrite(&mb, &v);
    CHECK(kq_peek(&mb, &ra) == Q_OK && kq_peek(&mb, &rb) == Q_OK && ra == 5u && rb == 5u && mb.count == 1u);   /* 둘 다 같은 최신 값 */
    v = 6; kq_overwrite(&mb, &v);
    CHECK(kq_peek(&mb, &ra) == Q_OK && ra == 6u && mb.count == 1u);
    CHECK(kq_receive(&mb, &rb, me, 0) == Q_OK && rb == 6u && kq_peek(&mb, &ra) == Q_EMPTY);  /* receive 는 지운다 */

    printf("[연습 4] use-after-send: 보이느냐는 우선순위, 잡느냐는 소유권 태그\n");
    cres_t c = run_ptr(6, 3, 2, 2, 3, 1, true, false);                            /* 소비자가 낮다 */
    CHECK(c.sent == 6u && c.bad == 6u && g_violations == 6u);                     /* 깨진 데이터가 보인다 */
    c = run_ptr(6, 3, 2, 2, 1, 3, true, false);                                   /* 소비자가 높다 */
    CHECK(c.sent == 6u && c.bad == 0u);                                           /* 데이터는 멀쩡해 보인다 ... */
    CHECK(g_violations == 6u);                                                    /* ... 그래도 태그는 6번 다 잡는다 */
    c = run_ptr(6, 3, 2, 2, 3, 1, false, false);
    CHECK(c.bad == 0u && g_violations == 0u && g_pool.nfree == POOL_N);           /* 버그 없으면 위반 0 */

    printf("[연습 5] free-queue 풀 = 연결 리스트 풀과 같은 결과\n");
    cres_t l4 = run_ptr(10, 3, 7, 4, 3, 1, false, false), f4 = run_ptr(10, 3, 7, 4, 3, 1, false, true);
    CHECK(f4.sent == l4.sent && f4.drop_nomem == l4.drop_nomem && f4.sent == 7u && f4.drop_nomem == 3u);
    CHECK(p_nfree() == POOL_N && f4.bad == 0u && g_violations == 0u);              /* 블록 4개 다 돌아왔다 */
    cres_t l1 = run_ptr(10, 3, 7, 1, 3, 1, false, false), f1 = run_ptr(10, 3, 7, 1, 3, 1, false, true);
    CHECK(f1.sent == l1.sent && f1.drop_full == l1.drop_full && f1.drop_full == 5u && p_nfree() == POOL_N);
    CHECK(g_freeq.item_size == sizeof(blk_t *) && g_freeq.len == POOL_N);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
