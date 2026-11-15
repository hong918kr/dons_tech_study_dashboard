/* COMM-M13 추가 연습 — 한 번에 검증
 *
 * 본문 드라이버(solution.c 와 같음) 위에 연습 문제 답을 얹었다. 본문과 다른 점:
 *   - SpiXfer 에 t_submit 필드 (spi_submit 이 기록) — 연습 3
 *   - q_remove 의 이동 루프 안에 선점 지점 — 연습 5 (크리티컬 섹션 없는 취소의 경쟁을 드러냄)
 *   - 스트레스의 취소 호출을 g_cancel_fn 으로 바꿔 끼울 수 있음 — 연습 5
 * 연습 1 spi_transfer_sync (비동기 위의 동기 래퍼, 스택 디스크립터, ISR 문맥 금지)
 * 연습 2 spi_cancel_soft  ("너무 늦음" 취소 정책)
 * 연습 3 spi_expire_queued (큐 대기 데드라인)
 * 연습 4 최악 IMU 지연 공식 vs 시뮬레이션 (FIFO/우선순위 × 디스크립터 수 × 청크)
 * 연습 5 취소 경로의 크리티컬 섹션 누락 → 선점 스트레스에서 불변식 위반
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c && ./a.out
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---------------- 설정 (이 레슨의 정의) ---------------- */
#define SPI_N_CS       4u     /* 칩 선택 라인 수 */
#define SPI_NPRIO      2u     /* 0 = 높음 (IMU 같은 주기 센서), 1 = 낮음 (플래시 같은 대용량) */
#define SPI_QCAP       8u     /* 우선순위별 대기 슬롯 수. 실행 중인 1개는 큐 밖(active)에 있다 */
#define SPI_MAX_LEN    1024u
#define HW_SETUP_TICKS 2u     /* 전송 시작 → 첫 바이트까지 준비 시간 (CS setup, DMA 시작 등) */

enum { SPI_OK = 0, SPI_E_ARG = -1, SPI_E_BUSY = -2, SPI_E_QFULL = -3, SPI_E_NOTFOUND = -4, SPI_E_STOPPED = -5 };
enum { SPI_ST_DONE = 0, SPI_ST_CANCELLED = 1, SPI_ST_TIMEOUT = 2 };          /* 콜백 status */
enum { XS_IDLE = 0, XS_QUEUED, XS_ACTIVE, XS_CALLBACK };                     /* 디스크립터 상태 */

typedef void (*spi_cb_t)(void *ctx, int status);
typedef struct SpiXfer {
    /* 호출자가 채우는 필드 */
    uint8_t        cs, prio;
    uint16_t       len;          /* 1..SPI_MAX_LEN */
    const uint8_t *tx;           /* NULL = 0xFF 를 보냄 (읽기 전용 전송) */
    uint8_t       *rx;           /* NULL = 받은 바이트 버림 (쓰기 전용 전송) */
    uint32_t       timeout;      /* 전송 "시작"부터 tick 수, 1 이상 */
    spi_cb_t       cb;           /* 필수. 제출 1번당 정확히 1번 호출 */
    void          *ctx;
    /* 드라이버 전용 (호출자는 읽기만) */
    uint8_t        state;        /* XS_IDLE 일 때만 호출자가 고치거나 다시 제출할 수 있다 */
    uint32_t       seq;          /* 제출 순번 (같은 우선순위 안의 순서 검사용) */
    uint32_t       t_submit;     /* 제출 tick (연습 3) */
} SpiXfer;

typedef struct SpiBus SpiBus;
typedef struct MockSpi MockSpi;
/* 드라이버 함수 (내부 함수도 시작 코드의 unused 경고를 피하려고 외부 연결로 둔다) */
void     spi_init(SpiBus *b, MockSpi *h);
int      spi_submit(SpiBus *b, SpiXfer *x);
int      spi_cancel(SpiBus *b, SpiXfer *x);
void     spi_isr(SpiBus *b);
void     spi_tick(SpiBus *b);
int      spi_flush(SpiBus *b);
void     spi_start_next(SpiBus *b);
SpiXfer *spi_detach_active(SpiBus *b, bool abort_hw);
void     spi_complete(SpiXfer *x, int status);
void     spi_log(SpiBus *b, uint8_t ev, const SpiXfer *x);
void     crit_enter(void);   /* __disable_irq 모델 (중첩 가능) */
void     crit_exit(void);    /* 바깥쪽 exit 에서 대기 중 IRQ 가 실행된다 */

static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }
static unsigned popcount8(uint8_t v) { unsigned n = 0; while (v) { n += v & 1u; v >>= 1; } return n; }

/* ================= 실행 문맥 모델: PRIMASK(크리티컬 섹션) + ISR 선점 지점 ================= */
static uint32_t g_now;                  /* SysTick 카운터 (1 tick = 1 µs) */
static int      g_crit;                 /* > 0 = 인터럽트 마스크 (중첩 가능 — PRIMASK 저장/복원 모델) */
static int      g_in_isr;               /* ISR 실행 중 (SPI IRQ 와 SysTick 은 같은 우선순위 → 서로 선점 안 함) */
static uint32_t g_pre_seed = 1u, g_pre_permille, g_preempts;   /* 선점 주입: 선점 지점마다 permille/1000 확률 */
static SpiBus  *g_irq_bus;

static uint32_t now_ticks(void) { return g_now; }
static void hw_tick(void);
static void irq_dispatch_if_pending(void);
void crit_enter(void) { g_crit++; }
void crit_exit(void) {                              /* 마스크가 풀리는 순간 대기 중이던 IRQ 가 들어온다 */
    if (--g_crit == 0 && !g_in_isr) irq_dispatch_if_pending();
}
/* 태스크 문맥 코드가 여기를 지나는 동안 하드웨어 시간이 흐르고, 마스크가 안 돼 있으면 ISR 이 끼어든다. */
static void preempt_point(void) {
    if (g_in_isr || g_pre_permille == 0u) return;
    if (xs32(&g_pre_seed) % 1000u >= g_pre_permille) return;
    g_preempts++;
    hw_tick();
    if (g_crit == 0) irq_dispatch_if_pending();
}

/* ================= mock SPI 하드웨어 (주어짐) ================= */
struct MockSpi {
    uint8_t        cs_mask;                  /* bit i = CSi 활성 (실제 핀은 active-low) */
    bool           busy, done, irq_pending, stuck;
    bool           stuck_next;               /* 장애 주입: 다음에 시작하는 전송은 영원히 안 끝난다 */
    uint8_t        extra_next;               /* 장애 주입: 다음 전송의 준비 시간 + extra tick */
    const uint8_t *tx;
    uint8_t       *rx;
    uint16_t       len, pos;
    uint32_t       setup_left, starts, aborts, last_start_tick;
    uint32_t       multi_cs, start_while_busy, start_without_cs, cs_cut;   /* 규칙 위반 카운터 — 0 이어야 한다 */
};
static MockSpi g_hw;

/* CSi 에 달린 가짜 디바이스가 i 번째 바이트에 내보내는 값 (테스트 기대값) */
static uint8_t dev_expect(uint8_t cs, uint8_t mosi, uint16_t i) {
    return (uint8_t)((mosi ^ (uint8_t)(0x35u * (cs + 1u))) + i);
}
static uint8_t dev_miso(uint8_t mask, uint8_t mosi, uint16_t i) {
    if (mask == 0u) return 0xFFu;                            /* 아무도 MISO 를 안 끈다 → 풀업 */
    uint8_t v = 0xFFu;
    for (uint8_t c = 0; c < SPI_N_CS; c++)
        if (mask & (1u << c)) v &= dev_expect(c, mosi, i);   /* 둘 이상 선택 = MISO 충돌 (AND 로 모델) */
    return v;
}
static void hw_cs(MockSpi *h, uint8_t cs, bool assert) {
    preempt_point();
    uint8_t bit = (uint8_t)(1u << cs);
    if (assert) { if (h->cs_mask & (uint8_t)~bit) h->multi_cs++; h->cs_mask |= bit; }
    else        { if (h->busy && (h->cs_mask & bit)) h->cs_cut++; h->cs_mask &= (uint8_t)~bit; }
}
static void hw_start(MockSpi *h, const uint8_t *tx, uint8_t *rx, uint16_t len) {
    preempt_point();
    if (h->busy) h->start_while_busy++;
    if (popcount8(h->cs_mask) != 1u) h->start_without_cs++;
    h->busy = true; h->tx = tx; h->rx = rx; h->len = len; h->pos = 0;
    h->setup_left = HW_SETUP_TICKS + h->extra_next; h->extra_next = 0;
    h->stuck = h->stuck_next; h->stuck_next = false;
    h->starts++; h->last_start_tick = g_now;
}
/* 완료 플래그(TC/EOT 류)를 읽고 지운다. 래치된 완료가 없었으면 false = 가짜/오래된 인터럽트. */
static bool hw_ack(MockSpi *h) { bool d = h->done; h->done = false; h->irq_pending = false; return d; }
static void hw_abort(MockSpi *h) { h->busy = h->stuck = h->done = h->irq_pending = false; h->aborts++; }
static void hw_tick(void) {                         /* 하드웨어 1 tick: 준비 → 바이트마다 rx 에 기록 → 완료 IRQ */
    MockSpi *h = &g_hw;
    if (!h->busy || h->stuck) return;
    if (h->setup_left > 0u) { h->setup_left--; return; }
    uint8_t mosi = h->tx ? h->tx[h->pos] : 0xFFu;
    uint8_t miso = dev_miso(h->cs_mask, mosi, h->pos);
    if (h->rx) h->rx[h->pos] = miso;                /* DMA 처럼 한 바이트씩 — 중단되면 rx 는 일부만 채워져 있다 */
    if (++h->pos == h->len) { h->busy = false; h->done = true; h->irq_pending = true; }
}

/* ================= 드라이버 자료구조 ================= */
typedef struct { SpiXfer *slot[SPI_QCAP]; uint8_t head, count; } XferQ;

enum { EV_SUBMIT, EV_START, EV_DONE, EV_CANCEL, EV_TIMEOUT, EV_QFULL };
static const char *const EV_NAME[] = { "SUBMIT ", "START  ", "DONE   ", "CANCEL ", "TIMEOUT", "QFULL  " };
typedef struct { uint32_t t, seq; uint16_t len; uint8_t ev, cs, prio; } LogEnt;
#define SPI_LOG_CAP 64u

struct SpiBus {
    XferQ    q[SPI_NPRIO];
    SpiXfer *active;                 /* 지금 버스를 쓰는 디스크립터 (없으면 NULL) */
    uint32_t start_tick;             /* active 가 시작된 tick */
    uint32_t seq_next;
    bool     stopped;                /* spi_flush 진행 중 — 새 제출 거절 */
    MockSpi *hw;
    uint32_t n_done, n_cancel, n_timeout, n_qfull;
    LogEnt   log[SPI_LOG_CAP];
    uint32_t log_n;                  /* 누적 개수, 인덱스 = log_n % SPI_LOG_CAP */
};

void spi_log(SpiBus *b, uint8_t ev, const SpiXfer *x) {
    LogEnt *e = &b->log[b->log_n++ % SPI_LOG_CAP];
    e->t = now_ticks(); e->ev = ev; e->cs = x->cs; e->prio = x->prio; e->len = x->len; e->seq = x->seq;
}

/* 링 큐 push/pop (주어짐). 일부러 "count 를 읽고 → 일하고 → 써 넣는" 모양이라 중간에 ISR 이 끼면 깨진다. */
static bool q_push(XferQ *q, SpiXfer *x) {
    uint8_t n = q->count;
    if (n == SPI_QCAP) return false;
    q->slot[(q->head + n) % SPI_QCAP] = x;
    preempt_point();                               /* ← 크리티컬 섹션 밖이면 여기서 ISR 이 q_pop 할 수 있다 */
    q->count = (uint8_t)(n + 1u);
    return true;
}
static SpiXfer *q_pop(XferQ *q) {
    uint8_t n = q->count;
    if (n == 0u) return NULL;
    SpiXfer *x = q->slot[q->head];
    preempt_point();
    q->head = (uint8_t)((q->head + 1u) % SPI_QCAP);
    q->count = (uint8_t)(n - 1u);
    return x;
}

/* TODO B1: 큐 가운데에 있는 x 를 빼고 나머지 순서를 유지한다. 있으면 true. */
static bool q_remove(XferQ *q, SpiXfer *x) {
    for (uint8_t i = 0; i < q->count; i++) {
        if (q->slot[(q->head + i) % SPI_QCAP] != x) continue;
        uint8_t n = q->count;
        for (uint8_t j = i; j + 1u < n; j++) {                       /* 뒤의 원소를 한 칸씩 앞으로 */
            q->slot[(q->head + j) % SPI_QCAP] = q->slot[(q->head + j + 1u) % SPI_QCAP];
            preempt_point();                                        /* 연습 5: 이동 도중 ISR 이 q_pop 하면? */
        }
        q->count = (uint8_t)(n - 1u);
        return true;
    }
    return false;
}

void spi_init(SpiBus *b, MockSpi *h) {
    memset(b, 0, sizeof *b);
    b->hw = h;
    g_irq_bus = b;
}

/* 실행 중인 전송을 떼어 낸다 (주어짐): 필요하면 하드웨어 중단 → CS↑ → active = NULL.
 * 디스크립터는 XS_CALLBACK (콜백 대기) 상태가 된다 — 아직 호출자 것이 아니다. 콜백은 부르지 않는다. */
SpiXfer *spi_detach_active(SpiBus *b, bool abort_hw) {
    SpiXfer *x = b->active;
    if (x == NULL) return NULL;
    if (abort_hw) hw_abort(b->hw);                 /* 클럭을 먼저 멈추고 */
    hw_cs(b->hw, x->cs, false);                    /* 그다음 CS 해제 */
    b->active = NULL;
    x->state = XS_CALLBACK;
    return x;
}
/* 콜백 호출 (주어짐): 소유권을 호출자에게 돌려준 "뒤에" 부른다 → 콜백 안에서 같은 디스크립터 재제출 가능. */
void spi_complete(SpiXfer *x, int status) {
    x->state = XS_IDLE;
    x->cb(x->ctx, status);
}

/* TODO A1: 버스가 비어 있으면 가장 높은 우선순위 큐의 맨 앞을 시작한다.
 * (인터럽트 마스크 안에서 또는 ISR 에서만 불린다) */
void spi_start_next(SpiBus *b) {
    if (b->active != NULL) return;
    for (unsigned p = 0; p < SPI_NPRIO; p++) {
        SpiXfer *x = q_pop(&b->q[p]);
        if (x == NULL) continue;
        x->state = XS_ACTIVE;
        b->active = x;
        b->start_tick = now_ticks();
        hw_cs(b->hw, x->cs, true);                 /* CS↓ 먼저, 그다음 클럭 시작 */
        hw_start(b->hw, x->tx, x->rx, x->len);
        spi_log(b, EV_START, x);
        return;
    }
}

/* TODO A2: 인자 검사 → (마스크 안에서) 정지 중/이미 제출됨/큐 가득 검사 → 큐에 넣고 버스가 비었으면 시작. */
int spi_submit(SpiBus *b, SpiXfer *x) {
    if (b == NULL || x == NULL || x->cb == NULL || x->len == 0u || x->len > SPI_MAX_LEN ||
        x->cs >= SPI_N_CS || x->prio >= SPI_NPRIO || x->timeout == 0u)
        return SPI_E_ARG;
    int rc = SPI_OK;
    crit_enter();
    if (b->stopped) {
        rc = SPI_E_STOPPED;
    } else if (x->state != XS_IDLE) {
        rc = SPI_E_BUSY;                           /* 큐에 있거나, 실행 중이거나, 콜백 대기 중 */
    } else if (!q_push(&b->q[x->prio], x)) {
        rc = SPI_E_QFULL;
        b->n_qfull++;
        spi_log(b, EV_QFULL, x);
    } else {
        x->seq = b->seq_next++;
        x->t_submit = now_ticks();
        x->state = XS_QUEUED;
        spi_log(b, EV_SUBMIT, x);
        spi_start_next(b);                         /* 버스가 놀고 있을 때만 실제로 시작 */
    }
    crit_exit();
    return rc;
}

/* TODO A3: 완료 ISR. 완료 플래그 확인 → 떼어 내기 → 다음 것 시작 → 끝난 것의 콜백(DONE). */
void spi_isr(SpiBus *b) {
    if (!hw_ack(b->hw)) return;                    /* 래치된 완료 없음 = 가짜 IRQ */
    SpiXfer *x = spi_detach_active(b, false);
    if (x == NULL) return;
    b->n_done++;
    spi_log(b, EV_DONE, x);
    spi_start_next(b);                             /* 콜백보다 먼저: 버스 공백 최소 + 콜백의 재제출이 새치기 못 함 */
    spi_complete(x, SPI_ST_DONE);
}

/* TODO B2: 취소. 대기 중이면 큐에서 빼고, 실행 중이면 하드웨어 중단 + CS 해제 + 다음 시작.
 * 찾았으면 (마스크 밖에서) cb(CANCELLED) 를 정확히 한 번 부르고 SPI_OK, 아니면 SPI_E_NOTFOUND. */
int spi_cancel(SpiBus *b, SpiXfer *x) {
    if (b == NULL || x == NULL) return SPI_E_ARG;
    bool found = false;
    crit_enter();
    if (x->state == XS_QUEUED && x->prio < SPI_NPRIO && q_remove(&b->q[x->prio], x)) {
        x->state = XS_CALLBACK;
        found = true;
    } else if (x->state == XS_ACTIVE && b->active == x) {
        spi_detach_active(b, true);                /* 완료 IRQ 가 이미 대기 중이어도 hw_abort 가 지운다 */
        spi_start_next(b);
        found = true;
    }
    if (found) { b->n_cancel++; spi_log(b, EV_CANCEL, x); }
    crit_exit();
    if (!found) return SPI_E_NOTFOUND;
    spi_complete(x, SPI_ST_CANCELLED);
    return SPI_OK;
}

/* TODO B3: SysTick 마다 불리는 감시. 실행 중인 전송이 시작 후 timeout tick 이 지나면 중단 → 다음 시작 → cb(TIMEOUT). */
void spi_tick(SpiBus *b) {
    SpiXfer *x = NULL;
    crit_enter();
    if (b->active != NULL && (uint32_t)(now_ticks() - b->start_tick) >= b->active->timeout) {   /* 랩어라운드 안전 */
        x = spi_detach_active(b, true);
        b->n_timeout++;
        spi_log(b, EV_TIMEOUT, x);
        spi_start_next(b);
    }
    crit_exit();
    if (x != NULL) spi_complete(x, SPI_ST_TIMEOUT);
}

/* TODO C1: 전부 취소 (전원 절약 진입 · 버스 복구 전). 실행 중 1개 + 모든 대기열을 떼어 내고 각각 cb(CANCELLED).
 * 콜백이 재제출해도 끝나도록 진행 중에는 제출을 거절(SPI_E_STOPPED). 반환 = 취소한 개수 (중첩 호출이면 SPI_E_STOPPED). */
int spi_flush(SpiBus *b) {
    SpiXfer *list[SPI_NPRIO * SPI_QCAP + 1u];
    size_t n = 0;
    crit_enter();
    if (b->stopped) { crit_exit(); return SPI_E_STOPPED; }
    b->stopped = true;
    SpiXfer *a = spi_detach_active(b, true);
    if (a != NULL) list[n++] = a;
    for (unsigned p = 0; p < SPI_NPRIO; p++) {
        SpiXfer *x;
        while ((x = q_pop(&b->q[p])) != NULL) { x->state = XS_CALLBACK; list[n++] = x; }
    }
    for (size_t i = 0; i < n; i++) { b->n_cancel++; spi_log(b, EV_CANCEL, list[i]); }
    crit_exit();
    for (size_t i = 0; i < n; i++) spi_complete(list[i], SPI_ST_CANCELLED);   /* 마스크 밖에서 */
    crit_enter();
    b->stopped = false;
    crit_exit();
    return (int)n;
}

/* ================= IRQ 배선 (주어짐) ================= */
static void irq_dispatch_if_pending(void) {
    if (!g_hw.irq_pending || g_irq_bus == NULL) return;
    int was = g_in_isr;
    g_in_isr = 1;
    spi_isr(g_irq_bus);
    g_in_isr = was;
}

/* ================= 테스트 하네스 ================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

#define JOB_BUF 256u
#define N_JOBS  16
typedef struct Job {
    SpiXfer x;
    uint8_t tx[JOB_BUF], rx[JOB_BUF];
    int id;
    uint32_t accepted, cbs, done, cancelled, timeouts;
    int last_status;
    uint32_t submit_tick, cb_tick, lat_n, lat_max;
    uint64_t lat_sum;
    int resubmit;                  /* 콜백에서 다시 제출할 남은 횟수 */
    uint32_t cb_cost;              /* 콜백이 CPU 를 쓰는 시간 (tick) */
    struct Job *cancel_target;     /* 콜백에서 취소할 다른 잡 */
    bool flush_in_cb, in_submit;
    int flush_rc;
} Job;

static Job      g_jobs[N_JOBS];
static SpiBus   g_bus;
static uint32_t g_inv_bad, g_cb_in_crit, g_cb_bad_state, g_extra_cb, g_bad_rx, g_order_bad, g_resub_fail;
static uint32_t g_last_seq[SPI_NPRIO];
static bool     g_have_seq[SPI_NPRIO];
static int      g_order[64], g_order_n;
static bool     g_stress;
static uint32_t g_stress_seed;
static uint32_t g_s_busy, g_s_qfull, g_s_stopped, g_s_cancel_ok, g_s_cancel_nf, g_cb_early;

static int check_inv(const SpiBus *b) {
    const MockSpi *h = b->hw;
    int bad = 0;
    if (popcount8(h->cs_mask) > 1u) bad |= 1;
    if (b->active != NULL) {
        if (h->cs_mask != (uint8_t)(1u << b->active->cs)) bad |= 2;          /* 켜진 CS == 실행 중의 CS */
        if (b->active->state != XS_ACTIVE) bad |= 4;
        if (!h->busy && !h->irq_pending) bad |= 8;                            /* 실행 중인데 하드웨어가 놀고 있음 */
    } else {
        if (h->cs_mask != 0u || h->busy) bad |= 16;
        for (unsigned p = 0; p < SPI_NPRIO; p++) if (b->q[p].count) bad |= 32;  /* 버스가 노는데 대기열이 있음 = 멈춤 */
    }
    if (h->multi_cs || h->start_while_busy || h->start_without_cs || h->cs_cut) bad |= 64;
    for (unsigned p = 0; p < SPI_NPRIO; p++)
        for (uint8_t i = 0; i < b->q[p].count; i++) {
            const SpiXfer *x = b->q[p].slot[(b->q[p].head + i) % SPI_QCAP];
            if (x == NULL || x->state != XS_QUEUED || x->prio != p || x == b->active) bad |= 128;
        }
    for (int k = 0; k < N_JOBS; k++) if (g_jobs[k].x.state == XS_CALLBACK) bad |= 256;  /* 콜백 미전달 */
    return bad;
}

static int (*g_submit_fn)(SpiBus *, SpiXfer *) = spi_submit;
static int (*g_cancel_fn)(SpiBus *, SpiXfer *) = spi_cancel;   /* 연습 5 */
static bool rx_ok(const Job *j) {
    if (j->x.rx == NULL) return true;
    for (uint16_t i = 0; i < j->x.len; i++)
        if (j->x.rx[i] != dev_expect(j->x.cs, j->x.tx ? j->x.tx[i] : 0xFFu, i)) return false;
    return true;
}
static int job_submit(Job *j) {
    uint32_t old_tick = j->submit_tick;
    j->accepted++; j->submit_tick = g_now;      /* 먼저 기록: 콜백이 spi_submit 반환 "전에" 올 수 있다 */
    j->in_submit = true;
    int rc = g_submit_fn(&g_bus, &j->x);
    j->in_submit = false;
    if (rc != SPI_OK) { j->accepted--; j->submit_tick = old_tick; }
    if (rc == SPI_E_BUSY) g_s_busy++;
    else if (rc == SPI_E_QFULL) g_s_qfull++;
    else if (rc == SPI_E_STOPPED) g_s_stopped++;
    return rc;
}
/* 콜백(ISR 문맥)이 CPU 를 n tick 쓰는 동안: 하드웨어는 계속 돌고, 새 IRQ 는 ISR 이 끝날 때까지 대기 */
static void cpu_spend(uint32_t n) { while (n--) { g_now++; hw_tick(); } }
static void stress_cb(Job *j);
static void job_cb(void *ctx, int status) {
    Job *j = (Job *)ctx;
    if (g_crit != 0) g_cb_in_crit++;                       /* 인터럽트 마스크 안에서 콜백 = 금지 */
    if (j->x.state != XS_IDLE) g_cb_bad_state++;            /* 콜백 시점엔 디스크립터가 호출자 것이어야 */
    if (j->in_submit) g_cb_early++;                         /* spi_submit 이 아직 안 돌아왔는데 콜백 */
    uint32_t t_entry = g_now;
    cpu_spend(j->cb_cost);
    j->cbs++; j->last_status = status; j->cb_tick = t_entry;
    if (j->cbs > j->accepted) g_extra_cb++;
    if (status == SPI_ST_DONE || status == SPI_ST_TIMEOUT) {   /* 하드웨어를 탄 것은 우선순위별 제출 순서 */
        uint8_t p = j->x.prio;
        if (g_have_seq[p] && j->x.seq <= g_last_seq[p]) g_order_bad++;
        g_have_seq[p] = true; g_last_seq[p] = j->x.seq;
    }
    if (status == SPI_ST_DONE) {
        j->done++;
        if (!rx_ok(j)) g_bad_rx++;
        uint32_t lat = g_now - j->submit_tick;
        j->lat_n++; j->lat_sum += lat; if (lat > j->lat_max) j->lat_max = lat;
        if (g_order_n < 64) g_order[g_order_n++] = j->id;
    } else if (status == SPI_ST_CANCELLED) j->cancelled++;
    else j->timeouts++;
    if (j->flush_in_cb) { j->flush_in_cb = false; j->flush_rc = spi_flush(&g_bus); }
    if (j->cancel_target) { Job *t = j->cancel_target; j->cancel_target = NULL; (void)spi_cancel(&g_bus, &t->x); }
    if (j->resubmit > 0) { j->resubmit--; if (job_submit(j) != SPI_OK) g_resub_fail++; }
    if (g_stress) stress_cb(j);
}
static void job_config(Job *j, uint8_t cs, uint8_t prio, uint16_t len, uint32_t timeout) {
    j->x.cs = cs; j->x.prio = prio; j->x.len = len; j->x.timeout = timeout;
    for (uint16_t i = 0; i < JOB_BUF; i++) { j->tx[i] = (uint8_t)(j->id * 17 + i * 3 + 1); j->rx[i] = 0xEE; }
    j->x.tx = j->tx; j->x.rx = j->rx; j->x.cb = job_cb; j->x.ctx = j;
}
static void world_reset(void) {
    memset(&g_hw, 0, sizeof g_hw);
    memset(g_jobs, 0, sizeof g_jobs);
    for (int k = 0; k < N_JOBS; k++) g_jobs[k].id = k;
    g_now = 0; g_crit = 0; g_in_isr = 0; g_pre_permille = 0; g_preempts = 0;
    g_inv_bad = g_cb_in_crit = g_cb_bad_state = g_extra_cb = g_bad_rx = g_order_bad = g_resub_fail = 0;
    g_s_busy = g_s_qfull = g_s_stopped = g_s_cancel_ok = g_s_cancel_nf = g_cb_early = 0;
    memset(g_have_seq, 0, sizeof g_have_seq);
    g_order_n = 0; g_stress = false;
    spi_init(&g_bus, &g_hw);
}
static void sim_tick(void) {
    g_now++;
    hw_tick();                                           /* 하드웨어 1 tick */
    irq_dispatch_if_pending();                           /* SPI 완료 IRQ */
    g_in_isr = 1; spi_tick(&g_bus); g_in_isr = 0;        /* SysTick ISR: 타임아웃 감시 */
    g_inv_bad |= (uint32_t)check_inv(&g_bus);
}
static void run(uint32_t n) { while (n--) sim_tick(); }
static bool bus_idle(void) { return g_bus.active == NULL && g_bus.q[0].count == 0 && g_bus.q[1].count == 0; }
static void run_until_idle(uint32_t cap) { while (!bus_idle() && cap--) sim_tick(); }
static bool order_is(const int *ids, int n) {
    if (g_order_n != n) return false;
    for (int i = 0; i < n; i++) if (g_order[i] != ids[i]) return false;
    return true;
}
static void log_dump(const SpiBus *b) {
    uint32_t from = b->log_n > SPI_LOG_CAP ? b->log_n - SPI_LOG_CAP : 0u;
    for (uint32_t i = from; i < b->log_n; i++) {
        const LogEnt *e = &b->log[i % SPI_LOG_CAP];
        printf("  t=%3u %s cs%u p%u len%3u seq %u\n", (unsigned)e->t, EV_NAME[e->ev], (unsigned)e->cs,
               (unsigned)e->prio, (unsigned)e->len, (unsigned)e->seq);
    }
}
static void log_kinds(const SpiBus *b, char *out, size_t cap) {      /* 이벤트 종류만 한 글자씩: S T D C O Q */
    size_t k = 0;
    for (uint32_t i = 0; i < b->log_n && i < SPI_LOG_CAP && k + 1 < cap; i++) out[k++] = "STDCOQ"[b->log[i].ev];
    out[k] = '\0';
}

/* ---- Part C: IMU 1 kHz (CS0) + 플래시 청크 연속 읽기 (CS1) 시나리오 ---- */
typedef struct { uint32_t imu_n, imu_max, imu_overrun, flash_chunks, flash_bytes; double imu_mean; } AppStat;
static AppStat app_run(uint32_t ticks, uint8_t imu_prio, uint16_t chunk, int depth, int chunks_per_job) {
    AppStat s = { 0, 0, 0, 0, 0, 0.0 };
    Job *imu = &g_jobs[0];
    job_config(imu, 0, imu_prio, 7, HW_SETUP_TICKS + 7u + 40u);              /* STATUS + XYZ 7 바이트 burst */
    for (int k = 1; k <= depth; k++) {                                        /* 플래시: depth 개 디스크립터가 돌며 */
        job_config(&g_jobs[k], 1, 1, chunk, HW_SETUP_TICKS + chunk + 40u);    /* 콜백 안에서 다음 청크를 재제출 */
        g_jobs[k].resubmit = chunks_per_job - 1;
        (void)job_submit(&g_jobs[k]);
    }
    for (uint32_t t = 0; t < ticks; t++) {
        if (g_now % 1000u == 500u) {                                          /* 1 kHz 샘플 타이머 */
            if (imu->x.state != XS_IDLE) s.imu_overrun++;
            else (void)job_submit(imu);
        }
        sim_tick();
    }
    s.imu_n = imu->done; s.imu_max = imu->lat_max;
    s.imu_mean = imu->lat_n ? (double)imu->lat_sum / imu->lat_n : 0.0;
    for (int k = 1; k <= depth; k++) s.flash_chunks += g_jobs[k].done;
    s.flash_bytes = s.flash_chunks * chunk;
    return s;
}

/* ---- Part C: 랜덤 스트레스 (태스크 + 콜백에서 제출/취소, 멈춤·지연 주입, ISR 선점 주입) ---- */
static void stress_config(Job *j) {
    uint16_t len = (uint16_t)(1u + xs32(&g_stress_seed) % 32u);
    uint32_t r = xs32(&g_stress_seed);
    uint32_t to = (r % 8u == 0u) ? 1u + (r >> 8) % (HW_SETUP_TICKS + len)   /* 1/8: 일부러 빠듯한 타임아웃 */
                                 : HW_SETUP_TICKS + len + 12u;
    job_config(j, (uint8_t)(xs32(&g_stress_seed) % SPI_N_CS), (uint8_t)(j->id < 4 ? 0 : 1), len, to);
    for (uint16_t i = 0; i < len; i++) j->tx[i] = (uint8_t)xs32(&g_stress_seed);
    if (xs32(&g_stress_seed) % 16u == 0u) j->x.tx = NULL;
    if (xs32(&g_stress_seed) % 16u == 0u) j->x.rx = NULL;
}
static void stress_cb(Job *j) {
    uint32_t r = xs32(&g_stress_seed) % 100u;
    Job *o = &g_jobs[xs32(&g_stress_seed) % N_JOBS];
    if (r < 30) { if (j->x.state == XS_IDLE) stress_config(j); (void)job_submit(j); }      /* 자기 재제출 */
    else if (r < 40) { if (spi_cancel(&g_bus, &o->x) == SPI_OK) g_s_cancel_ok++; else g_s_cancel_nf++; }
    else if (r < 50) { if (o->x.state == XS_IDLE) stress_config(o); (void)job_submit(o); }
}
typedef struct { uint32_t accepted, cbs, done, cancelled, timeouts, mismatch, not_idle; } StressStat;
static StressStat stress(uint32_t seed, uint32_t ticks, uint32_t preempt_permille) {
    world_reset();
    g_stress = true; g_stress_seed = seed; g_pre_seed = seed ^ 0xA5A5A5A5u; g_pre_permille = preempt_permille;
    for (uint32_t t = 0; t < ticks; t++) {
        uint32_t r = xs32(&g_stress_seed) % 1000u;
        Job *j = &g_jobs[xs32(&g_stress_seed) % N_JOBS];
        if (r < 250) { if (j->x.state == XS_IDLE) stress_config(j); (void)job_submit(j); }
        else if (r < 290) { if (g_cancel_fn(&g_bus, &j->x) == SPI_OK) g_s_cancel_ok++; else g_s_cancel_nf++; }
        else if (r < 300) g_hw.stuck_next = true;
        else if (r < 310) g_hw.irq_pending = true;           /* 가짜 IRQ (공유 벡터·잡음) */
        else if (r < 400) g_hw.extra_next = (uint8_t)(xs32(&g_stress_seed) % 4u);
        else if (r < 401) (void)spi_flush(&g_bus);
        preempt_point();                                  /* 태스크가 다른 일을 하는 동안에도 ISR 이 끼어든다 */
        sim_tick();
    }
    (void)spi_flush(&g_bus);                              /* 마무리: 남은 것 전부 취소 */
    run(4);
    g_stress = false; g_pre_permille = 0;
    StressStat s = { 0, 0, 0, 0, 0, 0, 0 };
    for (int k = 0; k < N_JOBS; k++) {
        const Job *j = &g_jobs[k];
        s.accepted += j->accepted; s.cbs += j->cbs; s.done += j->done; s.cancelled += j->cancelled; s.timeouts += j->timeouts;
        if (j->accepted != j->cbs) s.mismatch++;
        if (j->x.state != XS_IDLE) s.not_idle++;
    }
    return s;
}

/* ================= 추가 연습 ================= */

/* ---- 연습 1: 동기 래퍼 spi_transfer_sync (Linux spi_sync 가 spi_async 위에 있는 것과 같은 구조) ---- */
#define SPI_E_CONTEXT   (-6)    /* ISR/콜백 문맥에서 블로킹 호출 */
#define SPI_E_TIMEOUT   (-7)
#define SPI_E_CANCELLED (-8)
typedef struct { volatile bool done; int status; } SyncWait;
static void sync_cb(void *ctx, int status) { SyncWait *w = (SyncWait *)ctx; w->status = status; w->done = true; }
static uint32_t g_sync_waited;
int spi_transfer_sync(SpiBus *b, uint8_t cs, const uint8_t *tx, uint8_t *rx, uint16_t len,
                      uint32_t timeout, uint32_t max_wait) {
    if (g_in_isr) return SPI_E_CONTEXT;                   /* 콜백 안에서 기다리면 완료 ISR 이 영원히 안 온다 */
    SyncWait w = { false, -1 };
    SpiXfer x;                                            /* 스택 디스크립터: 돌아가기 전에 반드시 드라이버 손을 떠나야 */
    memset(&x, 0, sizeof x);
    x.cs = cs; x.prio = 0; x.len = len; x.tx = tx; x.rx = rx; x.timeout = timeout; x.cb = sync_cb; x.ctx = &w;
    int rc = spi_submit(b, &x);
    if (rc != SPI_OK) return rc;
    g_sync_waited = 0;
    while (!w.done && g_sync_waited < max_wait) { sim_tick(); g_sync_waited++; }   /* RTOS: xSemaphoreTake(sem, max_wait) */
    if (!w.done) (void)spi_cancel(b, &x);                 /* 큐에 스택 주소를 남기고 돌아가면 dangling */
    if (w.status == SPI_ST_DONE) return SPI_OK;
    return w.status == SPI_ST_TIMEOUT ? SPI_E_TIMEOUT : SPI_E_CANCELLED;
}
static int g_sync_from_cb_rc;
static void cb_calls_sync(void *ctx, int status) {
    (void)ctx; (void)status;
    uint8_t rx[2];
    g_sync_from_cb_rc = spi_transfer_sync(&g_bus, 0, NULL, rx, 2, 50, 100);
}

/* ---- 연습 2: "너무 늦음" 취소 정책 — 실행 중이면 건드리지 않고 SPI_E_BUSY (플래시 쓰기처럼 중단이 위험할 때) ---- */
int spi_cancel_soft(SpiBus *b, SpiXfer *x) {
    if (b == NULL || x == NULL) return SPI_E_ARG;
    crit_enter();
    bool running = (b->active == x);
    crit_exit();
    if (running) return SPI_E_BUSY;                      /* 정상 완료(DONE) 콜백을 기다린다 */
    return spi_cancel(b, x);                             /* 대기 중이면 기존과 같다 (그 사이 시작됐으면 abort 정책) */
}

/* ---- 연습 3: 큐 대기 데드라인 — 너무 오래 기다린 요청은 시작하지 말고 버린다 (오래된 센서 읽기는 쓸모없다) ---- */
int spi_expire_queued(SpiBus *b, uint32_t max_age) {
    SpiXfer *list[SPI_NPRIO * SPI_QCAP];
    size_t n = 0;
    crit_enter();
    for (unsigned p = 0; p < SPI_NPRIO; p++) {
        XferQ *q = &b->q[p];
        for (uint8_t i = 0; i < q->count; ) {
            SpiXfer *x = q->slot[(q->head + i) % SPI_QCAP];
            if ((uint32_t)(now_ticks() - x->t_submit) > max_age && q_remove(q, x)) {
                x->state = XS_CALLBACK; list[n++] = x; b->n_timeout++; spi_log(b, EV_TIMEOUT, x);
            } else i++;
        }
    }
    crit_exit();
    for (size_t i = 0; i < n; i++) spi_complete(list[i], SPI_ST_TIMEOUT);
    return (int)n;
}

/* ---- 연습 4: 최악 IMU 지연 공식 ---- */
static uint32_t imu_bound(bool prio, uint16_t chunk, int depth) {
    uint32_t flash = HW_SETUP_TICKS + chunk, imu = HW_SETUP_TICKS + 7u;
    return (prio ? flash : (uint32_t)depth * flash) + imu;
}

/* ---- 연습 5: 크리티컬 섹션 없는 취소 (q_remove 안 선점 지점이 경쟁을 드러낸다) ---- */
static int spi_cancel_nocrit(SpiBus *b, SpiXfer *x) {
    if (b == NULL || x == NULL) return SPI_E_ARG;
    bool found = false;
    if (x->state == XS_QUEUED && x->prio < SPI_NPRIO && q_remove(&b->q[x->prio], x)) { x->state = XS_CALLBACK; found = true; }
    else if (x->state == XS_ACTIVE && b->active == x) { spi_detach_active(b, true); spi_start_next(b); found = true; }
    if (!found) return SPI_E_NOTFOUND;
    b->n_cancel++;
    spi_complete(x, SPI_ST_CANCELLED);
    return SPI_OK;
}

int main(void) {
    /* 연습 1 */
    world_reset();
    {
        uint8_t tx[5] = { 1, 2, 3, 4, 5 }, rx[5] = { 0 };
        CHECK(spi_transfer_sync(&g_bus, 2, tx, rx, 5, 50, 100) == SPI_OK);
        CHECK(rx[0] == dev_expect(2, 1, 0) && rx[4] == dev_expect(2, 5, 4) && g_sync_waited == HW_SETUP_TICKS + 5u);
        job_config(&g_jobs[0], 1, 0, 30, 100);                       /* 버스가 바쁘면 앞의 것이 끝날 때까지 기다린다 */
        (void)job_submit(&g_jobs[0]);
        CHECK(spi_transfer_sync(&g_bus, 2, tx, rx, 5, 50, 100) == SPI_OK && g_sync_waited == 32u + 7u);
        (void)job_submit(&g_jobs[0]);                                /* 이번엔 기다릴 수 있는 시간(10)이 모자라다 */
        CHECK(spi_transfer_sync(&g_bus, 2, tx, rx, 5, 50, 10) == SPI_E_CANCELLED);
        CHECK(g_bus.q[0].count == 0 && g_bus.active == &g_jobs[0].x);  /* 스택 디스크립터는 큐에서 빠졌다 */
        run_until_idle(100);
        CHECK(g_jobs[0].done == 2 && g_inv_bad == 0);
        g_hw.stuck_next = true;                                      /* 하드웨어 멈춤 → 드라이버 타임아웃 */
        CHECK(spi_transfer_sync(&g_bus, 3, tx, rx, 5, 20, 100) == SPI_E_TIMEOUT && g_sync_waited == 20);
        job_config(&g_jobs[1], 1, 1, 4, 100); g_jobs[1].x.cb = cb_calls_sync;
        (void)spi_submit(&g_bus, &g_jobs[1].x);
        run_until_idle(100);
        CHECK(g_sync_from_cb_rc == SPI_E_CONTEXT);                   /* 콜백(ISR)에서 블로킹 금지 */
    }

    /* 연습 2 */
    world_reset();
    job_config(&g_jobs[0], 0, 1, 20, 100); job_config(&g_jobs[1], 1, 1, 4, 100);
    (void)job_submit(&g_jobs[0]); (void)job_submit(&g_jobs[1]);
    run(5);
    CHECK(spi_cancel_soft(&g_bus, &g_jobs[0].x) == SPI_E_BUSY && g_jobs[0].cbs == 0 && g_hw.aborts == 0);
    CHECK(spi_cancel_soft(&g_bus, &g_jobs[1].x) == SPI_OK && g_jobs[1].last_status == SPI_ST_CANCELLED);
    run_until_idle(100);
    CHECK(g_jobs[0].last_status == SPI_ST_DONE && rx_ok(&g_jobs[0]) && g_jobs[0].cbs == 1 && g_hw.starts == 1);

    /* 연습 3 */
    world_reset();
    job_config(&g_jobs[0], 1, 1, 100, 200);                          /* 102 tick 동안 버스 점유 */
    for (int k = 1; k <= 3; k++) job_config(&g_jobs[k], 0, 0, 7, 50);
    (void)job_submit(&g_jobs[0]); (void)job_submit(&g_jobs[1]);
    run(30); (void)job_submit(&g_jobs[2]);
    run(30); (void)job_submit(&g_jobs[3]);
    run(30);                                                         /* t=90: 나이 90, 60, 30 */
    CHECK(spi_expire_queued(&g_bus, 50) == 2);
    CHECK(g_jobs[1].last_status == SPI_ST_TIMEOUT && g_jobs[2].last_status == SPI_ST_TIMEOUT && g_jobs[3].cbs == 0);
    run_until_idle(200);
    CHECK(g_jobs[3].done == 1 && g_jobs[1].done == 0 && g_hw.starts == 2 && g_inv_bad == 0);
    {
        static const int ord[] = { 0, 3 };
        char kinds[40];
        printf("expire log:\n");
        log_dump(&g_bus);
        log_kinds(&g_bus, kinds, sizeof kinds);
        CHECK(order_is(ord, 2) && strcmp(kinds, "STSSSOODTD") == 0);
    }

    /* 연습 4 */
    {
        static const uint16_t chunks[3] = { 16, 64, 256 };
        int over = 0, viol = 0;
        printf("mode depth chunk | imu max  bound | overrun\n");
        for (int pm = 0; pm < 2; pm++)
            for (int d = 1; d <= 4; d += 3)
                for (int c = 0; c < 3; c++) {
                    world_reset();
                    AppStat s = app_run(50000, (uint8_t)(pm ? 0 : 1), chunks[c], d, 1 << 20);
                    uint32_t bd = imu_bound(pm != 0, chunks[c], d);
                    printf("%s %5d %5u | %7u %6u | %u\n", pm ? "prio" : "fifo", d, (unsigned)chunks[c],
                           (unsigned)s.imu_max, (unsigned)bd, (unsigned)s.imu_overrun);
                    if (s.imu_max > bd) viol++;
                    over += (int)s.imu_overrun;
                    if (g_bad_rx || g_order_bad || g_inv_bad) viol++;
                }
        CHECK(viol == 0);
        CHECK(over > 0);                     /* fifo, depth 4, 256 B: 최악 1041 > 1000 µs 주기 → 샘플을 놓친다 */
    }

    /* 연습 5 */
    {
        StressStat a = stress(0x5EED0005u, 60000u, 300);
        uint32_t inv_a = g_inv_bad, extra_a = g_extra_cb;
        g_cancel_fn = spi_cancel_nocrit;
        StressStat b = stress(0x5EED0005u, 60000u, 300);
        uint32_t inv_b = g_inv_bad, extra_b = g_extra_cb;
        g_cancel_fn = spi_cancel;
        printf("cancel with crit   : inv=0x%03x mismatch=%u extra_cb=%u\n", (unsigned)inv_a, (unsigned)a.mismatch, (unsigned)extra_a);
        printf("cancel without crit: inv=0x%03x mismatch=%u extra_cb=%u\n", (unsigned)inv_b, (unsigned)b.mismatch, (unsigned)extra_b);
        CHECK(inv_a == 0 && a.mismatch == 0 && extra_a == 0);
        CHECK(inv_b != 0 || b.mismatch != 0 || extra_b != 0);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
