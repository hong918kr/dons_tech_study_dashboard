/* COMM-B10 추가 연습 — 한 번에 검증
 *
 * 연습 1: CRC 잔여값(residue) 검사 — CRC 를 big-endian 으로 붙이면 LEN..CRC_LO 전체의 CRC 가 0
 * 연습 2: UART RX ISR → 2의 거듭제곱 링 버퍼 → 메인 루프 파서 (메인 루프가 멈추면 overflow, 이후 자동 복구)
 * 연습 3: TYPE 별 핸들러 표 + 길이 검사 + 응답 프레임 (PING / ECHO / SET_LED / NAK)
 * 연습 4: 라인 idle(바이트 타임아웃) 때 fp_flush — 미완성 가짜 프레임에 갇힌 진짜 프레임 꺼내기
 *
 * 프레임 형식·CRC·파서는 solution.c 와 같다 (축약판: 재동기화 루프를 fp_replay 로 분리).
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define FRAME_SOF          0xA5u
#define FRAME_MAX_PAYLOAD  64u
#define FRAME_OVERHEAD     5u
#define FRAME_MAX          (FRAME_MAX_PAYLOAD + FRAME_OVERHEAD)
#define FP_RAW_MAX         (FRAME_MAX - 1u)
enum { FE_ERR_LEN = -1, FE_ERR_CAP = -2 };

static uint16_t crc16_update(uint16_t crc, uint8_t b) {
    crc ^= (uint16_t)(b << 8);
    for (int i = 0; i < 8; i++)
        crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u) : (uint16_t)(crc << 1);
    return crc;
}
static uint16_t crc16(const uint8_t *p, size_t n) {
    uint16_t c = 0xFFFFu;
    for (size_t i = 0; i < n; i++) c = crc16_update(c, p[i]);
    return c;
}
static int frame_encode(uint8_t type, const uint8_t *payload, size_t len, uint8_t *out, size_t cap) {
    if (len > FRAME_MAX_PAYLOAD) return FE_ERR_LEN;
    if (out == NULL || cap < len + FRAME_OVERHEAD) return FE_ERR_CAP;
    out[0] = FRAME_SOF; out[1] = (uint8_t)len; out[2] = type;
    if (len) memcpy(out + 3, payload, len);
    uint16_t c = crc16(out + 1, len + 2);
    out[3 + len] = (uint8_t)(c >> 8); out[4 + len] = (uint8_t)c;
    return (int)(len + FRAME_OVERHEAD);
}

typedef enum { ST_SOF, ST_LEN, ST_TYPE, ST_PAYLOAD, ST_CRC_HI, ST_CRC_LO } fp_state;
typedef enum { EV_NONE, EV_FRAME, EV_ERR_LEN, EV_ERR_CRC } fp_event;
typedef void (*frame_cb)(void *ctx, uint8_t type, const uint8_t *payload, uint8_t len);
typedef struct {
    fp_state st; uint8_t raw[FP_RAW_MAX]; uint8_t raw_n, len; uint16_t crc;
    uint32_t frames_ok, crc_err, len_err, resyncs;
    frame_cb on_frame; void *ctx;
} frame_parser;

static void fp_init(frame_parser *p, frame_cb cb, void *ctx) { memset(p, 0, sizeof *p); p->on_frame = cb; p->ctx = ctx; }
static fp_event fp_step(frame_parser *p, uint8_t b) {
    if (p->st == ST_SOF) { if (b == FRAME_SOF) { p->st = ST_LEN; p->raw_n = 0; p->crc = 0xFFFFu; } return EV_NONE; }
    p->raw[p->raw_n++] = b;
    switch (p->st) {
    case ST_LEN:
        if (b > FRAME_MAX_PAYLOAD) { p->len_err++; p->st = ST_SOF; return EV_ERR_LEN; }
        p->len = b; p->crc = crc16_update(p->crc, b); p->st = ST_TYPE; return EV_NONE;
    case ST_TYPE:    p->crc = crc16_update(p->crc, b); p->st = p->len ? ST_PAYLOAD : ST_CRC_HI; return EV_NONE;
    case ST_PAYLOAD: p->crc = crc16_update(p->crc, b); if (p->raw_n == 2u + p->len) p->st = ST_CRC_HI; return EV_NONE;
    case ST_CRC_HI:  p->st = ST_CRC_LO; return EV_NONE;
    case ST_CRC_LO:
        p->st = ST_SOF;
        if ((uint16_t)((p->raw[p->raw_n - 2] << 8) | b) != p->crc) { p->crc_err++; return EV_ERR_CRC; }
        p->frames_ok++; return EV_FRAME;
    default: p->st = ST_SOF; return EV_NONE;
    }
}
/* q[0..qn) 을 훑는다. 오류가 나면 가짜 SOF 다음부터 다시 (solution.c 의 fp_feed 재동기화 루프와 같음) */
static int fp_replay(frame_parser *p, uint8_t *q, size_t qn) {
    int frames = 0;
    size_t i = 0;
    while (i < qn) {
        fp_event ev = fp_step(p, q[i++]);
        if (ev == EV_FRAME) { if (p->on_frame) p->on_frame(p->ctx, p->raw[1], p->raw + 2, p->len); frames++; }
        else if (ev != EV_NONE) {
            size_t rest = qn - i;
            memmove(q + p->raw_n, q + i, rest);
            memcpy(q, p->raw, p->raw_n);
            qn = p->raw_n + rest; i = 0; p->resyncs++;
        }
    }
    return frames;
}
static int fp_feed(frame_parser *p, uint8_t b) { uint8_t q[FP_RAW_MAX]; q[0] = b; return fp_replay(p, q, 1); }

/* 연습 4: 라인이 idle 이 되면(바이트 타임아웃) 미완성 프레임을 버리되, 그 안의 바이트는 다시 훑는다 */
static int fp_flush(frame_parser *p) {
    if (p->st == ST_SOF) return 0;
    uint8_t q[FP_RAW_MAX];
    size_t n = p->raw_n;
    memcpy(q, p->raw, n);
    p->st = ST_SOF;
    p->resyncs++;
    int frames = fp_replay(p, q, n);
    if (p->st != ST_SOF) frames += fp_flush(p);   /* 다시 훑다가 또 미완성으로 끝나면 한 번 더 (매번 짧아짐) */
    return frames;
}

/* ---------------- 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

typedef struct { uint8_t type, len; uint8_t pl[FRAME_MAX_PAYLOAD]; } rx_frame;
static rx_frame g_rx[1024];
static size_t g_rxn;
static void sink_cb(void *ctx, uint8_t type, const uint8_t *pl, uint8_t len) {
    (void)ctx;
    if (g_rxn >= 1024) return;
    g_rx[g_rxn].type = type; g_rx[g_rxn].len = len; memcpy(g_rx[g_rxn].pl, pl, len); g_rxn++;
}

/* ---------------- 연습 1: residue ---------------- */
static bool residue_ok(const uint8_t *frame, size_t n) {
    return n >= FRAME_OVERHEAD && frame[0] == FRAME_SOF && crc16(frame + 1, n - 1) == 0;
}

/* ---------------- 연습 2: ISR → 링 버퍼 → 메인 루프 ---------------- */
#define RB_SIZE 128u                       /* 2의 거듭제곱: 인덱스는 & (RB_SIZE-1) (DS-B04) */
typedef struct {
    uint8_t  buf[RB_SIZE];
    uint32_t head, tail;                   /* 자유 증가 카운터. 실기에선 ISR/메인 공유 → volatile + 순서 (C-M14, DS-A02) */
    uint32_t overflow;
    long     first_drop, last_drop;        /* 테스트용: 버린 바이트의 스트림 위치 */
} rb_t;
static void uart_rx_isr(rb_t *r, uint8_t b, long pos) {   /* RXNE 인터럽트 한 번 = 바이트 한 개 */
    if (r->head - r->tail == RB_SIZE) {                    /* 가득 참: 새 바이트를 버린다 */
        r->overflow++;
        if (r->first_drop < 0) r->first_drop = pos;
        r->last_drop = pos;
        return;
    }
    r->buf[r->head & (RB_SIZE - 1u)] = b;
    r->head++;
}
static void main_loop_poll(rb_t *r, frame_parser *p) {    /* 쌓인 바이트를 전부 파서로 */
    while (r->tail != r->head) { fp_feed(p, r->buf[r->tail & (RB_SIZE - 1u)]); r->tail++; }
}

/* ---------------- 연습 3: 디스패처 ---------------- */
enum { T_PING = 0x01, T_ECHO = 0x02, T_SET_LED = 0x03, T_NAK = 0x7F, T_REPLY = 0x80 };
enum { NAK_UNKNOWN_TYPE = 1, NAK_BAD_LEN = 2, NAK_BAD_ARG = 3 };
typedef int (*handler_fn)(const uint8_t *pl, uint8_t len, uint8_t *rsp, uint8_t *rsp_len);  /* 0 또는 NAK 이유 */
typedef struct { uint8_t type, min_len, max_len; handler_fn fn; } cmd_entry;

static uint8_t g_led;
static int h_ping(const uint8_t *pl, uint8_t len, uint8_t *rsp, uint8_t *rl) { (void)pl; (void)len; (void)rsp; *rl = 0; return 0; }
static int h_echo(const uint8_t *pl, uint8_t len, uint8_t *rsp, uint8_t *rl) { memcpy(rsp, pl, len); *rl = len; return 0; }
static int h_led(const uint8_t *pl, uint8_t len, uint8_t *rsp, uint8_t *rl) {
    (void)len;
    if (pl[0] > 1) return NAK_BAD_ARG;
    g_led = pl[0]; rsp[0] = g_led; *rl = 1; return 0;
}
static const cmd_entry CMDS[] = {
    { T_PING, 0, 0, h_ping }, { T_ECHO, 0, FRAME_MAX_PAYLOAD, h_echo }, { T_SET_LED, 1, 1, h_led },
};
static uint8_t g_tx[FRAME_MAX];
static int g_txn;
static void dispatch_cb(void *ctx, uint8_t type, const uint8_t *pl, uint8_t len) {
    (void)ctx;
    uint8_t rsp[FRAME_MAX_PAYLOAD], rl = 0;
    int why = NAK_UNKNOWN_TYPE;
    for (size_t i = 0; i < sizeof CMDS / sizeof CMDS[0]; i++) {
        if (CMDS[i].type != type) continue;
        why = (len < CMDS[i].min_len || len > CMDS[i].max_len) ? NAK_BAD_LEN : CMDS[i].fn(pl, len, rsp, &rl);
        break;
    }
    if (why == 0) g_txn = frame_encode((uint8_t)(type | T_REPLY), rsp, rl, g_tx, sizeof g_tx);
    else { uint8_t nak[2] = { type, (uint8_t)why }; g_txn = frame_encode(T_NAK, nak, 2, g_tx, sizeof g_tx); }
}
/* 요청 하나를 보내고, 응답 바이트를 다시 파서로 읽어 (type, payload) 를 돌려준다 */
static bool request(uint8_t type, const uint8_t *pl, uint8_t len, rx_frame *reply) {
    uint8_t f[FRAME_MAX + 4];
    frame_parser dev, host;
    fp_init(&dev, dispatch_cb, NULL);
    g_txn = 0;
    int n = (len <= FRAME_MAX_PAYLOAD) ? frame_encode(type, pl, len, f, sizeof f) : 0;
    if (len > FRAME_MAX_PAYLOAD) {                       /* 인코더가 거부하는 LEN 을 손으로 만든다 */
        f[0] = FRAME_SOF; f[1] = len; f[2] = type; n = 3;
    }
    for (int i = 0; i < n; i++) fp_feed(&dev, f[i]);
    if (g_txn <= 0) return false;
    g_rxn = 0;
    fp_init(&host, sink_cb, NULL);
    for (int i = 0; i < g_txn; i++) fp_feed(&host, g_tx[i]);
    if (g_rxn != 1) return false;
    *reply = g_rx[0];
    return true;
}

int main(void) {
    uint8_t fb[FRAME_MAX];
    uint32_t seed = 0x1234ABCDu;

    /* ===== 연습 1 ===== */
    {
        int bad = 0;
        for (unsigned len = 0; len <= FRAME_MAX_PAYLOAD; len++) {
            uint8_t pl[FRAME_MAX_PAYLOAD];
            for (unsigned i = 0; i < len; i++) pl[i] = (uint8_t)xs32(&seed);
            int n = frame_encode((uint8_t)len, pl, len, fb, sizeof fb);
            if (n <= 0 || !residue_ok(fb, (size_t)n)) bad++;
        }
        CHECK(bad == 0);
        const uint8_t hi[2] = { 0x48, 0x69 };
        int n = frame_encode(0x01, hi, 2, fb, sizeof fb);
        int flips_missed = 0;
        for (int byte = 1; byte < n; byte++)
            for (int bit = 0; bit < 8; bit++) {
                fb[byte] ^= (uint8_t)(1u << bit);
                if (residue_ok(fb, (size_t)n)) flips_missed++;
                fb[byte] ^= (uint8_t)(1u << bit);
            }
        CHECK(n == 7 && flips_missed == 0);
        uint8_t le[7]; memcpy(le, fb, 7); le[5] = fb[6]; le[6] = fb[5];        /* CRC 를 little-endian 으로 */
        CHECK(crc16(le + 1, 6) == 0xFB1A && !residue_ok(le, 7));
    }

    /* ===== 연습 2 ===== */
    {
        static uint8_t s[60000];
        static long fstart[600], fend[600];
        static rx_frame fx[600];
        size_t sn = 0;
        for (int k = 0; k < 500; k++) {
            fx[k].type = (uint8_t)xs32(&seed);
            fx[k].len = (uint8_t)(xs32(&seed) % 65u);
            for (unsigned i = 0; i < fx[k].len; i++) fx[k].pl[i] = (uint8_t)xs32(&seed);
            int n = frame_encode(fx[k].type, fx[k].pl, fx[k].len, s + sn, sizeof s - sn);
            fstart[k] = (long)sn; sn += (size_t)(n > 0 ? n : 0); fend[k] = (long)sn - 1;
        }
        memset(s + sn, 0, FP_RAW_MAX); sn += FP_RAW_MAX;

        for (int scenario = 0; scenario < 2; scenario++) {
            rb_t rb; memset(&rb, 0, sizeof rb); rb.first_drop = rb.last_drop = -1;
            frame_parser p; fp_init(&p, sink_cb, NULL); g_rxn = 0;
            for (size_t t = 0; t < sn; t++) {
                uart_rx_isr(&rb, s[t], (long)t);
                bool stalled = scenario == 1 && t >= 10000 && t < 10400;     /* 400 바이트 시간 동안 메인 루프 멈춤 */
                if (!stalled && t % 100u == 99u) main_loop_poll(&rb, &p);    /* 평소엔 100 바이트마다 */
            }
            main_loop_poll(&rb, &p);
            /* 손실 구간과 겹치지 않는 프레임은 전부 순서대로 받아야 한다 */
            size_t want = 0, j = 0, got = 0;
            for (int k = 0; k < 500; k++) {
                bool hit = rb.first_drop >= 0 && fend[k] >= rb.first_drop && fstart[k] <= rb.last_drop;
                if (hit) continue;
                want++;
                for (; j < g_rxn; j++)
                    if (g_rx[j].type == fx[k].type && g_rx[j].len == fx[k].len && memcmp(g_rx[j].pl, fx[k].pl, fx[k].len) == 0) { got++; j++; break; }
            }
            printf("practice 2 scenario %d: overflow=%u bytes (stream pos %ld..%ld), frames needed %zu got %zu, total rx %zu, crc_err=%u len_err=%u\n",
                   scenario, rb.overflow, rb.first_drop, rb.last_drop, want, got, g_rxn, p.crc_err, p.len_err);
            if (scenario == 0) CHECK(rb.overflow == 0 && got == 500 && g_rxn == 500);
            else CHECK(rb.overflow > 0 && got == want && want < 500 && g_rxn <= 500);
        }
    }

    /* ===== 연습 3 ===== */
    {
        rx_frame r;
        const uint8_t hello[5] = { 'h', 'e', 'l', 'l', 'o' };
        const uint8_t on = 1, bad = 7, two[2] = { 1, 1 };
        CHECK(request(T_PING, NULL, 0, &r) && r.type == (T_PING | T_REPLY) && r.len == 0);
        CHECK(request(T_ECHO, hello, 5, &r) && r.type == (T_ECHO | T_REPLY) && r.len == 5 && memcmp(r.pl, hello, 5) == 0);
        CHECK(request(T_SET_LED, &on, 1, &r) && r.type == (T_SET_LED | T_REPLY) && r.pl[0] == 1 && g_led == 1);
        CHECK(request(T_SET_LED, &bad, 1, &r) && r.type == T_NAK && r.pl[0] == T_SET_LED && r.pl[1] == NAK_BAD_ARG && g_led == 1);
        CHECK(request(T_SET_LED, two, 2, &r) && r.type == T_NAK && r.pl[1] == NAK_BAD_LEN);
        CHECK(request(T_PING, two, 1, &r) && r.type == T_NAK && r.pl[1] == NAK_BAD_LEN);
        CHECK(request(0x42, NULL, 0, &r) && r.type == T_NAK && r.pl[0] == 0x42 && r.pl[1] == NAK_UNKNOWN_TYPE);
        CHECK(!request(T_ECHO, hello, 65, &r));                     /* LEN 65: 파서가 len_err → 응답 없음 */
    }

    /* ===== 연습 4 ===== */
    {
        const uint8_t a_pl[3] = { 1, 2, 3 }, b_pl[4] = { 9, 8, 7, 6 };
        uint8_t s[64]; size_t sn = 0;
        int n = frame_encode(0x11, a_pl, 3, s, sizeof s); sn += (size_t)n;
        s[sn++] = 0xA5; s[sn++] = 0x3C;                              /* 쓰레기: 가짜 SOF + 가짜 LEN 60 */
        n = frame_encode(0x22, b_pl, 4, s + sn, sizeof s - sn); sn += (size_t)n;
        /* 그리고 라인이 조용해진다 (더 이상 바이트 없음) */
        frame_parser p;
        fp_init(&p, sink_cb, NULL); g_rxn = 0;
        for (size_t i = 0; i < sn; i++) fp_feed(&p, s[i]);
        CHECK(g_rxn == 1 && g_rx[0].type == 0x11 && p.st != ST_SOF);     /* B 는 가짜 프레임 안에 갇힘 */
        int got = fp_flush(&p);                                        /* idle 타임아웃 → flush */
        CHECK(got == 1 && g_rxn == 2 && g_rx[1].type == 0x22 && g_rx[1].len == 4 && memcmp(g_rx[1].pl, b_pl, 4) == 0);
        CHECK(p.st == ST_SOF && fp_flush(&p) == 0);                    /* 이미 SOF 대기면 아무 일 없음 */
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
