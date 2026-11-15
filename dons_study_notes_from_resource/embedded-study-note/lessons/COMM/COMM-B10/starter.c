/* COMM-B10 길이 prefix + 체크섬 프레임 인코더/파서
 *
 * 프레임: [SOF=0xA5][LEN][TYPE][PAYLOAD × LEN][CRC_HI][CRC_LO]
 *   LEN   = PAYLOAD 바이트 수만 센다 (TYPE·CRC 제외). 0..FRAME_MAX_PAYLOAD(64). 전체 길이 = LEN + 5.
 *   CRC16 = CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF, 반사 없음, xorout 0x0000)
 *           덮는 범위 = LEN, TYPE, PAYLOAD (SOF 는 제외). 와이어에는 상위 바이트 먼저 (big-endian).
 *
 * Part A: crc16_update, frame_encode (용량 검사), 바이트 단위 파서 상태 머신 fp_step (정상 경로)
 * Part B: fp_step 의 오류 경로 (LEN > MAX → len_err, CRC 불일치 → crc_err) + 잘림·중간 시작·SOF 값 데이터 테스트
 * Part C: fp_feed 재동기화 — 오류가 나면 "가짜 SOF 바로 다음 바이트"부터 다시 훑는다 (고정 68바이트 버퍼)
 *
 * 테스트: CRC check 값("123456789" → 0x29B1), 손으로 계산한 프레임 바이트, LEN 0..64 전수 왕복,
 *         단일 비트 오류 전수, 고정 시드 xorshift32 쓰레기+프레임(+10% 손상) 스트림에서 정상 프레임 전부 복구.
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define FRAME_SOF          0xA5u
#define FRAME_MAX_PAYLOAD  64u
#define FRAME_OVERHEAD     5u                                   /* SOF LEN TYPE CRC_HI CRC_LO */
#define FRAME_MAX          (FRAME_MAX_PAYLOAD + FRAME_OVERHEAD) /* 69 */
#define FP_RAW_MAX         (FRAME_MAX - 1u)                     /* SOF 뒤: LEN TYPE PAYLOAD CRC×2 = 68 */

enum { FE_ERR_LEN = -1, FE_ERR_CAP = -2 };

/* ---------------- Part A: CRC 와 인코더 ---------------- */

/* CRC-16/CCITT-FALSE 한 바이트 갱신 (MSB 먼저, 비트 단위 — 원리는 ALGO-M01, 테이블은 ALGO-M02) */
static uint16_t crc16_update(uint16_t crc, uint8_t b) {
    (void)b;
    return crc; /* TODO A1: b 를 상위 8비트에 XOR, 8번 "MSB 가 1 이면 <<1 ^ 0x1021, 아니면 <<1" */
}

static uint16_t crc16(const uint8_t *p, size_t n) {
    uint16_t c = 0xFFFFu;
    for (size_t i = 0; i < n; i++) c = crc16_update(c, p[i]);
    return c;
}

/* 프레임 하나를 out 에 쓴다. 반환: 쓴 바이트 수(len + 5) 또는 FE_ERR_LEN / FE_ERR_CAP (out 은 건드리지 않음).
 * len == 0 이면 payload 는 NULL 이어도 된다. */
static int frame_encode(uint8_t type, const uint8_t *payload, size_t len, uint8_t *out, size_t cap) {
    (void)type; (void)payload; (void)len; (void)out; (void)cap;
    return FE_ERR_CAP; /* TODO A2: LEN 검사 → 용량 검사 → SOF LEN TYPE PAYLOAD → CRC(LEN..PAYLOAD) 상위 바이트 먼저 */
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
    (void)b;
    return EV_NONE; /* TODO A3: ST_SOF→LEN→TYPE→PAYLOAD→CRC_HI→CRC_LO, SOF 뒤 바이트는 raw 에 기록, CRC 는 바이트마다 누적
                     * TODO B1: LEN > FRAME_MAX_PAYLOAD → len_err, CRC 불일치 → crc_err (둘 다 ST_SOF 로), 일치 → frames_ok */
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
        /* TODO C1: 오류(EV_ERR_LEN/CRC)가 나면 raw[0..raw_n) 을 q 앞에 붙여 다시 훑기 (resyncs++) */
        if (fp_step(p, b) == EV_FRAME) { fp_deliver(p); frames++; }
    }
    if (p->steps > p->max_steps) p->max_steps = p->steps;
    return frames;
}

/* ======================= 테스트 하네스 ======================= */

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

typedef struct { uint8_t type, len; uint8_t pl[FRAME_MAX_PAYLOAD]; } rx_frame;
#define SINK_MAX 4096
static rx_frame g_rx[SINK_MAX];
static size_t g_rxn;
static void sink_cb(void *ctx, uint8_t type, const uint8_t *pl, uint8_t len) {
    (void)ctx;
    if (g_rxn >= SINK_MAX || len > FRAME_MAX_PAYLOAD) return;
    g_rx[g_rxn].type = type; g_rx[g_rxn].len = len;
    memcpy(g_rx[g_rxn].pl, pl, len);
    g_rxn++;
}
static bool same_frame(const rx_frame *a, const rx_frame *b) {
    return a->type == b->type && a->len == b->len && memcmp(a->pl, b->pl, a->len) == 0;
}
/* 받은 프레임 목록이 기대 목록과 정확히 같은가 */
static bool rx_equals(const rx_frame *exp, size_t n) {
    if (g_rxn != n) return false;
    for (size_t i = 0; i < n; i++) if (!same_frame(&g_rx[i], &exp[i])) return false;
    return true;
}

/* 스트림 조립 도우미 (인코더가 실패하면 아무것도 붙이지 않는다) */
static uint8_t g_s[400000];
static size_t g_sn;
static void put_bytes(const uint8_t *b, size_t n) { if (g_sn + n <= sizeof g_s) { memcpy(g_s + g_sn, b, n); g_sn += n; } }
static rx_frame mk(uint8_t type, uint8_t len, uint8_t start) {   /* PAYLOAD = start, start+1, ... */
    rx_frame f; memset(&f, 0, sizeof f);
    f.type = type; f.len = len;
    for (uint8_t i = 0; i < len; i++) f.pl[i] = (uint8_t)(start + i);
    return f;
}
static int enc(const rx_frame *f, uint8_t *out) { return frame_encode(f->type, f->pl, f->len, out, FRAME_MAX); }
static void put_frame(const rx_frame *f) { uint8_t b[FRAME_MAX]; int n = enc(f, b); if (n > 0) put_bytes(b, (size_t)n); }
static bool has_sof(const uint8_t *b, size_t n) { for (size_t i = 0; i < n; i++) if (b[i] == FRAME_SOF) return true; return false; }

static frame_parser g_p;
static void run(bool resync) {          /* g_s 전체를 새 파서에 한 바이트씩 먹인다 */
    g_rxn = 0;
    fp_init(&g_p, resync, sink_cb, NULL);
    for (size_t i = 0; i < g_sn; i++) fp_feed(&g_p, g_s[i]);
}

int main(void) {
    /* ===== Part A: CRC, 인코더, 정상 파싱 ===== */
    CHECK(crc16((const uint8_t *)"123456789", 9) == 0x29B1);   /* CRC-16/CCITT-FALSE check 값 */
    CHECK(crc16(NULL, 0) == 0xFFFF);

    uint8_t out[FRAME_MAX + 8];
    const uint8_t hi[2] = { 0x48, 0x69 };                       /* "Hi" */
    const uint8_t hi_frame[7] = { 0xA5, 0x02, 0x01, 0x48, 0x69, 0x27, 0x72 };
    CHECK(frame_encode(0x01, hi, 2, out, sizeof out) == 7 && memcmp(out, hi_frame, 7) == 0);
    const uint8_t ping[5] = { 0xA5, 0x00, 0x10, 0x0F, 0x3E };  /* LEN 0, TYPE 0x10 */
    CHECK(frame_encode(0x10, NULL, 0, out, 5) == 5 && memcmp(out, ping, 5) == 0);
    memset(out, 0xEE, sizeof out);
    CHECK(frame_encode(0x01, hi, 2, out, 6) == FE_ERR_CAP && out[0] == 0xEE);   /* 1바이트 모자람 → 안 씀 */
    uint8_t big[FRAME_MAX_PAYLOAD + 1] = { 0 };
    CHECK(frame_encode(0x02, big, 64, out, 69) == 69 && out[1] == 64);
    CHECK(frame_encode(0x02, big, 65, out, sizeof out) == FE_ERR_LEN);
    CHECK(frame_encode(0x02, big, 64, out, 68) == FE_ERR_CAP);

    /* 바이트 하나씩: 마지막 바이트에서만 프레임 1개 */
    {
        g_rxn = 0;
        fp_init(&g_p, false, sink_cb, NULL);
        int r[7];
        for (int i = 0; i < 7; i++) r[i] = fp_feed(&g_p, hi_frame[i]);
        rx_frame e = { 0x01, 2, { 0x48, 0x69 } };
        CHECK(r[0] == 0 && r[5] == 0 && r[6] == 1);
        CHECK(rx_equals(&e, 1) && g_p.stats.frames_ok == 1 && g_p.st == ST_SOF);
    }

    /* LEN 0..64 × TYPE 4종, 이어 붙인 스트림 → 전부 그대로 (기본·재동기화 파서 둘 다) */
    static rx_frame exp[SINK_MAX];
    size_t en = 0;
    g_sn = 0;
    for (unsigned len = 0; len <= FRAME_MAX_PAYLOAD; len++)
        for (unsigned t = 0; t < 4; t++) {
            exp[en] = mk((uint8_t)(t * 0x55u), (uint8_t)len, (uint8_t)(len * 7u + t));
            put_frame(&exp[en]); en++;
        }
    run(false);
    CHECK(rx_equals(exp, en) && g_p.stats.frames_ok == 260 && g_p.stats.crc_err == 0 && g_p.stats.len_err == 0);
    run(true);
    CHECK(rx_equals(exp, en) && g_p.stats.resyncs == 0);

    /* ===== Part B: 오류 주입 (기본 파서: 오류 → 그 바이트까지 버리고 SOF 찾기) ===== */
    rx_frame X, Y, Z, W, E[4];
    uint8_t fb[FRAME_MAX] = { 0 };
    int fn;

    /* B1. 단일 비트 오류 전수: TYPE·PAYLOAD·CRC 의 모든 비트 (13바이트 × 8) → 그 프레임만 버리고 다음은 받음 */
    {
        X = mk(0x40, 10, 0x70); Y = mk(0x41, 3, 0x10);
        int bad = 0;
        for (int byte = 2; byte < 15; byte++)
            for (int bit = 0; bit < 8; bit++) {
                g_sn = 0;
                fn = enc(&X, fb);
                if (fn == 15) fb[byte] ^= (uint8_t)(1u << bit);
                put_bytes(fb, (size_t)(fn > 0 ? fn : 0));
                put_frame(&Y);
                run(false);
                if (!(rx_equals(&Y, 1) && g_p.stats.crc_err == 1)) bad++;
            }
        printf("B1 single-bit flips in TYPE/PAYLOAD/CRC: 104 cases, bad=%d\n", bad);
        CHECK(bad == 0);
    }

    /* B2. LEN > MAX: A5 41 ... 그리고 A5 FF ... → len_err, 뒤 프레임은 받음 */
    {
        const uint8_t junk[] = { 0xA5, 0x41, 0x01, 0x02, 0x03, 0xA5, 0xFF, 0x00, 0x00 };
        Y = mk(0x21, 4, 0x30);
        g_sn = 0; put_bytes(junk, sizeof junk); put_frame(&Y);
        run(false);
        CHECK(rx_equals(&Y, 1) && g_p.stats.len_err == 2 && g_p.stats.crc_err == 0);
    }

    /* B3. 잘린 프레임 뒤에 다음 프레임: X 의 앞 8바이트만 → Y, Z, W */
    X = mk(0x20, 20, 0x00); Y = mk(0x21, 4, 0x30); Z = mk(0x22, 10, 0x40); W = mk(0x23, 3, 0x50);
    g_sn = 0;
    fn = enc(&X, fb); put_bytes(fb, 8);
    put_frame(&Y);
    {   size_t z0 = g_sn; put_frame(&Z);
        CHECK(g_sn == z0 + 15 && !has_sof(g_s + z0 + 8, 7));       /* 전제: Z 의 뒤 7바이트에 0xA5 없음 */
    }
    put_frame(&W);
    run(false);
    CHECK(rx_equals(&W, 1) && g_p.stats.crc_err == 1);          /* 기본 파서: X 가 Y 전체 + Z 앞부분을 삼킴 → Y, Z 손실 */

    /* B4a. 스트림 중간에서 시작 (꼬리에 0xA5 없음) → 다음 프레임부터 정상 */
    X = mk(0x30, 12, 0x60);
    g_sn = 0;
    fn = enc(&X, fb);
    CHECK(fn == 17 && !has_sof(fb + 5, 12));
    put_bytes(fb + 5, 12); put_frame(&Y); put_frame(&Z);
    run(false);
    E[0] = Y; E[1] = Z;
    CHECK(rx_equals(E, 2) && g_p.stats.crc_err == 0 && g_p.stats.len_err == 0);

    /* B4b. 중간 시작, 꼬리 PAYLOAD 안에 A5 08 → 가짜 SOF, 가짜 LEN 8 이 Y 앞 2바이트까지 삼킴 */
    X = mk(0x30, 12, 0x60); X.pl[3] = 0xA5; X.pl[4] = 0x08;
    g_sn = 0;
    fn = enc(&X, fb);
    put_bytes(fb + 5, 12);                                       /* PAYLOAD[2] 부터 시작 */
    {   size_t y0 = g_sn; put_frame(&Y);
        CHECK(!has_sof(g_s + y0 + 2, 7) && !has_sof(fb + 8, 9));
    }
    put_frame(&Z); put_frame(&W);
    run(false);
    E[0] = Z; E[1] = W;
    CHECK(rx_equals(E, 2) && g_p.stats.crc_err == 1);           /* Y 손실 */

    /* B5. SOF 값이 PAYLOAD·CRC 안에: 동기가 맞으면 그냥 데이터다 (바이트 스터핑 불필요) */
    {
        rx_frame S = mk(0x50, 64, 0); memset(S.pl, 0xA5, 64);
        rx_frame Ch = mk(0, 1, 0), Cl = mk(0, 1, 0);
        bool fh = false, fl = false;
        for (unsigned v = 0; v < 65536u && !(fh && fl); v++) {
            uint8_t t = (uint8_t)(v >> 8), d = (uint8_t)v;
            uint8_t raw3[3] = { 1, t, d };
            uint16_t c = crc16(raw3, 3);
            if (!fh && (c >> 8) == 0xA5) { Ch.type = t; Ch.pl[0] = d; fh = true; }
            if (!fl && (c & 0xFF) == 0xA5) { Cl.type = t; Cl.pl[0] = d; fl = true; }
        }
        CHECK(fh && fl);
        g_sn = 0; put_frame(&S); put_frame(&Ch); put_frame(&Cl);
        printf("B5 CRC with 0xA5: hi-frame type=0x%02X data=0x%02X, lo-frame type=0x%02X data=0x%02X\n",
               Ch.type, Ch.pl[0], Cl.type, Cl.pl[0]);
        E[0] = S; E[1] = Ch; E[2] = Cl;
        run(false);
        CHECK(rx_equals(E, 3) && g_p.stats.crc_err == 0 && g_p.stats.len_err == 0);
        run(true);
        CHECK(rx_equals(E, 3) && g_p.stats.resyncs == 0);
    }

    /* B6. 쓰레기 0xA5 바로 뒤에 진짜 프레임: A5 | A5 04 21 ... → 진짜 SOF 를 LEN(0xA5=165)으로 읽음 */
    g_sn = 0;
    { const uint8_t a5 = 0xA5; put_bytes(&a5, 1); }
    put_frame(&Y); put_frame(&Z);
    run(false);
    CHECK(rx_equals(&Z, 1) && g_p.stats.len_err == 1);          /* Y 손실 */

    /* ===== Part C: 재동기화 — 같은 시나리오에서 손실 0 ===== */
    /* C1 = B3 (잘림) */
    X = mk(0x20, 20, 0x00);
    g_sn = 0; fn = enc(&X, fb); put_bytes(fb, 8); put_frame(&Y); put_frame(&Z); put_frame(&W);
    run(true);
    E[0] = Y; E[1] = Z; E[2] = W;
    CHECK(rx_equals(E, 3) && g_p.stats.crc_err == 1 && g_p.stats.resyncs == 1);

    /* C2 = B4b (중간 시작 + 가짜 SOF) */
    X = mk(0x30, 12, 0x60); X.pl[3] = 0xA5; X.pl[4] = 0x08;
    g_sn = 0; fn = enc(&X, fb); put_bytes(fb + 5, 12); put_frame(&Y); put_frame(&Z); put_frame(&W);
    run(true);
    E[0] = Y; E[1] = Z; E[2] = W;
    CHECK(rx_equals(E, 3) && g_p.stats.crc_err == 1);

    /* C3 = B6 (A5 A5) 와 B2 (LEN > MAX) */
    g_sn = 0;
    { const uint8_t a5 = 0xA5; put_bytes(&a5, 1); }
    put_frame(&Y); put_frame(&Z);
    run(true);
    E[0] = Y; E[1] = Z;
    CHECK(rx_equals(E, 2) && g_p.stats.len_err == 1 && g_p.stats.resyncs == 1);
    {
        const uint8_t junk[] = { 0xA5, 0x41, 0x01, 0x02, 0x03, 0xA5, 0xFF, 0x00, 0x00 };
        g_sn = 0; put_bytes(junk, sizeof junk); put_frame(&Y);
        run(true);
        CHECK(rx_equals(&Y, 1) && g_p.stats.len_err == 2);
    }

    /* C4. 랜덤 쓰레기 + 프레임 3000개 (10% 는 1비트 손상) + 꼬리 0x00 × 68.
     *     정상 프레임은 전부 순서대로, 손상·가짜 프레임은 하나도 안 나와야 한다. */
    {
        uint32_t seed = 0xC0FFEE01u;
        size_t ngood = 0, ncorrupt = 0, garbage = 0;
        g_sn = 0;
        for (int k = 0; k < 3000; k++) {
            unsigned gap = xs32(&seed) % 25u;
            for (unsigned g = 0; g < gap; g++) {
                uint32_t r = xs32(&seed);
                uint8_t gb = (r % 4u == 0) ? (uint8_t)FRAME_SOF : (uint8_t)(r >> 8);   /* 쓰레기의 1/4 이상이 0xA5 */
                put_bytes(&gb, 1); garbage++;
            }
            rx_frame f; memset(&f, 0, sizeof f);
            f.type = (uint8_t)xs32(&seed);
            f.len = (uint8_t)(xs32(&seed) % (FRAME_MAX_PAYLOAD + 1u));
            for (unsigned i = 0; i < f.len; i++) f.pl[i] = (uint8_t)xs32(&seed);
            fn = enc(&f, fb);
            if (fn <= 0) continue;
            if (xs32(&seed) % 10u == 0) {                        /* 프레임 어디든(SOF·LEN 포함) 1비트 뒤집기 */
                uint32_t r = xs32(&seed);
                fb[r % (uint32_t)fn] ^= (uint8_t)(1u << ((r >> 16) % 8u));
                ncorrupt++;
            } else {
                exp[ngood++] = f;
            }
            put_bytes(fb, (size_t)fn);
        }
        uint8_t zeros[FP_RAW_MAX] = { 0 };
        put_bytes(zeros, sizeof zeros);                          /* 마지막 가짜 프레임을 끝내 줄 꼬리 */

        run(false);
        size_t naive_got = 0, j = 0;                             /* 받은 프레임마다 기대 목록에서 앞으로 찾기 */
        for (size_t i = 0; i < g_rxn; i++)
            for (size_t k = j; k < ngood; k++)
                if (same_frame(&g_rx[i], &exp[k])) { naive_got++; j = k + 1; break; }
        fp_stats ns = g_p.stats;

        run(true);
        printf("C4 stream: %zu bytes, %zu good + %zu corrupted frames, %zu garbage bytes\n",
               g_sn, ngood, ncorrupt, garbage);
        printf("   naive : recovered %zu/%zu good (lost %zu)  frames_ok=%u crc_err=%u len_err=%u\n",
               naive_got, ngood, ngood - naive_got, ns.frames_ok, ns.crc_err, ns.len_err);
        printf("   resync: recovered %zu/%zu good             frames_ok=%u crc_err=%u len_err=%u resyncs=%u max_steps/byte=%u\n",
               g_rxn, ngood, g_p.stats.frames_ok, g_p.stats.crc_err, g_p.stats.len_err,
               g_p.stats.resyncs, g_p.max_steps);
        CHECK(ngood > 2500 && rx_equals(exp, ngood));
        CHECK(g_p.stats.frames_ok == ngood);
        CHECK(naive_got < ngood);                                /* 기본 파서는 손실이 있다 */
        CHECK(g_p.max_steps <= 1u + FP_RAW_MAX * (FP_RAW_MAX + 1u) / 2u);   /* 최악 한계 2347 */
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
