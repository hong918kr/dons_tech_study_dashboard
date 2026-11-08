/* RTOS-M01 태스크 알림: 세마포어·이벤트 그룹의 경량 대체
 *
 * 호스트 모델: FreeRTOS 의 direct-to-task notification 은 "TCB 안의 두 배열" 이 전부다.
 *     uint32_t ulNotifiedValue[N];  uint8_t ucNotifyState[N];   (tasks.c V11.3.2 438–439)
 *     상태 = NOT_WAITING(0) / WAITING(1, 이 인덱스를 기다리며 Blocked) / RECEIVED(2, 읽지 않은 알림 있음)
 * 블로킹 API 는 begin / end 두 함수로 쪼갠다. FreeRTOS 도 "블록할지 결정 → [Blocked] → 값 돌려주기·지우기"
 * 두 덩어리다 (ulTaskGenericNotifyTake 7797–7892, xTaskGenericNotifyWait 7899–8006).
 * 하네스가 begin → (ISR 호출) → end 순서를 직접 정하므로 스레드·경쟁 없이 결정적이다.
 * 같은 시나리오를 진짜 커널에 돌린 결과는 demo_freertos_notify.c (39/39 일치).
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
    (void)t; (void)idx; (void)v; (void)a; (void)prev;
    return NT_PASS; /* TODO */
}

/* xTaskGenericNotifyFromISR (tasks.c 8133–8308): nt_notify 와 같고, 깨운 태스크의 우선순위가
 * 지금 실행 중이던 태스크(running_prio)보다 높으면 *hptw = 1. *hptw 를 0 으로 되돌리지는 않는다. */
static int nt_notify_from_isr(ntask_t *t, unsigned idx, uint32_t v, nt_action_t a, uint32_t *prev,
                              int running_prio, int *hptw) {
    (void)t; (void)idx; (void)v; (void)a; (void)prev; (void)running_prio; (void)hptw;
    return NT_PASS; /* TODO */
}

/* vTaskGenericNotifyGiveFromISR (tasks.c 8315–8442) = eIncrement 의 ISR 판. */
static void nt_give_from_isr(ntask_t *t, unsigned idx, int running_prio, int *hptw) {
    (void)t; (void)idx; (void)running_prio; (void)hptw; /* TODO */
}

/* ulTaskNotifyTake 앞부분 (tasks.c 7810–7826): 값이 0 이고 ticks > 0 이면 WAITING 으로 표시하고
 * blocked_on = idx, 1(블록) 반환. 아니면 아무것도 바꾸지 않고 0. */
static int nt_take_begin(ntask_t *t, unsigned idx, uint32_t ticks) {
    (void)t; (void)idx; (void)ticks;
    return 0; /* TODO */
}

/* ulTaskNotifyTake 뒷부분 (7864–7886): 깨어났든 타임아웃이든 블록을 안 했든 여기로 온다.
 * 현재 값 r 을 돌려준다. r != 0 이면 clear_on_exit ? 0 : r - 1 로 바꾼다. 상태는 NOT_WAITING, blocked_on = -1. */
static uint32_t nt_take_end(ntask_t *t, unsigned idx, int clear_on_exit) {
    (void)t; (void)idx; (void)clear_on_exit;
    return 0; /* TODO */
}

/* xTaskNotifyWait 앞부분 (7913–7934): "읽지 않은 알림" 이 없고 ticks > 0 일 때만
 * value &= ~clr_entry, WAITING, blocked_on = idx, 1 반환. 아니면(이미 RECEIVED 이거나 ticks == 0) 0 — 이때는
 * clr_entry 도 적용하지 않는다. */
static int nt_wait_begin(ntask_t *t, unsigned idx, uint32_t clr_entry, uint32_t ticks) {
    (void)t; (void)idx; (void)clr_entry; (void)ticks;
    return 0; /* TODO */
}

/* xTaskNotifyWait 뒷부분 (7975–7999): *out(있으면) = 현재 값 (exit 지우기 "전").
 * 상태가 RECEIVED 면 value &= ~clr_exit 하고 NT_PASS, 아니면(타임아웃) 값은 그대로 NT_FAIL.
 * 상태는 NOT_WAITING, blocked_on = -1. */
static int nt_wait_end(ntask_t *t, unsigned idx, uint32_t clr_exit, uint32_t *out) {
    (void)t; (void)idx; (void)clr_exit;
    if (out) *out = 0;
    return NT_FAIL; /* TODO */
}

/* 블록 없이 부르는 편의 함수 (ticks = 0) — 주어진 코드 */
static uint32_t nt_take_now(ntask_t *t, unsigned idx, int clear) {
    (void)nt_take_begin(t, idx, 0u);
    return nt_take_end(t, idx, clear);
}
static int nt_wait_now(ntask_t *t, unsigned idx, uint32_t clr_entry, uint32_t clr_exit, uint32_t *out) {
    (void)nt_wait_begin(t, idx, clr_entry, 0u);
    return nt_wait_end(t, idx, clr_exit, out);
}

/* ================= 비교용 모델 (주어진 코드): 세마포어 · 이벤트 그룹 ================= */
typedef struct { uint32_t count, max; } csem_t;           /* max = 1 이면 바이너리 세마포어 */
static int sem_give(csem_t *s) { if (s->count >= s->max) return 0; s->count++; return 1; }
static int sem_take_now(csem_t *s) { if (s->count == 0u) return 0; s->count--; return 1; }

typedef struct { ntask_t *t; uint32_t want; int wait_all, clear_on_exit; uint32_t got; } eg_waiter_t;
typedef struct { uint32_t bits; eg_waiter_t *w[4]; int nw; } egroup_t;
static void eg_wait_block(egroup_t *g, eg_waiter_t *w) {   /* 조건이 아직 안 맞은 상태에서만 부른다 */
    g->w[g->nw++] = w;
}
/* xEventGroupSetBits 모델 (event_groups.c 547–647): 기다리는 "모든" 태스크를 검사해 조건이 맞으면 깨운다.
 * clear_on_exit 비트는 다 돈 뒤 한꺼번에 지운다. 반환: 깨운 태스크 수. */
static int eg_set_bits(egroup_t *g, uint32_t b) {
    uint32_t clear = 0;
    int woke = 0, k = 0;
    g->bits |= b;
    for (int i = 0; i < g->nw; i++) {
        eg_waiter_t *w = g->w[i];
        int ok = w->wait_all ? (g->bits & w->want) == w->want : (g->bits & w->want) != 0u;
        if (ok) {
            w->got = g->bits;
            if (w->clear_on_exit) clear |= w->want;
            w->t->wakes++;
            woke++;
        } else {
            g->w[k++] = w;
        }
    }
    g->nw = k;
    g->bits &= ~clear;
    return woke;
}

/* ================= Part B: 대체 패턴과 한계 ================= */

/* ADC 완료 ISR → 처리 태스크 "메일박스" (인덱스 0).
 * keep_unread = 1: eSetValueWithoutOverwrite — 안 읽은 값이 있으면 새 샘플을 버리고 (*dropped)++, 0 반환.
 * keep_unread = 0: eSetValueWithOverwrite — 항상 최신 값으로 덮고 1 반환. 깨우기/hptw 는 신경 쓰지 않는다. */
static int adc_post(ntask_t *t, uint32_t sample, int keep_unread, unsigned *dropped) {
    (void)t; (void)sample; (void)keep_unread; (void)dropped;
    return 0; /* TODO */
}

/* 이벤트 그룹의 "wait-all" 흉내: 알림은 비트가 하나만 와도 깨운다.
 * 깨어날 때마다 받은 비트 got 을 *acc 에 모으고, need 가 다 모이면 *acc 에서 need 를 지우고 1, 아니면 0. */
static int fusion_on_wake(uint32_t got, uint32_t need, uint32_t *acc) {
    (void)got; (void)need; (void)acc;
    return 0; /* TODO */
}

/* 수신자가 여럿일 때의 우회: ISR 이 n 개 태스크에 "각각" eSetBits 알림을 보낸다.
 * 반환: 이 호출로 깨어난 태스크 수. *hptw 는 nt_notify_from_isr 규칙대로. */
static int notify_fanout_from_isr(ntask_t *const *ts, int n, unsigned idx, uint32_t bits,
                                  int running_prio, int *hptw) {
    (void)ts; (void)n; (void)idx; (void)bits; (void)running_prio; (void)hptw;
    return 0; /* TODO */
}

/* ================= Part C: UART 드라이버 — 인덱스 0 = 앱 이벤트, 인덱스 1 = 드라이버 전용 ================= */
#define USART_ISR_ORE  (1u << 3)          /* STM32 USART_ISR 비트 위치 (overrun)        */
#define USART_ISR_IDLE (1u << 4)          /* idle line = 프레임 하나 끝 (DMA RX 와 함께) */
#define USART_ISR_TC   (1u << 6)          /* transmission complete                       */
typedef struct { uint32_t ISR; unsigned icr_writes; } usart_t;
static void usart_write_icr(usart_t *u, uint32_t m) {   /* ICR: 1 을 쓴 비트만 ISR 에서 지워진다 (W1C) */
    u->ISR &= ~m;
    u->icr_writes++;
}
#define IDX_APP 0u
#define IDX_TX  1u
#define EV_RX_FRAME   (1u << 0)
#define EV_RX_OVERRUN (1u << 1)
#define EV_CMD        (1u << 2)           /* 다른 태스크가 보내는 명령 */

/* USART IRQ 핸들러. 반환값 = portYIELD_FROM_ISR(xHigherPriorityTaskWoken) 에 넘길 값.
 *  - IDLE → EV_RX_FRAME, ORE → EV_RX_OVERRUN: 모아서 인덱스 0 에 eSetBits 알림 "한 번" (둘 다 없으면 안 보냄)
 *  - TC   → 인덱스 1 에 give (vTaskNotifyGiveIndexedFromISR)
 *  - 처리한 플래그(IDLE/ORE/TC 중 서 있던 것)만 ICR 에 한 번 써서 지운다. 플래그가 없으면 ICR 을 안 쓴다. */
static int usart_irq(usart_t *u, ntask_t *comm, int running_prio) {
    (void)u; (void)comm; (void)running_prio;
    return 0; /* TODO */
}

/* 비교용 (주어진 버그 코드): 인덱스를 하나만 쓰는 드라이버. TC 도 인덱스 0 에 give 하고,
 * uart_send 는 ulTaskNotifyTake(pdTRUE, ...) 로 인덱스 0 을 기다린다. */
static int usart_irq_one_index(usart_t *u, ntask_t *comm, int running_prio) {
    uint32_t sr = u->ISR, handled = sr & (USART_ISR_IDLE | USART_ISR_ORE | USART_ISR_TC);
    int hptw = 0;
    if (sr & USART_ISR_IDLE) (void)nt_notify_from_isr(comm, IDX_APP, EV_RX_FRAME, eSetBits, NULL, running_prio, &hptw);
    if (sr & USART_ISR_TC)   nt_give_from_isr(comm, IDX_APP, running_prio, &hptw);
    if (handled) usart_write_icr(u, handled);
    return hptw;
}

/* ================= 테스트 하네스 ================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main(void) {
    ntask_t me, w, lo, r1, r2, fu, comm;
    uint32_t v = 0, prev = 0;
    int hp;

    /* ---- Part A1: eIncrement + Take (decrement vs clear) — demo A1 과 같은 숫자 ---- */
    nt_init(&me, "me", 2);
    for (int i = 0; i < 5; i++) CHECK(nt_notify(&me, 0, 0, eIncrement, NULL) == NT_PASS);
    CHECK(nt_take_now(&me, 0, 0) == 5u);         /* pdFALSE: 5 를 돌려주고 4 로 */
    CHECK(nt_take_now(&me, 0, 0) == 4u);
    CHECK(nt_take_now(&me, 0, 1) == 3u);         /* pdTRUE: 3 을 돌려주고 0 으로 */
    CHECK(nt_take_now(&me, 0, 1) == 0u && me.value[0] == 0u);

    /* ---- A2: 덮어쓰기 / 덮어쓰지 않기 ---- */
    CHECK(nt_notify(&me, 0, 11, eSetValueWithoutOverwrite, NULL) == NT_PASS);
    CHECK(nt_notify(&me, 0, 22, eSetValueWithoutOverwrite, NULL) == NT_FAIL && me.value[0] == 11u);
    CHECK(nt_wait_now(&me, 0, 0, 0, &v) == NT_PASS && v == 11u);
    CHECK(nt_notify(&me, 0, 33, eSetValueWithoutOverwrite, NULL) == NT_PASS);   /* 읽었으니(상태) 다시 된다 */
    CHECK(nt_notify(&me, 0, 44, eSetValueWithOverwrite, &prev) == NT_PASS && prev == 33u);
    CHECK(nt_wait_now(&me, 0, 0, 0xFFFFFFFFu, &v) == NT_PASS && v == 44u && me.value[0] == 0u);

    /* ---- A3: eSetBits + xTaskNotifyWait 마스크 ---- */
    nt_notify(&me, 0, 0x1, eSetBits, NULL);
    nt_notify(&me, 0, 0x4, eSetBits, NULL);
    CHECK(nt_wait_now(&me, 0, 0, 0x1, &v) == NT_PASS && v == 0x5u);   /* *out 은 exit 지우기 "전" 값 */
    CHECK(me.value[0] == 0x4u && me.state[0] == NT_NOT_WAITING);
    CHECK(nt_wait_now(&me, 0, 0xFFFFFFFFu, 0xFFFFFFFFu, &v) == NT_FAIL && v == 0x4u);
    CHECK(me.value[0] == 0x4u);                  /* ticks == 0 이면 entry 지우기도 안 한다 */
    nt_notify(&me, 0, 0x2, eSetBits, NULL);
    CHECK(nt_wait_begin(&me, 0, 0xFFFFFFFFu, 10) == 0);              /* 이미 RECEIVED → 블록 안 함 */
    CHECK(nt_wait_end(&me, 0, 0, &v) == NT_PASS && v == 0x6u);        /* 대기 전 비트가 살아 있다 */
    CHECK(nt_wait_begin(&me, 0, 0xFFFFFFFFu, 5) == 1 && me.value[0] == 0u && me.blocked_on == 0);
    CHECK(nt_wait_end(&me, 0, 0xFFFFFFFFu, &v) == NT_FAIL && v == 0u && me.blocked_on == -1);  /* 타임아웃 */

    /* ---- A4: 블록 → ISR give → 깨어남, hptw 규칙 ---- */
    nt_init(&w, "W", 3);
    CHECK(nt_take_begin(&w, 0, NT_MAX_DELAY) == 1 && w.state[0] == NT_WAITING && w.blocked_on == 0);
    hp = 0;
    nt_give_from_isr(&w, 0, 2, &hp);             /* 실행 중이던 태스크 prio 2 < W prio 3 */
    CHECK(hp == 1 && w.blocked_on == -1 && w.wakes == 1u);
    CHECK(nt_take_end(&w, 0, 1) == 1u && w.value[0] == 0u && w.state[0] == NT_NOT_WAITING);
    nt_init(&lo, "L", 1);
    CHECK(nt_take_begin(&lo, 0, NT_MAX_DELAY) == 1);
    hp = 0;
    nt_give_from_isr(&lo, 0, 2, &hp);            /* L(1) 은 실행 중(2)보다 낮다 → 스위치 요청 없음 */
    CHECK(hp == 0 && lo.blocked_on == -1 && lo.wakes == 1u);
    hp = 1;
    nt_give_from_isr(&lo, 0, 2, &hp);            /* 이미 1 인 hptw 를 0 으로 되돌리면 안 된다 */
    CHECK(hp == 1);
    hp = 0;
    nt_give_from_isr(&w, 0, 2, &hp);             /* 블록 안 한 태스크에 give: 깨울 게 없다 → hptw 0 */
    CHECK(hp == 0 && w.value[0] == 1u && w.state[0] == NT_RECEIVED && w.wakes == 1u);

    /* ---- A5: eNoAction 깨우기 → Take 가 0, 타임아웃 → 0, eIncrement wrap, 비트에 Take ---- */
    nt_init(&w, "W", 3);
    CHECK(nt_take_begin(&w, 0, NT_MAX_DELAY) == 1);
    CHECK(nt_notify(&w, 0, 0, eNoAction, NULL) == NT_PASS && w.blocked_on == -1 && w.wakes == 1u);
    CHECK(nt_take_end(&w, 0, 1) == 0u);          /* 깨어났지만 값은 0: "0 = 타임아웃 또는 가짜" 로 처리 */
    CHECK(nt_take_begin(&w, 0, 10) == 1);
    CHECK(nt_take_end(&w, 0, 1) == 0u && w.state[0] == NT_NOT_WAITING && w.blocked_on == -1);
    nt_notify(&me, 0, 0x8, eSetBits, NULL);
    CHECK(nt_take_now(&me, 0, 1) == 8u && me.value[0] == 0u);       /* Take 는 비트를 "개수" 로 본다 */
    nt_notify(&me, 0, 0xFFFFFFFFu, eSetValueWithOverwrite, NULL);
    nt_notify(&me, 0, 0, eIncrement, NULL);
    CHECK(me.value[0] == 0u && nt_take_now(&me, 0, 1) == 0u);       /* 상한이 없어 2^32 개가 사라진다 */

    /* ---- Part B1: ISR 이 처리보다 빠를 때 — 바이너리 세마포어 vs 알림 ---- */
    {
        csem_t bin = {0, 1};
        int ok = 0, taken = 0;
        for (int i = 0; i < 5; i++) ok += sem_give(&bin);
        while (sem_take_now(&bin)) taken++;
        CHECK(ok == 1 && taken == 1);            /* 5개 중 4개가 합쳐져 사라짐 (RTOS-B08) */
        nt_init(&w, "rx", 3);
        hp = 0;
        for (int i = 0; i < 5; i++) nt_give_from_isr(&w, 0, 1, &hp);
        CHECK(nt_take_now(&w, 0, 1) == 5u);      /* clear-on-exit: 한 번에 "5개 왔다" 를 안다 → 일괄 처리 */
        for (int i = 0; i < 5; i++) nt_give_from_isr(&w, 0, 1, &hp);
        taken = 0;
        while (nt_take_now(&w, 0, 0) != 0u) taken++;
        CHECK(taken == 5);                       /* decrement: 카운팅 세마포어처럼 하나씩 */
    }

    /* ---- B2: 메일박스 (eSetValueWithoutOverwrite / WithOverwrite) ---- */
    {
        unsigned dropped = 0;
        nt_init(&w, "adc", 2);
        CHECK(adc_post(&w, 100, 1, &dropped) == 1);
        CHECK(adc_post(&w, 101, 1, &dropped) == 0 && adc_post(&w, 102, 1, &dropped) == 0 && dropped == 2u);
        CHECK(nt_wait_now(&w, 0, 0, 0, &v) == NT_PASS && v == 100u);  /* 가장 오래된 값을 지킨다 */
        CHECK(adc_post(&w, 103, 1, &dropped) == 1 && dropped == 2u);
        CHECK(adc_post(&w, 104, 0, &dropped) == 1 && adc_post(&w, 105, 0, &dropped) == 1);
        CHECK(nt_wait_now(&w, 0, 0, 0, &v) == NT_PASS && v == 105u && dropped == 2u);  /* 최신 값 */
    }

    /* ---- B3: 이벤트 그룹 wait-all vs 알림으로 흉내 — 깨어나는 횟수 ---- */
    {
        enum { ACC = 1u << 0, GYRO = 1u << 1, MAG = 1u << 2, ALL3 = ACC | GYRO | MAG };
        const uint32_t seq[3] = { ACC, GYRO, MAG };      /* 센서 DRDY ISR 세 개가 따로따로 */
        uint32_t acc = 0;
        int done = 0, done_at = -1;
        nt_init(&fu, "fusion", 3);
        for (int i = 0; i < 3; i++) {
            CHECK(nt_wait_begin(&fu, 0, 0, NT_MAX_DELAY) == 1);     /* 매번 블록 */
            hp = 0;
            nt_notify_from_isr(&fu, 0, seq[i], eSetBits, NULL, 0, &hp);
            nt_wait_end(&fu, 0, 0xFFFFFFFFu, &v);
            if (fusion_on_wake(v, ALL3, &acc) && !done) { done = 1; done_at = i; }
        }
        CHECK(done == 1 && done_at == 2 && acc == 0u);
        CHECK(fu.wakes == 3u);                   /* 알림: 비트마다 깨어남 → 스위치 3번 */
        acc = GYRO;                              /* 조건 일부만 → 아직 */
        CHECK(fusion_on_wake(ACC | 0x100u, ALL3, &acc) == 0 && acc == (ACC | GYRO));
        ntask_t eg_t;
        nt_init(&eg_t, "fusion-eg", 3);
        egroup_t g = {0};
        eg_waiter_t ew = { &eg_t, ALL3, 1, 1, 0 };
        eg_wait_block(&g, &ew);
        CHECK(eg_set_bits(&g, ACC) == 0 && eg_set_bits(&g, GYRO) == 0);
        CHECK(eg_set_bits(&g, MAG) == 1 && ew.got == ALL3 && g.bits == 0u);
        CHECK(eg_t.wakes == 1u);                 /* 이벤트 그룹: 다 모였을 때 한 번 */
    }

    /* ---- B4: 수신자 1명 제한 — 알림은 TCB 하나를 가리킨다 ---- */
    {
        nt_init(&r1, "R1", 3);
        nt_init(&r2, "R2", 3);
        CHECK(nt_take_begin(&r1, 0, NT_MAX_DELAY) == 1 && nt_take_begin(&r2, 0, NT_MAX_DELAY) == 1);
        hp = 0;
        nt_give_from_isr(&r1, 0, 1, &hp);        /* R1 에게만 */
        CHECK(r1.blocked_on == -1 && r2.blocked_on == 0 && r2.wakes == 0u);
        nt_take_end(&r1, 0, 1);
        CHECK(nt_take_begin(&r1, 0, NT_MAX_DELAY) == 1);
        ntask_t *const both[2] = { &r1, &r2 };
        hp = 0;
        CHECK(notify_fanout_from_isr(both, 2, 0, 0x1, 1, &hp) == 2 && hp == 1);   /* ISR 이 핸들 둘 다 알아야 함 */
        CHECK(r1.blocked_on == -1 && r2.blocked_on == -1 && r1.value[0] == 0x1u && r2.value[0] == 0x1u);
        CHECK(notify_fanout_from_isr(both, 2, 0, 0x2, 1, &hp) == 0);              /* 이미 깨어 있음: 비트만 OR */
        CHECK(r1.value[0] == 0x3u && r2.value[0] == 0x3u);
        ntask_t e1, e2;
        nt_init(&e1, "E1", 3);
        nt_init(&e2, "E2", 3);
        egroup_t g = {0};
        eg_waiter_t w1 = { &e1, 0x1, 0, 0, 0 }, w2 = { &e2, 0x1, 0, 0, 0 };
        eg_wait_block(&g, &w1);
        eg_wait_block(&g, &w2);
        CHECK(eg_set_bits(&g, 0x1) == 2 && e1.wakes == 1u && e2.wakes == 1u);    /* set 한 번에 둘 다 */
    }

    /* ---- Part C: UART — 인덱스 분리 드라이버 ---- */
    {
        usart_t u = {0, 0};
        nt_init(&comm, "comm", 3);
        CHECK(nt_wait_begin(&comm, IDX_APP, 0, NT_MAX_DELAY) == 1);          /* 메인 루프 대기 */
        u.ISR = USART_ISR_IDLE;
        CHECK(usart_irq(&u, &comm, 1) == 1 && u.ISR == 0u && u.icr_writes == 1u);   /* logger(1) 선점 */
        CHECK(nt_wait_end(&comm, IDX_APP, 0xFFFFFFFFu, &v) == NT_PASS && v == EV_RX_FRAME);
        CHECK(nt_take_begin(&comm, IDX_TX, 10) == 1 && comm.blocked_on == (int)IDX_TX);   /* 응답 송신, TC 대기 */
        u.ISR = USART_ISR_IDLE;                                              /* 송신 중 새 프레임 도착 */
        CHECK(usart_irq(&u, &comm, 1) == 0);                                 /* 인덱스 0 알림은 안 깨운다 */
        CHECK(comm.blocked_on == (int)IDX_TX && comm.value[IDX_APP] == EV_RX_FRAME);
        CHECK(comm.state[IDX_APP] == NT_RECEIVED);
        u.ISR = USART_ISR_TC;
        CHECK(usart_irq(&u, &comm, 1) == 1 && comm.blocked_on == -1);
        CHECK(nt_take_end(&comm, IDX_TX, 1) == 1u);                          /* TX 완료 */
        CHECK(nt_wait_begin(&comm, IDX_APP, 0, NT_MAX_DELAY) == 0);          /* 밀린 이벤트 → 블록 안 함 */
        CHECK(nt_wait_end(&comm, IDX_APP, 0xFFFFFFFFu, &v) == NT_PASS && v == EV_RX_FRAME);
        CHECK(nt_wait_begin(&comm, IDX_APP, 0, NT_MAX_DELAY) == 1);
        u.ISR = USART_ISR_IDLE | USART_ISR_ORE | (1u << 7);                  /* bit7(TXE) 는 이 ISR 몫이 아님 */
        CHECK(usart_irq(&u, &comm, 3) == 0);     /* comm(3) 이 실행 중이던 것보다 높지 않음 → 요청 없음 */
        CHECK(u.ISR == (1u << 7) && u.icr_writes == 4u);
        CHECK(nt_wait_end(&comm, IDX_APP, 0xFFFFFFFFu, &v) == NT_PASS && v == (EV_RX_FRAME | EV_RX_OVERRUN));
        u.ISR = 0;
        CHECK(usart_irq(&u, &comm, 1) == 0 && u.icr_writes == 4u && comm.state[IDX_APP] == NT_NOT_WAITING);
    }
    /* 버그 버전: 인덱스 하나에 TX 완료와 RX 이벤트를 섞으면 */
    {
        usart_t u = {0, 0};
        nt_init(&comm, "comm", 3);
        CHECK(nt_take_begin(&comm, IDX_APP, 10) == 1);                      /* uart_send: TC 대기 (인덱스 0) */
        u.ISR = USART_ISR_IDLE;                                             /* 아직 TC 전인데 RX 프레임 */
        usart_irq_one_index(&u, &comm, 1);
        CHECK(comm.blocked_on == -1);                                       /* TX 대기가 RX 때문에 깨어남 */
        CHECK(nt_take_end(&comm, IDX_APP, 1) == EV_RX_FRAME);               /* 1 → "TX 완료" 로 착각 */
        CHECK(comm.value[IDX_APP] == 0u);                                   /* 그리고 RX 비트는 지워졌다 */
        CHECK(nt_wait_now(&comm, IDX_APP, 0, 0xFFFFFFFFu, &v) == NT_FAIL);  /* 메인 루프는 프레임을 못 본다 */
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
