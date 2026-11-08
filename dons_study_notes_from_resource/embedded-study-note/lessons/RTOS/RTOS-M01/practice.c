/* RTOS-M01 추가 연습 — 한 번에 검증
 *
 * 연습 1  타임아웃 뒤 늦게 온 TX 완료 알림 (stale notification) 과 xTaskNotifyStateClear / ulTaskNotifyValueClear
 * 연습 2  RAM 손익 계산기 (Cortex-M4F 실측 크기 + heap_4 블록 규칙)
 * 연습 3  "최신 값 유지 + 버린 개수 세기" 메일박스
 * 연습 4  wait-all 을 알림으로 흉내 낼 때의 전체 타임아웃 (tick wrap 안전)
 * 모델(Part A)은 solution.c 와 같다.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define NT_INDEXES   2             /* configTASK_NOTIFICATION_ARRAY_ENTRIES */
#define NT_MAX_DELAY 0xFFFFFFFFu   /* portMAX_DELAY */

typedef enum { eNoAction = 0, eSetBits, eIncrement, eSetValueWithOverwrite, eSetValueWithoutOverwrite } nt_action_t;
enum { NT_NOT_WAITING = 0, NT_WAITING = 1, NT_RECEIVED = 2 };   /* tasks.c 113–115 */
enum { NT_FAIL = 0, NT_PASS = 1 };                               /* pdFAIL / pdPASS (= pdFALSE / pdTRUE) */

typedef struct {
    const char *name;
    int         prio;                    /* uxPriority (클수록 급함) */
    uint32_t    value[NT_INDEXES];       /* ulNotifiedValue[] */
    uint8_t     state[NT_INDEXES];       /* ucNotifyState[]   */
    int         blocked_on;              /* 알림을 기다리며 Blocked 인 인덱스, 아니면 -1 */
    unsigned    wakes;                   /* 블록 상태에서 깨어난 횟수 (= 컨텍스트 스위치 비용의 대리 지표) */
} ntask_t;

static void nt_init(ntask_t *t, const char *name, int prio) {
    memset(t, 0, sizeof *t);
    t->name = name;
    t->prio = prio;
    t->blocked_on = -1;
}

/* ================= Part A: 알림 커널 모델 ================= */

/* xTaskGenericNotify (tasks.c 8013–8126).
 * *prev(있으면) = 갱신 전 값. 상태를 RECEIVED 로 만들고 eAction 대로 값을 바꾼다.
 * eSetValueWithoutOverwrite 는 "읽지 않은 알림" 이 있으면 값을 안 바꾸고 NT_FAIL. 나머지는 항상 NT_PASS.
 * 이 인덱스를 기다리며 Blocked 였으면(원래 상태 WAITING) 깨운다: blocked_on = -1, wakes++. */
static int nt_notify(ntask_t *t, unsigned idx, uint32_t v, nt_action_t a, uint32_t *prev) {
    int ret = NT_PASS;
    if (prev) *prev = t->value[idx];
    uint8_t orig = t->state[idx];
    t->state[idx] = NT_RECEIVED;
    switch (a) {
    case eSetBits:               t->value[idx] |= v; break;
    case eIncrement:             t->value[idx]++; break;                  /* 상한 없음: 0xFFFFFFFF + 1 = 0 */
    case eSetValueWithOverwrite: t->value[idx] = v; break;
    case eSetValueWithoutOverwrite:
        if (orig != NT_RECEIVED) t->value[idx] = v;
        else ret = NT_FAIL;
        break;
    case eNoAction:
    default:                     break;
    }
    if (orig == NT_WAITING) {                                             /* tasks.c 8088 */
        t->blocked_on = -1;
        t->wakes++;
    }
    return ret;
}

/* xTaskGenericNotifyFromISR (tasks.c 8133–8308): nt_notify 와 같고, 깨운 태스크의 우선순위가
 * 지금 실행 중이던 태스크(running_prio)보다 높으면 *hptw = 1. *hptw 를 0 으로 되돌리지는 않는다. */
static int nt_notify_from_isr(ntask_t *t, unsigned idx, uint32_t v, nt_action_t a, uint32_t *prev,
                              int running_prio, int *hptw) {
    int was_waiting = (t->state[idx] == NT_WAITING);
    int ret = nt_notify(t, idx, v, a, prev);
    if (was_waiting && t->prio > running_prio && hptw) *hptw = 1;        /* tasks.c 8265–8271 */
    return ret;
}

/* vTaskGenericNotifyGiveFromISR (tasks.c 8315–8442) = eIncrement 의 ISR 판. */
static void nt_give_from_isr(ntask_t *t, unsigned idx, int running_prio, int *hptw) {
    (void)nt_notify_from_isr(t, idx, 0, eIncrement, NULL, running_prio, hptw);
}

/* ulTaskNotifyTake 앞부분 (tasks.c 7810–7826): 값이 0 이고 ticks > 0 이면 WAITING 으로 표시하고
 * blocked_on = idx, 1(블록) 반환. 아니면 아무것도 바꾸지 않고 0. */
static int nt_take_begin(ntask_t *t, unsigned idx, uint32_t ticks) {
    if (t->value[idx] == 0u && ticks > 0u) {
        t->state[idx] = NT_WAITING;
        t->blocked_on = (int)idx;
        return 1;
    }
    return 0;
}

/* ulTaskNotifyTake 뒷부분 (7864–7886): 깨어났든 타임아웃이든 블록을 안 했든 여기로 온다.
 * 현재 값 r 을 돌려준다. r != 0 이면 clear_on_exit ? 0 : r - 1 로 바꾼다. 상태는 NOT_WAITING, blocked_on = -1. */
static uint32_t nt_take_end(ntask_t *t, unsigned idx, int clear_on_exit) {
    uint32_t r = t->value[idx];
    if (r != 0u) t->value[idx] = clear_on_exit ? 0u : r - 1u;
    t->state[idx] = NT_NOT_WAITING;
    t->blocked_on = -1;
    return r;
}

/* xTaskNotifyWait 앞부분 (7913–7934): "읽지 않은 알림" 이 없고 ticks > 0 일 때만
 * value &= ~clr_entry, WAITING, blocked_on = idx, 1 반환. 아니면(이미 RECEIVED 이거나 ticks == 0) 0 — 이때는
 * clr_entry 도 적용하지 않는다. */
static int nt_wait_begin(ntask_t *t, unsigned idx, uint32_t clr_entry, uint32_t ticks) {
    if (t->state[idx] != NT_RECEIVED && ticks > 0u) {
        t->value[idx] &= ~clr_entry;
        t->state[idx] = NT_WAITING;
        t->blocked_on = (int)idx;
        return 1;
    }
    return 0;
}

/* xTaskNotifyWait 뒷부분 (7975–7999): *out(있으면) = 현재 값 (exit 지우기 "전").
 * 상태가 RECEIVED 면 value &= ~clr_exit 하고 NT_PASS, 아니면(타임아웃) 값은 그대로 NT_FAIL.
 * 상태는 NOT_WAITING, blocked_on = -1. */
static int nt_wait_end(ntask_t *t, unsigned idx, uint32_t clr_exit, uint32_t *out) {
    int ret;
    if (out) *out = t->value[idx];
    if (t->state[idx] == NT_RECEIVED) {
        t->value[idx] &= ~clr_exit;
        ret = NT_PASS;
    } else {
        ret = NT_FAIL;
    }
    t->state[idx] = NT_NOT_WAITING;
    t->blocked_on = -1;
    return ret;
}


/* ================= 연습 1: stale notification ================= */
#define IDX_TX 1u
/* xTaskNotifyStateClearIndexed: 읽지 않은 알림(RECEIVED)이 있으면 NOT_WAITING 으로, NT_PASS. 없으면 NT_FAIL. */
static int nt_state_clear(ntask_t *t, unsigned idx) {
    if (t->state[idx] == NT_RECEIVED) { t->state[idx] = NT_NOT_WAITING; return NT_PASS; }
    return NT_FAIL;
}
/* ulTaskNotifyValueClearIndexed: 지우기 "전" 값을 돌려주고 value &= ~bits. 상태는 안 바꾼다. */
static uint32_t nt_value_clear(ntask_t *t, unsigned idx, uint32_t bits) {
    uint32_t old = t->value[idx];
    t->value[idx] &= ~bits;
    return old;
}
/* 새 전송 시작 전 정리: (실제로는 먼저 이전 전송을 하드웨어에서 중단 — TCIE 끄기/DMA abort) 후 상태·값 비우기 */
static void uart_tx_prepare(ntask_t *comm) {
    (void)nt_state_clear(comm, IDX_TX);
    (void)nt_value_clear(comm, IDX_TX, 0xFFFFFFFFu);
}

/* ================= 연습 2: RAM 손익 계산기 ================= */
/* demo_sizes_cm4.c 실측 (Cortex-M4F, 32비트 tick, trace/queue set 끔) */
#define CM4_SEM      72u
#define CM4_EGROUP   28u
static const unsigned CM4_TCB_BY_ENTRIES[5] = { 80u, 84u, 88u, 92u, 100u };   /* entries = 0(알림 끔)..4 */
/* heap_4: 요청 + BlockLink_t(Cortex-M 8바이트) 를 8 바이트 정렬로 올림 (heap_4.c 158, 188–196) */
static unsigned heap4_block(unsigned obj) { return (obj + 8u + 7u) & ~7u; }
typedef struct {
    unsigned n_sem, n_egroup;          /* 알림으로 바꿀 세마포어 / 이벤트 그룹 수 */
    unsigned n_tcb;                    /* 시스템의 모든 TCB 수 (idle, timer 태스크 포함) */
    unsigned entries_before, entries_after;
    int      dynamic;                  /* 1 = xSemaphoreCreateBinary 등 heap_4, 0 = ...Static */
} design_t;
/* 절약되는 바이트 (음수 = 오히려 손해) */
static long ram_saved(design_t d) {
    long per_sem = d.dynamic ? (long)heap4_block(CM4_SEM) : (long)CM4_SEM;
    long per_eg  = d.dynamic ? (long)heap4_block(CM4_EGROUP) : (long)CM4_EGROUP;
    long tcb_growth = (long)CM4_TCB_BY_ENTRIES[d.entries_after] - (long)CM4_TCB_BY_ENTRIES[d.entries_before];
    return (long)d.n_sem * per_sem + (long)d.n_egroup * per_eg - (long)d.n_tcb * tcb_growth;
}

/* ================= 연습 3: 최신 값 유지 + 버린 개수 ================= */
/* 먼저 NoOverwrite 로 시도 → 실패하면 (*dropped)++ 하고 Overwrite. 항상 최신 값이 남는다. */
static void adc_post_newest(ntask_t *t, uint32_t sample, unsigned *dropped) {
    if (nt_notify(t, 0, sample, eSetValueWithoutOverwrite, NULL) == NT_FAIL) {
        (*dropped)++;
        (void)nt_notify(t, 0, sample, eSetValueWithOverwrite, NULL);
    }
}

/* ================= 연습 4: wait-all + 전체 타임아웃 ================= */
/* start 부터 timeout tick 중 남은 tick (wrap 안전: 부호 없는 뺄셈). 0 이면 만료. */
static uint32_t ticks_left(uint32_t start, uint32_t now, uint32_t timeout) {
    uint32_t elapsed = now - start;
    return elapsed >= timeout ? 0u : timeout - elapsed;
}
typedef struct { uint32_t tick, bits; } ev_t;
/* 이벤트 ev[] 가 시간순으로 ISR 에서 온다. 태스크는 매번 남은 시간으로 xTaskNotifyWait 를 다시 부른다.
 * 모두 모이면 1 (*done_tick = 그 tick), 그 전에 만료되면 0. *wakes = 깨어난 횟수. */
static int wait_all_deadline(const ev_t *ev, int n, uint32_t need, uint32_t start, uint32_t timeout,
                             uint32_t *done_tick, unsigned *wakes) {
    ntask_t t;
    uint32_t acc = 0, v;
    nt_init(&t, "fusion", 3);
    for (int i = 0; i < n; i++) {
        uint32_t left = ticks_left(start, ev[i].tick, timeout);
        if (left == 0u) break;                              /* 이 이벤트 전에 만료 */
        (void)nt_wait_begin(&t, 0, 0, left);
        (void)nt_notify(&t, 0, ev[i].bits, eSetBits, NULL);
        (void)nt_wait_end(&t, 0, 0xFFFFFFFFu, &v);
        acc |= v & need;
        if ((acc & need) == need) { *done_tick = ev[i].tick; *wakes = t.wakes; return 1; }
    }
    *wakes = t.wakes;
    return 0;
}

/* ================= 테스트 ================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main(void) {
    ntask_t comm;
    int hp = 0;

    /* 연습 1 */
    nt_init(&comm, "comm", 3);
    CHECK(nt_take_begin(&comm, IDX_TX, 10) == 1);
    CHECK(nt_take_end(&comm, IDX_TX, 1) == 0u);               /* 전송 1: 타임아웃 */
    nt_give_from_isr(&comm, IDX_TX, 1, &hp);                  /* 늦게 온 전송 1 의 TC */
    CHECK(comm.state[IDX_TX] == NT_RECEIVED && comm.value[IDX_TX] == 1u);
    {
        ntask_t naive = comm;                                 /* 정리 없이 전송 2 시작 */
        CHECK(nt_take_begin(&naive, IDX_TX, 10) == 0);        /* 블록조차 안 하고 */
        CHECK(nt_take_end(&naive, IDX_TX, 1) == 1u);          /* "전송 2 완료" 로 착각 */
    }
    uart_tx_prepare(&comm);
    CHECK(comm.state[IDX_TX] == NT_NOT_WAITING && comm.value[IDX_TX] == 0u);
    CHECK(nt_take_begin(&comm, IDX_TX, 10) == 1);             /* 이제 제대로 기다린다 */
    nt_give_from_isr(&comm, IDX_TX, 1, &hp);                  /* 전송 2 의 진짜 TC */
    CHECK(nt_take_end(&comm, IDX_TX, 1) == 1u);
    CHECK(nt_state_clear(&comm, IDX_TX) == NT_FAIL);          /* 비울 게 없으면 pdFAIL */
    comm.value[0] = 0x5u;
    CHECK(nt_value_clear(&comm, 0, 0x1u) == 0x5u && comm.value[0] == 0x4u);

    /* 연습 2 */
    CHECK(heap4_block(CM4_SEM) == 80u && heap4_block(CM4_EGROUP) == 40u);
    CHECK(ram_saved((design_t){ 6, 2, 12, 1, 1, 1 }) == 560);   /* 인덱스 그대로: 순수 절약 */
    CHECK(ram_saved((design_t){ 6, 2, 12, 1, 2, 1 }) == 512);   /* 인덱스 1→2: TCB 12개 × 4 B */
    CHECK(ram_saved((design_t){ 6, 2, 12, 1, 2, 0 }) == 440);   /* 정적 할당이면 헤더 절약분 없음 */
    CHECK(ram_saved((design_t){ 1, 0, 20, 1, 4, 1 }) == -240);  /* 세마포어 1개 없애려 인덱스 4개 → 손해 */

    /* 연습 3 */
    {
        ntask_t a;
        unsigned dropped = 0;
        uint32_t v = 0;
        nt_init(&a, "adc", 2);
        adc_post_newest(&a, 100, &dropped);
        adc_post_newest(&a, 101, &dropped);
        adc_post_newest(&a, 102, &dropped);
        CHECK(dropped == 2u && nt_wait_begin(&a, 0, 0, 0) == 0);
        CHECK(nt_wait_end(&a, 0, 0, &v) == NT_PASS && v == 102u);
        adc_post_newest(&a, 103, &dropped);
        CHECK(dropped == 2u && a.value[0] == 103u);
    }

    /* 연습 4 */
    {
        enum { ACC = 1u, GYRO = 2u, MAG = 4u, ALL3 = 7u };
        const uint32_t s = 0xFFFFFFFCu;                       /* 곧 wrap 하는 tick */
        const ev_t ok[3]   = { { s + 3u, ACC }, { s + 7u, GYRO }, { s + 9u, MAG } };
        const ev_t late[3] = { { s + 3u, ACC }, { s + 7u, GYRO }, { s + 12u, MAG } };
        const ev_t dup[4]  = { { s + 1u, ACC }, { s + 2u, ACC }, { s + 4u, GYRO | ACC }, { s + 5u, MAG } };
        uint32_t done = 0;
        unsigned wakes = 0;
        CHECK(ticks_left(s, s + 3u, 10) == 7u && ticks_left(s, s + 10u, 10) == 0u && ticks_left(s, s, 10) == 10u);
        CHECK(wait_all_deadline(ok, 3, ALL3, s, 10, &done, &wakes) == 1 && done == 5u && wakes == 3u);
        CHECK(wait_all_deadline(late, 3, ALL3, s, 10, &done, &wakes) == 0 && wakes == 2u);
        CHECK(wait_all_deadline(dup, 4, ALL3, s, 10, &done, &wakes) == 1 && done == 1u && wakes == 4u);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
