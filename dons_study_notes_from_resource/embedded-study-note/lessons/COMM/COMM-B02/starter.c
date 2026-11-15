/* COMM-B02 UART 수신기 모델: 16x 오버샘플링과 에러 검출
 *
 * RX 라인을 baud 의 16배로 샘플링한 값(0/1)을 틱마다 하나씩 받는 수신기 상태 머신.
 *   Part A — vote3 (샘플 8/9/10 다수결) + rx_tick (falling edge → start 확인 → data → parity → stop)
 *   Part B — frame_flags (NF/PE/FE/BRK) · rx_deliver (RDR/RXNE, overrun 정책) · uart_read
 *   Part C — ISR + 링 버퍼 (ring_push / ring_pop / uart_isr), 인터럽트 지연과 overrun 경계
 * 송신 쪽 파형 생성기는 (COMM-B01 모델 축약판) 으로 주어진다.
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o sol && ./sol
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define OVS   16          /* 비트당 샘플 수 (16x 오버샘플링)                     */
#define SMP_A 8           /* 다수결에 쓰는 샘플 번호 (비트 안 1..16) — STM32 대표값 */
#define SMP_C 10          /* SMP_A..SMP_C = 8, 9, 10                            */

typedef enum { PAR_NONE = 0, PAR_EVEN, PAR_ODD } parity_t;
typedef struct { uint8_t data_bits; parity_t parity; uint8_t stop_bits; } uart_cfg_t; /* data 7/8, stop 1/2(송신만) */

/* 상태 레지스터 비트 — STM32 USART_SR/ISR 이름을 본뜬 "우리 모델"의 정의 */
enum { SR_RXNE = 1u << 0, SR_ORE = 1u << 1, SR_NF = 1u << 2, SR_FE = 1u << 3, SR_PE = 1u << 4, SR_BRK = 1u << 5 };

typedef enum { RX_IDLE = 0, RX_START, RX_DATA, RX_PARITY, RX_STOP } rx_state_t;
enum { EV_FRAME = 1, EV_FALSE_START = 2 };
typedef struct { uint8_t kind, flags; uint16_t data; uint32_t t_start, t_done; } rx_event_t;

#define LOG_CAP 1024
typedef struct {
    uart_cfg_t cfg;
    /* 상태 머신 */
    rx_state_t st;
    uint8_t  prev;     /* 직전 샘플 (falling edge 검출용). 리셋 값 1 = idle   */
    uint8_t  ph;       /* 현재 비트 안의 샘플 번호 1..16                       */
    uint8_t  smp;      /* 이번 비트의 샘플 8/9/10 → bit0/bit1/bit2             */
    uint8_t  nbit;     /* 받은 data 비트 수                                    */
    uint8_t  noisy;    /* 이 프레임 어느 비트에서든 3샘플 불일치가 있었나       */
    uint8_t  par;      /* 받은 parity 비트                                     */
    uint16_t shift;    /* 시프트 레지스터 (LSB first 로 채움)                  */
    uint32_t t, t_start;
    /* 소프트웨어가 보는 "레지스터" */
    uint8_t  rdr;      /* receive data register — 1단 (FIFO 없음)              */
    uint8_t  sr;       /* SR_* 비트                                            */
    /* 통계 + 이벤트 로그: 수신기가 본 프레임 그대로 (RDR/overrun 과 무관) */
    uint32_t n_frames, n_false_start, n_overrun;
    rx_event_t log[LOG_CAP];
    uint32_t nlog, log_drop;
} uart_rx_t;

static void rx_init(uart_rx_t *r, uart_cfg_t c) {
    memset(r, 0, sizeof *r);
    r->cfg = c;
    r->st = RX_IDLE;
    r->prev = 1;
}
static void log_event(uart_rx_t *r, uint8_t kind, uint16_t data, uint8_t flags) {
    if (r->nlog >= LOG_CAP) { r->log_drop++; return; }
    rx_event_t *e = &r->log[r->nlog++];
    e->kind = kind; e->flags = flags; e->data = data; e->t_start = r->t_start; e->t_done = r->t;
}
static int parity_of(uint16_t v) { int p = 0; while (v) { p ^= v & 1u; v >>= 1; } return p; }

static uint8_t frame_flags(const uart_cfg_t *c, uint16_t data, uint8_t par, uint8_t stop, uint8_t noisy);
static void    rx_deliver(uart_rx_t *r, uint8_t data, uint8_t flags);

/* ---------------- Part A: 다수결 + 수신 상태 머신 ---------------- */
/* smp: bit0=샘플8, bit1=샘플9, bit2=샘플10. 반환 = 다수결 값, *noisy = 셋이 모두 같지 않으면 1 */
static int vote3(uint8_t smp, uint8_t *noisy) {
    (void)smp;
    *noisy = 0;
    return 1; /* TODO: 샘플 셋의 다수결, 셋이 모두 같지 않으면 *noisy = 1 */
}

/* 한 틱 = 샘플 하나. idle 에서 1→0 (falling edge) 을 보면 그 샘플이 start 비트의 1번 샘플.
 * 각 비트의 샘플 8/9/10 을 모아 10번에서 판정, 16번이 지나면 다음 비트.
 * start 판정이 1 이면 false start → IDLE + prev=1 (재무장). stop 판정(10번) 에서 프레임 완료 → 로그 + rx_deliver → IDLE. */
static void rx_tick(uart_rx_t *r, int sample) {
    (void)sample; (void)log_event; (void)frame_flags; (void)rx_deliver;
    r->t++;
    /* TODO:
     * IDLE: prev==1 && s==0 → START, ph=1, t_start=t, smp/nbit/shift/noisy/par = 0
     * 그 외: ph++; ph 가 8..10 이면 smp 에 모으고, ph==10 에서 v = vote3(smp, &nz), noisy |= nz
     *   START: v==1 → false start (IDLE, n_false_start++, EV_FALSE_START 로그, prev=1 로 재무장 후 return)
     *   DATA : shift |= v << nbit, nbit++        PARITY: par = v
     *   STOP : f = frame_flags(...), n_frames++, EV_FRAME 로그, rx_deliver(r, shift, f), IDLE
     * ph==16 (IDLE 아님): ph=0, START→DATA, DATA(nbit==data_bits)→PARITY/STOP, PARITY→STOP
     * 마지막에 prev = s */
}

/* ---------------- Part B: 에러 플래그 · RDR · overrun ---------------- */
/* NF: 어느 비트든 3샘플 불일치 / PE: parity 불일치 / FE: stop=0 / BRK: FE 이고 data=0, parity 비트도 0 */
static uint8_t frame_flags(const uart_cfg_t *c, uint16_t data, uint8_t par, uint8_t stop, uint8_t noisy) {
    (void)c; (void)data; (void)par; (void)stop; (void)noisy;
    return 0; /* TODO: SR_NF / SR_PE / SR_FE / SR_BRK */
}

/* 완성된 문자를 RDR 로. 정책: RXNE 가 아직 1 이면 overrun — ORE 세우고 옛 바이트 유지, 새 바이트(와 그 플래그) 버림 */
static void rx_deliver(uart_rx_t *r, uint8_t data, uint8_t flags) {
    (void)r; (void)data; (void)flags;
    /* TODO: RXNE 가 이미 1 이면 ORE + n_overrun++ (옛 바이트 유지). 아니면 rdr = data, sr |= RXNE | flags */
}

/* 소프트웨어 읽기: 상태 스냅샷을 돌려주고, RXNE 면 *data = RDR. 읽으면 모든 플래그가 지워진다 (우리 모델) */
static uint8_t uart_read(uart_rx_t *r, uint8_t *data) {
    (void)r; (void)data;
    return 0; /* TODO: sr 스냅샷, RXNE 면 *data = rdr, 플래그 전부 지우기 */
}

/* ---------------- Part C: ISR + 링 버퍼 ---------------- */
#define RB_SIZE 16u                                   /* 2의 거듭제곱 */
typedef struct { uint8_t buf[RB_SIZE]; uint32_t head, tail; } ring_t;  /* head: ISR 가 씀, tail: main 이 읽음 */
typedef struct { uint32_t rx, ore, nf, fe, pe, brk, ring_drop; } link_stats_t;

static bool ring_push(ring_t *q, uint8_t b) {
    (void)q; (void)b;
    return false; /* TODO: head - tail == RB_SIZE 면 false, 아니면 buf[head & mask] = b, head++ */
}
static bool ring_pop(ring_t *q, uint8_t *b) {
    (void)q; (void)b;
    return false; /* TODO: 비었으면 false, 아니면 *b = buf[tail & mask], tail++ */
}
/* RXNE 인터럽트 핸들러: 읽고 → 에러 분류 → 정상 바이트만 링에. BRK 는 FE 보다 먼저 본다 */
static void uart_isr(uart_rx_t *r, ring_t *q, link_stats_t *st) {
    (void)r; (void)q; (void)st;
    /* TODO: uart_read → RXNE 없으면 return. ORE/NF 세기, BRK→brk, FE→fe, PE→pe 면 버림, 아니면 rx++ 후 ring_push (실패 → ring_drop++) */
}

/* ================= 송신 파형 생성기 (COMM-B01 모델 축약판) ================= */
#define LINE_CAP (1u << 17)
static uint8_t g_line[LINE_CAP];
static size_t g_n;

static void put_level(int level, size_t nsamples) {
    for (size_t i = 0; i < nsamples && g_n < LINE_CAP; i++) g_line[g_n++] = (uint8_t)level;
}
/* 한 문자: start 0, data LSB first, [parity], stop 1 × stop_bits, 그 뒤 gap_bits 비트 동안 idle */
static void put_char(uint16_t byte, const uart_cfg_t *c, int gap_bits) {
    uint16_t d = (uint16_t)(byte & ((1u << c->data_bits) - 1u));
    put_level(0, OVS);
    for (int i = 0; i < c->data_bits; i++) put_level((d >> i) & 1u, OVS);
    if (c->parity != PAR_NONE) put_level(parity_of(d) ^ (c->parity == PAR_ODD), OVS);
    put_level(1, (size_t)OVS * c->stop_bits);
    put_level(1, (size_t)OVS * (size_t)gap_bits);
}
static void run_line(uart_rx_t *r) { for (size_t i = 0; i < g_n; i++) rx_tick(r, g_line[i]); }

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t g_rng = 0x2545F491u;
static uint32_t xs32(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }

static const uart_cfg_t C8N1 = {8, PAR_NONE, 1}, C8E1 = {8, PAR_EVEN, 1}, C8O1 = {8, PAR_ODD, 1},
                        C7E1 = {7, PAR_EVEN, 1}, C7O2 = {7, PAR_ODD, 2}, C8N2 = {8, PAR_NONE, 2};

static uart_rx_t g_rx;     /* 스택에 두기엔 로그가 크다 */

/* 프레임 이벤트만 세고, i 번째 프레임이 (data, flags) 인지 */
static uint32_t count_frames(const uart_rx_t *r) {
    uint32_t n = 0;
    for (uint32_t i = 0; i < r->nlog; i++) n += (r->log[i].kind == EV_FRAME);
    return n;
}
static bool frame_is(const uart_rx_t *r, uint32_t k, uint16_t data, uint8_t flags) {
    for (uint32_t i = 0; i < r->nlog; i++)
        if (r->log[i].kind == EV_FRAME && k-- == 0) return r->log[i].data == data && r->log[i].flags == flags;
    return false;
}

/* 'A' 의 data 비트 1 (값 0) 안에서 샘플 [lo, hi] 를 1 로 뒤집은 스트림 */
static void stream_A_with_spike(const uart_cfg_t *c, int bit_index, int lo, int hi, int level) {
    g_n = 0;
    put_level(1, 32);
    size_t frame0 = g_n;
    put_char(0x41, c, 4);
    for (int ph = lo; ph <= hi; ph++) g_line[frame0 + (size_t)bit_index * OVS + (size_t)(ph - 1)] = (uint8_t)level;
}

/* Part C: 64바이트(8N1, 연속)를 보내고 RXNE → latency 틱 뒤 ISR, drain 틱마다 main 이 링을 비운다 */
static size_t run_link(const uint8_t *tx, size_t n, uint32_t latency, uint32_t drain,
                       uint8_t *out, size_t cap, link_stats_t *st) {
    g_n = 0;
    put_level(1, 40);
    for (size_t i = 0; i < n; i++) put_char(tx[i], &C8N1, 0);
    put_level(1, 2000);
    rx_init(&g_rx, C8N1);
    ring_t q;
    memset(&q, 0, sizeof q);
    memset(st, 0, sizeof *st);
    bool pending = false;
    uint32_t due = 0;
    size_t nout = 0;
    uint8_t b;
    for (size_t i = 0; i < g_n; i++) {
        rx_tick(&g_rx, g_line[i]);
        if (!pending && (g_rx.sr & SR_RXNE)) { pending = true; due = g_rx.t + latency; }
        if (pending && g_rx.t >= due) { uart_isr(&g_rx, &q, st); pending = false; }
        if (g_rx.t % drain == 0) while (ring_pop(&q, &b)) if (nout < cap) out[nout++] = b;
    }
    while (ring_pop(&q, &b)) if (nout < cap) out[nout++] = b;
    return nout;
}

int main(void) {
    /* ---- Part A ---- */
    {   /* vote3 진리표 */
        static const int want_v[8]  = {0, 0, 0, 1, 0, 1, 1, 1};
        static const int want_nz[8] = {0, 1, 1, 1, 1, 1, 1, 0};
        int ok = 1;
        for (uint8_t s = 0; s < 8; s++) { uint8_t nz = 9; int v = vote3(s, &nz); ok &= (v == want_v[s] && nz == want_nz[s]); }
        CHECK(ok);
    }
    {   /* 'A' = 0x41, 8N1: idle 20 샘플 → start 1번 샘플은 t=21, stop 의 10번 샘플은 t = 21 + 9*16 + 9 = 174 */
        g_n = 0; put_level(1, 20); put_char(0x41, &C8N1, 2);
        rx_init(&g_rx, C8N1); run_line(&g_rx);
        CHECK(g_rx.nlog == 1 && g_rx.log[0].kind == EV_FRAME);
        CHECK(g_rx.log[0].data == 0x41 && g_rx.log[0].flags == 0);
        CHECK(g_rx.log[0].t_start == 21 && g_rx.log[0].t_done == 174);
        printf("'A' 8N1: start edge t=%u, char done (stop sample 10) t=%u\n",
               (unsigned)g_rx.log[0].t_start, (unsigned)g_rx.log[0].t_done);
    }
    {   /* 전수: 설정 6개 × 256 바이트, 문자 사이 gap 0~3 비트 */
        const uart_cfg_t *cfgs[] = {&C8N1, &C8E1, &C8O1, &C7E1, &C7O2, &C8N2};
        int bad = 0;
        for (int k = 0; k < 6; k++) {
            g_n = 0; put_level(1, 17);
            for (int b = 0; b < 256; b++) put_char((uint16_t)b, cfgs[k], (b * 7) % 4);
            rx_init(&g_rx, *cfgs[k]); run_line(&g_rx);
            uint16_t mask = (uint16_t)((1u << cfgs[k]->data_bits) - 1u);
            if (count_frames(&g_rx) != 256 || g_rx.nlog != 256) { bad += 256; continue; }
            for (int b = 0; b < 256; b++) bad += !frame_is(&g_rx, (uint32_t)b, (uint16_t)(b & mask), 0);
        }
        printf("exhaustive 6 cfgs x 256 bytes: bad=%d\n", bad);
        CHECK(bad == 0);
    }
    {   /* 랜덤: 40 묶음 × 100 바이트, 설정·앞 idle(1~50 샘플)·gap(0~5 비트) 무작위 */
        const uart_cfg_t *cfgs[] = {&C8N1, &C8E1, &C8O1, &C7E1, &C7O2, &C8N2};
        int bad = 0;
        for (int batch = 0; batch < 40; batch++) {
            const uart_cfg_t *c = cfgs[xs32() % 6];
            uint8_t tx[100];
            g_n = 0; put_level(1, 1 + xs32() % 50);
            for (int i = 0; i < 100; i++) { tx[i] = (uint8_t)xs32(); put_char(tx[i], c, (int)(xs32() % 6)); }
            rx_init(&g_rx, *c); run_line(&g_rx);
            uint16_t mask = (uint16_t)((1u << c->data_bits) - 1u);
            if (g_rx.nlog != 100) { bad += 100; continue; }
            for (int i = 0; i < 100; i++) bad += !frame_is(&g_rx, (uint32_t)i, (uint16_t)(tx[i] & mask), 0);
        }
        printf("random 4000 chars: bad=%d\n", bad);
        CHECK(bad == 0);
    }

    /* ---- Part B ---- */
    {   /* idle 중 길이 L 샘플짜리 low 글리치: L<=8 거부, L=9 → 0xFF+NF, L>=10 → 0xFF (8E1 이면 PE) */
        int ok = 1;
        printf("glitch L | 8N1            | 8E1\n");
        for (int L = 1; L <= 16; L++) {
            char col[2][32];
            for (int k = 0; k < 2; k++) {
                const uart_cfg_t *c = k ? &C8E1 : &C8N1;
                g_n = 0; put_level(1, 32); put_level(0, (size_t)L); put_level(1, 16 * 14);
                rx_init(&g_rx, *c); run_line(&g_rx);
                if (g_rx.nlog != 1) { ok = 0; snprintf(col[k], sizeof col[k], "nlog=%u", (unsigned)g_rx.nlog); continue; }
                const rx_event_t *e = &g_rx.log[0];
                if (e->kind == EV_FALSE_START) snprintf(col[k], sizeof col[k], "false start");
                else snprintf(col[k], sizeof col[k], "0x%02X%s%s%s", e->data, (e->flags & SR_NF) ? " NF" : "",
                              (e->flags & SR_PE) ? " PE" : "", (e->flags & SR_FE) ? " FE" : "");
                uint8_t want_kind = (L <= 8) ? EV_FALSE_START : EV_FRAME;
                uint8_t want_f = (uint8_t)((L == 9 ? SR_NF : 0) | (k ? SR_PE : 0));
                if (e->kind != want_kind) ok = 0;
                if (want_kind == EV_FRAME && (e->data != 0xFF || e->flags != want_f)) ok = 0;
            }
            printf("   %2d    | %-14s | %s\n", L, col[0], col[1]);
        }
        CHECK(ok);
    }
    {   /* 'A' data 비트 1(값 0, 프레임 안 비트 번호 2) 에 1 스파이크 */
        stream_A_with_spike(&C8N1, 2, 9, 9, 1);  rx_init(&g_rx, C8N1); run_line(&g_rx);
        CHECK(g_rx.nlog == 1 && frame_is(&g_rx, 0, 0x41, SR_NF));          /* 1샘플: 다수결이 막음, NF */
        stream_A_with_spike(&C8N1, 2, 3, 5, 1);  rx_init(&g_rx, C8N1); run_line(&g_rx);
        CHECK(g_rx.nlog == 1 && frame_is(&g_rx, 0, 0x41, 0));              /* 샘플 창 밖: 안 보임 */
        stream_A_with_spike(&C8N1, 2, 8, 9, 1);  rx_init(&g_rx, C8N1); run_line(&g_rx);
        CHECK(g_rx.nlog == 1 && frame_is(&g_rx, 0, 0x43, SR_NF));          /* 2/3: 비트가 뒤집힘, NF 로는 보임 */
        stream_A_with_spike(&C8N1, 2, 8, 10, 1); rx_init(&g_rx, C8N1); run_line(&g_rx);
        CHECK(g_rx.nlog == 1 && frame_is(&g_rx, 0, 0x43, 0));              /* 3/3: 조용한 오류 (8N1) */
        stream_A_with_spike(&C8E1, 2, 8, 10, 1); rx_init(&g_rx, C8E1); run_line(&g_rx);
        CHECK(g_rx.nlog == 1 && frame_is(&g_rx, 0, 0x43, SR_PE));          /* 8E1 이면 parity 가 잡음 */
        stream_A_with_spike(&C8N1, 0, 9, 9, 1);  rx_init(&g_rx, C8N1); run_line(&g_rx);
        CHECK(g_rx.nlog == 1 && frame_is(&g_rx, 0, 0x41, SR_NF));          /* start 비트 안 스파이크: 시작은 인정, NF */
    }
    {   /* 진짜 start edge 보다 k 샘플 앞의 1샘플 low 글리치 (k=1..18): 'A' 'B' 둘 다 맞아야 (k=8 은 NF) */
        int ok = 1;
        for (int k = 1; k <= 18; k++) {
            g_n = 0; put_level(1, 40);
            size_t e = g_n; put_char(0x41, &C8N1, 4); put_char(0x42, &C8N1, 4);
            g_line[e - (size_t)k] = 0;
            rx_init(&g_rx, C8N1); run_line(&g_rx);
            ok &= (count_frames(&g_rx) == 2 && frame_is(&g_rx, 0, 0x41, k == 8 ? SR_NF : 0) && frame_is(&g_rx, 1, 0x42, 0));
            ok &= (g_rx.n_false_start == (k >= 9 ? 1u : 0u));
        }
        CHECK(ok);
    }
    {   /* framing error: 'A' 의 stop 비트를 0 으로 → FE (BRK 아님), 다음 'B' 는 정상 */
        g_n = 0; put_level(1, 32);
        size_t f0 = g_n; put_char(0x41, &C8N1, 3);
        memset(&g_line[f0 + 9 * OVS], 0, OVS);
        put_char(0x42, &C8N1, 3);
        rx_init(&g_rx, C8N1); run_line(&g_rx);
        CHECK(g_rx.nlog == 2 && frame_is(&g_rx, 0, 0x41, SR_FE) && frame_is(&g_rx, 1, 0x42, 0));
    }
    {   /* break: 30 비트 시간 low → 프레임 딱 1개 (0x00, FE|BRK), 그 뒤 'B' 정상. 8O1 이면 PE 도 */
        const uart_cfg_t *cs[2] = {&C8N1, &C8O1};
        const uint8_t want[2] = {SR_FE | SR_BRK, SR_FE | SR_BRK | SR_PE};
        for (int k = 0; k < 2; k++) {
            g_n = 0; put_level(1, 32); put_level(0, 30 * OVS); put_level(1, 3 * OVS); put_char(0x42, cs[k], 3);
            rx_init(&g_rx, *cs[k]); run_line(&g_rx);
            CHECK(g_rx.nlog == 2 && frame_is(&g_rx, 0, 0x00, want[k]) && frame_is(&g_rx, 1, 0x42, 0));
        }
        /* 0x00 바이트(stop=1)는 break 가 아니다 */
        g_n = 0; put_level(1, 32); put_char(0x00, &C8N1, 2);
        rx_init(&g_rx, C8N1); run_line(&g_rx);
        CHECK(g_rx.nlog == 1 && frame_is(&g_rx, 0, 0x00, 0));
    }
    {   /* overrun: 3바이트 연속, 소프트웨어가 안 읽음 → RXNE|ORE, RDR=첫 바이트, 2개 유실 */
        uint8_t d = 0, s;
        g_n = 0; put_level(1, 32); put_char(0x11, &C8N1, 0); put_char(0x22, &C8N1, 0); put_char(0x33, &C8N1, 4);
        rx_init(&g_rx, C8N1); run_line(&g_rx);
        CHECK(g_rx.nlog == 3);                                   /* 수신기는 세 개 다 봤다 */
        CHECK(g_rx.sr == (SR_RXNE | SR_ORE) && g_rx.rdr == 0x11 && g_rx.n_overrun == 2);
        s = uart_read(&g_rx, &d);
        CHECK(s == (SR_RXNE | SR_ORE) && d == 0x11);
        CHECK(g_rx.sr == 0);                                     /* 읽으면 지워진다 */
        g_n = 0; put_char(0x44, &C8N1, 2); run_line(&g_rx);
        s = uart_read(&g_rx, &d);
        CHECK(s == SR_RXNE && d == 0x44);
    }
    {   /* 플래그는 문자에 붙어 왔다가 읽으면 사라진다: NF 문자 → 정상 문자 */
        uint8_t d = 0, s1, s2;
        stream_A_with_spike(&C8N1, 2, 9, 9, 1); rx_init(&g_rx, C8N1); run_line(&g_rx);
        s1 = uart_read(&g_rx, &d);
        CHECK(s1 == (SR_RXNE | SR_NF) && d == 0x41);
        g_n = 0; put_char(0x5A, &C8N1, 2); run_line(&g_rx);
        s2 = uart_read(&g_rx, &d);
        CHECK(s2 == SR_RXNE && d == 0x5A);
        CHECK(uart_read(&g_rx, &d) == 0);                        /* 빈 RDR 읽기: RXNE 없음 */
    }

    /* ---- Part C ---- */
    {   /* 링 버퍼: 16개까지, FIFO 순서, free-running 카운터가 2^32 를 넘어도 */
        ring_t q; memset(&q, 0, sizeof q);
        q.head = q.tail = 0xFFFFFFF8u;
        int ok = 1; uint8_t b;
        for (int i = 0; i < 16; i++) ok &= ring_push(&q, (uint8_t)i);
        CHECK(ok && !ring_push(&q, 99));
        for (int i = 0; i < 16; i++) ok &= ring_pop(&q, &b) && b == i;
        CHECK(ok && !ring_pop(&q, &b) && q.head == 8u);
    }
    uint8_t tx[256], out[256];
    for (int i = 0; i < 256; i++) tx[i] = (uint8_t)xs32();
    link_stats_t st;
    {   /* 즉시 ISR + 자주 비움: 200 바이트 무손실 */
        size_t n = run_link(tx, 200, 0, 100, out, sizeof out, &st);
        CHECK(n == 200 && memcmp(out, tx, 200) == 0 && st.ore == 0 && st.rx == 200);
    }
    {   /* ISR 지연 경계: 8N1 연속 수신이면 다음 RXNE 는 160 틱(=10비트) 뒤 */
        static const uint32_t lat[] = {0, 80, 159, 160, 200, 320, 479, 480};
        printf("isr latency (ticks) | bytes in ring / 64 | ORE seen | lost (n_overrun)\n");
        for (size_t k = 0; k < sizeof lat / sizeof lat[0]; k++) {
            size_t n = run_link(tx, 64, lat[k], 50, out, sizeof out, &st);
            printf("   %3u (%5.2f bit)  |        %2zu          |   %2u     |   %2u\n",
                   (unsigned)lat[k], lat[k] / 16.0, n, (unsigned)st.ore, (unsigned)g_rx.n_overrun);
        }
        size_t n = run_link(tx, 64, 159, 50, out, sizeof out, &st);
        CHECK(n == 64 && st.ore == 0 && memcmp(out, tx, 64) == 0);
        n = run_link(tx, 64, 160, 50, out, sizeof out, &st);
        CHECK(st.ore > 0 && n < 64);
        n = run_link(tx, 64, 200, 50, out, sizeof out, &st);    /* 옛 바이트 유지 정책 → 짝수 번째만 남는다 */
        int ok = (n == 32 && g_rx.n_overrun == 32 && st.ore == 32);
        for (size_t i = 0; i < n && i < 32; i++) ok &= (out[i] == tx[2 * i]);
        CHECK(ok);
    }
    {   /* main 루프가 늦게 비우면: HW overrun 이 아니라 SW 링 오버플로 */
        size_t n = run_link(tx, 256, 0, 2400, out, sizeof out, &st);   /* 2400 틱 = 15 문자 < 16 칸 */
        CHECK(n == 256 && st.ring_drop == 0 && st.ore == 0 && memcmp(out, tx, 256) == 0);
        n = run_link(tx, 256, 0, 4000, out, sizeof out, &st);          /* 25 문자 > 16 칸 */
        printf("drain every 4000 ticks: kept %zu, ring_drop %u, ORE %u\n", n, (unsigned)st.ring_drop, (unsigned)st.ore);
        CHECK(st.ring_drop > 0 && st.ore == 0 && n + st.ring_drop == 256 && st.rx == 256);
    }
    {   /* 에러 섞인 스트림: 'H' 'i' [FE 문자] [break] '!' → 링엔 "Hi!" 만, 카운터 fe=1 brk=1 */
        ring_t q; memset(&q, 0, sizeof q);
        memset(&st, 0, sizeof st);
        g_n = 0; put_level(1, 32); put_char('H', &C8N1, 1); put_char('i', &C8N1, 1);
        size_t f0 = g_n; put_char('x', &C8N1, 2); memset(&g_line[f0 + 9 * OVS], 0, OVS);
        put_level(0, 20 * OVS); put_level(1, 2 * OVS); put_char('!', &C8N1, 2);
        rx_init(&g_rx, C8N1);
        for (size_t i = 0; i < g_n; i++) { rx_tick(&g_rx, g_line[i]); if (g_rx.sr & SR_RXNE) uart_isr(&g_rx, &q, &st); }
        char got[8] = {0}; size_t k = 0; uint8_t b;
        while (k < 7 && ring_pop(&q, &b)) got[k++] = (char)b;
        CHECK(strcmp(got, "Hi!") == 0 && st.fe == 1 && st.brk == 1 && st.rx == 3);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
