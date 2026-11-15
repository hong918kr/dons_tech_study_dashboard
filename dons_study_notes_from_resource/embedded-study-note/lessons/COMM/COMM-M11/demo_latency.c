/* COMM-M11 demo — IRQ 지연 vs 손실률, HT 켬/끔, 연속 스트림 vs idle 로 나뉜 프레임
 *
 * solution.c 의 모델(축약)을 그대로 쓴다. 핸들러는 이벤트(HT·TC·IDLE) 뒤 0..Lmax 슬롯(균등 랜덤) 늦게 실행된다.
 * 측정: 잃은 바이트 비율, 실제 오버런 실행(실행 사이 d ≥ N) 수, 그중 HT/TC 랩 검출기가 잡은 비율, 최대 배치.
 * 빌드: cc -std=c11 -Wall -Wextra -O2 demo_latency.c -o demo_latency && ./demo_latency
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

typedef struct {
    uint8_t  buf[1024];
    uint16_t n, ndtr;
    bool     ht, tc, idle, rx_since_idle;
    uint32_t written;
} dma_hw;

static void hw_rx_byte(dma_hw *h, uint8_t b) {
    h->buf[h->n - h->ndtr] = b;
    h->ndtr--; h->written++; h->rx_since_idle = true;
    if (h->ndtr == h->n / 2) h->ht = true;
    if (h->ndtr == 0) { h->tc = true; h->ndtr = h->n; }
}
static void hw_line_idle(dma_hw *h) { if (h->rx_since_idle) { h->idle = true; h->rx_since_idle = false; } }

static bool lap_suspect(uint16_t last, uint16_t pos, uint16_t n, bool ht, bool tc) {
    uint16_t d0 = (uint16_t)((pos + n - last) % n);
    bool ht_exp = (uint16_t)((n / 2u - 1u + n - last) % n) < d0;
    bool tc_exp = (uint16_t)((n - 1u + n - last) % n) < d0;
    return (ht && !ht_exp) || (tc && !tc_exp);
}

typedef struct {
    dma_hw   hw;
    uint16_t last;
    bool     ht_irq;
    uint32_t lat_max, seed, written_at_run;
    int32_t  delay;
    uint64_t delivered, overruns, caught, false_alarm, max_batch;
} sim_t;

static void run_handler(sim_t *s) {
    dma_hw *h = &s->hw;
    bool ht = h->ht, tc = h->tc;
    h->ht = h->tc = h->idle = false;
    uint16_t pos = (uint16_t)(h->n - h->ndtr);
    if (pos >= h->n) pos = 0;
    bool over = h->written - s->written_at_run >= h->n;          /* 정답지 */
    bool sus = lap_suspect(s->last, pos, h->n, ht, tc);
    if (over) { s->overruns++; if (sus) s->caught++; }
    else if (sus) s->false_alarm++;
    uint32_t d = (uint32_t)((pos + h->n - s->last) % h->n);
    s->delivered += d;
    if (d > s->max_batch) s->max_batch = d;
    s->last = pos;
    s->written_at_run = h->written;
    s->delay = -1;
}

static void slot(sim_t *s, int byte) {
    if (byte >= 0) hw_rx_byte(&s->hw, (uint8_t)byte); else hw_line_idle(&s->hw);
    bool ev = s->hw.tc || s->hw.idle || (s->ht_irq && s->hw.ht);
    if (ev && s->delay < 0) s->delay = s->lat_max ? (int32_t)(xs32(&s->seed) % (s->lat_max + 1u)) : 0;
    if (s->delay == 0) run_handler(s);
    else if (s->delay > 0) s->delay--;
}

/* traffic: 0 = 쉬지 않는 연속 바이트, 1 = 프레임 5..37 바이트 + idle 1..4 슬롯 */
static void one(uint16_t n, bool ht_irq, int traffic, uint32_t lat_max, uint32_t nbytes) {
    static sim_t s;
    memset(&s, 0, sizeof s);
    s.hw.n = n; s.hw.ndtr = n; s.ht_irq = ht_irq; s.lat_max = lat_max; s.delay = -1;
    s.seed = 0x5EEDu ^ n ^ (lat_max << 12) ^ ((uint32_t)traffic << 24);
    uint32_t sent = 0, tseed = 0xFACEu;
    while (sent < nbytes) {
        if (traffic == 0) { slot(&s, (int)(sent & 0xFF)); sent++; continue; }
        uint32_t len = 5u + xs32(&tseed) % 33u, gap = 1u + xs32(&tseed) % 4u;
        for (uint32_t i = 0; i < len; i++) { slot(&s, (int)(sent & 0xFF)); sent++; }
        for (uint32_t g = 0; g < gap; g++) slot(&s, -1);
    }
    for (uint32_t g = 0; g < lat_max + 2u; g++) slot(&s, -1);
    run_handler(&s);
    double loss = 100.0 * (double)(sent - s.delivered) / sent;
    char caught[24];
    if (s.overruns) snprintf(caught, sizeof caught, "%.1f%%", 100.0 * (double)s.caught / (double)s.overruns);
    else snprintf(caught, sizeof caught, "-");
    printf("| %4u | %-3s | %-11s | %5u | %9.4f%% | %8llu | %6s | %3llu | %5llu |\n",
           n, ht_irq ? "on" : "off", traffic ? "frames+idle" : "continuous", lat_max, loss,
           (unsigned long long)s.overruns, caught, (unsigned long long)s.false_alarm,
           (unsigned long long)s.max_batch);
}

int main(void) {
    const uint32_t nbytes = 1000000;
    static const uint16_t ns[] = { 64, 256 };
    printf("실험: 바이트 %u 개/행, 지연 = 이벤트 뒤 0..Lmax 슬롯 균등 랜덤 (1 슬롯 = 1 바이트 시간)\n\n", nbytes);
    printf("|    N | HT  | traffic     |  Lmax |   loss     | overruns | caught | FA  | maxb  |\n");
    printf("|---|---|---|---|---|---|---|---|---|\n");
    for (int t = 0; t < 2; t++) {
        uint16_t n = ns[t];
        const uint32_t lats[] = { 0, n / 8u, n / 4u, n / 2u - 1u, n / 2u, 3u * n / 4u, n - 1u, n, 2u * n };
        for (int ht = 1; ht >= 0; ht--)
            for (int tr = 0; tr < 2; tr++)
                for (int k = 0; k < 9; k++) {
                    if (!ht && k > 0 && k != 3 && k != 6) continue;   /* HT 끔은 몇 점만 */
                    one(n, ht != 0, tr, lats[k], nbytes);
                }
    }
    return 0;
}
