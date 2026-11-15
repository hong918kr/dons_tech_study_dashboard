/* COMM-B10 demo_resync.c — 기본 파서 vs 재동기화 파서: 쓰레기 속 SOF 밀도별 복구율, 거짓 수락률, 바이트당 비용
 *
 * 실험 1: 프레임 100000개 사이에 쓰레기 0..24 바이트(그중 비율 d 가 0xA5), 프레임 손상 0% / 10%.
 *         정상 프레임 복구율(기본 vs 재동기화), 거짓 수락(기대 목록에 없는 프레임), fp_step 수/입력 바이트, 최악 한 바이트 비용.
 * 실험 2: 순수 랜덤 바이트(절반이 0xA5)를 재동기화 파서에 넣어 거짓 수락 수를 이론값
 *         (#0xA5 위치 × P(LEN 유효) × 2^-16) 과 비교.
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 demo_resync.c -o demo_resync && ./demo_resync
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---- solution.c 의 CRC·인코더·파서 그대로 ---- */
#define FRAME_SOF          0xA5u
#define FRAME_MAX_PAYLOAD  64u
#define FRAME_OVERHEAD     5u                                   /* SOF LEN TYPE CRC_HI CRC_LO */
#define FRAME_MAX          (FRAME_MAX_PAYLOAD + FRAME_OVERHEAD) /* 69 */
#define FP_RAW_MAX         (FRAME_MAX - 1u)                     /* SOF 뒤: LEN TYPE PAYLOAD CRC×2 = 68 */

enum { FE_ERR_LEN = -1, FE_ERR_CAP = -2 };

/* ---------------- Part A: CRC 와 인코더 ---------------- */

/* CRC-16/CCITT-FALSE 한 바이트 갱신 (MSB 먼저, 비트 단위 — 원리는 ALGO-M01, 테이블은 ALGO-M02) */
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

/* 프레임 하나를 out 에 쓴다. 반환: 쓴 바이트 수(len + 5) 또는 FE_ERR_LEN / FE_ERR_CAP (out 은 건드리지 않음).
 * len == 0 이면 payload 는 NULL 이어도 된다. */
static int frame_encode(uint8_t type, const uint8_t *payload, size_t len, uint8_t *out, size_t cap) {
    if (len > FRAME_MAX_PAYLOAD) return FE_ERR_LEN;
    if (out == NULL || cap < len + FRAME_OVERHEAD) return FE_ERR_CAP;
    out[0] = FRAME_SOF;
    out[1] = (uint8_t)len;
    out[2] = type;
    if (len) memcpy(out + 3, payload, len);
    uint16_t c = crc16(out + 1, len + 2);          /* LEN, TYPE, PAYLOAD */
    out[3 + len] = (uint8_t)(c >> 8);              /* CRC 상위 바이트 먼저 */
    out[4 + len] = (uint8_t)c;
    return (int)(len + FRAME_OVERHEAD);
}

/* ---------------- 파서 ---------------- */

typedef enum { ST_SOF, ST_LEN, ST_TYPE, ST_PAYLOAD, ST_CRC_HI, ST_CRC_LO } fp_state;
typedef enum { EV_NONE, EV_FRAME, EV_ERR_LEN, EV_ERR_CRC } fp_event;

typedef void (*frame_cb)(void *ctx, uint8_t type, const uint8_t *payload, uint8_t len);

typedef struct { uint32_t frames_ok, crc_err, len_err, resyncs; } fp_stats;

typedef struct {
    fp_state st;
    uint8_t  raw[FP_RAW_MAX];   /* SOF 뒤에 받은 바이트 그대로: [0]=LEN [1]=TYPE [2..]=PAYLOAD, 그 뒤 CRC 2개 */
    uint8_t  raw_n;
    uint8_t  len;               /* raw[0] 사본 (검사 통과한 값) */
    uint16_t crc;               /* LEN 부터 누적하는 CRC */
    bool     resync;            /* false = Part A/B 기본 파서, true = Part C 재동기화 */
    fp_stats stats;
    frame_cb on_frame;
    void    *ctx;
    uint32_t steps, max_steps;  /* fp_feed 한 번에 돈 fp_step 수 — ISR 비용 측정용 */
} frame_parser;

static void fp_init(frame_parser *p, bool resync, frame_cb cb, void *ctx) {
    memset(p, 0, sizeof *p);
    p->st = ST_SOF;
    p->resync = resync;
    p->on_frame = cb;
    p->ctx = ctx;
}

/* 바이트 하나로 상태를 한 칸 옮긴다. EV_FRAME 이면 p->raw + 2 에 PAYLOAD, p->len 바이트 (다음 호출 전까지 유효).
 * 오류(EV_ERR_*)면 통계를 올리고 ST_SOF 로 돌아간다. 오류를 일으킨 바이트까지 raw 에 남아 있다. */
static fp_event fp_step(frame_parser *p, uint8_t b) {
    p->steps++;
    if (p->st == ST_SOF) {
        if (b == FRAME_SOF) { p->st = ST_LEN; p->raw_n = 0; p->crc = 0xFFFFu; }
        return EV_NONE;
    }
    p->raw[p->raw_n++] = b;                     /* 최대 2 + 64 + 2 = 68 = FP_RAW_MAX */
    switch (p->st) {
    case ST_LEN:
        if (b > FRAME_MAX_PAYLOAD) { p->stats.len_err++; p->st = ST_SOF; return EV_ERR_LEN; }
        p->len = b;
        p->crc = crc16_update(p->crc, b);
        p->st = ST_TYPE;
        return EV_NONE;
    case ST_TYPE:
        p->crc = crc16_update(p->crc, b);
        p->st = p->len ? ST_PAYLOAD : ST_CRC_HI;
        return EV_NONE;
    case ST_PAYLOAD:
        p->crc = crc16_update(p->crc, b);
        if (p->raw_n == 2u + p->len) p->st = ST_CRC_HI;
        return EV_NONE;
    case ST_CRC_HI:
        p->st = ST_CRC_LO;
        return EV_NONE;
    case ST_CRC_LO: {
        p->st = ST_SOF;
        uint16_t rx = (uint16_t)((p->raw[p->raw_n - 2] << 8) | b);
        if (rx != p->crc) { p->stats.crc_err++; return EV_ERR_CRC; }
        p->stats.frames_ok++;
        return EV_FRAME;
    }
    default:
        p->st = ST_SOF;
        return EV_NONE;
    }
}

static void fp_deliver(frame_parser *p) {
    if (p->on_frame) p->on_frame(p->ctx, p->raw[1], p->raw + 2, p->len);
}

/* 수신 바이트 하나를 먹인다 (UART RX ISR 또는 메인 루프에서). 반환: 이번에 전달한 프레임 수. */
static int fp_feed(frame_parser *p, uint8_t b) {
    int frames = 0;
    p->steps = 0;
    if (!p->resync) {
        if (fp_step(p, b) == EV_FRAME) { fp_deliver(p); frames++; }
    } else {
        /* q = 아직 훑지 않은 바이트. 오류가 나면 q = (가짜 SOF 뒤에 먹은 바이트) + (q 의 남은 부분).
         * 매번 가짜 SOF 한 바이트만큼 짧아지므로 q 는 FP_RAW_MAX 를 넘지 않고 루프는 끝난다. */
        uint8_t q[FP_RAW_MAX];
        size_t qn = 1, i = 0;
        q[0] = b;
        while (i < qn) {
            fp_event ev = fp_step(p, q[i++]);
            if (ev == EV_FRAME) {
                fp_deliver(p);
                frames++;
            } else if (ev == EV_ERR_LEN || ev == EV_ERR_CRC) {
                size_t rest = qn - i;
                memmove(q + p->raw_n, q + i, rest);
                memcpy(q, p->raw, p->raw_n);
                qn = p->raw_n + rest;
                i = 0;
                p->stats.resyncs++;
            }
        }
    }
    if (p->steps > p->max_steps) p->max_steps = p->steps;
    return frames;
}

/* ---- 실험 ---- */
static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

typedef struct { uint8_t type, len; uint8_t pl[FRAME_MAX_PAYLOAD]; } rx_frame;
#define NF 100000
static rx_frame g_exp[NF];
static size_t g_nexp, g_next, g_got, g_spur;
static uint64_t g_steps;
static void cb(void *ctx, uint8_t type, const uint8_t *pl, uint8_t len) {
    (void)ctx;
    for (size_t k = g_next; k < g_nexp && k < g_next + 64; k++) {     /* 앞으로 64개 안에서 찾기 */
        const rx_frame *e = &g_exp[k];
        if (e->type == type && e->len == len && memcmp(e->pl, pl, len) == 0) { g_got++; g_next = k + 1; return; }
    }
    g_spur++;
}

static uint8_t g_s[NF * (24 + FRAME_MAX) + FP_RAW_MAX];
static size_t g_sn;

static void build(uint32_t seed, unsigned d_num, unsigned d_den, unsigned corrupt_pct) {
    g_sn = 0; g_nexp = 0;
    for (int k = 0; k < NF; k++) {
        unsigned gap = xs32(&seed) % 25u;
        for (unsigned g = 0; g < gap; g++) {
            uint32_t r = xs32(&seed);
            g_s[g_sn++] = (r % d_den < d_num) ? (uint8_t)FRAME_SOF : (uint8_t)(r >> 8);
        }
        rx_frame f; memset(&f, 0, sizeof f);
        f.type = (uint8_t)xs32(&seed);
        f.len = (uint8_t)(xs32(&seed) % (FRAME_MAX_PAYLOAD + 1u));
        for (unsigned i = 0; i < f.len; i++) f.pl[i] = (uint8_t)xs32(&seed);
        uint8_t fb[FRAME_MAX];
        int fn = frame_encode(f.type, f.pl, f.len, fb, sizeof fb);
        if (fn <= 0) continue;
        if (xs32(&seed) % 100u < corrupt_pct) {
            uint32_t r = xs32(&seed);
            fb[r % (uint32_t)fn] ^= (uint8_t)(1u << ((r >> 16) % 8u));
        } else {
            g_exp[g_nexp++] = f;
        }
        memcpy(g_s + g_sn, fb, (size_t)fn); g_sn += (size_t)fn;
    }
    memset(g_s + g_sn, 0, FP_RAW_MAX); g_sn += FP_RAW_MAX;
}

static frame_parser g_p;
static void run(bool resync) {
    g_next = g_got = g_spur = 0; g_steps = 0;
    fp_init(&g_p, resync, cb, NULL);
    for (size_t i = 0; i < g_sn; i++) { fp_feed(&g_p, g_s[i]); g_steps += g_p.steps; }
}

int main(void) {
    printf("실험 1: 프레임 %d개, 쓰레기 간격 0..24 바이트 (평균 12)\n", NF);
    printf("| 쓰레기 중 0xA5 비율 | 손상 | 기본 파서 복구 | 재동기화 복구 | 거짓 수락 | step/byte | 최악 step/feed |\n");
    printf("|---|---|---|---|---|---|---|\n");
    const struct { unsigned num, den; const char *name; } dens[] = {
        { 0, 1, "0 (랜덤만, ≈1/256)" }, { 1, 16, "1/16" }, { 1, 4, "1/4" }, { 1, 2, "1/2" } };
    for (unsigned c = 0; c < 2; c++)
        for (unsigned d = 0; d < 4; d++) {
            unsigned cp = c ? 10u : 0u;
            build(0xBEEF0000u + d * 7u + c, dens[d].num, dens[d].den, cp);
            run(false);
            double naive = 100.0 * (double)g_got / (double)g_nexp;
            size_t nspur = g_spur;
            run(true);
            printf("| %s | %u%% | %.2f%% | %.3f%% (%zu/%zu) | %zu / %zu | %.2f | %u |\n",
                   dens[d].name, cp, naive, 100.0 * (double)g_got / (double)g_nexp, g_got, g_nexp,
                   nspur, g_spur, (double)g_steps / (double)g_sn, g_p.max_steps);
        }

    /* 실험 2 */
    uint32_t seed = 0x5EED1234u;
    const size_t N = 60000000u;
    size_t n_a5 = 0;
    g_nexp = 0; g_next = g_got = g_spur = 0;
    fp_init(&g_p, true, cb, NULL);
    uint64_t steps = 0;
    for (size_t i = 0; i < N; i++) {
        uint32_t r = xs32(&seed);
        uint8_t b = (r & 1u) ? (uint8_t)FRAME_SOF : (uint8_t)(r >> 8);
        if (b == FRAME_SOF) n_a5++;
        fp_feed(&g_p, b); steps += g_p.steps;
    }
    double theory = (double)n_a5 * (65.0 / 512.0) / 65536.0;
    printf("\n실험 2: 랜덤 %zu 바이트 (0xA5 %zu개), 재동기화 파서\n", N, n_a5);
    printf("  거짓 수락 %zu개 (이론 ≈ %.1f = #A5 × 65/512 × 2^-16), len_err=%u crc_err=%u, step/byte=%.2f, 최악 step/feed=%u\n",
           g_spur, theory, g_p.stats.len_err, g_p.stats.crc_err, (double)steps / (double)N, g_p.max_steps);
    return 0;
}
