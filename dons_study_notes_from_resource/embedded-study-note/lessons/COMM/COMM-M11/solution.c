/* COMM-M11 DMA 기반 UART 수신: circular DMA + idle line 감지
 *
 * 모델 (STM32 USART RX + DMA circular 를 흉내 낸 결정적 시뮬레이터 — 레지스터 이름은 대표값, RM 확인 권장):
 *   - DMA 버퍼 buf[N] (N 짝수, ≤ 256). NDTR 은 N 에서 시작해 바이트마다 1 감소, 0 이 되면 TC 플래그 + N 으로 재장전.
 *   - 쓰기 위치 pos = N − NDTR   (NDTR == N → 0. NDTR 을 0 으로 읽는 경계값(HAL 의 Size == N 과 같은 꼴)도 → 0).
 *   - HT 플래그: 칸 N/2−1 을 쓴 직후 (NDTR == N/2). TC 플래그: 칸 N−1 을 쓴 직후.
 *     플래그는 인터럽트 enable 과 상관없이 선다. 인터럽트를 부를지는 시뮬레이터 설정(ht_irq)이 정한다.
 *   - IDLE 플래그: 마지막 바이트 뒤 라인이 1 프레임 이상 조용하면. IDLE 뒤 새 바이트가 와야 다시 선다.
 *   - HT / TC / IDLE 세 IRQ 가 모두 같은 함수 rx_dma_irq() 를 부른다 (플래그 클리어 → NDTR 1 회 읽기 → 처리).
 *   - 시간 단위 = 1 바이트 시간 (8N1 이면 10 비트). 한 슬롯에 바이트 하나가 오거나 라인이 조용하다.
 *
 * Part A: dma_pos (NDTR → 위치), rx_dma_process (last..pos 를 sink 로, 랩어라운드면 두 조각)
 * Part B: rx_lap_suspect — 핸들러 사이에 N 바이트 이상 와서 한 바퀴 돌았는지 HT/TC 플래그로 의심
 * Part C: app_check_seq (프레임 SEQ 끊김 = 오버런 검출기), rx_poll_from_task (태스크에서 부를 때 임계 구역)
 * 테스트: 위치 표, 명명된 경계 사례, 고정 시드 xorshift32 랜덤 도착/이벤트 순서 3000 케이스, 랩 검출 전수 조사,
 *         프레임 스트림 + 오버런 주입, IRQ 가 태스크를 선점하는 결정적 주입.
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define DMA_MAX_N 256u

/* ---------------- mock 하드웨어: USART RX → DMA stream (circular) ---------------- */

typedef struct {
    uint8_t  buf[DMA_MAX_N];   /* DMA 대상 메모리 (앞 n 칸만 사용) */
    uint16_t n;                /* NDTR 초기값 = 버퍼 크기 (짝수) */
    uint16_t ndtr;             /* 남은 전송 수: n → 1, 0 이 되면 n 으로 재장전 */
    bool     ht, tc, idle;     /* pending 플래그 (HTIF, TCIF, USART IDLE) */
    bool     rx_since_idle;    /* IDLE 은 지난 IDLE 이후 바이트가 1 개 이상 왔을 때만 선다 */
    bool     ndtr_reads_zero;  /* 모델 옵션: 칸 n−1 을 쓴 뒤 다음 바이트 전까지 NDTR 을 0 으로 읽게 (경계값 시험) */
    uint32_t written;          /* 테스트 전용: DMA 가 지금까지 쓴 바이트 총수 (펌웨어는 모른다) */
} dma_hw;

static void hw_init(dma_hw *h, uint16_t n) {
    memset(h, 0, sizeof *h);
    h->n = n;
    h->ndtr = n;
}

static void hw_rx_byte(dma_hw *h, uint8_t b) {        /* UART 가 바이트 하나를 받고 DMA 가 옮긴다 */
    if (h->ndtr == 0) h->ndtr = h->n;                 /* (ndtr_reads_zero 모델) 늦은 재장전 */
    h->buf[h->n - h->ndtr] = b;
    h->ndtr--;
    h->written++;
    h->rx_since_idle = true;
    if (h->ndtr == h->n / 2) h->ht = true;
    if (h->ndtr == 0) {
        h->tc = true;
        if (!h->ndtr_reads_zero) h->ndtr = h->n;
    }
}

static void hw_line_idle(dma_hw *h) {                 /* 바이트 시간 하나 동안 라인이 조용했다 */
    if (h->rx_since_idle) { h->idle = true; h->rx_since_idle = false; }
}

static uint16_t hw_read_ndtr(const dma_hw *h) { return h->ndtr; }

/* ---------------- 펌웨어: circular DMA 수신 핸들러 ---------------- */

typedef void (*rx_sink)(void *ctx, const uint8_t *p, size_t len);

typedef struct {
    uint32_t calls;        /* 핸들러 진입 수 */
    uint32_t empty_calls;  /* 새 바이트 0 개 (늦게 온 IDLE, 이미 처리한 HT 등) */
    uint32_t wraps;        /* pos < last 라서 [last,N) + [0,pos) 로 나눈 횟수 */
    uint32_t bytes;        /* sink 로 넘긴 총 바이트 */
    uint32_t max_batch;    /* 한 번에 넘긴 최대 바이트 = 여유 지표 (N 에 가까우면 위험) */
    uint32_t lap_suspect;  /* Part B: 한 바퀴 이상 돌았다고 의심한 횟수 */
} rx_stats;

typedef struct {
    dma_hw  *hw;
    uint16_t last;         /* 다음에 처리할 칸 = 지난번 pos */
    rx_sink  sink;
    void    *ctx;
    rx_stats st;
} uart_dma_rx;

static void rx_init(uart_dma_rx *rx, dma_hw *hw, rx_sink sink, void *ctx) {
    memset(rx, 0, sizeof *rx);
    rx->hw = hw;
    rx->sink = sink;
    rx->ctx = ctx;
}

/* ---- Part A ---- */

/* NDTR → 쓰기 위치. NDTR == n 이면 0, 재장전 전에 0 으로 읽히면 n − 0 = n → 0 으로 접는다. 결과는 항상 [0, n). */
static uint16_t dma_pos(uint16_t n, uint16_t ndtr) {
    uint16_t pos = (uint16_t)(n - ndtr);
    return pos >= n ? 0 : pos;
}

/* [last, pos) 를 sink 로 넘긴다. pos < last 면 [last, n) 다음 [0, pos). pos == last 면 아무것도 안 함.
 * 통계(empty_calls, wraps, bytes, max_batch)를 갱신하고 last = pos. 반환: 넘긴 바이트 수. */
static size_t rx_dma_process(uart_dma_rx *rx, uint16_t pos) {
    uint16_t n = rx->hw->n, last = rx->last;
    size_t total;
    if (pos == last) { rx->st.empty_calls++; return 0; }
    if (pos > last) {
        total = (size_t)(pos - last);
        rx->sink(rx->ctx, &rx->hw->buf[last], total);
    } else {
        rx->sink(rx->ctx, &rx->hw->buf[last], (size_t)(n - last));
        if (pos > 0) rx->sink(rx->ctx, &rx->hw->buf[0], pos);
        total = (size_t)(n - last) + pos;
        rx->st.wraps++;
    }
    rx->last = pos;
    rx->st.bytes += (uint32_t)total;
    if (total > rx->st.max_batch) rx->st.max_batch = (uint32_t)total;
    return total;
}

/* ---- Part B ---- */

/* 핸들러 사이 새 바이트 수 d 는 d0 = (pos − last) mod n 일 수도, d0 + n, d0 + 2n … 일 수도 있다 (NDTR 만으론 모름).
 * d0 칸 [last, last + d0) (mod n) 이 칸 n/2−1 을 지나지 않는데 HT 가 서 있거나, 칸 n−1 을 지나지 않는데 TC 가
 * 서 있으면 d ≥ n 이 확실 → true. (d0 구간이 두 칸을 다 지나면 한 바퀴 더 돌아도 플래그로는 모른다 — 한계) */
static bool rx_lap_suspect(uint16_t last, uint16_t pos, uint16_t n, bool ht, bool tc) {
    uint16_t d0 = (uint16_t)((pos + n - last) % n);
    bool ht_exp = (uint16_t)((n / 2u - 1u + n - last) % n) < d0;   /* 칸 c ∈ 구간 ⇔ (c − last) mod n < d0 */
    bool tc_exp = (uint16_t)((n - 1u + n - last) % n) < d0;
    return (ht && !ht_exp) || (tc && !tc_exp);
}

/* HT / TC / IDLE 공통 IRQ 진입점: 플래그 스냅샷+클리어 → NDTR 딱 한 번 → 의심 검사 → 처리 */
static size_t rx_dma_irq(uart_dma_rx *rx) {
    dma_hw *h = rx->hw;
    bool ht = h->ht, tc = h->tc;
    h->ht = h->tc = h->idle = false;                   /* 클리어 먼저, NDTR 은 그다음 (연습 3) */
    uint16_t pos = dma_pos(h->n, hw_read_ndtr(h));
    rx->st.calls++;
    if (rx_lap_suspect(rx->last, pos, h->n, ht, tc)) rx->st.lap_suspect++;
    return rx_dma_process(rx, pos);
}

/* ---- Part C: 임계 구역 모델 (PRIMASK 흉내) ---- */

static bool         g_irq_masked;     /* __disable_irq() 상태 */
static bool         g_irq_pending;    /* 막혀 있는 동안 들어온 DMA/USART IRQ */
static uart_dma_rx *g_irq_rx;         /* IRQ 가 처리할 수신기 */

static void irq_raise(void) {         /* 하드웨어가 IRQ 를 올린다: 막혀 있으면 pending, 아니면 즉시 선점 */
    if (g_irq_masked) g_irq_pending = true;
    else rx_dma_irq(g_irq_rx);
}
static void irq_disable(void) { g_irq_masked = true; }
static void irq_enable(void) {
    g_irq_masked = false;
    if (g_irq_pending) { g_irq_pending = false; rx_dma_irq(g_irq_rx); }   /* 미뤄진 IRQ 가 바로 실행 */
}

/* 태스크(예: 수신 타임아웃 폴링)에서 같은 핸들러를 부른다 — IRQ 와 last 를 공유하므로 임계 구역 안에서. */
static size_t rx_poll_from_task(uart_dma_rx *rx) {
    irq_disable();
    size_t got = rx_dma_irq(rx);
    irq_enable();
    return got;
}

/* 비교용(일부러 틀린 것): 임계 구역 없이 태스크에서 호출 */
static size_t naive_poll_from_task(uart_dma_rx *rx) { return rx_dma_irq(rx); }

/* ---- Part C: 프레임 계층 (COMM-B10 모델 축약판: [A5][LEN][SEQ][PAYLOAD][CRC16 BE], CRC-16/CCITT-FALSE) ---- */

#define F_SOF   0xA5u
#define F_MAXPL 32u

static uint16_t crc16_upd(uint16_t c, uint8_t b) {
    c ^= (uint16_t)(b << 8);
    for (int k = 0; k < 8; k++) c = (c & 0x8000u) ? (uint16_t)((c << 1) ^ 0x1021u) : (uint16_t)(c << 1);
    return c;
}

static size_t frame_encode(uint8_t seq, const uint8_t *pl, uint8_t len, uint8_t *out, size_t cap) {
    if (len > F_MAXPL || cap < (size_t)len + 5u) return 0;
    uint16_t c = 0xFFFFu;
    out[0] = F_SOF; out[1] = len; out[2] = seq;
    if (len) memcpy(out + 3, pl, len);
    for (size_t i = 1; i < (size_t)len + 3u; i++) c = crc16_upd(c, out[i]);
    out[len + 3] = (uint8_t)(c >> 8);
    out[len + 4] = (uint8_t)c;
    return (size_t)len + 5u;
}

enum { FS_SOF, FS_LEN, FS_SEQ, FS_PL, FS_CH, FS_CL };
typedef void (*frame_fn)(void *ctx, uint8_t seq, const uint8_t *pl, uint8_t len);
typedef struct {
    int      st;
    uint8_t  len, seq, i, ch;
    uint8_t  pl[F_MAXPL];
    uint16_t crc;
    uint32_t ok, crc_err, len_err;
    frame_fn on_frame;
    void    *ctx;
} fparser;

static void fp_feed(fparser *p, uint8_t b) {
    switch (p->st) {
    case FS_SOF: if (b == F_SOF) { p->st = FS_LEN; p->crc = 0xFFFFu; } break;
    case FS_LEN:
        if (b > F_MAXPL) { p->len_err++; p->st = FS_SOF; break; }
        p->len = b; p->crc = crc16_upd(p->crc, b); p->st = FS_SEQ; break;
    case FS_SEQ: p->seq = b; p->crc = crc16_upd(p->crc, b); p->i = 0; p->st = p->len ? FS_PL : FS_CH; break;
    case FS_PL:
        p->pl[p->i++] = b; p->crc = crc16_upd(p->crc, b);
        if (p->i == p->len) p->st = FS_CH;
        break;
    case FS_CH: p->ch = b; p->st = FS_CL; break;
    default: {
        uint16_t got = (uint16_t)((p->ch << 8) | b);
        p->st = FS_SOF;
        if (got == p->crc) { p->ok++; if (p->on_frame) p->on_frame(p->ctx, p->seq, p->pl, p->len); }
        else p->crc_err++;
    } break;
    }
}

/* 응용 계층의 SEQ 검사 = 오버런(조용한 손실) 검출기 */
typedef struct {
    bool     synced;
    uint8_t  expect;
    uint32_t frames, seq_gaps, lost_est;
} app_seq;

/* 첫 프레임은 동기만 맞춘다. 그다음부터 seq != expect 면 seq_gaps++ 그리고 lost_est += (seq − expect) mod 256.
 * 마지막에 expect = seq + 1 (mod 256). (frames 는 호출하는 쪽이 센다) */
static void app_check_seq(app_seq *a, uint8_t seq) {
    if (a->synced && seq != a->expect) {
        a->seq_gaps++;
        a->lost_est += (uint8_t)(seq - a->expect);
    }
    a->synced = true;
    a->expect = (uint8_t)(seq + 1u);
}

/* ======================================================================= */
/*                                 테스트                                   */
/* ======================================================================= */

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

/* 받은 바이트 수집 sink */
#define COLLECT_MAX 65536u
static uint8_t g_sent[COLLECT_MAX], g_got[COLLECT_MAX];
static size_t  g_sent_n, g_got_n;
static bool    g_got_ovf;
static size_t  g_chunk_len[8];
static int     g_chunks;
static void  (*g_sink_hook)(void);                     /* 한 번만 실행되는 주입점 (sink 안 = last 저장 전) */

static void collect_reset(void) { g_sent_n = g_got_n = 0; g_got_ovf = false; g_chunks = 0; }
static void sink_collect(void *ctx, const uint8_t *p, size_t len) {
    (void)ctx;
    if (g_chunks < 8) g_chunk_len[g_chunks] = len;
    g_chunks++;
    if (g_got_n + len > COLLECT_MAX) { g_got_ovf = true; return; }
    memcpy(g_got + g_got_n, p, len);
    g_got_n += len;
    if (g_sink_hook) { void (*h)(void) = g_sink_hook; g_sink_hook = NULL; h(); }
}
static bool got_equals_sent(void) {
    return !g_got_ovf && g_got_n == g_sent_n && memcmp(g_got, g_sent, g_sent_n) == 0;
}

/* 슬롯 단위 시뮬레이터: 바이트 하나 또는 idle 하나 → 이벤트 → (지연 뒤) 핸들러 */
typedef struct {
    dma_hw      hw;
    uart_dma_rx rx;
    bool        ht_irq;        /* HT 인터럽트 enable */
    uint32_t    lat_max;       /* IRQ 지연 0..lat_max 슬롯 (균등) */
    uint32_t    guard;         /* ≠0: 마지막 실행 뒤 guard 바이트가 쌓이면 강제 실행 (오버런 없는 테스트용) */
    uint32_t    spurious_pct;  /* 매 슬롯 이 확률(%)로 이벤트 없이 핸들러 실행 */
    uint32_t    block_until;   /* hw.written 이 이 값에 닿기 전엔 핸들러 금지 (IRQ 블랙아웃 = 오버런 주입) */
    int32_t     delay;         /* -1: 대기 중인 IRQ 없음 */
    uint32_t    written_at_run, runs, both_pending, overruns;
    uint32_t    seed;
} sim_t;

static void sim_init(sim_t *s, uint16_t n, rx_sink sink, void *ctx, uint32_t seed) {
    memset(s, 0, sizeof *s);
    hw_init(&s->hw, n);
    rx_init(&s->rx, &s->hw, sink, ctx);
    s->ht_irq = true;
    s->delay = -1;
    s->seed = seed;
}

static void sim_run_handler(sim_t *s) {
    if (s->hw.ht && s->hw.tc) s->both_pending++;
    if (s->hw.written - s->written_at_run >= s->hw.n) s->overruns++;   /* 정답지: 한 바퀴 이상 */
    rx_dma_irq(&s->rx);
    s->runs++;
    s->written_at_run = s->hw.written;
    s->delay = -1;
}

static void sim_slot(sim_t *s, int byte) {                           /* byte < 0 → idle 슬롯 */
    if (byte >= 0) { hw_rx_byte(&s->hw, (uint8_t)byte); if (g_sent_n < COLLECT_MAX) g_sent[g_sent_n++] = (uint8_t)byte; }
    else hw_line_idle(&s->hw);
    bool ev = s->hw.tc || s->hw.idle || (s->ht_irq && s->hw.ht);
    if (ev && s->delay < 0) s->delay = s->lat_max ? (int32_t)(xs32(&s->seed) % (s->lat_max + 1u)) : 0;
    bool run = false;
    if (s->delay == 0) run = true;
    else if (s->delay > 0) s->delay--;
    if (s->guard && s->hw.written - s->written_at_run >= s->guard) run = true;
    if (s->spurious_pct && xs32(&s->seed) % 100u < s->spurious_pct) run = true;
    if (s->hw.written < s->block_until) run = false;                 /* 블랙아웃: pending 은 유지 */
    if (run) sim_run_handler(s);
}

static void sim_flush(sim_t *s) {                                    /* 꼬리: 조용히 두고 마지막에 한 번 */
    for (uint32_t k = 0; k < s->lat_max + 2u; k++) sim_slot(s, -1);
    sim_run_handler(s);
}

/* ---- Part C 프레임 수신 콜백 ---- */
#define NFR 1500
typedef struct { uint8_t seq, len; uint8_t pl[F_MAXPL]; } frame_rec;
static frame_rec g_sent_fr[NFR], g_rx_fr[NFR];
static size_t    g_rx_fr_n;
typedef struct { fparser fp; app_seq app; } link_ctx;

static void on_frame(void *ctx, uint8_t seq, const uint8_t *pl, uint8_t len) {
    link_ctx *L = ctx;
    L->app.frames++;
    app_check_seq(&L->app, seq);
    if (g_rx_fr_n < NFR) {
        frame_rec *r = &g_rx_fr[g_rx_fr_n++];
        r->seq = seq; r->len = len; memcpy(r->pl, pl, len);
    }
}
static void sink_to_parser(void *ctx, const uint8_t *p, size_t len) {
    link_ctx *L = ctx;
    for (size_t i = 0; i < len; i++) fp_feed(&L->fp, p[i]);
}
static bool rx_frames_are_subsequence(size_t nsent) {               /* 받은 프레임 = 보낸 프레임의 순서 유지 부분열 */
    size_t j = 0;
    for (size_t i = 0; i < g_rx_fr_n; i++) {
        while (j < nsent && !(g_sent_fr[j].seq == g_rx_fr[i].seq && g_sent_fr[j].len == g_rx_fr[i].len &&
                              memcmp(g_sent_fr[j].pl, g_rx_fr[i].pl, g_rx_fr[i].len) == 0)) j++;
        if (j == nsent) return false;
        j++;
    }
    return true;
}

/* ---- Part C 선점 주입: 태스크가 sink 안(= last 저장 전)에 있을 때 바이트 5 개 + IRQ ---- */
static dma_hw     *g_pre_hw;
static const char *g_pre_more = "56789";
static void hook_bytes_then_irq(void) {
    for (int i = 0; g_pre_more[i]; i++) {
        hw_rx_byte(g_pre_hw, (uint8_t)g_pre_more[i]);
        g_sent[g_sent_n++] = (uint8_t)g_pre_more[i];
    }
    hw_line_idle(g_pre_hw);
    irq_raise();
}

int main(void) {
    /* ---------------- Part A ---------------- */
    /* A1. NDTR → pos 표 */
    CHECK(dma_pos(16, 16) == 0);
    CHECK(dma_pos(16, 15) == 1);
    CHECK(dma_pos(16, 8) == 8);
    CHECK(dma_pos(16, 1) == 15);
    CHECK(dma_pos(16, 0) == 0);                                      /* 재장전 전 0 으로 읽힘 → 0 */
    CHECK(dma_pos(256, 256) == 0 && dma_pos(256, 1) == 255);

    /* A2. 기본: N=16, "HELLO" 뒤 IDLE → 5 바이트 한 조각 */
    {
        dma_hw hw; uart_dma_rx rx;
        collect_reset(); hw_init(&hw, 16); rx_init(&rx, &hw, sink_collect, NULL);
        const char *m = "HELLO";
        for (int i = 0; m[i]; i++) { hw_rx_byte(&hw, (uint8_t)m[i]); g_sent[g_sent_n++] = (uint8_t)m[i]; }
        hw_line_idle(&hw);
        CHECK(hw.idle && !hw.ht && !hw.tc);
        size_t k = rx_dma_irq(&rx);
        CHECK(k == 5 && got_equals_sent() && g_chunks == 1 && rx.last == 5);
        hw_line_idle(&hw);                                           /* 계속 조용 → IDLE 다시 안 섬 */
        CHECK(!hw.idle);
    }

    /* A3. 랩어라운드: last = 12, 7 바이트 더 → [12,16) 4 바이트 + [0,3) 3 바이트, 두 조각 */
    {
        dma_hw hw; uart_dma_rx rx;
        collect_reset(); hw_init(&hw, 16); rx_init(&rx, &hw, sink_collect, NULL);
        for (int i = 0; i < 12; i++) { hw_rx_byte(&hw, (uint8_t)i); g_sent[g_sent_n++] = (uint8_t)i; }
        rx_dma_irq(&rx);
        g_chunks = 0;
        for (int i = 12; i < 19; i++) { hw_rx_byte(&hw, (uint8_t)i); g_sent[g_sent_n++] = (uint8_t)i; }
        size_t k = rx_dma_irq(&rx);
        CHECK(k == 7 && g_chunks == 2 && g_chunk_len[0] == 4 && g_chunk_len[1] == 3);
        CHECK(got_equals_sent() && rx.last == 3 && rx.st.wraps == 1);
        /* 정확히 끝 칸에서 멈춘 경우: last = 3 → 13 바이트 더 = pos 16 → 0, 조각 하나 */
        g_chunks = 0;
        for (int i = 19; i < 32; i++) { hw_rx_byte(&hw, (uint8_t)i); g_sent[g_sent_n++] = (uint8_t)i; }
        k = rx_dma_irq(&rx);
        CHECK(k == 13 && g_chunks == 1 && rx.last == 0 && got_equals_sent());
    }

    /* A4. 랜덤: N ∈ {8,16,32,64,256}, 버스트/idle/지연/가짜 호출 무작위, 실행 사이 ≤ N−1 바이트 보장 */
    {
        static const uint16_t ns[] = { 8, 16, 32, 64, 256 };
        uint32_t seed = 0x11D1E5u, cases = 3000, bad = 0, both = 0, empty = 0, wraps = 0, laps = 0;
        uint64_t bytes = 0;
        sim_t s;
        for (uint32_t c = 0; c < cases; c++) {
            uint16_t n = ns[xs32(&seed) % 5u];
            collect_reset();
            sim_init(&s, n, sink_collect, NULL, xs32(&seed));
            s.lat_max = xs32(&seed) % (2u * n);                     /* 지연은 N 보다 길 수도 — guard 가 막는다 */
            s.guard = n - 1u;
            s.spurious_pct = xs32(&seed) % 6u;
            s.ht_irq = (xs32(&seed) & 3u) != 0;
            uint32_t total = 20u + xs32(&seed) % (4u * n + 200u);
            for (uint32_t sent = 0; sent < total; ) {
                uint32_t burst = 1u + xs32(&seed) % (2u * n);
                for (uint32_t b = 0; b < burst && sent < total; b++, sent++) sim_slot(&s, (int)(xs32(&seed) & 0xFFu));
                uint32_t gap = xs32(&seed) % 4u;
                for (uint32_t g = 0; g < gap; g++) sim_slot(&s, -1);
            }
            sim_flush(&s);
            if (!got_equals_sent() || s.overruns) bad++;
            both += s.both_pending; empty += s.rx.st.empty_calls; wraps += s.rx.st.wraps;
            laps += s.rx.st.lap_suspect; bytes += g_sent_n;
        }
        printf("A4 random: %u cases, %llu bytes, wraps=%u empty_calls=%u HT+TC-both-pending=%u lap_suspect=%u bad=%u\n",
               cases, (unsigned long long)bytes, wraps, empty, both, laps, bad);
        CHECK(bad == 0);
        CHECK(laps == 0);                                            /* 오버런 없는 실행에서 거짓 경보 0 */
        CHECK(both > 0 && empty > 0 && wraps > 0);                   /* 그 경우들이 실제로 나왔다 (커버리지) */
    }

    /* ---------------- Part B ---------------- */
    /* B1. 새 바이트 0 개 이벤트: HT 에서 다 처리했는데 IDLE 이 또 옴 */
    {
        dma_hw hw; uart_dma_rx rx;
        collect_reset(); hw_init(&hw, 16); rx_init(&rx, &hw, sink_collect, NULL);
        for (int i = 0; i < 8; i++) { hw_rx_byte(&hw, (uint8_t)i); g_sent[g_sent_n++] = (uint8_t)i; }
        CHECK(hw.ht);
        rx_dma_irq(&rx);                                             /* HT IRQ: 8 바이트 */
        hw_line_idle(&hw);                                           /* 같은 8 바이트 뒤의 IDLE */
        size_t k = rx_dma_irq(&rx);
        CHECK(k == 0 && rx.st.empty_calls == 1 && g_chunks == 1 && got_equals_sent() && rx.st.lap_suspect == 0);
    }

    /* B2. HT 와 TC 가 함께 pending (핸들러가 칸 7 이전부터 칸 15 이후까지 늦음, 하지만 < N) */
    {
        dma_hw hw; uart_dma_rx rx;
        collect_reset(); hw_init(&hw, 16); rx_init(&rx, &hw, sink_collect, NULL);
        for (int i = 0; i < 5; i++) { hw_rx_byte(&hw, (uint8_t)i); g_sent[g_sent_n++] = (uint8_t)i; }
        rx_dma_irq(&rx);                                             /* last = 5 */
        for (int i = 5; i < 5 + 14; i++) { hw_rx_byte(&hw, (uint8_t)i); g_sent[g_sent_n++] = (uint8_t)i; }
        CHECK(hw.ht && hw.tc);
        size_t k = rx_dma_irq(&rx);
        CHECK(k == 14 && got_equals_sent() && rx.st.lap_suspect == 0 && rx.last == 3);
    }

    /* B3. 정확히 N 바이트 사이 → pos == last → "새 바이트 0" 으로 보임 (모호!) → 의심 플래그 */
    {
        dma_hw hw; uart_dma_rx rx;
        collect_reset(); hw_init(&hw, 16); rx_init(&rx, &hw, sink_collect, NULL);
        for (int i = 0; i < 10; i++) { hw_rx_byte(&hw, (uint8_t)i); g_sent[g_sent_n++] = (uint8_t)i; }
        rx_dma_irq(&rx);                                             /* last = 10 */
        for (int i = 0; i < 16; i++) { hw_rx_byte(&hw, (uint8_t)(100 + i)); g_sent[g_sent_n++] = (uint8_t)(100 + i); }
        size_t k = rx_dma_irq(&rx);
        CHECK(k == 0 && g_got_n == 10);                              /* 16 바이트 조용히 사라짐 */
        CHECK(rx.st.lap_suspect == 1);                               /* 검출기는 잡는다 */
    }

    /* B4. N + 5 바이트 → 마지막 5 바이트만 넘어오고 16 바이트 손실 → 의심 */
    {
        dma_hw hw; uart_dma_rx rx;
        collect_reset(); hw_init(&hw, 16); rx_init(&rx, &hw, sink_collect, NULL);
        for (int i = 0; i < 10; i++) { hw_rx_byte(&hw, (uint8_t)i); g_sent[g_sent_n++] = (uint8_t)i; }
        rx_dma_irq(&rx);
        for (int i = 0; i < 21; i++) { hw_rx_byte(&hw, (uint8_t)(100 + i)); g_sent[g_sent_n++] = (uint8_t)(100 + i); }
        size_t k = rx_dma_irq(&rx);
        CHECK(k == 5 && g_got_n == 15 && memcmp(g_got + 10, g_sent + 26, 5) == 0);  /* 최신 5 바이트 */
        CHECK(rx.st.lap_suspect == 1);
    }

    /* B5. 플래그로는 못 잡는 오버런: last = 7, d = 16 + 15 → d0 구간이 칸 7·15 를 다 지남 */
    {
        dma_hw hw; uart_dma_rx rx;
        collect_reset(); hw_init(&hw, 16); rx_init(&rx, &hw, sink_collect, NULL);
        for (int i = 0; i < 7; i++) { hw_rx_byte(&hw, (uint8_t)i); g_sent[g_sent_n++] = (uint8_t)i; }
        rx_dma_irq(&rx);
        for (int i = 0; i < 31; i++) { hw_rx_byte(&hw, (uint8_t)(100 + i)); g_sent[g_sent_n++] = (uint8_t)(100 + i); }
        size_t k = rx_dma_irq(&rx);
        CHECK(k == 15 && g_got_n == 22);                             /* 16 바이트 손실 */
        CHECK(rx.st.lap_suspect == 0);                               /* 한계: 플래그로는 안 보임 → Part C 의 SEQ */
    }

    /* B6. 랩 검출기 전수 조사: N ∈ {8,16,64}, last ∈ [0,N), d ∈ [0,3N] — 무차별 대입 기대값과 비교 */
    {
        static const uint16_t ns[] = { 8, 16, 64 };
        uint32_t mism = 0, lt_flag = 0, eqn_miss = 0;
        for (int t = 0; t < 3; t++) {
            uint16_t n = ns[t];
            uint32_t lossy = 0, caught = 0;
            for (uint16_t last = 0; last < n; last++) {
                for (uint32_t d = 0; d <= 3u * n; d++) {
                    dma_hw hw; uart_dma_rx rx;
                    collect_reset(); hw_init(&hw, n); rx_init(&rx, &hw, sink_collect, NULL);
                    for (uint16_t i = 0; i < last; i++) hw_rx_byte(&hw, 0);
                    rx_dma_irq(&rx);
                    for (uint32_t i = 0; i < d; i++) hw_rx_byte(&hw, 1);
                    bool eht = hw.ht, etc = hw.tc;
                    uint32_t d0 = d % n;
                    bool cov_ht = false, cov_tc = false;           /* 무차별: d0 구간이 덮는 칸을 하나씩 */
                    for (uint32_t k = 0; k < d0; k++) {
                        uint32_t cell = (last + k) % n;
                        if (cell == n / 2u - 1u) cov_ht = true;
                        if (cell == n - 1u) cov_tc = true;
                    }
                    bool expect = (eht && !cov_ht) || (etc && !cov_tc);
                    rx_dma_irq(&rx);
                    bool got = rx.st.lap_suspect == 1;
                    if (got != expect) mism++;
                    if (d < n && got) lt_flag++;
                    if (d == n && !got) eqn_miss++;
                    if (d > n) { lossy++; if (got) caught++; }
                }
            }
            printf("B6 N=%3u: overrun (d>N) cases %5u, caught by HT/TC check %5u (%.1f%%)\n",
                   n, lossy, caught, 100.0 * caught / lossy);
        }
        CHECK(mism == 0);
        CHECK(lt_flag == 0);                                         /* d < N: 절대 경보 없음 */
        CHECK(eqn_miss == 0);                                        /* d == N (모호한 경우): 항상 잡음 */
    }

    /* B7. NDTR 이 0 으로 읽히는 경계값 (HAL 의 TC 콜백 Size == N 과 같은 꼴) */
    {
        dma_hw hw; uart_dma_rx rx;
        collect_reset(); hw_init(&hw, 16); rx_init(&rx, &hw, sink_collect, NULL);
        hw.ndtr_reads_zero = true;
        for (int i = 0; i < 12; i++) { hw_rx_byte(&hw, (uint8_t)i); g_sent[g_sent_n++] = (uint8_t)i; }
        rx_dma_irq(&rx);
        for (int i = 12; i < 16; i++) { hw_rx_byte(&hw, (uint8_t)i); g_sent[g_sent_n++] = (uint8_t)i; }
        CHECK(hw_read_ndtr(&hw) == 0);
        size_t k = rx_dma_irq(&rx);                                  /* pos = 16 → 0 */
        CHECK(k == 4 && rx.last == 0 && got_equals_sent());
        for (int i = 16; i < 20; i++) { hw_rx_byte(&hw, (uint8_t)i); g_sent[g_sent_n++] = (uint8_t)i; }
        k = rx_dma_irq(&rx);
        CHECK(k == 4 && got_equals_sent() && rx.st.lap_suspect == 0);
    }

    /* B8. HT 끄고 TC+IDLE 만, 쉬지 않는 연속 스트림, 핸들러 지연 0 → 매 TC 에서 pos == last == 0 → 전부 손실 */
    {
        sim_t s;
        collect_reset();
        sim_init(&s, 16, sink_collect, NULL, 1u);
        s.ht_irq = false;
        for (int i = 0; i < 160; i++) sim_slot(&s, i & 0xFF);       /* 10 바퀴, idle 없음 */
        CHECK(g_got_n == 0 && s.rx.st.empty_calls == 10);
        CHECK(s.rx.st.lap_suspect == 10);                            /* 검출기는 매번 경보 */
        s.ht_irq = true;                                             /* HT 켜면 같은 스트림이 무손실 */
        collect_reset();
        sim_init(&s, 16, sink_collect, NULL, 1u);
        for (int i = 0; i < 160; i++) sim_slot(&s, i & 0xFF);
        CHECK(got_equals_sent() && s.rx.st.lap_suspect == 0);
    }

    /* ---------------- Part C ---------------- */
    /* C1. 프레임 스트림 (N=64, HT 켬, 지연 ≤ N/2−1): 무손실 / 블랙아웃 10 회 주입: 전부 검출 */
    {
        uint16_t c = 0xFFFFu;
        for (const char *q = "123456789"; *q; q++) c = crc16_upd(c, (uint8_t)*q);
        CHECK(c == 0x29B1u);                                         /* CRC-16/CCITT-FALSE check (RevEng) */

        for (int scen = 0; scen < 2; scen++) {
            static link_ctx L;
            sim_t s;
            memset(&L, 0, sizeof L);
            L.fp.on_frame = on_frame; L.fp.ctx = &L;
            collect_reset(); g_rx_fr_n = 0;
            sim_init(&s, 64, sink_to_parser, &L, 0xC0FFEEu + (uint32_t)scen);
            s.lat_max = 64u / 2u - 1u;
            uint32_t seed = 0xF00Du, blackouts = 0, detected = 0;
            uint32_t by_lap = 0, by_crc = 0, by_seq = 0, snap_lap = 0, snap_crc = 0, snap_seq = 0;
            for (int k = 0; k < NFR; k++) {
                if (scen == 1 && k >= 100 && k % 150 == 100) {        /* 블랙아웃 시작: N + r 바이트 동안 IRQ 금지 */
                    if (blackouts) {                                 /* 지난 블랙아웃 창에서 무엇이 울렸나 */
                        uint32_t lap = s.rx.st.lap_suspect, crc = L.fp.crc_err + L.fp.len_err, sq = L.app.seq_gaps;
                        detected += (lap + crc + sq) > (snap_lap + snap_crc + snap_seq);
                        by_lap += lap > snap_lap; by_crc += crc > snap_crc; by_seq += sq > snap_seq;
                    }
                    snap_lap = s.rx.st.lap_suspect; snap_crc = L.fp.crc_err + L.fp.len_err; snap_seq = L.app.seq_gaps;
                    s.block_until = s.written_at_run + 64u + 1u + xs32(&seed) % 64u;
                    blackouts++;
                }
                frame_rec *f = &g_sent_fr[k];
                f->seq = (uint8_t)k;
                f->len = (uint8_t)(xs32(&seed) % (F_MAXPL + 1u));
                for (uint8_t i = 0; i < f->len; i++) f->pl[i] = (uint8_t)xs32(&seed);
                uint8_t fb[F_MAXPL + 5];
                size_t fn = frame_encode(f->seq, f->pl, f->len, fb, sizeof fb);
                for (size_t i = 0; i < fn; i++) sim_slot(&s, fb[i]);
                uint32_t gap = xs32(&seed) % 5u;                     /* 0 이면 프레임이 붙어서 온다 */
                for (uint32_t g = 0; g < gap; g++) sim_slot(&s, -1);
            }
            sim_flush(&s);
            if (scen == 1) {                                         /* 마지막 블랙아웃 창 */
                uint32_t lap = s.rx.st.lap_suspect, crc = L.fp.crc_err + L.fp.len_err, sq = L.app.seq_gaps;
                detected += (lap + crc + sq) > (snap_lap + snap_crc + snap_seq);
                by_lap += lap > snap_lap; by_crc += crc > snap_crc; by_seq += sq > snap_seq;
            }
            /* 기동(bring-up) 엔지니어가 읽는 한 줄 로그 */
            printf("C1 %-9s uart1 rx: calls=%u empty=%u wraps=%u bytes=%u max_batch=%u/64 lap_suspect=%u | "
                   "frames ok=%u crc_err=%u len_err=%u seq_gaps=%u lost_est=%u (sent %d, overrun runs %u)\n",
                   scen ? "blackout" : "normal", s.rx.st.calls, s.rx.st.empty_calls, s.rx.st.wraps, s.rx.st.bytes,
                   s.rx.st.max_batch, s.rx.st.lap_suspect, L.fp.ok, L.fp.crc_err, L.fp.len_err, L.app.seq_gaps,
                   L.app.lost_est, NFR, s.overruns);
            CHECK(rx_frames_are_subsequence(NFR));                   /* 가짜·뒤섞인 프레임 0 */
            if (scen == 0) {
                CHECK(L.app.frames == NFR && L.app.seq_gaps == 0 && L.fp.crc_err == 0 && s.overruns == 0);
                CHECK(s.rx.st.max_batch <= 63u && s.rx.st.lap_suspect == 0);
            } else {
                printf("   blackouts=%u detected=%u (windows with: lap_suspect %u, crc/len err %u, seq gap %u)\n",
                       blackouts, detected, by_lap, by_crc, by_seq);
                CHECK(blackouts == 10 && detected == 10);
                CHECK(L.app.lost_est == NFR - L.app.frames);            /* SEQ 로 센 손실 = 실제 손실 */
                CHECK(L.app.frames < NFR && L.app.seq_gaps > 0);
            }
        }
    }

    /* C2. 태스크와 IRQ 가 같은 핸들러를 부를 때: 태스크가 sink 안에 있는 동안 IRQ 가 선점 */
    {
        for (int fixed = 0; fixed < 2; fixed++) {
            static dma_hw hw; static uart_dma_rx rx;
            collect_reset(); hw_init(&hw, 32); rx_init(&rx, &hw, sink_collect, NULL);
            g_irq_rx = &rx; g_irq_masked = false; g_irq_pending = false; g_pre_hw = &hw;
            const char *m = "ABCDEFGHIJ";
            for (int i = 0; m[i]; i++) { hw_rx_byte(&hw, (uint8_t)m[i]); g_sent[g_sent_n++] = (uint8_t)m[i]; }
            g_sink_hook = hook_bytes_then_irq;                       /* 태스크의 첫 sink 호출 중에 IRQ */
            if (fixed) rx_poll_from_task(&rx); else naive_poll_from_task(&rx);
            g_sink_hook = NULL;
            irq_raise();                                             /* 다음 IDLE 한 번 더 */
            printf("C2 %s: sent %zu bytes, got %zu: \"%.*s\"\n", fixed ? "with critical section" : "naive (no lock)  ",
                   g_sent_n, g_got_n, (int)g_got_n, (const char *)g_got);
            if (fixed) CHECK(got_equals_sent() && rx.last == 15);
            else CHECK(g_got_n > g_sent_n);                          /* 위험 재현: 중복 전달 */
        }
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
