/* RTOS-B04 미니 커널 1: 우선순위 비트맵 ready list (CLZ)
 *
 *   Part A  my_clz32 (이진 탐색, 내장 함수 없이) · bm_highest = 31 - clz · 우선순위별 FIFO (intrusive 이중 연결 리스트)
 *           rq_push_tail / rq_remove (중간 삭제 O(1), 리스트가 비면 비트 끄기) / rq_pop_highest
 *   Part B  CLZ 를 __builtin_clz 와 대조 (0, 단일 비트 32개, 두 비트 496개, 하위 채움 32개, 무작위 1,000,000개)
 *           ready list 무작위 200,000 연산을 독립 참조 모델(배열 + 선형 탐색)과 대조 + 매 연산 비트맵 불변식
 *   Part C  ucontext 미니 커널 (협력형, 선점 없음 → RTOS-B05): sched_pick_next() = rq_pop_highest,
 *           task_yield() = 자기를 같은 우선순위 꼬리에 다시 넣고 스케줄러로 전환.
 *           C1 같은 우선순위 라운드 로빈 / C2 BLE 센서 노드 (ISR 이 RADIO·IMU 를 깨움) — 타임라인 + 독립 오라클
 *
 * 우선순위: 0..31, 숫자가 클수록 급하다 (FreeRTOS 방향. Zephyr / µC/OS 는 반대 — 작은 숫자가 급함).
 * macOS: <ucontext.h> 는 _XOPEN_SOURCE 를 정의해야 선언되고, 함수들이 deprecated 로 표시돼 있다 →
 *        _XOPEN_SOURCE 600 + 커널 부분에서만 -Wdeprecated-declarations 를 끈다 (push/pop).
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o st && ./st        (solution.c 도 같은 명령)
 * 틀린 코드도 멈추지 않는다: 커널은 tick 상한에서 끝나고, READY 가 아닌 태스크는 실행하지 않는다.
 */
#define _XOPEN_SOURCE 600
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <ucontext.h>

#define NPRIO     32
#define MAX_TASKS 8
#define STACK_SZ  (64 * 1024)
#define TL_LEN    64

typedef enum { TASK_READY, TASK_RUNNING, TASK_BLOCKED, TASK_SUSPENDED, TASK_DELETED } task_state_t;

typedef struct tcb {
    const char  *name;
    uint8_t      prio;            /* 0..31, 클수록 급함 */
    task_state_t state;
    uint32_t     wake_tick;       /* RTOS-B06 에서 씀 (여기선 미사용) */
    void        *wait_obj;        /* RTOS-B08~ 에서 씀 (여기선 미사용) */
    struct tcb  *next, *prev;     /* ready list 링크 — rq_* (학생 코드) 가 관리 */
    bool         in_rq;           /* ready list 안에 있나 (FreeRTOS: xStateListItem.pxContainer != NULL) */
    /* ---- 커널 / 하네스 전용 ---- */
    char         tag;             /* 트레이스에 찍을 한 글자 */
    ucontext_t   ctx;
    void       (*entry)(void);
    unsigned     ready_seq;       /* READY 가 된 순번 — 오라클의 FIFO 판정용 (rq_* 는 쓰지 않는다) */
    int          slices;
    char         tl[TL_LEN + 1];
} tcb_t;

typedef struct {
    uint32_t bitmap;              /* 불변식: bit p == 1  ⇔  head[p] != NULL */
    tcb_t   *head[NPRIO];         /* 우선순위 p 의 맨 앞 (다음에 나갈 태스크) */
    tcb_t   *tail[NPRIO];         /* 우선순위 p 의 맨 뒤 (방금 들어온 태스크) */
    unsigned count;               /* ready list 안의 태스크 총수 */
} readyq_t;

/* ============================ Part A — 학생 코드 ============================ */

/* TODO A1: x 의 MSB 쪽 연속된 0 비트 개수. x == 0 이면 32. 내장 함수(__builtin_clz) 금지 */
int my_clz32(uint32_t x)
{
    (void)x;
    return 0;                             /* TODO */
}

/* TODO A2: 켜진 비트 중 가장 높은 번호 (= 가장 급한 ready 우선순위). bm == 0 이면 -1 */
int bm_highest(uint32_t bm)
{
    (void)bm;
    return -1;                            /* TODO */
}

void rq_init(readyq_t *q) { memset(q, 0, sizeof *q); }

/* TODO A3: t 를 우선순위 t->prio 리스트의 꼬리에 붙이고 비트를 켠다. O(1).
 *          이미 들어 있거나(in_rq) prio >= NPRIO 이면 아무것도 하지 않고 false */
bool rq_push_tail(readyq_t *q, tcb_t *t)
{
    (void)q; (void)t;
    return false;                         /* TODO */
}

/* TODO A4: t 를 (머리·중간·꼬리 어디서든) 빼고, 그 우선순위 리스트가 비면 비트를 끈다. O(1).
 *          리스트에 없으면 false */
bool rq_remove(readyq_t *q, tcb_t *t)
{
    (void)q; (void)t;
    return false;                         /* TODO */
}

/* TODO A5: 가장 높은 우선순위 리스트의 맨 앞을 꺼낸다. 비었으면 NULL. O(1) — 루프 없이 */
tcb_t *rq_pop_highest(readyq_t *q)
{
    (void)q;
    return NULL;                          /* TODO */
}

/* ============================ 테스트 하네스 ============================ */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t g_rng = 0x2545F491u;
static uint32_t xorshift32(void)
{
    uint32_t x = g_rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return g_rng = x;
}

/* 비트맵 불변식 전수 검사: 0 이면 OK */
static int rq_check(const readyq_t *q)
{
    int bad = 0; unsigned total = 0;
    for (int p = 0; p < NPRIO; p++) {
        bool bit = (q->bitmap >> p) & 1u;
        if (bit != (q->head[p] != NULL)) bad++;
        if ((q->head[p] == NULL) != (q->tail[p] == NULL)) bad++;
        const tcb_t *prev = NULL;
        for (const tcb_t *t = q->head[p]; t; t = t->next) {
            if (t->prev != prev || t->prio != p || !t->in_rq) { bad++; break; }
            prev = t; total++;
            if (total > 10000) { bad++; break; }      /* 고리(cycle) 방어 */
        }
        if (prev != q->tail[p]) bad++;
    }
    if (total != q->count) bad++;
    return bad;
}

static void mk(tcb_t *t, const char *name, uint8_t prio)
{
    memset(t, 0, sizeof *t);
    t->name = name; t->prio = prio; t->tag = name[0];
}

static void test_part_a(void)
{
    puts("== Part A: CLZ, 비트맵, 우선순위별 FIFO ==");
    CHECK(my_clz32(0) == 32 && my_clz32(1) == 31 && my_clz32(0x80000000u) == 0);
    CHECK(my_clz32(0x00010000u) == 15 && my_clz32(0x0000FFFFu) == 16 && my_clz32(0xFFFFFFFFu) == 0);
    CHECK(bm_highest(0) == -1 && bm_highest(1) == 0 && bm_highest(0x80000000u) == 31);
    CHECK(bm_highest(0x00000A50u) == 11 && bm_highest((1u << 5) | (1u << 17)) == 17);

    readyq_t q; rq_init(&q);
    tcb_t a, b, c, d, e;
    mk(&a, "a", 5); mk(&b, "b", 5); mk(&c, "c", 5); mk(&d, "d", 9); mk(&e, "e", 0);
    CHECK(rq_push_tail(&q, &a) && rq_push_tail(&q, &b) && rq_push_tail(&q, &c) &&
          rq_push_tail(&q, &d) && rq_push_tail(&q, &e));
    CHECK(q.bitmap == ((1u << 9) | (1u << 5) | 1u) && q.count == 5 && rq_check(&q) == 0);
    char order[8] = {0}; int k = 0;
    for (tcb_t *t; (t = rq_pop_highest(&q)) != NULL && k < 7; ) order[k++] = t->tag;
    printf("pop order (a,b,c=p5  d=p9  e=p0): %s\n", order);
    CHECK(strcmp(order, "dabce") == 0);                  /* 높은 것 먼저, 같은 p5 는 넣은 순 */
    CHECK(q.bitmap == 0 && q.count == 0 && rq_pop_highest(&q) == NULL);

    /* 중간 삭제: p7 에 x y z, p3 에 w */
    tcb_t x, y, z, w;
    mk(&x, "x", 7); mk(&y, "y", 7); mk(&z, "z", 7); mk(&w, "w", 3);
    rq_push_tail(&q, &x); rq_push_tail(&q, &y); rq_push_tail(&q, &z); rq_push_tail(&q, &w);
    CHECK(rq_remove(&q, &y) && !y.in_rq && y.next == NULL && y.prev == NULL);
    CHECK(x.next == &z && z.prev == &x && (q.bitmap & (1u << 7)) && bm_highest(q.bitmap) == 7);
    CHECK(rq_remove(&q, &x) && q.head[7] == &z && q.tail[7] == &z && rq_check(&q) == 0);   /* 머리 삭제 */
    CHECK(rq_remove(&q, &z) && !(q.bitmap & (1u << 7)) && bm_highest(q.bitmap) == 3);     /* 마지막 → 비트 끔 */
    CHECK(!rq_remove(&q, &z) && !rq_push_tail(&q, &w) && q.count == 1);  /* 없는 것 삭제 / 이중 삽입 거부 */
    tcb_t hi, bad;
    mk(&hi, "H", 31); mk(&bad, "!", 32);
    CHECK(rq_push_tail(&q, &hi) && q.bitmap == ((1u << 31) | (1u << 3)) && rq_pop_highest(&q) == &hi);
    CHECK(!rq_push_tail(&q, &bad) && rq_check(&q) == 0 && rq_pop_highest(&q) == &w);
}

/* ---------------- Part B: 대조 테스트 ---------------- */
#define NREF 64
static int ref_list[NPRIO][NREF], ref_len[NPRIO];

static void ref_push(int p, int id) { ref_list[p][ref_len[p]++] = id; }
static void ref_remove(int p, int id)
{
    int i = 0;
    while (i < ref_len[p] && ref_list[p][i] != id) i++;
    for (; i + 1 < ref_len[p]; i++) ref_list[p][i] = ref_list[p][i + 1];
    ref_len[p]--;
}
static int ref_pop(void)                 /* 선형 탐색: 31 → 0 */
{
    for (int p = NPRIO - 1; p >= 0; p--)
        if (ref_len[p]) { int id = ref_list[p][0]; ref_remove(p, id); return id; }
    return -1;
}

static void test_part_b(void)
{
    puts("== Part B: __builtin_clz 대조 + 무작위 ready list 대조 ==");
    int mis = 0, cases = 0;
    for (int i = 0; i < 32; i++, cases++)                       /* 단일 비트 32개 */
        if (my_clz32(1u << i) != __builtin_clz(1u << i)) mis++;
    CHECK(mis == 0 && cases == 32);
    mis = cases = 0;
    for (int i = 0; i < 32; i++)                                 /* 두 비트 C(32,2) = 496개 */
        for (int j = 0; j < i; j++, cases++) {
            uint32_t v = (1u << i) | (1u << j);
            if (my_clz32(v) != __builtin_clz(v) || bm_highest(v) != i) mis++;
        }
    CHECK(mis == 0 && cases == 496);
    mis = 0;
    for (int k = 1; k <= 32; k++) {                              /* 하위 k 비트 채움 */
        uint32_t v = (k == 32) ? 0xFFFFFFFFu : ((1u << k) - 1u);
        if (my_clz32(v) != __builtin_clz(v) || bm_highest(v) != k - 1) mis++;
    }
    CHECK(mis == 0);
    mis = 0;
    for (int n = 0; n < 1000000; n++) {                          /* 무작위: 크기도 고르게 (>> 0..31) */
        uint32_t v = xorshift32() >> (xorshift32() & 31u);
        if (v == 0) continue;                                    /* __builtin_clz(0) 은 UB */
        if (my_clz32(v) != __builtin_clz(v)) mis++;
    }
    printf("clz vs __builtin_clz: single 32 + double 496 + low-mask 32 + random 1000000 -> mismatches %d\n", mis);
    CHECK(mis == 0);

    /* ready list 무작위 200,000 연산 vs 참조 모델 */
    static tcb_t pool[NREF];
    readyq_t q; rq_init(&q);
    memset(ref_len, 0, sizeof ref_len);
    for (int i = 0; i < NREF; i++) { mk(&pool[i], "t", 0); pool[i].ready_seq = (unsigned)i; }
    long ops = 0, pops = 0, mids = 0, pop_mis = 0, inv_bad = 0, ret_bad = 0;
    for (int n = 0; n < 200000; n++, ops++) {
        uint32_t r = xorshift32();
        int id = (int)((r >> 8) % NREF);
        tcb_t *t = &pool[id];
        switch (r % 3) {
        case 0:                                                   /* push (없을 때만 성공해야) */
            if (!t->in_rq) {
                uint32_t r2 = xorshift32();               /* 절반은 p0..3 에 몰아 긴 리스트를 만든다 */
                t->prio = (uint8_t)((r2 & 1u) ? (r2 >> 1) % NPRIO : (r2 >> 1) % 4u);
                if (!rq_push_tail(&q, t)) ret_bad++;
                ref_push(t->prio, id);
            } else if (rq_push_tail(&q, t)) ret_bad++;
            break;
        case 1:                                                   /* 아무 위치에서나 삭제 */
            if (t->in_rq) {
                if (q.head[t->prio] != t && q.tail[t->prio] != t) mids++;
                int p = t->prio;
                if (!rq_remove(&q, t)) ret_bad++;
                ref_remove(p, id);
            } else if (rq_remove(&q, t)) ret_bad++;
            break;
        default: {                                                /* pop highest */
            tcb_t *got = rq_pop_highest(&q);
            int want = ref_pop();
            if ((got ? (int)(got - pool) : -1) != want) pop_mis++;
            pops++;
        } }
        if ((n & 63) == 0 && rq_check(&q) != 0) inv_bad++;        /* 전수 검사는 64 연산마다 */
        int rp = -1;                                              /* 매 연산: 비트맵 최고 == 참조 최고 */
        for (int p = NPRIO - 1; p >= 0; p--) if (ref_len[p]) { rp = p; break; }
        if (bm_highest(q.bitmap) != rp) inv_bad++;
    }
    printf("ready list random: ops=%ld pops=%ld middle-removes=%ld pop-mismatch=%ld invariant-bad=%ld ret-bad=%ld\n",
           ops, pops, mids, pop_mis, inv_bad, ret_bad);
    CHECK(pop_mis == 0 && pops > 60000);
    CHECK(inv_bad == 0 && ret_bad == 0 && mids > 500);
    int order_bad = 0;                                            /* 끝 상태: 리스트 순서 == 참조 순서 */
    for (int p = 0; p < NPRIO; p++) {
        int i = 0;
        for (tcb_t *t = q.head[p]; t && i <= NREF; t = t->next, i++)   /* 고리 방어 */
            if (i >= ref_len[p] || ref_list[p][i] != (int)(t - pool)) order_bad++;
        if (i != ref_len[p]) order_bad++;
    }
    CHECK(order_bad == 0 && rq_check(&q) == 0);
}

/* ============================ Part C — ucontext 미니 커널 ============================ */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"   /* macOS: ucontext 함수는 deprecated 표시 */

static readyq_t   g_rq;
static tcb_t      g_tcb[MAX_TASKS];
static int        g_ntask;
static tcb_t     *g_cur;              /* 지금 slice 를 도는 태스크 (스케줄러 문맥에서는 NULL) */
static ucontext_t g_sched_ctx;        /* 스케줄러(= main) 문맥 */
static _Alignas(16) unsigned char g_stack[MAX_TASKS][STACK_SZ];
static unsigned   g_seq;
static int        g_order_err, g_isr_lost, g_idle_ticks, g_not_ready_pick;
static char       g_trace[TL_LEN + 1];          /* tick 마다 실행된 태스크 tag, idle = '.' */
static uint32_t   g_bm_at_pick[TL_LEN];         /* 고르기 직전 비트맵 */
static void     (*g_isr_hook)(int now);

static void k_reset(void)
{
    memset(g_tcb, 0, sizeof g_tcb);
    rq_init(&g_rq);
    g_ntask = 0; g_cur = NULL; g_seq = 0;
    g_order_err = g_isr_lost = g_idle_ticks = g_not_ready_pick = 0;
    memset(g_trace, 0, sizeof g_trace);
    memset(g_bm_at_pick, 0, sizeof g_bm_at_pick);
    g_isr_hook = NULL;
}

static void k_make_ready(tcb_t *t)        /* 커널 서비스: READY 로 만들고 ready list 꼬리에 */
{
    t->state = TASK_READY;
    t->ready_seq = ++g_seq;
    rq_push_tail(&g_rq, t);
}

static void k_switch_to_sched(void) { swapcontext(&g_cur->ctx, &g_sched_ctx); }

static void task_trampoline(void)         /* makecontext 진입점: entry 가 return 하면 DELETED */
{
    g_cur->entry();
    g_cur->state = TASK_DELETED;          /* 이 함수가 끝나면 uc_link(= g_sched_ctx) 로 돌아간다 */
}

static tcb_t *task_create(const char *name, uint8_t prio, void (*entry)(void))
{
    tcb_t *t = &g_tcb[g_ntask];
    t->name = name; t->tag = name[0]; t->prio = prio; t->entry = entry;
    getcontext(&t->ctx);
    t->ctx.uc_stack.ss_sp   = g_stack[g_ntask];
    t->ctx.uc_stack.ss_size = STACK_SZ;
    t->ctx.uc_link          = &g_sched_ctx;
    makecontext(&t->ctx, task_trampoline, 0);
    g_ntask++;
    k_make_ready(t);
    return t;
}

static void task_block(void)              /* 이벤트를 기다리며 잠든다 (ISR 이 task_unblock) */
{
    g_cur->state = TASK_BLOCKED;
    k_switch_to_sched();
}

static void task_unblock(tcb_t *t)        /* ISR 문맥에서 부른다. 잠들어 있지 않으면 이벤트 유실 */
{
    if (t->state == TASK_BLOCKED) k_make_ready(t);
    else g_isr_lost++;
}

/* TODO C1: 다음에 돌릴 태스크를 고른다 — 가장 높은 우선순위, 같으면 먼저 READY 된 태스크. O(1) */
static tcb_t *sched_pick_next(void)
{
    return NULL;                          /* TODO */
}

/* TODO C2: 협력형 양보 — 자기를 READY 로 (같은 우선순위 꼬리에) 되돌리고 스케줄러로 전환 */
static void task_yield(void)
{
    /* TODO */
}

/* 오라클: 태스크 표 전체를 훑어 "READY 중 최고 우선순위, 같으면 ready_seq 최소" (학생 코드와 독립) */
static tcb_t *oracle_pick(void)
{
    tcb_t *best = NULL;
    for (int i = 0; i < g_ntask; i++) {
        tcb_t *t = &g_tcb[i];
        if (t->state != TASK_READY) continue;
        if (!best || t->prio > best->prio || (t->prio == best->prio && t->ready_seq < best->ready_seq))
            best = t;
    }
    return best;
}

static bool k_all_deleted(void)
{
    for (int i = 0; i < g_ntask; i++) if (g_tcb[i].state != TASK_DELETED) return false;
    return true;
}

/* tick 하나 = ① ISR 훅 → ② sched_pick_next → ③ 그 태스크가 yield/block/return 할 때까지 1 slice */
static int kernel_run(int limit)
{
    int tick;
    for (tick = 0; tick < limit && tick < TL_LEN; tick++) {
        if (g_isr_hook) g_isr_hook(tick);
        if (k_all_deleted()) break;
        g_bm_at_pick[tick] = g_rq.bitmap;
        tcb_t *want = oracle_pick();
        tcb_t *t = sched_pick_next();
        if (t != want) g_order_err++;
        for (int i = 0; i < g_ntask; i++) {
            tcb_t *u = &g_tcb[i];
            char c = u->state == TASK_READY ? 'r' : u->state == TASK_BLOCKED ? 'B' :
                     u->state == TASK_SUSPENDED ? 'S' : '.';
            u->tl[tick] = (u == t) ? 'R' : c;
        }
        if (!t) { g_idle_ticks++; g_trace[tick] = '.'; continue; }
        if (t->state != TASK_READY) { g_not_ready_pick++; g_trace[tick] = '?'; continue; }  /* 실행 거부 */
        g_trace[tick] = t->tag;
        t->state = TASK_RUNNING; t->slices++;
        g_cur = t;
        swapcontext(&g_sched_ctx, &t->ctx);     /* 태스크가 yield/block/return 하면 여기로 */
        g_cur = NULL;
    }
    return tick;
}

static void print_timeline(int end)
{
    printf("tick :");
    for (int k = 0; k < end; k++) printf(" %d", k % 10);
    printf("\n");
    for (int i = 0; i < g_ntask; i++) {
        printf("%-5.5s:", g_tcb[i].name);
        for (int k = 0; k < end; k++) printf(" %c", g_tcb[i].tl[k] ? g_tcb[i].tl[k] : '.');
        printf("\n");
    }
}

/* ---- C1: 같은 우선순위 라운드 로빈 + 더 높은 태스크의 yield ---- */
static char g_applog[128];
static int  g_applen;
static void applog(char c) { if (g_applen < 127) g_applog[g_applen++] = c; }

static void rr_task(void)                 /* 3번 일하고 매번 양보, 그다음 return */
{
    for (int i = 0; i < 3; i++) { applog(g_cur->tag); task_yield(); }
}

/* ---- C2: nRF52 BLE 센서 노드 ---- */
static tcb_t *g_radio, *g_imu;
static int g_tx, g_samples, g_ctrl_steps;

static void radio_task(void)              /* p6: BLE connection event 마다 패킷 1개 송신 */
{
    for (int n = 0; n < 2; n++) { task_block(); g_tx++; applog('R'); }
}
static void imu_task(void)                /* p4: DRDY 마다 FIFO 읽기 → 양보 → 필터 */
{
    for (int n = 0; n < 3; n++) {
        task_block();
        applog('i'); task_yield();        /* FIFO 읽기 (I2C) */
        g_samples++; applog('I');         /* 필터 */
    }
}
static void ctrl_task(void)               /* p4: 제어 루프 3 스텝 */
{
    for (int n = 0; n < 3; n++) { g_ctrl_steps++; applog('C'); task_yield(); }
}
static void log_task(void)  { for (int n = 0; n < 3; n++) { applog('L'); task_yield(); } }   /* p2 */
static void blink_task(void){ for (int n = 0; n < 3; n++) { applog('B'); task_yield(); } }   /* p2 */

static void ble_isr(int now)              /* 하네스의 "하드웨어": 정해진 tick 에 ISR 이 태스크를 깨운다 */
{
    if (now == 2 || now == 7 || now == 12) task_unblock(g_imu);     /* IMU DRDY (EXTI) */
    if (now == 7 || now == 22) task_unblock(g_radio);               /* RADIO 이벤트 — 7 에선 IMU 다음에 깨움 */
}

static void test_part_c(void)
{
    puts("== Part C1: 같은 우선순위 라운드 로빈 (A,B,C = p3) + H(p5) ==");
    k_reset(); g_applen = 0; memset(g_applog, 0, sizeof g_applog);
    task_create("A", 3, rr_task); task_create("B", 3, rr_task); task_create("C", 3, rr_task);
    task_create("H", 5, rr_task);
    int end = kernel_run(40);
    printf("trace: %s  (end tick %d)\n", g_trace, end);
    print_timeline(end);
    CHECK(strcmp(g_trace, "HHHHABCABCABCABC") == 0);   /* H 는 양보해도 혼자 최고 → 4 slice 연속 */
    CHECK(g_order_err == 0 && g_not_ready_pick == 0 && k_all_deleted() && strcmp(g_applog, "HHHABCABCABC") == 0);

    puts("== Part C2: BLE 센서 노드 (RADIO p6, IMU p4, CTRL p4, LOG p2, BLINK p2) ==");
    k_reset(); g_applen = 0; memset(g_applog, 0, sizeof g_applog);
    g_tx = g_samples = g_ctrl_steps = 0;
    g_radio = task_create("RADIO", 6, radio_task);
    g_imu   = task_create("IMU",   4, imu_task);
    task_create("CTRL",  4, ctrl_task);
    task_create("LOG",   2, log_task);
    task_create("BLINK", 2, blink_task);
    g_isr_hook = ble_isr;
    end = kernel_run(40);
    printf("trace: %s  (end tick %d, idle ticks %d)\n", g_trace, end, g_idle_ticks);
    print_timeline(end);
    printf("bitmap@pick:");
    for (int k = 0; k < end; k++) printf(" %02X", (unsigned)g_bm_at_pick[k]);
    printf("\napp log: %s  tx=%d samples=%d ctrl=%d isr_lost=%d order_err=%d\n",
           g_applog, g_tx, g_samples, g_ctrl_steps, g_isr_lost, g_order_err);
    CHECK(strcmp(g_trace, "RICICICRCIILIIBLBLBLB.R") == 0);
    CHECK(g_order_err == 0 && g_not_ready_pick == 0 && g_isr_lost == 0);
    CHECK(k_all_deleted() && g_tx == 2 && g_samples == 3 && g_ctrl_steps == 3 && g_idle_ticks == 1);
    CHECK(g_bm_at_pick[0] == 0x54 && g_bm_at_pick[7] == 0x54 && g_bm_at_pick[21] == 0);  /* p6|p4|p2, idle */
}
#pragma clang diagnostic pop

int main(void)
{
    test_part_a();
    test_part_b();
    test_part_c();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
