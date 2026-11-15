/* COMM-M11 추가 연습 — 한 번에 검증
 *
 * (solution.c 의 mock DMA/USART 모델과 rx_dma_process 를 축약해 다시 넣었다 — 파일 하나로 빌드된다)
 *   연습 1: 지연 예산 — 최대 허용 IRQ 지연 = N/2 − 1 바이트 시간(HT 켬), 시간 환산 표, 시뮬레이션으로 확인
 *   연습 2: 줄 조립기 — 랩어라운드·핸들러 경계에 걸친 CR/LF 줄, 너무 긴 줄 버리기
 *   연습 3: 플래그 클리어 순서 — "NDTR 읽고 나서 클리어" 가 마지막 바이트를 가두는 경쟁 재현
 *   연습 4: Cortex-M7 D-cache 모델 — invalidate 없으면 stale, 정렬 안 된 버퍼 invalidate 는 옆 변수를 지움
 *   연습 5: HAL 식 콜백 어댑터 — Size 는 "새 바이트 수" 가 아니라 "버퍼 위치"
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

/* ---------------- 모델 (solution.c 축약) ---------------- */
#define DMA_MAX_N 256u
typedef struct {
    uint8_t  buf[DMA_MAX_N];
    uint16_t n, ndtr;
    bool     ht, tc, idle, rx_since_idle;
    uint32_t written;
} dma_hw;

static void hw_init(dma_hw *h, uint16_t n) { memset(h, 0, sizeof *h); h->n = n; h->ndtr = n; }
static void hw_rx_byte(dma_hw *h, uint8_t b) {
    h->buf[h->n - h->ndtr] = b;
    h->ndtr--; h->written++; h->rx_since_idle = true;
    if (h->ndtr == h->n / 2) h->ht = true;
    if (h->ndtr == 0) { h->tc = true; h->ndtr = h->n; }
}
static void hw_line_idle(dma_hw *h) { if (h->rx_since_idle) { h->idle = true; h->rx_since_idle = false; } }

typedef void (*rx_sink)(void *ctx, const uint8_t *p, size_t len);
typedef struct { dma_hw *hw; uint16_t last; rx_sink sink; void *ctx; } uart_dma_rx;

static uint16_t dma_pos(uint16_t n, uint16_t ndtr) { uint16_t p = (uint16_t)(n - ndtr); return p >= n ? 0 : p; }

static size_t rx_dma_process(uart_dma_rx *rx, uint16_t pos) {
    uint16_t n = rx->hw->n, last = rx->last;
    if (pos == last) return 0;
    size_t total;
    if (pos > last) { total = (size_t)(pos - last); rx->sink(rx->ctx, &rx->hw->buf[last], total); }
    else {
        rx->sink(rx->ctx, &rx->hw->buf[last], (size_t)(n - last));
        if (pos) rx->sink(rx->ctx, &rx->hw->buf[0], pos);
        total = (size_t)(n - last) + pos;
    }
    rx->last = pos;
    return total;
}
static size_t rx_dma_irq(uart_dma_rx *rx) {                       /* 올바른 순서: 클리어 → NDTR */
    rx->hw->ht = rx->hw->tc = rx->hw->idle = false;
    return rx_dma_process(rx, dma_pos(rx->hw->n, rx->hw->ndtr));
}

/* 수집 sink */
static uint8_t g_sent[65536], g_got[65536];
static size_t  g_sent_n, g_got_n;
static void collect_reset(void) { g_sent_n = g_got_n = 0; }
static void sink_collect(void *ctx, const uint8_t *p, size_t len) {
    (void)ctx;
    if (g_got_n + len <= sizeof g_got) { memcpy(g_got + g_got_n, p, len); g_got_n += len; }
}
static bool got_equals_sent(void) { return g_got_n == g_sent_n && memcmp(g_got, g_sent, g_sent_n) == 0; }

/* ======================= 연습 1: 지연 예산 ======================= */

/* 어떤 트래픽에서도 손실이 없는 최대 IRQ 지연(정수 바이트 시간). HT 를 끄면 연속 스트림에서 지연 0 도 안전하지 않다 → −1 */
static int32_t max_safe_latency_bytes(uint16_t n, bool ht_irq) { return ht_irq ? (int32_t)(n / 2u) - 1 : -1; }
static uint64_t byte_time_ns(uint32_t baud, uint32_t bits_per_char) { return (uint64_t)bits_per_char * 1000000000ull / baud; }
static uint64_t wrap_time_ns(uint16_t n, uint32_t baud, uint32_t bits) { return (uint64_t)n * bits * 1000000000ull / baud; }
/* 연속시간 예산: 이벤트(HT/TC) 뒤 NDTR 을 읽고 [last,pos) 를 다 소비할 때까지 < N/2 바이트 시간 */
static uint64_t budget_ns(uint16_t n, uint32_t baud, uint32_t bits) { return (uint64_t)(n / 2u) * bits * 1000000000ull / baud; }

/* 고정 지연 L 슬롯, 쉬지 않는 연속 스트림 → 잃은 바이트 수 */
static size_t lossy_bytes_fixed_latency(uint16_t n, bool ht_irq, uint32_t L, uint32_t nbytes) {
    dma_hw hw; uart_dma_rx rx = { &hw, 0, sink_collect, NULL };
    int32_t delay = -1;
    collect_reset(); hw_init(&hw, n);
    for (uint32_t i = 0; i < nbytes + L + 2u; i++) {
        if (i < nbytes) { hw_rx_byte(&hw, (uint8_t)(i * 7u)); g_sent[g_sent_n++] = (uint8_t)(i * 7u); }
        else hw_line_idle(&hw);
        bool ev = hw.tc || hw.idle || (ht_irq && hw.ht);
        if (ev && delay < 0) delay = (int32_t)L;
        if (delay == 0) { rx_dma_irq(&rx); delay = -1; }
        else if (delay > 0) delay--;
    }
    rx_dma_irq(&rx);
    if (got_equals_sent()) return 0;
    return g_got_n < g_sent_n ? g_sent_n - g_got_n : 1;
}

static void practice1(void) {
    CHECK(max_safe_latency_bytes(256, true) == 127 && max_safe_latency_bytes(256, false) == -1);
    CHECK(byte_time_ns(115200, 10) == 86805);                     /* 10/115200 s = 86.805 µs */
    CHECK(wrap_time_ns(256, 115200, 10) == 22222222);             /* 22.22 ms */
    CHECK(budget_ns(256, 115200, 10) == 11111111);                /* 11.11 ms */
    CHECK(wrap_time_ns(256, 1000000, 10) == 2560000 && budget_ns(256, 1000000, 10) == 1280000);
    printf("P1 N=256, 8N1 (10 bit/char):\n");
    printf("   baud      byte time   wrap (N bytes)   budget HT on (< N/2 bytes)\n");
    static const uint32_t bauds[] = { 115200, 460800, 921600, 1000000, 3000000 };
    for (int i = 0; i < 5; i++)
        printf("   %-8u  %7.2f us   %9.3f ms     %9.3f ms\n", bauds[i], byte_time_ns(bauds[i], 10) / 1e3,
               wrap_time_ns(256, bauds[i], 10) / 1e6, budget_ns(256, bauds[i], 10) / 1e6);

    /* 시뮬레이션으로 문턱 확인: 고정 지연 L 을 늘려 가며 처음 손실이 나는 L */
    static const uint16_t ns[] = { 16, 64 };
    for (int t = 0; t < 2; t++) {
        uint16_t n = ns[t];
        int32_t first_loss = -1;
        for (uint32_t L = 0; L <= n && first_loss < 0; L++)
            if (lossy_bytes_fixed_latency(n, true, L, 20u * n) != 0) first_loss = (int32_t)L;
        size_t ht_off_loss = lossy_bytes_fixed_latency(n, false, 0, 20u * n);
        printf("P1 sim N=%u: HT on → first loss at latency %d bytes (max safe %d); HT off, latency 0 → lost %zu/%u\n",
               n, first_loss, first_loss - 1, ht_off_loss, 20u * n);
        CHECK(first_loss - 1 == max_safe_latency_bytes(n, true));
        CHECK(ht_off_loss == 20u * n);
    }
}

/* ======================= 연습 2: 줄 조립기 ======================= */
#define LINE_MAX 64u
typedef struct {
    char     cur[LINE_MAX + 1];
    size_t   len;
    bool     discarding;                  /* 너무 긴 줄: 다음 '\n' 까지 버림 */
    uint32_t lines, overlong, multi_chunk;
    uint32_t chunks_in_line;
    char     out[600][LINE_MAX + 1];      /* 테스트용 결과 */
} line_asm;

static void line_feed(void *ctx, const uint8_t *p, size_t n) {
    line_asm *a = ctx;
    if (a->len || a->discarding) a->chunks_in_line++;        /* 이 줄이 앞 조각에서 이어진다 */
    for (size_t i = 0; i < n; i++) {
        char c = (char)p[i];
        if (c == '\r') continue;                                 /* CR 은 무시: LF 와 CRLF 모두 줄 끝 하나 */
        if (c == '\n') {
            if (!a->discarding) {
                a->cur[a->len] = '\0';
                if (a->lines < 600) memcpy(a->out[a->lines], a->cur, a->len + 1);
                a->lines++;
                if (a->chunks_in_line) a->multi_chunk++;
            }
            a->len = 0; a->discarding = false; a->chunks_in_line = 0;
        } else if (!a->discarding) {
            if (a->len == LINE_MAX) { a->discarding = true; a->overlong++; a->len = 0; }
            else a->cur[a->len++] = c;
        }
    }
}

static void practice2(void) {
    static line_asm A;
    static char expect[600][LINE_MAX + 2];
    memset(&A, 0, sizeof A);
    dma_hw hw; uart_dma_rx rx = { &hw, 0, line_feed, &A };
    hw_init(&hw, 32);
    uint32_t seed = 0x1A2B3C4Du, nexp = 0, nover = 0, total_bytes = 0;
    int32_t delay = -1;
    for (int k = 0; k < 500; k++) {
        uint32_t len = xs32(&seed) % 80u;                       /* 0..79: 64 넘는 줄도 섞임 */
        bool crlf = xs32(&seed) & 1u;
        char line[96];
        for (uint32_t i = 0; i < len; i++) line[i] = (char)(' ' + 1 + xs32(&seed) % 94u);
        if (len <= LINE_MAX) { memcpy(expect[nexp], line, len); expect[nexp][len] = '\0'; nexp++; }
        else nover++;
        if (crlf) line[len++] = '\r';
        line[len++] = '\n';
        uint32_t gap = xs32(&seed) % 3u;
        for (uint32_t i = 0; i < len + gap; i++) {
            if (i < len) { hw_rx_byte(&hw, (uint8_t)line[i]); total_bytes++; } else hw_line_idle(&hw);
            if ((hw.tc || hw.idle || hw.ht) && delay < 0) delay = (int32_t)(xs32(&seed) % 16u);   /* ≤ N/2 − 1 */
            if (delay == 0) { rx_dma_irq(&rx); delay = -1; } else if (delay > 0) delay--;
        }
    }
    for (int i = 0; i < 20; i++) { hw_line_idle(&hw); if (delay >= 0 && delay-- == 0) rx_dma_irq(&rx); }
    rx_dma_irq(&rx);
    bool same = A.lines == nexp;
    for (uint32_t i = 0; same && i < nexp; i++) same = strcmp(A.out[i], expect[i]) == 0;
    printf("P2 lines: %u bytes through N=32, lines ok %u/%u, overlong dropped %u/%u, lines split across chunks %u\n",
           total_bytes, A.lines, nexp, A.overlong, nover, A.multi_chunk);
    CHECK(same);
    CHECK(A.overlong == nover);
    CHECK(A.multi_chunk > 50);                                   /* 랩·핸들러 경계에 걸린 줄이 충분히 나왔다 */
}

/* ======================= 연습 3: 플래그 클리어 순서 ======================= */
static dma_hw *g_p3_hw;
static void inject_C_then_idle(void) { hw_rx_byte(g_p3_hw, 'C'); g_sent[g_sent_n++] = 'C'; hw_line_idle(g_p3_hw); }

static size_t irq_wrong_order(uart_dma_rx *rx, void (*between)(void)) {
    uint16_t pos = dma_pos(rx->hw->n, rx->hw->ndtr);          /* NDTR 먼저 */
    if (between) between();                                     /* ← 여기서 바이트 + IDLE */
    rx->hw->ht = rx->hw->tc = rx->hw->idle = false;             /* 방금 선 IDLE 까지 지워짐 */
    return rx_dma_process(rx, pos);
}
static size_t irq_right_order(uart_dma_rx *rx, void (*between)(void)) {
    rx->hw->ht = rx->hw->tc = rx->hw->idle = false;             /* 클리어 먼저 */
    if (between) between();                                     /* ← 여기서 바이트 + IDLE → 플래그 다시 섬 */
    return rx_dma_process(rx, dma_pos(rx->hw->n, rx->hw->ndtr));
}

static void practice3(void) {
    for (int right = 0; right < 2; right++) {
        dma_hw hw; uart_dma_rx rx = { &hw, 0, sink_collect, NULL };
        collect_reset(); hw_init(&hw, 16); g_p3_hw = &hw;
        hw_rx_byte(&hw, 'A'); hw_rx_byte(&hw, 'B'); g_sent[g_sent_n++] = 'A'; g_sent[g_sent_n++] = 'B';
        hw_line_idle(&hw);                                      /* IDLE IRQ 진입 */
        if (right) irq_right_order(&rx, inject_C_then_idle); else irq_wrong_order(&rx, inject_C_then_idle);
        int extra = 0;
        while ((hw.idle || hw.ht || hw.tc) && extra < 4) { irq_right_order(&rx, NULL); extra++; }   /* 남은 IRQ */
        for (int i = 0; i < 10; i++) hw_line_idle(&hw);          /* 라인은 계속 조용 → 새 IDLE 없음 */
        bool stuck = hw.idle == false && g_got_n < g_sent_n;
        printf("P3 %s: got \"%.*s\" of \"%.*s\"%s\n", right ? "clear→NDTR (right)" : "NDTR→clear (wrong)",
               (int)g_got_n, (const char *)g_got, (int)g_sent_n, (const char *)g_sent,
               stuck ? "  ← 'C' 는 DMA 버퍼에 있는데 다음 트래픽까지 아무 IRQ 도 없다" : "");
        if (right) CHECK(got_equals_sent() && extra == 1);       /* IDLE 이 한 번 더 와서 빈 호출 */
        else CHECK(stuck && g_got_n == 2 && extra == 0);
    }
}

/* ======================= 연습 4: Cortex-M7 D-cache (ARCH-M12 축약) ======================= */
#define LINE 32u
#define MEM  160u
typedef struct {
    uint8_t ram[MEM];                     /* DMA 는 여기만 본다 */
    bool    valid[MEM / LINE], dirty[MEM / LINE];
    uint8_t line[MEM / LINE][LINE];       /* 주소 그대로 매핑(직접 사상, 전 영역이 들어가는 작은 모델) */
} cache_model;

static uint8_t cpu_read(cache_model *c, uint32_t a) {
    uint32_t l = a / LINE;
    if (!c->valid[l]) { memcpy(c->line[l], &c->ram[l * LINE], LINE); c->valid[l] = true; c->dirty[l] = false; }
    return c->line[l][a % LINE];
}
static void cpu_write(cache_model *c, uint32_t a, uint8_t v) {   /* write-back, write-allocate */
    cpu_read(c, a);
    c->line[a / LINE][a % LINE] = v;
    c->dirty[a / LINE] = true;
}
static void dcache_invalidate(cache_model *c, uint32_t a, uint32_t len) {  /* 범위에 닿는 라인 전부 버림 (dirty 도) */
    if (!len) return;
    for (uint32_t l = a / LINE; l <= (a + len - 1u) / LINE; l++) { c->valid[l] = false; c->dirty[l] = false; }
}

/* DMA 링 (base 오프셋에 놓인 n=64) 을 CPU 가 캐시를 통해 읽는다 */
static uint32_t p4_run(bool invalidate, uint32_t base, cache_model *c, uint32_t *neighbor_ok) {
    memset(c, 0, sizeof *c);
    const uint16_t n = 64;
    uint16_t ndtr = n, last = 0;
    uint32_t seed = 77u, mism = 0, sent = 0, got = 0;
    uint8_t expect[2048];
    cpu_write(c, 0, 0x55);                                       /* 주소 0 의 CPU 변수 (예: 통계 카운터) */
    for (int burst = 0; burst < 120; burst++) {
        uint32_t k = 1u + xs32(&seed) % 13u;
        for (uint32_t i = 0; i < k; i++) {                       /* DMA: RAM 에 직접 */
            uint8_t b = (uint8_t)xs32(&seed);
            c->ram[base + (uint32_t)(n - ndtr)] = b;
            if (sent < sizeof expect) expect[sent] = b;
            sent++;
            if (--ndtr == 0) ndtr = n;
        }
        uint16_t pos = dma_pos(n, ndtr);                         /* IDLE 핸들러 */
        if (invalidate) {                                        /* 읽기 전에 [last,pos) 를 덮는 라인 invalidate */
            if (pos > last) dcache_invalidate(c, base + last, (uint32_t)(pos - last));
            else { dcache_invalidate(c, base + last, (uint32_t)(n - last)); dcache_invalidate(c, base, pos); }
        }
        for (uint16_t p = last; p != pos; p = (uint16_t)((p + 1u) % n)) {
            if (got < sizeof expect && cpu_read(c, base + p) != expect[got]) mism++;
            got++;
        }
        last = pos;
    }
    *neighbor_ok = cpu_read(c, 0) == 0x55;
    return mism;
}

static void practice4(void) {
    static cache_model c;
    uint32_t nb1, nb2, nb3;
    uint32_t m_noinv = p4_run(false, 32, &c, &nb1);              /* 32 B 정렬, invalidate 없음 */
    uint32_t m_inv   = p4_run(true, 32, &c, &nb2);               /* 32 B 정렬, invalidate 함 */
    uint32_t m_unal  = p4_run(true, 16, &c, &nb3);               /* 16 B 오프셋: 라인 0 을 변수와 공유 */
    printf("P4 cache: no invalidate → %u stale bytes; aligned + invalidate → %u stale, neighbor %s; "
           "unaligned(+16) + invalidate → %u stale, neighbor %s\n",
           m_noinv, m_inv, nb2 ? "intact" : "LOST", m_unal, nb3 ? "intact" : "LOST");
    CHECK(m_noinv > 0 && nb1);
    CHECK(m_inv == 0 && nb2);
    CHECK(m_unal == 0 && !nb3);                                  /* 데이터는 맞지만 옆 변수의 쓰기가 사라짐 */
}

/* ======================= 연습 5: HAL 식 RxEvent 콜백 ======================= */
/* HAL 의 circular ReceiveToIdle 은 HT 에서 Size = N/2, TC 에서 Size = N, IDLE 에서 Size = N − NDTR 를 넘긴다고
 * 알려져 있다 (HAL 버전별 소스 확인 권장). Size 는 "버퍼 안 위치" 다. */
static uart_dma_rx *g_p5_rx;
static void rx_event_adapter(uint16_t size) {                    /* 옳은 사용: 위치로 보고 last 와 비교 */
    rx_dma_process(g_p5_rx, size >= g_p5_rx->hw->n ? 0 : size);
}
static void rx_event_naive(uint16_t size) {                      /* 흔한 실수: Size 바이트를 buf[0] 부터 */
    g_p5_rx->sink(g_p5_rx->ctx, g_p5_rx->hw->buf, size);
}

static void practice5(void) {
    for (int naive = 0; naive < 2; naive++) {
        dma_hw hw; uart_dma_rx rx = { &hw, 0, sink_collect, NULL };
        collect_reset(); hw_init(&hw, 32); g_p5_rx = &rx;
        uint32_t seed = 0xABCDu, events = 0;
        for (int k = 0; k < 300; k++) {
            uint32_t burst = 1u + xs32(&seed) % 40u;
            for (uint32_t i = 0; i <= burst; i++) {
                if (i < burst) { uint8_t b = (uint8_t)xs32(&seed); hw_rx_byte(&hw, b); g_sent[g_sent_n++] = b; }
                else hw_line_idle(&hw);
                uint16_t sz[3]; int ne = 0;                       /* 이 슬롯의 이벤트들 → HAL Size */
                if (hw.ht) sz[ne++] = (uint16_t)(hw.n / 2u);
                if (hw.tc) sz[ne++] = hw.n;
                if (hw.idle) sz[ne++] = (uint16_t)(hw.n - hw.ndtr);
                hw.ht = hw.tc = hw.idle = false;
                for (int e = 0; e < ne; e++) { (naive ? rx_event_naive : rx_event_adapter)(sz[e]); events++; }
            }
        }
        printf("P5 %s: %u events, sent %zu bytes, got %zu bytes, stream %s\n", naive ? "naive (Size = count)" :
               "adapter (Size = pos) ", events, g_sent_n, g_got_n, got_equals_sent() ? "identical" : "WRONG");
        if (naive) CHECK(!got_equals_sent());
        else CHECK(got_equals_sent());
    }
}

int main(void) {
    practice1();
    practice2();
    practice3();
    practice4();
    practice5();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
