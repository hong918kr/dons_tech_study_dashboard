/* DS-A02 ISR↔태스크 링 버퍼와 Bip Buffer(DMA 연속 영역)
 *
 * 맥에는 인터럽트가 없으므로 "결정적 단일 스레드 인터리버" 로 ISR 을 흉내 낸다.
 *   공유 메모리 접근(IDX_LOAD/IDX_STORE/BYTE_RD/BYTE_WR)마다 STEP 이 하나씩 있고, 테스트는
 *   "k 번째 접근 직전에 ISR 을 끝까지 실행" 을 k = 0, 1, 2, ... 전부에 대해 돌린다. ISR 은 태스크를 선점하고
 *   끝까지 돈 뒤 돌아온다 = 단일 코어 Cortex-M 과 같은 구조. 스레드가 없으니 결과는 매번 같다.
 *
 *   Part A  단일 코어 ISR ↔ 태스크 바이트 링: RX(ISR 생산자) · TX(ISR 소비자), 배리어 위치 검사,
 *           컴파일러/CPU 가 순서를 바꾼 "나쁜 변형" 이 실제로 깨지는 k 를 찾아낸다.
 *   Part B  Bip buffer: reserve / publish / commit / read / release, 모든 랩 시나리오,
 *           도달 가능한 모든 (r, w, wm) 상태의 전수 탐색, 공간 보장 정리 N >= D + 2M.
 *   Part C  UART RX DMA (예약 영역에 직접 수신) + TX DMA (scatter 없이 연속 영역 전송), ISR 을 매 접근마다 발사.
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so   (starter.c 도 같은 명령)
 */
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(e) do { g_total++; if (e) { g_pass++; printf("PASS %s\n", #e); } \
                      else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #e); } while (0)

static uint32_t g_rng = 0x2545F491u;
static uint32_t rnd(void) { uint32_t x = g_rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; g_rng = x; return x; }

/* ---------- 인터리버: "ISR 을 언제 발사할까" ----------
 * at/period : step == at, at+period, at+2*period ... 에서 발사 (period 0 = 한 번만). NEVER = 안 함.
 * pct       : 매 step 마다 pct% 확률로 발사 (100 = 모든 접근 앞에서).
 * mask      : IRQ_OFF() 중이면 발사를 pending 으로 미룬다 (PRIMASK 모사) → IRQ_ON() 에서 곧바로 발사. */
#define NEVER 0xFFFFFFFFu
typedef void (*isr_fn_t)(void *);
static struct {
    isr_fn_t fn; void *arg;
    uint32_t step, at, period, pct;
    bool     in_isr, pending;
    uint32_t mask;
    uint64_t fired;
    char     trace[96];
    uint32_t tlen;
    bool     tracing;
} g_ic;

static void ic_off(void) { g_ic.fn = NULL; g_ic.pct = 0; g_ic.at = NEVER; g_ic.period = 0; g_ic.pending = false; g_ic.mask = 0; }
static void ic_arm(isr_fn_t fn, void *arg, uint32_t at, uint32_t period, uint32_t pct) {
    g_ic.fn = fn; g_ic.arg = arg; g_ic.step = 0; g_ic.at = at; g_ic.period = period; g_ic.pct = pct;
    g_ic.fired = 0; g_ic.pending = false; g_ic.mask = 0;
}
static void ic_fire(void) {
    if (!g_ic.fn || g_ic.in_isr) return;
    g_ic.in_isr = true; g_ic.fired++; g_ic.fn(g_ic.arg); g_ic.in_isr = false;
}
static void ic_step(void) {
    if (g_ic.in_isr || !g_ic.fn) return;
    uint32_t s = g_ic.step++;
    bool want = false;
    if (g_ic.at != NEVER && s >= g_ic.at)
        want = (s == g_ic.at) || (g_ic.period && (s - g_ic.at) % g_ic.period == 0);
    if (g_ic.pct && rnd() % 100u < g_ic.pct) want = true;
    if (!want) return;
    if (g_ic.mask) { g_ic.pending = true; return; }
    ic_fire();
}
#define IRQ_OFF() do { g_ic.mask++; } while (0)
#define IRQ_ON()  do { if (--g_ic.mask == 0 && g_ic.pending) { g_ic.pending = false; ic_fire(); } } while (0)

static void tr(char c) { if (g_ic.tracing && !g_ic.in_isr && g_ic.tlen + 1 < sizeof g_ic.trace) g_ic.trace[g_ic.tlen++] = c; }
static void trace_begin(void) { g_ic.tracing = true; g_ic.tlen = 0; g_ic.trace[0] = 0; }
static const char *trace_end(void) { g_ic.tracing = false; g_ic.trace[g_ic.tlen] = 0; return g_ic.trace; }

/* ---------- 계측된 공유 메모리 접근. 공유 변수는 반드시 이것으로만 만진다 ----------
 * 인덱스는 relaxed atomic (ISR 이 찢어진 값을 볼 일이 없고 컴파일러가 접근을 지우거나 합치지 못한다).
 * 데이터 바이트는 일반 메모리. 순서는 아래 배리어가 잡는다. trace 문자: L/S = 인덱스 load/store, r/w = 바이트, | = 배리어 */
__attribute__((unused)) static uint32_t IDX_LOAD(const _Atomic uint32_t *p) { ic_step(); tr('L'); return atomic_load_explicit(p, memory_order_relaxed); }
__attribute__((unused)) static void IDX_STORE(_Atomic uint32_t *p, uint32_t v) { ic_step(); tr('S'); atomic_store_explicit(p, v, memory_order_relaxed); }
static const uint8_t *g_guard_lo, *g_guard_hi;   /* 지금 시험 중인 버퍼. 벗어난 바이트 접근은 하지 않고 센다 (틀린 구현이 크래시 대신 FAIL 하도록) */
static uint32_t g_wild;
__attribute__((unused)) static uint8_t BYTE_RD(const uint8_t *p) { ic_step(); tr('r'); if (p < g_guard_lo || p >= g_guard_hi) { g_wild++; return 0; } return *p; }
__attribute__((unused)) static void BYTE_WR(uint8_t *p, uint8_t v) { ic_step(); tr('w'); if (p < g_guard_lo || p >= g_guard_hi) { g_wild++; return; } *p = v; }

/* 컴파일러 배리어: 단일 코어 ISR 에서는 이것이면 충분하다 (명령 0개, demo_asm.c 의 @MEMBARRIER).
 * 코어가 둘 이상이면 이 두 매크로를 atomic_thread_fence(...) 로 바꾼다 — 한 줄 차이다 (§5 표). */
#define ACQ_BARRIER() do { atomic_signal_fence(memory_order_acquire); tr('|'); } while (0)
#define REL_BARRIER() do { atomic_signal_fence(memory_order_release); tr('|'); } while (0)

/* ======================= Part A — 단일 코어 ISR ↔ 태스크 바이트 링 ======================= */
#define RB_N    8u
#define RB_MASK (RB_N - 1u)
_Static_assert((RB_N & (RB_N - 1u)) == 0, "RB_N must be a power of two");

typedef struct {
    _Atomic uint32_t head;    /* 자유 증가. 생산자만 쓴다 */
    _Atomic uint32_t tail;    /* 자유 증가. 소비자만 쓴다 */
    uint32_t dropped;         /* 생산자만 쓴다 */
    uint8_t  buf[RB_N];
} rb_t;

static void rb_init(rb_t *q) { atomic_init(&q->head, 0u); atomic_init(&q->tail, 0u); q->dropped = 0; memset(q->buf, 0xEE, RB_N); g_guard_lo = q->buf; g_guard_hi = q->buf + RB_N; }

/* RX 방향: ISR 생산자. ISR 안에서는 소비자가 끼어들 수 없다. 가득 차면 버리고 센다 (ISR 은 기다리지 않는다). */
static bool rb_isr_put(rb_t *q, uint8_t v) {
    /* TODO: h, t 를 IDX_LOAD → 가득(h - t == RB_N)이면 dropped++, false.
     *       BYTE_WR(칸, v) → REL_BARRIER() → IDX_STORE(head, h + 1) → true */
    (void)q; (void)v;
    return false;
}

/* RX 방향: 태스크 소비자. 어느 접근 앞에서든 ISR 이 끼어들 수 있다 → 배리어 2개가 여기서 일을 한다. */
static bool rb_task_get(rb_t *q, uint8_t *out) {
    /* TODO: t, h 를 IDX_LOAD → 비었으면 false → ACQ_BARRIER() → BYTE_RD(칸) → REL_BARRIER() → IDX_STORE(tail, t + 1) */
    (void)q; (void)out;
    return false;
}

/* TX 방향: 태스크 생산자. 이번엔 태스크가 "게시" 하는 쪽 → 칸 쓰기 뒤에 REL_BARRIER 가 일을 한다. */
static bool rb_task_put(rb_t *q, uint8_t v) {
    /* TODO: h, t IDX_LOAD → 가득이면 false → ACQ_BARRIER() → BYTE_WR(칸, v) → REL_BARRIER() → IDX_STORE(head, h + 1) */
    (void)q; (void)v;
    return false;
}

/* TX 방향: ISR 소비자. */
static bool rb_isr_get(rb_t *q, uint8_t *out) {
    /* TODO: rb_task_get 과 같은 순서 (ISR 안이라 끼어들 상대가 없지만, 코어가 늘어도 맞게 둔다) */
    (void)q; (void)out;
    return false;
}

/* ======================= Part B — Bip buffer ======================= */
/* 상태 3개 (공유) + 비공개 예약/읽기 상태.
 *   안 감김 (w >= r): 유효 데이터 [r, w).                              빈 곳: [w, cap) 와 [0, r)
 *   감김   (w <  r): 유효 데이터 [r, wm) (영역 A) + [0, w) (영역 B).    빈 곳: [w, r) (w 는 r 을 따라잡으면 안 된다)
 *   [wm, cap) = 버려진 구멍 (예약이 꼬리에 안 들어가서 건너뛴 곳). wm 은 감긴 동안에만 뜻이 있다.
 *   쓰기 주체: r 은 소비자, w·wm 은 생산자. cap 은 2의 거듭제곱일 필요가 없다 (마스크를 안 쓴다). */
typedef struct {
    _Atomic uint32_t r, w, wm;
    uint8_t *buf;
    uint32_t cap;
    /* 생산자 전용 */
    bool res_active, res_wrap;    /* res_wrap: 이 예약이 0 번지로 감겼고 아직 wm 을 게시하지 않았다 */
    uint32_t res_start, res_len;
    /* 소비자 전용 */
    bool rd_active;
    uint32_t rd_len;
} bip_t;

static void bip_init(bip_t *b, uint8_t *mem, uint32_t cap) {
    memset(b, 0, sizeof *b);
    atomic_init(&b->r, 0u); atomic_init(&b->w, 0u); atomic_init(&b->wm, 0u);
    b->buf = mem; b->cap = cap;
    g_guard_lo = mem; g_guard_hi = mem + cap;
}

/* n 바이트 연속 영역 예약. 꼬리에 들어가면 꼬리, 아니면 0 번지(n < r 일 때만). 없으면 NULL. 예약 중 재호출도 NULL.
 * 공유 상태는 아무것도 바꾸지 않는다. */
static uint8_t *bip_reserve(bip_t *b, uint32_t n) {
    /* TODO: res_active / n == 0 / n > cap 이면 NULL.
     *       w, r 을 IDX_LOAD → ACQ_BARRIER()
     *       감김(w < r): n < r - w 여야 시작 = w  (w 가 r 과 같아지면 "빈 것" 과 구분이 안 된다)
     *       안 감김: cap - w >= n 이면 시작 = w,  아니면 n < r 이면 시작 = 0 (res_wrap = true)
     *       res_active/res_start/res_len/res_wrap 을 저장하고 buf + start 반환 */
    (void)b; (void)n;
    return NULL;
}

/* DMA 용: 지금 줄 수 있는 가장 큰 연속 영역 (최소 min_n, 최대 max_n; max_n == 0 이면 제한 없음).
 * 꼬리가 min_n 이상이면 꼬리를 쓰고, 아니면 0 번지 쪽 r-1 바이트. 없으면 NULL, *got = 0. */
static uint8_t *bip_reserve_max(bip_t *b, uint32_t min_n, uint32_t max_n, uint32_t *got) {
    /* TODO: bip_reserve 와 같은 분기. 감김: r - w - 1, 안 감김: 꼬리 cap - w, 모자라면 앞쪽 r - 1(r > 0).
     *       min_n 을 못 채우면 NULL. max_n != 0 이면 n 을 max_n 으로 자른다. 예약을 기록 (min_n 이 0 이면 1 로 친다) */
    (void)b; (void)min_n; (void)max_n;
    *got = 0;
    return NULL;
}

/* 예약의 앞 k 바이트를 게시하고 나머지는 계속 예약 상태로 둔다 (k 가 남은 길이보다 크면 false).
 * DMA 진행 중 IDLE 에서 "받은 만큼만 먼저 공개" 할 때 쓴다. */
static bool bip_publish(bip_t *b, uint32_t k) {
    /* TODO: 예약 없음 / k > res_len → false. k == 0 → true (아무것도 안 함).
     *       감긴 예약의 첫 게시면: 옛 w 를 wm 에 먼저 저장 → REL_BARRIER() → res_wrap = false
     *       그다음 REL_BARRIER() → IDX_STORE(w, res_start + k) → res_start += k, res_len -= k */
    (void)b; (void)k;
    return false;
}

/* 앞 k 바이트 게시 + 예약 종료. 나머지는 돌려준다. k == 0 은 취소. */
static bool bip_commit(bip_t *b, uint32_t k) {
    /* TODO: bip_publish 가 실패하면 false. 성공하면 res_active = false, res_len = 0 */
    (void)b; (void)k;
    return false;
}

/* 지금 연속으로 읽을 수 있는 첫 덩어리. 없으면 NULL. 소비자가 이미 들고 있으면 NULL.
 * 감긴 상태에서 r == wm (영역 A 가 비었음) 이면 r 을 0 으로 옮기고 영역 B 를 돌려준다 (늦은 점프). */
static const uint8_t *bip_read(bip_t *b, uint32_t *n) {
    /* TODO: *n = 0. rd_active 면 NULL.
     *       r, w 순서로 IDX_LOAD (wm 은 w 가 "감김" 을 말한 뒤에 읽는다) → ACQ_BARRIER()
     *       감김(w < r): wm 을 읽고 r == wm 이면 r = 0 저장, limit = w. 아니면 limit = wm
     *       안 감김: limit = w.  limit == r 이면 NULL.  rd_active, rd_len, *n 저장, buf + r 반환 */
    (void)b;
    *n = 0;
    return NULL;
}

/* 읽은 덩어리의 앞 m 바이트 반납. 감긴 상태에서 r 이 wm 에 닿으면 곧장 r = 0 (빠른 점프 → 구멍을 일찍 되돌린다). */
static bool bip_release(bip_t *b, uint32_t m) {
    /* TODO: rd_active 아님 / m > rd_len → false. rd_active = false. m == 0 → true.
     *       nr = r + m. w 를 읽어 w < nr 이면 wm 을 읽고 nr == wm 일 때 nr = 0.
     *       REL_BARRIER() → IDX_STORE(r, nr) (바이트를 다 읽은 뒤에 공간을 내준다) */
    (void)b; (void)m;
    return false;
}

/* ======================= Part C — UART DMA ======================= */
/* 하드웨어 모델. STM32 의 CMAR/CNDTR 처럼 메모리 주소와 "남은 전송 수" 만 있다. */
typedef struct { uint8_t *mar; const uint8_t *src; uint32_t ndtr; bool en; } dma_t;
static uint32_t g_dma_starts;
__attribute__((unused)) static void dma_start(dma_t *d, uint8_t *mar, const uint8_t *src, uint32_t n) { d->mar = mar; d->src = src; d->ndtr = n; d->en = true; g_dma_starts++; }

/* DMA 는 ISR 이 도는 동안에도 멈추지 않는다: ndtr 을 읽은 직후에 한 바이트가 더 들어올 수 있다 (하네스의 훅). */
static void (*g_dma_race)(void);
__attribute__((unused)) static uint32_t dma_ndtr(const dma_t *d) { uint32_t v = d->ndtr; if (g_dma_race) g_dma_race(); return v; }

#define UART_RX_MIN 4u   /* 이보다 작은 연속 영역에는 DMA 를 걸지 않는다 (금방 TC 가 나서 ISR 만 잦아진다) */
#ifndef UART_RX_CHUNK
#define UART_RX_CHUNK 16u   /* 한 번에 거는 DMA 길이 상한. 0 이면 무제한 (빈 곳을 전부 예약 → TC 때 다음 영역이 없어 줄을 흘린다, §5 Part C) */
#endif

typedef struct {
    bip_t *b;
    dma_t dma;
    _Atomic uint32_t armed;   /* 1 = DMA 가 현재 예약에 돌고 있다 */
    uint32_t cur_len;         /* 현재 예약(=DMA 전송) 길이 */
    uint32_t seen;            /* 그중 이미 publish 한 바이트 수 */
} uart_rx_t;

/* 예약을 얻어 DMA 를 건다. 영역이 없으면 false (armed 는 그대로 0). */
static bool uart_rx_arm(uart_rx_t *u) {
    /* TODO: bip_reserve_max(b, UART_RX_MIN, UART_RX_CHUNK, &n) → 없으면 false.
     *       cur_len = n, seen = 0, dma_start(&u->dma, p, NULL, n), IDX_STORE(&u->armed, 1), true */
    (void)u;
    return false;
}

/* IDLE / 전송 완료(TC) 공용 ISR. 받은 만큼 공개하고, 예약을 다 썼으면 다음 영역으로 넘어간다. */
static void uart_rx_isr(uart_rx_t *u) {
    /* TODO: armed 가 0 이면 return.
     *       left = dma_ndtr(&u->dma);  got = cur_len - left;  got > seen 이면 bip_publish(got - seen), seen = got.
     *       left == 0 이면 bip_commit(0) (예약 닫기) → armed = 0 → uart_rx_arm() 다시 시도 */
    (void)u;
}

/* 태스크: 덩어리 하나를 읽어 out 에 복사하고 반납한다. 공간이 생겼으면 멈춘 DMA 를 다시 건다. 복사한 수 반환. */
static uint32_t uart_rx_task(uart_rx_t *u, uint8_t *out, uint32_t max) {
    /* TODO: bip_read → min(n, max) 바이트 BYTE_RD 복사 → bip_release(복사한 수).
     *       그 뒤 armed == 0 이면 IRQ_OFF() { armed 다시 확인 후 uart_rx_arm } IRQ_ON() */
    (void)u; (void)out; (void)max;
    return 0;
}

typedef struct {
    bip_t *b;
    dma_t dma;
    _Atomic uint32_t busy;    /* 1 = TX DMA 가 돌고 있다. 이 플래그가 "지금 소비자가 누구인가" 의 바통이다 */
    uint32_t inflight;        /* 지금 전송 중인 덩어리 길이 */
} uart_tx_t;

/* 놀고 있으면 다음 덩어리를 DMA 로 내보낸다. busy == 0 이면 TC ISR 이 절대 오지 않으므로 태스크가 소비자가 된다. */
static void uart_tx_kick(uart_tx_t *u) {
    /* TODO: busy 가 1 이면 return. bip_read → 없으면 return.
     *       inflight = n, busy = 1, dma_start(&u->dma, NULL, p, n) */
    (void)u;
}

/* 메시지 하나를 연속 영역에 넣는다. 공간 없으면 false. commit 을 먼저 하고 그다음 kick (순서가 핵심). */
static bool uart_tx_send(uart_tx_t *u, const uint8_t *msg, uint32_t len) {
    /* TODO: bip_reserve(len) → NULL 이면 false → BYTE_WR 로 복사 → bip_commit(len) → uart_tx_kick → true */
    (void)u; (void)msg; (void)len;
    return false;
}

/* TC ISR: 보낸 덩어리를 반납하고 다음 덩어리를 건다. 없으면 busy = 0. */
static void uart_tx_isr(uart_tx_t *u) {
    /* TODO: bip_release(inflight) → bip_read → 있으면 inflight = n, dma_start, 없으면 inflight = 0, busy = 0 */
    (void)u;
}

/* =========================================================================================
 * 테스트 하네스 (이 아래는 고치지 않는다)
 * ========================================================================================= */

/* ---- Part A: 나쁜 변형들 — "컴파일러나 CPU 가 이렇게 바꿔도 되는가?" 를 코드로 적은 것 ---- */
static bool bad_get_tail_first(rb_t *q, uint8_t *out) {           /* [B2] 없음: M7 get_plain 이 실제로 낸 순서 */
    uint32_t t = IDX_LOAD(&q->tail), h = IDX_LOAD(&q->head);
    if (h == t) return false;
    IDX_STORE(&q->tail, t + 1u);
    *out = BYTE_RD(&q->buf[t & RB_MASK]);
    return true;
}
static bool bad_get_slot_first(rb_t *q, uint8_t *out) {           /* [B1] 없음: 칸을 추측으로 먼저 읽는다 */
    uint32_t t = IDX_LOAD(&q->tail);
    uint8_t v = BYTE_RD(&q->buf[t & RB_MASK]);
    uint32_t h = IDX_LOAD(&q->head);
    if (h == t) return false;
    IDX_STORE(&q->tail, t + 1u);
    *out = v;
    return true;
}
static bool bad_put_head_first(rb_t *q, uint8_t v) {              /* [B3] 없음: 게시 먼저, 쓰기 나중 */
    uint32_t h = IDX_LOAD(&q->head), t = IDX_LOAD(&q->tail);
    if (h - t == RB_N) return false;
    IDX_STORE(&q->head, h + 1u);
    BYTE_WR(&q->buf[h & RB_MASK], v);
    return true;
}
static bool isr_put_head_first(rb_t *q, uint8_t v) {              /* ISR 쪽 순서 뒤집기: 단일 코어에서는 무해 */
    uint32_t h = IDX_LOAD(&q->head), t = IDX_LOAD(&q->tail);
    if (h - t == RB_N) { q->dropped++; return false; }
    IDX_STORE(&q->head, h + 1u);
    BYTE_WR(&q->buf[h & RB_MASK], v);
    return true;
}

typedef bool (*rb_get_fn)(rb_t *, uint8_t *);
typedef bool (*rb_put_fn)(rb_t *, uint8_t);

typedef struct { rb_t *q; rb_put_fn put; rb_get_fn get; uint32_t burst; uint8_t *log; uint32_t *nlog; uint8_t next; } rb_isr_ctx_t;
static void isr_rx_feed(void *v) {     /* ISR 생산자: burst 바이트를 넣고, 들어간 것을 기록 */
    rb_isr_ctx_t *c = v;
    for (uint32_t i = 0; i < c->burst; i++) { uint8_t x = c->next++; if (c->put(c->q, x)) c->log[(*c->nlog)++] = x; }
}
static void isr_tx_drain(void *v) {    /* ISR 소비자: burst 바이트를 빼서 기록 */
    rb_isr_ctx_t *c = v;
    for (uint32_t i = 0; i < c->burst; i++) { uint8_t x; if (!c->get(c->q, &x)) break; c->log[(*c->nlog)++] = x; }
}

#define RB_LOG (4u * RB_N)
/* RX: 미리 p 바이트를 넣어 두고, 태스크가 비울 때까지 get. k 번째 접근 앞에서 ISR 이 burst 바이트를 넣는다. 위반 run 수 반환. */
static uint32_t rb_sweep_rx(rb_get_fn get, rb_put_fn isr_put, uint32_t *runs_out) {
    uint32_t bad = 0, runs = 0;
    for (uint32_t p = 0; p <= RB_N; p++) for (uint32_t burst = 1; burst <= RB_N; burst++) for (uint32_t k = 0; k < 400u; k++) {
        rb_t q; rb_init(&q);
        uint8_t acc[RB_LOG], got[RB_LOG]; uint32_t nacc = 0, ngot = 0; uint8_t seq = 1;
        ic_off();
        for (uint32_t i = 0; i < p; i++) { uint8_t x = seq++; if (isr_put(&q, x)) acc[nacc++] = x; }
        rb_isr_ctx_t c = { &q, isr_put, NULL, burst, acc, &nacc, seq };
        ic_arm(isr_rx_feed, &c, k, 0, 0);
        for (uint32_t i = 0; i < 3u * RB_N; i++) { uint8_t v; if (get(&q, &v) && ngot < RB_LOG) got[ngot++] = v; }
        bool fired = g_ic.fired != 0;
        ic_off();
        for (uint32_t i = 0; i < 2u * RB_N; i++) { uint8_t v; if (get(&q, &v) && ngot < RB_LOG) got[ngot++] = v; }   /* ISR 이 마지막에 넣은 것까지 */
        runs++;
        if (ngot != nacc || memcmp(got, acc, nacc) != 0) bad++;
        if (!fired) break;   /* k 가 전체 접근 수를 넘었다 */
    }
    if (runs_out) *runs_out = runs;
    return bad;
}
/* TX: 미리 p 바이트. 태스크가 N+2 바이트 put. k 번째 접근 앞에서 ISR 이 최대 burst 바이트를 뺀다. 마지막에 다 비운다. */
static uint32_t rb_sweep_tx(rb_put_fn put, rb_get_fn isr_get, uint32_t *runs_out) {
    uint32_t bad = 0, runs = 0;
    for (uint32_t p = 0; p <= RB_N; p++) for (uint32_t burst = 1; burst <= RB_N; burst++) for (uint32_t k = 0; k < 400u; k++) {
        rb_t q; rb_init(&q);
        uint8_t acc[RB_LOG], got[RB_LOG]; uint32_t nacc = 0, ngot = 0; uint8_t seq = 1;
        ic_off();
        for (uint32_t i = 0; i < p; i++) { uint8_t x = seq++; if (put(&q, x)) acc[nacc++] = x; }
        rb_isr_ctx_t c = { &q, NULL, isr_get, burst, got, &ngot, 0 };
        ic_arm(isr_tx_drain, &c, k, 0, 0);
        for (uint32_t i = 0; i < RB_N + 2u; i++) { uint8_t x = seq++; if (put(&q, x)) acc[nacc++] = x; }
        bool fired = g_ic.fired != 0;
        ic_off();
        uint8_t x;
        while (ngot < RB_LOG && isr_get(&q, &x)) got[ngot++] = x;
        runs++;
        if (ngot != nacc || memcmp(got, acc, nacc) != 0) bad++;
        if (!fired) break;
    }
    if (runs_out) *runs_out = runs;
    return bad;
}

static bool trace_fenced(const char *t, char from, char to) {  /* 마지막 from 과 그 뒤 첫 to 사이에 '|' 가 있나 */
    const char *q = strchr(t, to);
    if (!q) return false;
    const char *p = q;
    while (p > t && p[-1] != from) p--;
    if (p == t) return false;
    for (const char *s = p; s < q; s++) if (*s == '|') return true;
    return false;
}

/* 난수 스트레스: ops 번의 태스크 연산, 매 접근마다 pct% 확률로 ISR. 작은 참조 FIFO 로 순서·무손실 확인.
 * 검사 간격 K = 1024 연산 (처음 4096 연산은 매번). 위반 수 반환. */
typedef struct { uint8_t q[64]; uint32_t h, t; } mini_fifo_t;
static void mf_push(mini_fifo_t *m, uint8_t v) { m->q[m->h++ & 63u] = v; }
static bool mf_pop(mini_fifo_t *m, uint8_t *v) { if (m->h == m->t) return false; *v = m->q[m->t++ & 63u]; return true; }
typedef struct { rb_t *q; mini_fifo_t *m; uint8_t next; uint32_t bad; } rb_stress_t;
static void isr_rb_stress_rx(void *v) {
    rb_stress_t *c = v;
    for (uint32_t i = 0, n = rnd() % 4u; i < n; i++) { uint8_t x = c->next++; if (rb_isr_put(c->q, x)) mf_push(c->m, x); }
}
static void isr_rb_stress_tx(void *v) {
    rb_stress_t *c = v;
    for (uint32_t i = 0, n = rnd() % 4u; i < n; i++) { uint8_t x, e; if (!rb_isr_get(c->q, &x)) break; if (!mf_pop(c->m, &e) || e != x) c->bad++; }
}
static uint32_t rb_stress(bool tx, uint32_t ops, uint32_t pct) {
    rb_t q; rb_init(&q);
    mini_fifo_t m = {{0},0,0};
    rb_stress_t c = { &q, &m, 1, 0 };
    ic_arm(tx ? isr_rb_stress_tx : isr_rb_stress_rx, &c, NEVER, 0, pct);
    uint8_t seq = 1;
    for (uint32_t i = 0; i < ops; i++) {
        if (tx) { if (rnd() % 4u != 0) { uint8_t x = seq++; if (rb_task_put(&q, x)) mf_push(&m, x); } }
        else    { uint8_t x, e; if (rb_task_get(&q, &x)) { if (!mf_pop(&m, &e) || e != x) c.bad++; } }
        if (i < 4096u || i % 1024u == 0) {
            uint32_t h = atomic_load(&q.head), t = atomic_load(&q.tail);
            if (h - t > RB_N || h - t != m.h - m.t) c.bad++;
        }
    }
    ic_off();
    return c.bad;
}

/* ---- Part B 검사 도구 ---- */
static void bs(bip_t *b, uint8_t *mem, uint32_t cap, uint32_t r, uint32_t w, uint32_t wm) {
    bip_init(b, mem, cap);
    atomic_store(&b->r, r); atomic_store(&b->w, w); atomic_store(&b->wm, wm);
}
static bool beq(const bip_t *b, uint32_t r, uint32_t w, uint32_t wm) {
    return atomic_load(&b->r) == r && atomic_load(&b->w) == w && atomic_load(&b->wm) == wm;
}
static bool in_range(uint32_t x, uint32_t lo, uint32_t hi) { return x >= lo && x < hi; }
static bool bip_valid_contains(const bip_t *b, uint32_t idx) {
    uint32_t r = atomic_load(&b->r), w = atomic_load(&b->w), wm = atomic_load(&b->wm);
    if (w < r) return in_range(idx, r, wm) || in_range(idx, 0, w);
    return in_range(idx, r, w);
}
/* 불변식 검사 + (expected 가 있으면) 유효 바이트가 FIFO 내용과 정확히 같은가. 위반 수를 돌려준다. */
static uint32_t bip_check(const bip_t *b, const uint8_t *expected, uint32_t live) {
    uint32_t r = atomic_load(&b->r), w = atomic_load(&b->w), wm = atomic_load(&b->wm), bad = 0;
    if (r > b->cap || w > b->cap) return 1u;
    uint32_t n1, n2 = 0;
    if (w < r) {                  /* 감김 */
        if (wm > b->cap || wm < r) return 1u;
        if (w == 0) bad++;
        n1 = wm - r; n2 = w;
    } else {
        n1 = w - r;
    }
    if (expected) {
        if (n1 + n2 != live) bad++;
        else if (memcmp(b->buf + r, expected, n1) != 0 || memcmp(b->buf, expected + n1, n2) != 0) bad++;
    }
    if (b->res_active) {          /* 예약은 유효 데이터와 겹치면 안 된다 */
        if (b->res_start + b->res_len > b->cap) bad++;
        for (uint32_t i = 0; i < b->res_len; i++) if (bip_valid_contains(b, b->res_start + i)) { bad++; break; }
    }
    return bad;
}

#define BN 16u
static void test_bip_named(void) {
    uint8_t mem[BN]; bip_t b; uint32_t n; uint8_t *p; const uint8_t *c;
    ic_off();
    printf("-- 랩 시나리오 (cap=16)\n");
    bs(&b, mem, BN, 0, 0, 0); p = bip_reserve(&b, 5);
    CHECK(p == mem && beq(&b, 0, 0, 0) /* S1: reserve 는 공유 상태를 안 건드린다 */);
    CHECK(bip_commit(&b, 5) && beq(&b, 0, 5, 0));
    bs(&b, mem, BN, 0, 0, 0); p = bip_reserve(&b, 16); bip_commit(&b, 16);
    CHECK(p == mem && beq(&b, 0, 16, 0) && bip_reserve(&b, 1) == NULL /* S2: 안 감김이면 16 바이트 전부 사용 */);
    c = bip_read(&b, &n); CHECK(c == mem && n == 16u);

    bs(&b, mem, BN, 8, 14, 0); p = bip_reserve(&b, 4);
    CHECK(p == mem && beq(&b, 8, 14, 0) /* S3: 꼬리 2 < 4 → 0 번지. 게시 전에는 wm 도 그대로 */);
    CHECK(bip_commit(&b, 4) && beq(&b, 8, 4, 14) /* wm = 옛 w, [14,16) 은 구멍 */);

    bs(&b, mem, BN, 3, 15, 0);
    CHECK(bip_reserve(&b, 3) == NULL && beq(&b, 3, 15, 0) && !b.res_active /* S4: n == r 은 거절 (w == r 금지) */);
    p = bip_reserve(&b, 2); CHECK(p == mem && b.res_wrap);

    bs(&b, mem, BN, 8, 14, 0); p = bip_reserve(&b, 2);
    CHECK(p == mem + 14 && !b.res_wrap && bip_commit(&b, 2) && beq(&b, 8, 16, 0) /* S5: 꼬리에 딱 맞음 → w == cap */);

    bs(&b, mem, BN, 0, 0, 0); p = bip_reserve(&b, 3);
    CHECK(p != NULL && bip_reserve(&b, 1) == NULL /* S6: 예약 중 재예약 */);
    CHECK(!bip_commit(&b, 4) && b.res_active /* 길이 초과 commit 거절, 예약 유지 */);
    CHECK(bip_commit(&b, 0) && beq(&b, 0, 0, 0) && !b.res_active /* commit(0) = 취소 */);
    CHECK(!bip_commit(&b, 1) /* 예약 없이 commit */);
    CHECK(bip_reserve(&b, 0) == NULL && bip_reserve(&b, 17) == NULL);

    bs(&b, mem, BN, 8, 14, 99); p = bip_reserve(&b, 4); bip_commit(&b, 0);
    CHECK(p == mem && beq(&b, 8, 14, 99) /* S8: 감긴 예약의 취소는 wm 을 안 건드린다 */);
    bs(&b, mem, BN, 8, 14, 0); (void)bip_reserve(&b, 4); bip_commit(&b, 2);
    CHECK(beq(&b, 8, 2, 14) /* S9: 감긴 예약의 부분 commit */);

    bs(&b, mem, BN, 8, 4, 14);
    c = bip_read(&b, &n); CHECK(c == mem + 8 && n == 6u /* S10: 감김 → 영역 A 만 (w 까지 안 이어 준다) */);
    CHECK(bip_read(&b, &n) == NULL && n == 0u /* 들고 있는 동안 재읽기 금지 */);
    CHECK(bip_release(&b, 2) && beq(&b, 10, 4, 14));
    CHECK(!bip_release(&b, 1) /* 읽기 없이 반납 */);
    c = bip_read(&b, &n); CHECK(c == mem + 10 && n == 4u);
    CHECK(!bip_release(&b, 5) && b.rd_active /* 받은 것보다 많이 반납 → 거절 */);
    CHECK(bip_release(&b, 4) && beq(&b, 0, 4, 14) /* A 소진 → 빠른 점프 r = 0 */);
    c = bip_read(&b, &n); CHECK(c == mem && n == 4u /* B 가 새 A */);

    bs(&b, mem, BN, 8, 4, 14);
    CHECK(bip_reserve(&b, 4) == NULL /* S11: 구멍 안에서도 w+n == r 금지 */);
    p = bip_reserve(&b, 3); CHECK(p == mem + 4 && !b.res_wrap);
    CHECK(bip_commit(&b, 3) && beq(&b, 8, 7, 14));
    CHECK(bip_reserve(&b, 1) == NULL);

    bs(&b, mem, BN, 16, 16, 0);
    CHECK(bip_read(&b, &n) == NULL /* S12: 끝에서 비었음 */);
    p = bip_reserve(&b, 3); CHECK(p == mem && b.res_wrap);
    bip_commit(&b, 3); CHECK(beq(&b, 16, 3, 16));
    c = bip_read(&b, &n); CHECK(c == mem && n == 3u && atomic_load(&b.r) == 0u /* r == wm 상태 → 늦은 점프 */);

    bs(&b, mem, BN, 8, 14, 0);
    c = bip_read(&b, &n); CHECK(c == mem + 8 && n == 6u /* S13: 안 감김 상태로 받은 덩어리 */);
    p = bip_reserve(&b, 6); CHECK(p == mem);
    bip_commit(&b, 6); CHECK(beq(&b, 8, 6, 14) /* 들고 있는 사이 생산자가 감겼다 */);
    CHECK(bip_release(&b, 6) && beq(&b, 0, 6, 14));
    p = bip_reserve(&b, 10); CHECK(p == mem + 6 /* S14: 구멍 [14,16) 이 다시 쓰인다, 낡은 wm 무시 */);
    bip_commit(&b, 10); c = bip_read(&b, &n); CHECK(c == mem && n == 16u);

    bs(&b, mem, BN, 8, 4, 14); p = bip_reserve(&b, 3);
    c = bip_read(&b, &n); bip_release(&b, n);
    CHECK(p == mem + 4 && beq(&b, 0, 4, 14) /* S15: 예약 도중 r 이 0 으로 점프 */);
    CHECK(bip_commit(&b, 3) && beq(&b, 0, 7, 14));
    c = bip_read(&b, &n); CHECK(c == mem && n == 7u);

    bs(&b, mem, BN, 8, 14, 0); p = bip_reserve(&b, 4);
    c = bip_read(&b, &n); bip_release(&b, n);
    CHECK(p == mem && beq(&b, 14, 14, 0) /* S16: 감긴 예약 중에 소비자가 다 비움 */);
    bip_commit(&b, 4); CHECK(beq(&b, 14, 4, 14));
    c = bip_read(&b, &n); CHECK(c == mem && n == 4u && atomic_load(&b.r) == 0u);

    bs(&b, mem, BN, 8, 14, 0); p = bip_reserve_max(&b, 1, 0, &n);
    CHECK(p == mem + 14 && n == 2u /* S17: max 는 꼬리 우선 */); bip_commit(&b, 0);
    p = bip_reserve_max(&b, 3, 0, &n); CHECK(p == mem && n == 7u && b.res_wrap /* min 이 크면 앞쪽 r-1 */); bip_commit(&b, 0);
    bs(&b, mem, BN, 0, 0, 0); p = bip_reserve_max(&b, 1, 5, &n); CHECK(p == mem && n == 5u && b.res_len == 5u /* max_n 상한 */); bip_commit(&b, 0);
    bs(&b, mem, BN, 0, 16, 0); p = bip_reserve_max(&b, 1, 0, &n); CHECK(p == NULL && n == 0u);
    bs(&b, mem, BN, 8, 4, 14); p = bip_reserve_max(&b, 1, 0, &n); CHECK(p == mem + 4 && n == 3u); bip_commit(&b, 0);
    bs(&b, mem, BN, 8, 7, 14); p = bip_reserve_max(&b, 1, 0, &n); CHECK(p == NULL && n == 0u);

    bs(&b, mem, BN, 0, 0, 0); (void)bip_reserve(&b, 6);
    CHECK(bip_publish(&b, 2) && beq(&b, 0, 2, 0) && b.res_active && b.res_len == 4u /* S18: publish 는 예약 유지 */);
    CHECK(bip_publish(&b, 3) && beq(&b, 0, 5, 0) && bip_reserve(&b, 1) == NULL);
    CHECK(bip_commit(&b, 1) && beq(&b, 0, 6, 0) && !b.res_active);
    bs(&b, mem, BN, 8, 14, 0); (void)bip_reserve(&b, 4);
    CHECK(bip_publish(&b, 1) && beq(&b, 8, 1, 14) && !b.res_wrap /* 감긴 예약의 첫 publish 에서만 wm */);
    CHECK(bip_publish(&b, 2) && beq(&b, 8, 3, 14));
    CHECK(bip_commit(&b, 1) && beq(&b, 8, 4, 14));

    bs(&b, mem, BN, 0, 0, 0); CHECK(bip_read(&b, &n) == NULL && n == 0u);
    bs(&b, mem, BN, 2, 5, 0); (void)bip_read(&b, &n); CHECK(bip_release(&b, 0) && !b.rd_active && beq(&b, 2, 5, 0));
}

/* 배리어/접근 순서 검사 (trace) */
static void test_traces(void) {
    uint8_t mem[BN]; bip_t b; rb_t q; uint8_t v; uint32_t n;
    ic_off();
    rb_init(&q); atomic_store(&q.head, 1u);
    trace_begin(); bool ok = rb_task_get(&q, &v); const char *t = trace_end();
    printf("  rb_task_get trace: %s\n", t);
    CHECK(ok && trace_fenced(t, 'L', 'r') /* [B1] */ && trace_fenced(t, 'r', 'S') /* [B2] */);
    rb_init(&q);
    trace_begin(); ok = rb_task_put(&q, 7); t = trace_end();
    printf("  rb_task_put trace: %s\n", t);
    CHECK(ok && trace_fenced(t, 'w', 'S') /* [B3] */);
    bs(&b, mem, BN, 8, 14, 0); (void)bip_reserve(&b, 4);
    trace_begin(); ok = bip_commit(&b, 4); t = trace_end();
    printf("  wrap commit trace: %s\n", t);
    const char *s1 = strchr(t, 'S'), *s2 = s1 ? strchr(s1 + 1, 'S') : NULL;
    CHECK(ok && s1 && s2 && memchr(s1, '|', (size_t)(s2 - s1)) != NULL /* [B4] wm 저장과 w 저장 사이 */);
    CHECK(beq(&b, 8, 4, 14));
    bs(&b, mem, BN, 8, 4, 14);
    trace_begin(); (void)bip_read(&b, &n); t = trace_end();
    printf("  wrapped read trace: %s\n", t);
    CHECK(n == 6u && strchr(t, '|') != NULL);
}

/* ---- 전수 탐색 ---- */
#define BFS_MAX 12u
typedef struct { uint8_t r, w, wm, live, nxt; uint8_t buf[BFS_MAX], fifo[BFS_MAX]; } node_t;
static node_t  g_nodes[(BFS_MAX + 1) * (BFS_MAX + 1) * (BFS_MAX + 1)];
static int32_t g_ids[BFS_MAX + 1][BFS_MAX + 1][BFS_MAX + 1];
static uint32_t g_nn;
static uint64_t g_ntrans;

static void bfs_reset(void) { g_nn = 0; g_ntrans = 0; memset(g_ids, -1, sizeof g_ids); }
static void bfs_load(bip_t *b, uint8_t *mem, uint32_t cap, const node_t *s) {
    bs(b, mem, cap, s->r, s->w, s->wm);
    memcpy(mem, s->buf, cap);
}
static uint32_t bfs_add(const bip_t *b, const uint8_t *fifo, uint32_t live, uint8_t nxt) {
    uint32_t r = atomic_load(&b->r), w = atomic_load(&b->w), wm = atomic_load(&b->wm);
    uint32_t bad = bip_check(b, fifo, live);
    if (bad || r > BFS_MAX || w > BFS_MAX || wm > BFS_MAX || live > BFS_MAX) return bad ? bad : 1u;
    g_ntrans++;
    if (g_ids[r][w][wm] < 0) {
        node_t *n = &g_nodes[g_nn];
        n->r = (uint8_t)r; n->w = (uint8_t)w; n->wm = (uint8_t)wm; n->live = (uint8_t)live; n->nxt = nxt;
        memcpy(n->buf, b->buf, b->cap); memcpy(n->fifo, fifo, live);
        g_ids[r][w][wm] = (int32_t)g_nn++;
    }
    return 0;
}
/* 꼬리/앞/구멍을 명세대로 계산한 "reserve 가 성공해야 하는가 + 어디에" */
static bool oracle_fit(const node_t *s, uint32_t cap, uint32_t n, uint32_t *start) {
    if (n == 0 || n > cap) return false;
    if (s->w < s->r) { *start = s->w; return s->w + n < s->r; }
    if (cap - s->w >= n) { *start = s->w; return true; }
    *start = 0;
    return n < s->r;
}
/* 도달 가능한 모든 상태에서, 모든 reserve/commit/read/release 조합(예약이 열린 채 소비, 읽기가 열린 채 생산 포함)을 돌린다.
 * 내용은 상태별 대표 버퍼 + 진짜 FIFO 로 확인. 위반 수 반환. */
static uint32_t bfs_explore(uint32_t cap) {
    uint8_t mem[BFS_MAX]; bip_t b; uint32_t bad = 0;
    ic_off();
    bfs_reset();
    bs(&b, mem, cap, 0, 0, 0); memset(mem, 0, cap);
    uint8_t none[1] = {0};
    bad += bfs_add(&b, none, 0, 1);
    for (uint32_t id = 0; id < g_nn; id++) {
        node_t s = g_nodes[id];
        uint32_t n;
        /* A. 소비자만: 읽기는 live > 0 이면 절대 NULL 이 아니고, 내용이 FIFO 앞부분과 같아야 한다 */
        for (uint32_t m = 0; m <= cap; m++) {
            bfs_load(&b, mem, cap, &s);
            const uint8_t *c = bip_read(&b, &n);
            if ((c != NULL) != (s.live > 0)) { bad++; continue; }
            if (!c) break;
            if (n > s.live || memcmp(c, s.fifo, n) != 0) bad++;
            if (m > n) break;
            bip_release(&b, m);
            bad += bfs_add(&b, s.fifo + m, s.live - m, s.nxt);
        }
        /* B. 생산자만: reserve 는 오라클과 일치, commit 은 k 바이트 게시 */
        for (uint32_t nres = 1; nres <= cap; nres++) {
            uint32_t ostart = 0;
            bool ofit = oracle_fit(&s, cap, nres, &ostart);
            for (uint32_t k = 0; k <= nres; k++) {
                bfs_load(&b, mem, cap, &s);
                uint8_t *p = bip_reserve(&b, nres);
                if ((p != NULL) != ofit) { bad++; continue; }
                if (!p) break;
                if ((uint32_t)(p - mem) != ostart) bad++;
                bad += bip_check(&b, NULL, 0);
                uint8_t f[BFS_MAX + 1]; uint32_t live = s.live; memcpy(f, s.fifo, live);
                uint8_t nx = s.nxt;
                for (uint32_t i = 0; i < k; i++) { p[i] = nx; if (live < BFS_MAX) f[live++] = nx; nx++; }
                bip_commit(&b, k);
                bad += bfs_add(&b, f, live, nx);
            }
        }
        /* C. 예약이 열린 채 소비: reserve → read → release(m) → commit(k).  D. 읽기가 열린 채 reserve/commit → release */
        for (uint32_t nres = 1; nres <= cap; nres++) for (uint32_t m = 1; m <= cap; m++) for (uint32_t k = 1; k <= nres; k++) for (uint32_t order = 0; order < 2; order++) {
            bfs_load(&b, mem, cap, &s);
            uint8_t *p = NULL; const uint8_t *c = NULL;
            if (order == 0) { p = bip_reserve(&b, nres); if (!p) continue; c = bip_read(&b, &n); if (!c) continue; }
            else            { c = bip_read(&b, &n); if (!c) continue; p = bip_reserve(&b, nres); if (!p) continue; }
            if (m > n) continue;
            /* 읽기 덩어리와 예약이 겹치면 안 된다 */
            uint32_t rs = (uint32_t)(c - mem), ps = (uint32_t)(p - mem);
            if (rs < ps + nres && ps < rs + n) bad++;
            uint8_t f[BFS_MAX + 1]; uint32_t live = s.live - m; memcpy(f, s.fifo + m, live);
            uint8_t nx = s.nxt;
            for (uint32_t i = 0; i < k; i++) { p[i] = nx; if (live < BFS_MAX) f[live++] = nx; nx++; }
            if (order == 0) { bip_release(&b, m); bip_commit(&b, k); }
            else            { bip_commit(&b, k); bip_release(&b, m); }
            bad += bfs_add(&b, f, live, nx);
        }
        /* E. reserve_max → 전부 commit */
        for (uint32_t minv = 1; minv <= cap; minv++) {
            bfs_load(&b, mem, cap, &s);
            uint32_t got; uint8_t *p = bip_reserve_max(&b, minv, 0, &got);
            if (!p) continue;
            if (got < minv) bad++;
            uint8_t f[BFS_MAX + 1]; uint32_t live = s.live; memcpy(f, s.fifo, live);
            uint8_t nx = s.nxt;
            bool over = false;
            for (uint32_t i = 0; i < got; i++) { p[i] = nx; if (live < BFS_MAX) f[live++] = nx; else over = true; nx++; }
            if (over) { bad++; continue; }
            bip_commit(&b, got);
            bad += bfs_add(&b, f, live, nx);
        }
    }
    return bad;
}

/* 공간 보장 정리 — reserve 크기 <= M, reserve 시점 live <= D 만 허용하며 도달한 모든 상태에서 reserve(M) 이 성공하나 */
static bool guarantee_holds(uint32_t cap, uint32_t D, uint32_t M) {
    uint8_t mem[BFS_MAX]; bip_t b; uint32_t n;
    ic_off();
    bfs_reset();
    bs(&b, mem, cap, 0, 0, 0); memset(mem, 0, cap);
    uint8_t none[1] = {0};
    (void)bfs_add(&b, none, 0, 1);
    for (uint32_t id = 0; id < g_nn; id++) {
        node_t s = g_nodes[id];
        if (s.live <= D) {
            bfs_load(&b, mem, cap, &s);
            if (!bip_reserve(&b, M)) return false;
        }
        for (uint32_t m = 1; m <= s.live; m++) {
            bfs_load(&b, mem, cap, &s);
            const uint8_t *c = bip_read(&b, &n);
            if (!c || m > n) continue;
            bip_release(&b, m);
            (void)bfs_add(&b, s.fifo + m, s.live - m, s.nxt);
        }
        if (s.live > D) continue;
        for (uint32_t nres = 1; nres <= M; nres++) for (uint32_t k = 1; k <= nres; k++) {
            bfs_load(&b, mem, cap, &s);
            uint8_t *p = bip_reserve(&b, nres);
            if (!p) continue;
            uint8_t f[BFS_MAX + 1]; uint32_t live = s.live; memcpy(f, s.fifo, live);
            uint8_t nx = s.nxt;
            for (uint32_t i = 0; i < k; i++) { p[i] = nx; if (live < BFS_MAX) f[live++] = nx; nx++; }
            bip_commit(&b, k);
            (void)bfs_add(&b, f, live, nx);
        }
    }
    return true;
}

/* ---- 도달 가능한 모든 상태 × ISR 발사 위치 전수 ---- */
typedef bool (*commit_fn)(bip_t *, uint32_t);
typedef const uint8_t *(*read_fn)(bip_t *, uint32_t *);
#define IL_LOG 128u
typedef struct { bip_t *b; uint32_t n, m; uint8_t *log; uint32_t *nlog; uint8_t *nxt; commit_fn commit; read_fn read; } il_ctx_t;
/* 나쁜 변형은 낡은 wm 때문에 말도 안 되는 길이를 돌려줄 수 있다. 읽기 전에 범위를 보고 위반으로 센다 (진짜 하드웨어라면 쓰레기 읽기). */
static uint32_t g_il_viol;
static bool grant_ok(const bip_t *b, const uint8_t *p, uint32_t n) {
    if ((uint32_t)(p - b->buf) + n <= b->cap && n <= b->cap) return true;
    g_il_viol++;
    return false;
}
static void isr_bip_producer(void *v) {
    il_ctx_t *c = v;
    uint8_t *p = bip_reserve(c->b, c->n);
    if (!p) return;
    for (uint32_t i = 0; i < c->n; i++) { uint8_t x = (*c->nxt)++; BYTE_WR(&p[i], x); c->log[(*c->nlog)++] = x; }
    (void)c->commit(c->b, c->n);
}
static void isr_bip_consumer(void *v) {
    il_ctx_t *c = v;
    uint32_t n; const uint8_t *p = c->read(c->b, &n);
    if (!p) return;
    if (!grant_ok(c->b, p, n)) { c->b->rd_active = false; return; }
    uint32_t m = c->m ? (c->m < n ? c->m : n) : n;
    for (uint32_t i = 0; i < m; i++) c->log[(*c->nlog)++] = BYTE_RD(&p[i]);
    bip_release(c->b, m);
}
static uint32_t drain_all(bip_t *b, read_fn rd, uint8_t *log, uint32_t *nlog) {
    uint32_t n, guard = 0; const uint8_t *p;
    while ((p = rd(b, &n)) != NULL && guard++ < 64u) {
        if (!grant_ok(b, p, n)) { b->rd_active = false; break; }
        for (uint32_t i = 0; i < n && *nlog < IL_LOG; i++) log[(*nlog)++] = BYTE_RD(&p[i]);
        bip_release(b, n);
    }
    return guard;
}
/* 태스크=소비자(RX), ISR=생산자. 위반 run 수 반환. 상태 목록(g_nodes)을 그대로 쓴다. */
static uint32_t il_rx(uint32_t cap, read_fn rd, commit_fn commit, uint64_t *runs) {
    uint8_t mem[BFS_MAX]; bip_t b; uint32_t bad = 0; uint64_t rn = 0;
    node_t snap[BFS_MAX * BFS_MAX * 4 + 1]; uint32_t nsnap = g_nn < (sizeof snap / sizeof snap[0]) ? g_nn : (uint32_t)(sizeof snap / sizeof snap[0]);
    memcpy(snap, g_nodes, nsnap * sizeof(node_t));
    for (uint32_t i = 0; i < nsnap; i++) for (uint32_t isr_n = 1; isr_n <= cap; isr_n++) for (uint32_t k = 0; k < 400u; k++) {
        const node_t *s = &snap[i];
        uint8_t got[IL_LOG], acc[IL_LOG]; uint32_t ngot = 0, nacc = 0; uint8_t nx = s->nxt;
        memcpy(acc, s->fifo, s->live); nacc = s->live;
        bfs_load(&b, mem, cap, s);
        g_il_viol = 0;
        il_ctx_t c = { &b, isr_n, 0, acc, &nacc, &nx, commit, NULL };
        ic_arm(isr_bip_producer, &c, k, 0, 0);
        drain_all(&b, rd, got, &ngot);
        bool fired = g_ic.fired != 0;
        ic_off();
        drain_all(&b, rd, got, &ngot);
        rn++;
        if (g_il_viol || ngot != nacc || memcmp(got, acc, nacc) != 0 || bip_check(&b, NULL, 0) != 0) bad++;
        if (!fired) break;
    }
    *runs += rn;
    return bad;
}
/* 태스크=생산자(TX), ISR=소비자 (m = 0 이면 전부, 아니면 앞 m 바이트만 반납). */
static uint32_t il_tx(uint32_t cap, read_fn rd, commit_fn commit, uint64_t *runs) {
    uint8_t mem[BFS_MAX]; bip_t b; uint32_t bad = 0; uint64_t rn = 0;
    node_t snap[BFS_MAX * BFS_MAX * 4 + 1]; uint32_t nsnap = g_nn < (sizeof snap / sizeof snap[0]) ? g_nn : (uint32_t)(sizeof snap / sizeof snap[0]);
    memcpy(snap, g_nodes, nsnap * sizeof(node_t));
    for (uint32_t i = 0; i < nsnap; i++) for (uint32_t tn = 1; tn <= cap; tn++) for (uint32_t m = 0; m <= 1; m++) for (uint32_t k = 0; k < 400u; k++) {
        const node_t *s = &snap[i];
        uint8_t got[IL_LOG], acc[IL_LOG]; uint32_t ngot = 0, nacc = 0; uint8_t nx = s->nxt;
        memcpy(acc, s->fifo, s->live); nacc = s->live;
        bfs_load(&b, mem, cap, s);
        g_il_viol = 0;
        il_ctx_t c = { &b, 0, m, got, &ngot, &nx, NULL, rd };
        ic_arm(isr_bip_consumer, &c, k, 0, 0);
        uint8_t *p = bip_reserve(&b, tn);
        if (p) {
            for (uint32_t j = 0; j < tn; j++) { uint8_t x = nx++; BYTE_WR(&p[j], x); acc[nacc++] = x; }
            commit(&b, tn);
        }
        bool fired = g_ic.fired != 0;
        ic_off();
        drain_all(&b, rd, got, &ngot);
        rn++;
        if (g_il_viol || ngot != nacc || memcmp(got, acc, nacc) != 0 || bip_check(&b, NULL, 0) != 0) bad++;
        if (!fired) break;
    }
    *runs += rn;
    return bad;
}

/* 나쁜 변형: w 를 wm 보다 먼저 게시 */
static bool bad_commit_w_first(bip_t *b, uint32_t k) {
    if (!b->res_active || k > b->res_len) return false;
    if (k) {
        uint32_t nw = b->res_start + k;
        uint32_t old_w = IDX_LOAD(&b->w);
        IDX_STORE(&b->w, nw);
        if (b->res_wrap) IDX_STORE(&b->wm, old_w);
        b->res_wrap = false;
    }
    b->res_active = false; b->res_len = 0;
    return true;
}
/* 나쁜 변형: wm 을 w 보다 먼저 읽는다 */
static const uint8_t *bad_read_wm_first(bip_t *b, uint32_t *n) {
    *n = 0;
    if (b->rd_active) return NULL;
    uint32_t r = IDX_LOAD(&b->r);
    uint32_t wm = IDX_LOAD(&b->wm);
    uint32_t w = IDX_LOAD(&b->w);
    uint32_t limit = w;
    if (w < r) {
        if (r == wm) { r = 0; IDX_STORE(&b->r, 0u); }
        else limit = wm;
    }
    if (limit <= r) return NULL;
    b->rd_active = true; b->rd_len = limit - r; *n = b->rd_len;
    return b->buf + r;
}

/* 100만 연산 난수 스트레스: 참조 FIFO 와 바이트 단위 비교. 불변식은 처음 4096 연산 매번, 이후 K = 1024 연산마다 + 끝.
 * cap = 97 (2의 거듭제곱 아님). mode 0: ISR 없음, 예약을 연 채 소비 섞기. 1: ISR 생산자. 2: ISR 소비자. */
#define REF_N 4096u
typedef struct { bip_t *b; uint8_t q[REF_N]; uint32_t h, t; uint8_t nxt; uint32_t bad; uint64_t wraps; } st_t;
static void st_append(st_t *s, uint8_t x) { s->q[s->h++ % REF_N] = x; }
static void st_produce(st_t *s, uint32_t n) {
    uint32_t r0 = atomic_load(&s->b->r); (void)r0;
    uint8_t *p = bip_reserve(s->b, n);
    if (!p) return;
    if (s->b->res_wrap) s->wraps++;
    uint32_t k = rnd() % (n + 1u);
    for (uint32_t i = 0; i < k; i++) { uint8_t x = s->nxt++; BYTE_WR(&p[i], x); st_append(s, x); }
    if (!bip_commit(s->b, k)) s->bad++;
}
static void st_consume(st_t *s) {
    uint32_t n; const uint8_t *p = bip_read(s->b, &n);
    if (!p) return;
    if ((uint32_t)(p - s->b->buf) + n > s->b->cap || n > s->b->cap) { s->bad++; s->b->rd_active = false; return; }
    uint32_t m = rnd() % (n + 1u);
    for (uint32_t i = 0; i < m; i++) {
        uint8_t v = BYTE_RD(&p[i]);
        if (s->t == s->h || s->q[s->t % REF_N] != v) s->bad++;
        s->t++;
    }
    if (!bip_release(s->b, m)) s->bad++;
}
static void isr_st_prod(void *v) { st_t *s = v; if (!s->b->res_active) st_produce(s, 1u + rnd() % 40u); }
static void isr_st_cons(void *v) { st_t *s = v; if (!s->b->rd_active) st_consume(s); }
static uint32_t bip_stress(uint32_t mode, uint32_t ops, uint32_t pct, uint64_t *wraps) {
    static uint8_t mem[97];
    static bip_t b;
    static st_t s;
    bip_init(&b, mem, 97u);
    memset(&s, 0, sizeof s);
    s.b = &b;
    if (mode == 1) ic_arm(isr_st_prod, &s, NEVER, 0, pct);
    else if (mode == 2) ic_arm(isr_st_cons, &s, NEVER, 0, pct);
    else ic_off();
    for (uint32_t i = 0; i < ops; i++) {
        uint32_t c = rnd() % 8u;
        if (mode == 0) {
            if (c < 4u) st_produce(&s, 1u + rnd() % 40u);
            else st_consume(&s);
            if (rnd() % 16u == 0) {           /* 예약을 연 채 소비 */
                uint8_t *p = bip_reserve(&b, 1u + rnd() % 30u);
                st_consume(&s);
                if (p) (void)bip_commit(&b, 0u);
            }
        } else if (mode == 1) {
            st_consume(&s);
        } else {
            if (c < 6u) st_produce(&s, 1u + rnd() % 40u);
        }
        if (i < 4096u || (i & 1023u) == 0u) s.bad += bip_check(&b, NULL, 0);
    }
    ic_off();
    /* 끝: 남은 것을 전부 빼서 참조와 맞춘다 */
    uint32_t n, guard = 0; const uint8_t *p;
    while ((p = bip_read(&b, &n)) != NULL && guard++ < 16u) {
        if (n > b.cap || (uint32_t)(p - b.buf) + n > b.cap) { s.bad++; break; }
        for (uint32_t i = 0; i < n; i++, s.t++) if (s.t == s.h || s.q[s.t % REF_N] != p[i]) s.bad++;
        bip_release(&b, n);
    }
    if (s.t != s.h) s.bad++;
    *wraps = s.wraps;
    return s.bad;
}

/* ---- Part C: UART RX DMA 시뮬레이션 ---- */
#define RXB_N 64u
#define RX_LOG 20000u
typedef struct {
    uart_rx_t u; bip_t b; uint8_t mem[RXB_N];
    uint32_t sent, total, lost, viol, nacc, burst, idle_pct;
    bool tc_pending, race;
    bool hold_full; uint8_t hold;   /* UART 수신 레지스터 1 바이트: DMA 가 꺼져 있어도 한 바이트는 여기서 기다린다 (다음 바이트가 오면 ORE) */
    uint8_t acc[RX_LOG];
} rx_world_t;
static rx_world_t g_rx;

static void rx_dma_store(rx_world_t *x, uint8_t v) {
    dma_t *d = &x->u.dma;
    uint32_t idx = (uint32_t)(d->mar - x->mem);
    if (idx >= RXB_N) { x->viol++; d->en = false; return; }          /* 범위 밖 주소로는 쓰지 않는다 */
    if (bip_valid_contains(&x->b, idx)) x->viol++;                   /* DMA 가 유효 데이터를 덮으면 안 된다 */
    *d->mar++ = v;
    d->ndtr--;
    if (x->nacc < RX_LOG) x->acc[x->nacc++] = v;
    if (d->ndtr == 0) { d->en = false; x->tc_pending = true; }
}
static void rx_service_hold(rx_world_t *x) {
    if (x->hold_full && x->u.dma.en) { x->hold_full = false; rx_dma_store(x, x->hold); }
}
static void rx_line_one(rx_world_t *x) {
    rx_service_hold(x);
    if (x->sent >= x->total) return;
    uint8_t v = (uint8_t)((x->sent * 37u + 11u) ^ (x->sent >> 8));
    x->sent++;
    if (x->u.dma.en) { rx_dma_store(x, v); return; }
    if (!x->hold_full) { x->hold_full = true; x->hold = v; return; }
    x->lost++;                                  /* 수신 레지스터도 찼다 = ORE: 줄에서 사라진다 */
}
static void rx_race_hook(void) { if (g_rx.race && g_rx.u.dma.en) rx_line_one(&g_rx); }   /* DMA 가 돌고 있을 때만 ISR 도중 한 바이트가 더 온다 */
static void rx_hw_irq(void *arg) {        /* 인터럽트 하나: 바이트들이 들어오고, TC 또는 IDLE 이면 ISR */
    rx_world_t *x = arg;
    for (uint32_t i = 0; i < x->burst; i++) rx_line_one(x);
    bool idle = x->sent >= x->total || (rnd() % 100u) < x->idle_pct;
    if (x->tc_pending || idle) { x->tc_pending = false; uart_rx_isr(&x->u); }
}
static void rx_world_init(rx_world_t *x, uint32_t total, uint32_t burst, uint32_t idle_pct, bool race) {
    memset(x, 0, sizeof *x);
    bip_init(&x->b, x->mem, RXB_N);
    x->u.b = &x->b;
    atomic_init(&x->u.armed, 0u);
    x->total = total; x->burst = burst; x->idle_pct = idle_pct; x->race = race;
    g_dma_race = rx_race_hook;
    (void)uart_rx_arm(&x->u);
}
static uint32_t rx_finish(rx_world_t *x, uint8_t *out, uint32_t cap) {   /* 줄을 끝까지 흘리고 남은 것을 다 받는다 */
    uint32_t n = 0, guard = 0;
    while ((x->sent < x->total || x->tc_pending || x->hold_full || guard == 0) && guard++ < 200000u) {
        rx_hw_irq(x);
        n += uart_rx_task(&x->u, out + n, cap - n);
    }
    g_ic.in_isr = true; uart_rx_isr(&x->u); g_ic.in_isr = false;    /* 마지막 IDLE */
    for (uint32_t i = 0; i < 8u; i++) n += uart_rx_task(&x->u, out + n, cap - n);
    return n;
}
static uint8_t g_rx_out[RX_LOG];
/* 스트림 시나리오 하나. 위반 수 반환. */
static uint32_t rx_run(uint32_t total, uint32_t burst, uint32_t idle_pct, bool race, uint32_t at, uint32_t period, uint32_t pct, uint32_t *lost) {
    rx_world_t *x = &g_rx;
    rx_world_init(x, total, burst, idle_pct, race);
    ic_arm(rx_hw_irq, x, at, period, pct);
    uint32_t n = 0, guard = 0;
    while (x->sent < x->total && guard++ < 400000u)
        n += uart_rx_task(&x->u, g_rx_out + n, RX_LOG - n);
    ic_off();
    n += rx_finish(x, g_rx_out + n, RX_LOG - n);
    g_dma_race = NULL;
    if (lost) *lost = x->lost;
    uint32_t bad = x->viol;
    if (x->sent != x->total) bad++;
    if (x->nacc + x->lost + (x->hold_full ? 1u : 0u) != x->total) bad++;
    if (n != x->nacc || memcmp(g_rx_out, x->acc, n) != 0) bad++;
    bad += bip_check(&x->b, NULL, 0);
    return bad;
}

/* ---- Part C: UART TX DMA 시뮬레이션 ---- */
#define TXB_N 64u
#define TX_MSGS 300u
#define TX_WIRE 8192u
typedef struct {
    uart_tx_t u; bip_t b; uint8_t mem[TXB_N];
    uint8_t wire[TX_WIRE]; uint32_t nwire;
    uint32_t xfer_start[TX_MSGS * 2u + 4u]; uint32_t nxfer;
    uint32_t last_starts;
    uint32_t burst, viol;
} tx_world_t;
static tx_world_t g_tx;

static void tx_note_xfer(tx_world_t *x) {
    if (g_dma_starts != x->last_starts) {   /* 새 DMA 시작 → 그 첫 바이트의 wire 위치를 기록 */
        x->last_starts = g_dma_starts;
        if (x->nxfer < sizeof x->xfer_start / sizeof x->xfer_start[0]) x->xfer_start[x->nxfer++] = x->nwire;
    }
}
static void tx_hw_irq(void *arg) {
    tx_world_t *x = arg;
    dma_t *d = &x->u.dma;
    for (uint32_t i = 0; i < x->burst && d->en; i++) {
        tx_note_xfer(x);
        uint32_t idx = (uint32_t)(d->src - x->mem);
        if (idx >= TXB_N) { x->viol++; d->en = false; break; }            /* 범위 밖에서는 읽지 않는다 */
        if (!bip_valid_contains(&x->b, idx)) x->viol++;                   /* 전송 중 바이트는 아직 유효 데이터여야 한다 */
        if (x->nwire < TX_WIRE) x->wire[x->nwire++] = *d->src;
        d->src++;
        if (--d->ndtr == 0) { d->en = false; uart_tx_isr(&x->u); }
    }
}
static void tx_world_init(tx_world_t *x, uint32_t burst) {
    memset(x, 0, sizeof *x);
    bip_init(&x->b, x->mem, TXB_N);
    x->u.b = &x->b;
    atomic_init(&x->u.busy, 0u);
    x->burst = burst;
    x->last_starts = g_dma_starts;
}
typedef bool (*tx_send_fn)(uart_tx_t *, const uint8_t *, uint32_t);

/* 나쁜 변형: kick(=busy 확인)을 commit 앞에 둔다 → ISR 이 그 사이에 끝나면 데이터가 영원히 묶인다 */
static bool bad_send_kick_first(uart_tx_t *u, const uint8_t *msg, uint32_t len) {
    uint8_t *p = bip_reserve(u->b, len);
    if (!p) return false;
    for (uint32_t i = 0; i < len; i++) BYTE_WR(&p[i], msg[i]);
    uart_tx_kick(u);
    bip_commit(u->b, len);
    return true;
}

static uint32_t msg_build(uint8_t *m, uint32_t idx, uint32_t *rng) {
    uint32_t x = *rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; *rng = x;
    uint32_t len = 2u + x % 23u;                    /* 2..24 바이트 */
    m[0] = (uint8_t)idx; m[1] = (uint8_t)len;
    for (uint32_t i = 2; i < len; i++) m[i] = (uint8_t)(idx * 7u + i * 3u);
    return len;
}
/* 메시지 경계가 DMA 전송 경계에 걸리지 않았는가 */
static uint32_t tx_verify(const tx_world_t *x, uint32_t nmsg, uint32_t seed, const uint32_t *lens) {
    uint32_t bad = 0, off = 0, rng = seed, xi = 0;
    uint8_t m[32];
    for (uint32_t i = 0; i < nmsg; i++) {
        uint32_t len = msg_build(m, i, &rng);
        if (lens && lens[i] != len) bad++;
        if (off + len > x->nwire || memcmp(x->wire + off, m, len) != 0) { bad++; break; }
        while (xi + 1u < x->nxfer && x->xfer_start[xi + 1u] <= off) xi++;
        if (xi + 1u < x->nxfer && x->xfer_start[xi + 1u] < off + len) bad++;   /* 메시지가 전송 둘로 쪼개졌다 */
        off += len;
    }
    if (off != x->nwire) bad++;
    return bad;
}
/* nmsg 개 메시지를 보낸다. at/period/pct 로 TX ISR 을 발사. 끝까지 비운 뒤 wire 를 검증. 위반 수 반환. */
static uint32_t tx_run(tx_send_fn send, uint32_t nmsg, uint32_t burst, uint32_t at, uint32_t period, uint32_t pct, uint32_t seed, uint32_t *stranded) {
    tx_world_t *x = &g_tx;
    tx_world_init(x, burst);
    ic_arm(tx_hw_irq, x, at, period, pct);
    uint32_t rng = seed, sent = 0, guard = 0;
    uint8_t m[32];
    uint32_t len = msg_build(m, 0, &rng);
    while (sent < nmsg && guard++ < 2000000u) {
        if (send(&x->u, m, len)) {
            if (++sent < nmsg) len = msg_build(m, sent, &rng);
        } else {
            ic_fire();                                /* 공간 대기 = 시간이 흐른다 */
        }
    }
    bool saved_fired = g_ic.fired != 0;
    (void)saved_fired;
    for (uint32_t i = 0; i < 2000u && x->u.dma.en; i++) ic_fire();  /* DMA 가 다 나갈 때까지 */
    ic_off();
    g_ic.fn = NULL;
    uint32_t n, left = 0, safety = 0;
    /* 묶인 데이터: 전송이 끝났는데 버퍼에 남아 있으면 stranded */
    bool stuck = (bip_check(&x->b, NULL, 0) != 0);
    uint32_t r = atomic_load(&x->b.r), w = atomic_load(&x->b.w), wm = atomic_load(&x->b.wm);
    left = (w < r) ? (wm - r + w) : (w - r);
    (void)n; (void)safety;
    if (stranded) *stranded = left;
    uint32_t bad = x->viol + (stuck ? 1u : 0u);
    if (sent != nmsg) bad++;
    if (x->nxfer == 0 && nmsg > 0) bad++;
    if (atomic_load(&x->u.busy) != 0u) bad++;
    if (left != 0u) bad++;
    else bad += tx_verify(x, nmsg, seed, NULL);
    return bad;
}

/* ---- 테스트 묶음 ---- */
static void test_part_a(void) {
    uint32_t runs = 0, bad;
    printf("== Part A: single-core ISR <-> task ring\n");
    {   /* 기본 동작 + 2^32 경계 (DS-B04) */
        rb_t q; rb_init(&q); uint8_t v = 0; bool all = true;
        ic_off();
        for (uint32_t i = 0; i < RB_N; i++) all = all && rb_isr_put(&q, (uint8_t)(i + 1u));
        CHECK(all);
        CHECK(!rb_isr_put(&q, 99) && q.dropped == 1u /* 가득 → 버리고 센다 */);
        all = true;
        for (uint32_t i = 0; i < RB_N; i++) all = all && rb_task_get(&q, &v) && v == (uint8_t)(i + 1u);
        CHECK(all && !rb_task_get(&q, &v));
        rb_init(&q); atomic_store(&q.head, 0xFFFFFFFFu); atomic_store(&q.tail, 0xFFFFFFFFu);
        bool w1 = rb_task_put(&q, 5) && rb_task_put(&q, 6);
        CHECK(w1 && atomic_load(&q.head) == 1u && rb_isr_get(&q, &v) && v == 5u && rb_isr_get(&q, &v) && v == 6u && !rb_isr_get(&q, &v) /* head 가 UINT32_MAX 를 넘어도 */);
    }
    test_traces();
    bad = rb_sweep_rx(rb_task_get, rb_isr_put, &runs);
    printf("  RX sweep: %u runs (p, burst, k 전부), 위반 %u\n", runs, bad);
    CHECK(runs > 1000u && bad == 0u);
    bad = rb_sweep_tx(rb_task_put, rb_isr_get, &runs);
    printf("  TX sweep: %u runs, 위반 %u\n", runs, bad);
    CHECK(runs > 1000u && bad == 0u);

    uint32_t b1 = rb_sweep_rx(bad_get_tail_first, rb_isr_put, NULL);
    uint32_t b2 = rb_sweep_rx(bad_get_slot_first, rb_isr_put, NULL);
    uint32_t b3 = rb_sweep_tx(bad_put_head_first, rb_isr_get, NULL);
    uint32_t b4 = rb_sweep_rx(rb_task_get, isr_put_head_first, NULL);
    printf("  순서를 뒤집은 변형의 위반 run 수: tail 먼저 %u, 칸 먼저 %u, TX head 먼저 %u, ISR 쪽 head 먼저 %u\n", b1, b2, b3, b4);
    CHECK(b1 > 0u /* [B2] 없으면 깨진다 */);
    CHECK(b2 > 0u /* [B1] 없으면 깨진다 */);
    CHECK(b3 > 0u /* [B3] 없으면 깨진다 */);
    CHECK(b4 == 0u /* ISR 쪽 순서는 단일 코어에서 무해 */);

    uint32_t s1 = rb_stress(false, 1000000u, 30u), s2 = rb_stress(true, 1000000u, 30u);
    printf("  stress: RX %u, TX %u violations (각 100만 연산, ISR 30%%/접근, 검사 K=1024)\n", s1, s2);
    CHECK(s1 == 0u && s2 == 0u);
    uint32_t e1 = rb_stress(false, 20000u, 100u), e2 = rb_stress(true, 20000u, 100u);
    CHECK(e1 == 0u && e2 == 0u /* ISR 을 모든 접근 앞에서 */);
}

static void test_part_b(void) {
    printf("== Part B: bip buffer\n");
    test_bip_named();
    printf("-- 전수 탐색\n");
    uint32_t bad_tot = 0;
    for (uint32_t cap = 1; cap <= 9u; cap++) {
        uint32_t bad = bfs_explore(cap);
        bad_tot += bad;
        printf("  cap=%u: 도달 상태 %u, 전이 %llu, 위반 %u\n", cap, g_nn, (unsigned long long)g_ntrans, bad);
    }
    CHECK(bad_tot == 0u && g_nn > 100u);

    bool suf = true, tight = true; uint32_t combos = 0;
    for (uint32_t D = 0; D <= 4u; D++) for (uint32_t M = 1; M <= 4u; M++) {
        uint32_t N = D + 2u * M;
        if (N > BFS_MAX) continue;
        combos++;
        if (!guarantee_holds(N, D, M)) { suf = false; printf("  guarantee FAILS at D=%u M=%u N=%u\n", D, M, N); }
        if (N >= 1u + M && guarantee_holds(N - 1u, D, M)) { tight = false; printf("  N-1 still holds at D=%u M=%u N-1=%u\n", D, M, N - 1u); }
    }
    printf("  보장 정리: %u 조합, N = D + 2M 충분 %s, N - 1 이면 실패 %s\n", combos, suf ? "yes" : "NO", tight ? "yes" : "NO");
    CHECK(suf /* N >= D + 2M → 크기 M 이하 reserve 가 live <= D 일 때 항상 성공 */);
    CHECK(tight /* 한 바이트 줄이면 깨지는 상태가 있다 */);

    printf("-- ISR 발사 위치 전수 (cap=8, 도달 가능한 모든 상태)\n");
    (void)bfs_explore(8u);
    uint64_t rn = 0, tn = 0;
    uint32_t b_rx = il_rx(8u, bip_read, bip_commit, &rn);
    uint32_t b_tx = il_tx(8u, bip_read, bip_commit, &tn);
    printf("  상태 %u개: RX %llu runs 위반 %u, TX %llu runs 위반 %u\n", g_nn, (unsigned long long)rn, b_rx, (unsigned long long)tn, b_tx);
    CHECK(rn > 10000u && b_rx == 0u);
    CHECK(tn > 10000u && b_tx == 0u);
    uint64_t dummy = 0;
    uint32_t b_wfirst = il_tx(8u, bip_read, bad_commit_w_first, &dummy);
    uint32_t b_wmfirst = il_rx(8u, bad_read_wm_first, bip_commit, &dummy);
    printf("  나쁜 변형 위반 run 수: w 먼저 게시 %u, wm 먼저 읽기 %u\n", b_wfirst, b_wmfirst);
    CHECK(b_wfirst > 0u /* [B4] */);
    CHECK(b_wmfirst > 0u /* [B6] */);

    printf("-- 난수 스트레스 (cap=97)\n");
    uint64_t w0 = 0, w1 = 0, w2 = 0;
    uint32_t s0 = bip_stress(0, 600000u, 0, &w0);
    uint32_t s1 = bip_stress(1, 200000u, 25u, &w1);
    uint32_t s2 = bip_stress(2, 200000u, 25u, &w2);
    printf("  60만 단독: 위반 %u (랩 %llu) · 20만 ISR 생산자: 위반 %u (랩 %llu) · 20만 ISR 소비자: 위반 %u (랩 %llu)\n",
           s0, (unsigned long long)w0, s1, (unsigned long long)w1, s2, (unsigned long long)w2);
    CHECK(s0 == 0u && s1 == 0u && s2 == 0u);
    CHECK(w0 > 100u && w1 > 100u && w2 > 100u /* 랩을 실제로 지나갔다 */);
}

static void test_part_c(void) {
    printf("== Part C: UART DMA\n");
    uint32_t lost = 0, bad;
    {   /* 기본: DMA 가 예약 영역에 쓰고 IDLE 에 공개, 태스크가 읽는다 */
        rx_world_t *x = &g_rx; uint8_t out[32]; uint32_t n = 0;
        rx_world_init(x, 12u, 1u, 0u, false);
        ic_off();
        CHECK(atomic_load(&x->u.armed) == 1u && x->u.dma.ndtr == (UART_RX_CHUNK ? UART_RX_CHUNK : RXB_N) /* 첫 DMA 는 상한 길이로 */);
        for (uint32_t i = 0; i < 12u; i++) rx_line_one(x);
        CHECK(uart_rx_task(&x->u, out, sizeof out) == 0u /* IDLE 전에는 공개되지 않는다 */);
        uart_rx_isr(&x->u);
        n = uart_rx_task(&x->u, out, sizeof out);
        CHECK(n == 12u && memcmp(out, x->acc, 12u) == 0 && x->u.dma.en && x->u.seen == 12u /* DMA 는 멈추지 않고 계속 */);
        g_dma_race = NULL;
    }
    {   /* 태스크가 놀면 버퍼가 차서 DMA 를 못 건다 → 태스크가 비운 뒤 재무장 */
        rx_world_t *x = &g_rx; uint8_t out[RXB_N]; uint32_t got = 0;
        rx_world_init(x, 300u, 1u, 0u, false);
        ic_off();
        for (uint32_t i = 0; i < 100u; i++) { rx_line_one(x); if (x->tc_pending) { x->tc_pending = false; uart_rx_isr(&x->u); } }
        CHECK(atomic_load(&x->u.armed) == 0u && x->lost == 100u - RXB_N - 1u && x->hold_full /* 64 바이트 + 수신 레지스터 1 뒤로는 줄에서 사라진다 */);
        for (uint32_t i = 0; i < 4u; i++) got += uart_rx_task(&x->u, out + got, sizeof out - got);
        CHECK(got == RXB_N && memcmp(out, x->acc, RXB_N) == 0 && atomic_load(&x->u.armed) == 1u /* 재무장 */);
        uint32_t before = x->nacc;
        for (uint32_t i = 0; i < 5u; i++) rx_line_one(x);
        CHECK(x->nacc == before + 6u /* 기다리던 1 바이트가 먼저 DMA 로 간다 */);
        g_dma_race = NULL;
    }
    bad = rx_run(6000u, 1u, 5u, true, NEVER, 0, 12u, &lost);
    printf("  RX 12%%/접근, IDLE 5%%: 위반 %u, 유실 %u\n", bad, lost);
    CHECK(bad == 0u && lost == 0u /* 태스크가 따라가면 한 바이트도 안 잃는다 */);
    bad = rx_run(6000u, 2u, 10u, true, NEVER, 0, 100u, &lost);
    printf("  RX 매 접근 발사 2바이트씩: 위반 %u, 유실 %u\n", bad, lost);
    CHECK(bad == 0u && lost > 0u /* 넘치면 유실은 생기지만 받은 것은 순서·내용 정확 */);
    {
        uint32_t bad_tot = 0, runs = 0;
        for (uint32_t period = 1; period <= 7u; period++) for (uint32_t k = 0; k < period; k++) {
            bad_tot += rx_run(1500u, 1u, 15u, true, k, period, 0, NULL); runs++;
        }
        printf("  RX 주기 발사 %u 조합: 위반 %u\n", runs, bad_tot);
        CHECK(bad_tot == 0u && runs == 28u);
        bad_tot = 0; runs = 0;
        for (uint32_t k = 0; k < 800u; k++) { bad_tot += rx_run(120u, 1u, 15u, true, k, 3u, 0, NULL); runs++; }
        CHECK(bad_tot == 0u && runs == 800u /* 발사 시작 위치 전수 */);
    }

    {   /* 기본 TX */
        tx_world_t *x = &g_tx; const uint8_t msg[5] = {9,8,7,6,5};
        tx_world_init(x, 1000u);
        ic_off();
        CHECK(uart_tx_send(&x->u, msg, 5u));
        CHECK(atomic_load(&x->u.busy) == 1u && x->u.inflight == 5u && x->u.dma.en);
        g_ic.in_isr = true; tx_hw_irq(x); g_ic.in_isr = false;
        CHECK(x->nwire == 5u && memcmp(x->wire, msg, 5u) == 0 && atomic_load(&x->u.busy) == 0u && !x->u.dma.en);
    }
    uint32_t stranded = 0;
    bad = tx_run(uart_tx_send, TX_MSGS, 1000u, NEVER, 0, 25u, 0xC0FFEEu, &stranded);
    printf("  TX 25%%/접근, 큰 burst: 위반 %u, 남은 바이트 %u\n", bad, stranded);
    CHECK(bad == 0u);
    bad = tx_run(uart_tx_send, TX_MSGS, 3u, NEVER, 0, 100u, 0xBADC0DEu, &stranded);
    printf("  TX 매 접근 발사 3바이트씩: 위반 %u, 남은 바이트 %u, DMA 전송 %u회\n", bad, stranded, g_tx.nxfer);
    CHECK(bad == 0u);
    {
        uint32_t bad_tot = 0, runs = 0, stuck_tot = 0;
        for (uint32_t period = 1; period <= 9u; period++) for (uint32_t k = 0; k < period; k++) {
            bad_tot += tx_run(uart_tx_send, 60u, 1000u, k, period, 0, 0x1234567u, &stranded); stuck_tot += stranded; runs++;
        }
        printf("  TX 주기 발사 %u 조합: 위반 %u\n", runs, bad_tot);
        CHECK(bad_tot == 0u && stuck_tot == 0u);
        bad_tot = 0; runs = 0;
        for (uint32_t k = 0; k < 1500u; k++) { bad_tot += tx_run(uart_tx_send, 12u, 1000u, k, 7u, 0, 0x7654321u, NULL); runs++; }
        CHECK(bad_tot == 0u && runs == 1500u /* 발사 시작 위치 전수 */);
    }
    {   /* 나쁜 변형: commit 전에 busy 확인 → 어떤 k 에서는 데이터가 묶인다 */
        uint32_t caught = 0;
        for (uint32_t k = 0; k < 600u; k++) {
            uint32_t s = 0;
            uint32_t b = tx_run(bad_send_kick_first, 12u, 1000u, k, 0, 0, 0x7654321u, &s);
            if (b != 0u && s != 0u) caught++;
        }
        printf("  commit 전에 kick 하는 변형: 데이터가 묶인 k 개수 %u / 600\n", caught);
        CHECK(caught > 0u);
    }
    g_dma_race = NULL;
}

int main(void) {
    test_part_a();
    test_part_b();
    test_part_c();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
