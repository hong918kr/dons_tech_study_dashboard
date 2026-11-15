/* COMM-B12 COBS 인코더/디코더 + 랜덤 왕복 테스트
 *
 * Part A: cobs_encode (코드 바이트 자리 예약 → 나중에 채우기, 한 번 훑기) / cobs_decode
 * Part B: cobs_max_encoded (최악 길이 n + max(1, ceil(n/254))), 254/255/508 경계, 손상 입력 거부
 *         (0x00 이 안에 있음, 코드 바이트가 끝을 넘어감, 빈 입력), 출력 용량 부족
 * Part C: 펌웨어 링크: frame_build = COBS(payload ‖ CRC-16/CCITT-FALSE, 상위 바이트 먼저) ‖ 0x00,
 *         UART RX ISR 가 한 바이트씩 넣는 스트리밍 디코더 cobs_rx_feed (정적 버퍼, 통계 카운터)
 *
 * 테스트: Wikipedia/Cheshire-Baker 예제 벡터 + 다른 방식(앞을 내다보는 두 단계)으로 짠 기준 인코더와
 *         바이트 단위 비교, 길이 0~1000 랜덤 왕복(xorshift32 고정 시드), 0 하나 위치 전수(n ≤ 520),
 *         고장 주입(빠진 바이트, 깨진 바이트, 쓰레기, 너무 긴 프레임) 스트림.
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c && ./a.out
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum { COBS_ERR_OVERFLOW = -1, COBS_ERR_ZERO = -2, COBS_ERR_TRUNC = -3, COBS_ERR_EMPTY = -4 };

/* ---------------- Part A: 인코드 / 디코드 ---------------- */

/* src[0..n) → dst (구분자 0x00 은 붙이지 않는다). 반환: 인코딩 길이, 또는 COBS_ERR_OVERFLOW.
 * 블록 = [코드 c][0 아닌 바이트 c-1 개]. c < 0xFF 이면 블록 뒤에 0 이 하나 있었다는 뜻,
 * c == 0xFF 는 "0 없이 254 바이트". 데이터 끝에는 가상의 0 이 있다고 보고 마지막에 버린다. */
static int cobs_encode(const uint8_t *src, size_t n, uint8_t *dst, size_t cap) {
    (void)src; (void)n; (void)dst; (void)cap;
    return 0; /* TODO: 코드 자리 예약 → 0 이나 254 바이트에서 채우기. 용량 검사, 254 로 딱 끝나면 새 블록 없음 */
}

/* src[0..n) (구분자 제외) → dst. 반환: 디코딩 길이, 또는 음수 오류.
 * 쓰는 위치가 항상 읽는 위치보다 뒤에 있으므로 dst == src (제자리 디코딩) 도 안전하다. */
static int cobs_decode(const uint8_t *src, size_t n, uint8_t *dst, size_t cap) {
    (void)src; (void)n; (void)dst; (void)cap;
    return 0; /* TODO: 코드 c → 데이터 c-1 개 복사 → c < 0xFF 이고 끝이 아니면 0. Part B: 빈 입력/0/끝 너머/용량 */
}

/* ---------------- Part B: 최악 길이 ---------------- */

/* n 바이트를 인코딩한 최대 길이 (구분자 제외): n + max(1, ceil(n/254)) */
static size_t cobs_max_encoded(size_t n) {
    return n; /* TODO: n + max(1, ceil(n/254)) */
}

/* ---------------- Part C: 프레임 송신 + 스트리밍 수신 ---------------- */

/* CRC-16/CCITT-FALSE (= CRC-16/IBM-3740): poly 0x1021, init 0xFFFF, 반사 없음, xorout 0. check = 0x29B1
 * (원리는 ALGO-M01, 검출 능력은 COMM-B08) */
static uint16_t crc16_ccitt_false(const uint8_t *p, size_t n) {
    uint16_t c = 0xFFFF;
    for (size_t i = 0; i < n; i++) {
        c ^= (uint16_t)(p[i] << 8);
        for (int b = 0; b < 8; b++) c = (c & 0x8000) ? (uint16_t)((c << 1) ^ 0x1021) : (uint16_t)(c << 1);
    }
    return c;
}

#define TX_PAYLOAD_MAX 254u
#define RX_PAYLOAD_MAX 64u
#define RX_MAX (RX_PAYLOAD_MAX + 2u)            /* 디코딩 후: payload + CRC 2 바이트 */

/* out = COBS(payload ‖ crc_hi ‖ crc_lo) ‖ 0x00. 반환: 프레임 길이(구분자 포함) 또는 COBS_ERR_OVERFLOW */
static int frame_build(const uint8_t *payload, size_t n, uint8_t *out, size_t cap) {
    (void)payload; (void)n; (void)out; (void)cap;
    return 0; /* TODO: payload ‖ CRC(상위 먼저) → cobs_encode → 0x00 구분자. 길이·용량 검사 */
}

enum { RX_NONE = 0, RX_FRAME = 1, RX_ERR_COBS = -1, RX_ERR_CRC = -2, RX_ERR_OVERFLOW = -3, RX_ERR_SHORT = -4 };
typedef struct { uint32_t frames_ok, cobs_err, crc_err, overflow, short_err, empty; } rx_stats;
typedef struct {
    uint8_t buf[RX_MAX];   /* 디코딩된 바이트 (payload + CRC) */
    size_t len;            /* 이번 프레임에서 디코딩된 바이트 수 */
    size_t frame_len;      /* RX_FRAME 을 돌려줄 때 payload 길이 (buf[0..frame_len)) */
    uint8_t remain;        /* 현재 블록에서 남은 데이터 바이트 수. 0 = 다음 바이트는 코드 바이트 */
    bool zero_pending;     /* 직전 블록 코드 < 0xFF → 다음 코드 바이트가 오면 그때 0 을 넣는다 */
    bool started;          /* 직전 구분자 뒤로 0 아닌 바이트가 왔나 (아니면 빈 프레임) */
    bool overflow;         /* 이번 프레임은 구분자까지 버린다 */
    rx_stats st;
} cobs_rx;

static void rx_put(cobs_rx *rx, uint8_t b) {
    (void)rx; (void)b; /* TODO: 넘치면 overflow 플래그, 아니면 buf 에 추가 */
}

/* UART RX ISR 에서 바이트마다 부른다. 0x00 에서 프레임을 판정하고 상태를 초기화(재동기화)한다. */
static int cobs_rx_feed(cobs_rx *rx, uint8_t b) {
    (void)rx; (void)b; (void)rx_put; (void)crc16_ccitt_false;
    return RX_NONE; /* TODO: 0x00 → 판정(빈/넘침/블록 중간/짧음/CRC/OK) 후 초기화. 아니면 코드 바이트/데이터 바이트 처리 */
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

/* 기준 인코더: 되돌아가 채우지 않고, 다음 0 까지(최대 254) 앞을 먼저 세는 두 단계 방식 */
static size_t ref_encode(const uint8_t *s, size_t n, uint8_t *d) {
    size_t o = 0, i = 0;
    for (;;) {
        size_t run = 0;
        while (i + run < n && s[i + run] != 0 && run < 254) run++;
        d[o++] = (uint8_t)(run + 1);
        memcpy(d + o, s + i, run); o += run; i += run;
        if (i == n) break;                     /* 마지막 블록 (FF 이든 아니든 뒤에 블록 없음) */
        if (run < 254) i++;                    /* 코드 < FF: 0 하나를 소비 */
    }
    return o;
}

/* 0 비율을 바꿔 가며 랜덤 버퍼 채우기: mode 0 균등, 1 0이 절반, 2 0 없음, 3 전부 0 */
static void fill(uint8_t *p, size_t n, int mode, uint32_t *seed) {
    for (size_t i = 0; i < n; i++) {
        uint32_t r = xs32(seed);
        if (mode == 0) p[i] = (uint8_t)r;
        else if (mode == 1) p[i] = (r & 0x100) ? 0 : (uint8_t)(r | 1);
        else if (mode == 2) p[i] = (uint8_t)(1 + r % 255u);
        else p[i] = 0;
    }
}

static bool has_zero(const uint8_t *p, size_t n) { for (size_t i = 0; i < n; i++) if (!p[i]) return true; return false; }

/* 인코딩 → (기준과 같은가, 0 없음, 길이 한계) → 디코딩 왕복 */
static bool roundtrip_ok(const uint8_t *src, size_t n) {
    static uint8_t enc[1200], ref[1200], dec[1100];
    int e = cobs_encode(src, n, enc, sizeof enc);
    if (e < 0 || (size_t)e > cobs_max_encoded(n)) return false;
    size_t r = ref_encode(src, n, ref);
    if ((size_t)e != r || memcmp(enc, ref, r) != 0 || has_zero(enc, (size_t)e)) return false;
    int d = cobs_decode(enc, (size_t)e, dec, sizeof dec);
    return d == (int)n && (n == 0 || memcmp(dec, src, n) == 0);
}

static bool enc_is(const uint8_t *src, size_t n, const uint8_t *want, size_t wn) {
    uint8_t out[600];
    int e = cobs_encode(src, n, out, sizeof out);
    return e == (int)wn && memcmp(out, want, wn) == 0;
}

/* 랜덤 스트림용: 모든 바이트를 ISR 처럼 넣고, 나온 프레임을 보낸 순서와 비교 */
static cobs_rx g_rx;
static int g_nrecv, g_mism, *g_nsent;
static uint8_t (*g_sent)[RX_PAYLOAD_MAX];
static size_t *g_sent_len;
static void feed_check(uint8_t byte) {
    if (cobs_rx_feed(&g_rx, byte) != RX_FRAME) return;
    if (g_nrecv >= *g_nsent || g_rx.frame_len != g_sent_len[g_nrecv]
        || memcmp(g_rx.buf, g_sent[g_nrecv], g_rx.frame_len) != 0) g_mism++;
    g_nrecv++;
}

int main(void) {
    uint32_t seed = 0xC0B5u;
    static uint8_t a[1100], b[1200], c[1200];

    /* ===== Part A: 예제 벡터 (Wikipedia "Consistent Overhead Byte Stuffing" 예 1~11, 구분자 제외) ===== */
    CHECK(enc_is(NULL, 0, (const uint8_t[]){0x01}, 1));
    CHECK(enc_is((const uint8_t[]){0x00}, 1, (const uint8_t[]){0x01, 0x01}, 2));
    CHECK(enc_is((const uint8_t[]){0x00, 0x00}, 2, (const uint8_t[]){0x01, 0x01, 0x01}, 3));
    CHECK(enc_is((const uint8_t[]){0x00, 0x11, 0x00}, 3, (const uint8_t[]){0x01, 0x02, 0x11, 0x01}, 4));
    CHECK(enc_is((const uint8_t[]){0x11, 0x22, 0x00, 0x33}, 4, (const uint8_t[]){0x03, 0x11, 0x22, 0x02, 0x33}, 5));
    CHECK(enc_is((const uint8_t[]){0x11, 0x22, 0x33, 0x44}, 4, (const uint8_t[]){0x05, 0x11, 0x22, 0x33, 0x44}, 5));
    CHECK(enc_is((const uint8_t[]){0x11, 0x00, 0x00, 0x00}, 4, (const uint8_t[]){0x02, 0x11, 0x01, 0x01, 0x01}, 5));
    for (int i = 0; i < 254; i++) a[i] = (uint8_t)(i + 1);              /* 01..FE */
    b[0] = 0xFF; memcpy(b + 1, a, 254);
    CHECK(enc_is(a, 254, b, 255));                                      /* 예 7: FF 01..FE */
    c[0] = 0x00; memcpy(c + 1, a, 254);                                 /* 00 01..FE */
    b[0] = 0x01; b[1] = 0xFF; memcpy(b + 2, a, 254);
    CHECK(enc_is(c, 255, b, 256));                                      /* 예 8: 01 FF 01..FE */
    for (int i = 0; i < 255; i++) c[i] = (uint8_t)(i + 1);              /* 01..FF */
    b[0] = 0xFF; memcpy(b + 1, c, 254); b[255] = 0x02; b[256] = 0xFF;
    CHECK(enc_is(c, 255, b, 257));                                      /* 예 9: FF 01..FE 02 FF */
    for (int i = 0; i < 254; i++) c[i] = (uint8_t)(i + 2); c[254] = 0;  /* 02..FF 00 */
    b[0] = 0xFF; memcpy(b + 1, c, 254); b[255] = 0x01; b[256] = 0x01;
    CHECK(enc_is(c, 255, b, 257));                                      /* 예 10: FF 02..FF 01 01 */
    for (int i = 0; i < 253; i++) c[i] = (uint8_t)(i + 3); c[253] = 0; c[254] = 1;   /* 03..FF 00 01 */
    b[0] = 0xFE; memcpy(b + 1, c, 253); b[254] = 0x02; b[255] = 0x01;
    CHECK(enc_is(c, 255, b, 256));                                      /* 예 11: FE 03..FF 02 01 */
    {   uint8_t dd[8];
        CHECK(cobs_decode((const uint8_t[]){0x03, 0x11, 0x22, 0x02, 0x33}, 5, dd, sizeof dd) == 4
              && memcmp(dd, (const uint8_t[]){0x11, 0x22, 0x00, 0x33}, 4) == 0);
        CHECK(cobs_decode((const uint8_t[]){0x01}, 1, dd, sizeof dd) == 0);      /* 빈 데이터 */
        CHECK(cobs_decode((const uint8_t[]){0x01, 0x01, 0x01}, 3, dd, sizeof dd) == 2 && dd[0] == 0 && dd[1] == 0);
    }

    /* ===== Part A: 랜덤 왕복, 길이 0~1000, 0 비율 4가지 ===== */
    int bad = 0;
    for (int t = 0; t < 4000; t++) {
        size_t n = xs32(&seed) % 1001u;
        fill(a, n, t & 3, &seed);
        if (!roundtrip_ok(a, n)) bad++;
    }
    printf("random round trip: 4000 buffers (len 0..1000, 4 zero densities), bad=%d\n", bad);
    CHECK(bad == 0);

    /* ===== Part B: 최악 길이 공식 ===== */
    CHECK(cobs_max_encoded(0) == 1);
    CHECK(cobs_max_encoded(1) == 2);
    CHECK(cobs_max_encoded(253) == 254);
    CHECK(cobs_max_encoded(254) == 255);
    CHECK(cobs_max_encoded(255) == 257);
    CHECK(cobs_max_encoded(508) == 510);
    CHECK(cobs_max_encoded(509) == 512);
    CHECK(cobs_max_encoded(1000) == 1004);

    /* 0 없는 데이터 = 최악: 정확히 한계에 닿는다. 경계 길이의 첫 코드 바이트까지 확인 */
    {   static const size_t L[] = {1, 253, 254, 255, 508, 509, 762, 1000};
        int badb = 0;
        for (size_t k = 0; k < sizeof L / sizeof L[0]; k++) {
            fill(a, L[k], 2, &seed);
            int e = cobs_encode(a, L[k], b, sizeof b);
            if (e != (int)cobs_max_encoded(L[k]) || !roundtrip_ok(a, L[k])) badb++;
            if (b[0] != (L[k] >= 254 ? 0xFF : (uint8_t)(L[k] + 1))) badb++;
            if (e >= 1 && cobs_encode(a, L[k], c, (size_t)e - 1) != COBS_ERR_OVERFLOW) badb++;   /* 1 바이트 모자람 */
        }
        CHECK(badb == 0);
    }
    fill(a, 508, 2, &seed);
    CHECK(cobs_encode(a, 508, b, sizeof b) == 510 && b[0] == 0xFF && b[255] == 0xFF);   /* FF[254] FF[254] */

    /* n ≤ 254 이면 내용과 무관하게 정확히 n + 1 */
    int badn = 0;
    for (int t = 0; t < 3000; t++) {
        size_t n = xs32(&seed) % 255u;
        fill(a, n, t & 3, &seed);
        if (cobs_encode(a, n, b, sizeof b) != (int)n + 1) badn++;
    }
    CHECK(badn == 0);

    /* 0 하나의 위치 전수: n = 1..520, 0 위치 0..n-1 (나머지는 0 아님) — 254/255/508 경계를 모두 지난다 */
    int bad1 = 0; long cnt1 = 0;
    for (size_t n = 1; n <= 520; n++) {
        fill(a, n, 2, &seed);
        for (size_t z = 0; z < n; z++) {
            uint8_t keep = a[z]; a[z] = 0;
            if (!roundtrip_ok(a, n)) bad1++;
            a[z] = keep; cnt1++;
        }
    }
    printf("single-zero placements: %ld cases (n=1..520), bad=%d\n", cnt1, bad1);
    CHECK(bad1 == 0);

    /* 전부 0: 0 하나가 코드 01 하나로 → 길이 n + 1, 모든 바이트 01 */
    int badz = 0;
    for (size_t n = 0; n <= 1000; n += 37) {
        memset(a, 0, n);
        int e = cobs_encode(a, n, b, sizeof b);
        if (e != (int)n + 1) badz++;
        for (int i = 0; i < e; i++) if (b[i] != 0x01) badz++;
        if (!roundtrip_ok(a, n)) badz++;
    }
    CHECK(badz == 0);

    /* 손상 입력 거부 */
    {   uint8_t dd[300];
        CHECK(cobs_decode(NULL, 0, dd, sizeof dd) == COBS_ERR_EMPTY);
        CHECK(cobs_decode((const uint8_t[]){0x00}, 1, dd, sizeof dd) == COBS_ERR_ZERO);
        CHECK(cobs_decode((const uint8_t[]){0x03, 0x11, 0x00}, 3, dd, sizeof dd) == COBS_ERR_ZERO);        /* 블록 안 0 */
        CHECK(cobs_decode((const uint8_t[]){0x02, 0x11, 0x00, 0x01}, 4, dd, sizeof dd) == COBS_ERR_ZERO);  /* 코드 자리 0 */
        CHECK(cobs_decode((const uint8_t[]){0x05, 0x11, 0x22}, 3, dd, sizeof dd) == COBS_ERR_TRUNC);       /* 끝 너머 */
        CHECK(cobs_decode((const uint8_t[]){0x03, 0x11, 0x22, 0x02}, 4, dd, sizeof dd) == COBS_ERR_TRUNC);
        memset(c, 0x5A, 254); c[0] = 0xFF;
        CHECK(cobs_decode(c, 254, dd, sizeof dd) == COBS_ERR_TRUNC);                                       /* FF 뒤 253 바이트뿐 */
        CHECK(cobs_decode(c, 255, dd, 253) == COBS_ERR_OVERFLOW);                                          /* 254 를 253 칸에 */
        CHECK(cobs_decode(c, 255, dd, 254) == 254);
        CHECK(cobs_decode((const uint8_t[]){0x02, 0x11, 0x02, 0x22}, 4, dd, 2) == COBS_ERR_OVERFLOW);     /* 11 00 22 → 3 */
        CHECK(cobs_encode((const uint8_t[]){0x11}, 1, dd, 0) == COBS_ERR_OVERFLOW);
        CHECK(cobs_encode((const uint8_t[]){0x11, 0x00}, 2, dd, 2) == COBS_ERR_OVERFLOW);                 /* 02 11 01 = 3 */
    }

    /* 랜덤 바이트를 디코더에 넣기 (퍼징 맛보기): 죽지 않고, 받아들인 것은 다시 인코딩해도 같은 데이터 */
    int accepted = 0, badf = 0;
    for (int t = 0; t < 20000; t++) {
        size_t n = 1 + xs32(&seed) % 300u;
        fill(b, n, (t & 1) ? 0 : 2, &seed);
        int d = cobs_decode(b, n, a, 300);
        if (d >= 0) { accepted++; if (!roundtrip_ok(a, (size_t)d)) badf++; }
        else if (d != COBS_ERR_ZERO && d != COBS_ERR_TRUNC && d != COBS_ERR_OVERFLOW) badf++;
    }
    printf("decoder fuzz: 20000 random inputs, accepted=%d, bad=%d\n", accepted, badf);
    CHECK(badf == 0);

    /* ===== Part C: CRC 기준값과 프레임 바이트 ===== */
    CHECK(crc16_ccitt_false((const uint8_t *)"123456789", 9) == 0x29B1);
    {   uint8_t f[32];
        static const uint8_t want[] = {0x0C, '1', '2', '3', '4', '5', '6', '7', '8', '9', 0x29, 0xB1, 0x00};
        CHECK(frame_build((const uint8_t *)"123456789", 9, f, sizeof f) == 13 && memcmp(f, want, 13) == 0);
        CHECK(frame_build((const uint8_t *)"123456789", 9, f, 12) == COBS_ERR_OVERFLOW);   /* 구분자 자리 없음 */
        CHECK(frame_build(NULL, 0, f, sizeof f) == 4 && f[0] == 0x03 && f[1] == 0xFF && f[2] == 0xFF && f[3] == 0);
        CHECK(frame_build(a, TX_PAYLOAD_MAX + 1, b, sizeof b) == COBS_ERR_OVERFLOW);
    }

    /* ===== Part C: 이름 붙인 고장 시나리오 — 한 바이트씩 ISR 처럼 넣는다 ===== */
    {
        static cobs_rx rx;
        memset(&rx, 0, sizeof rx);
        uint8_t st[2048]; size_t sn = 0; uint8_t f[300]; int fl;
#define PUSH(buf, len) do { if ((len) > 0) { memcpy(st + sn, (buf), (size_t)(len)); sn += (size_t)(len); } } while (0)
        st[sn++] = 0; st[sn++] = 0; st[sn++] = 0;                                     /* 빈 프레임 3개 */
        fl = frame_build((const uint8_t *)"hello", 5, f, sizeof f); PUSH(f, fl);
        static const uint8_t z4[] = {0x00, 0x00, 0x01, 0x00};
        fl = frame_build(z4, 4, f, sizeof f); PUSH(f, fl);
        fl = frame_build((const uint8_t *)"drop-me", 7, f, sizeof f);                 /* 한 바이트 빠짐 */
        if (fl >= 5) { memcpy(st + sn, f, 3); memcpy(st + sn + 3, f + 4, (size_t)fl - 4); sn += (size_t)fl - 1; }
        fl = frame_build((const uint8_t *)"flip-me", 7, f, sizeof f);                 /* 데이터 바이트 깨짐 */
        f[3] ^= 0x20; PUSH(f, fl);
        memset(a, 0x77, 100);
        fl = frame_build(a, 100, f, sizeof f); PUSH(f, fl);   /* 너무 김 */
        st[sn++] = 0xAB; st[sn++] = 0xCD; st[sn++] = 0xEF; st[sn++] = 0x00;           /* 쓰레기 */
        st[sn++] = 0x01; st[sn++] = 0x00;                                             /* 디코딩 0 바이트 */
        fl = frame_build(NULL, 0, f, sizeof f); PUSH(f, fl);  /* 빈 payload */
        fill(a, 64, 1, &seed);
        fl = frame_build(a, 64, f, sizeof f); PUSH(f, fl);    /* 최대 64 */
        fill(c, 65, 0, &seed);
        fl = frame_build(c, 65, f, sizeof f); PUSH(f, fl);    /* 65: 넘침 */

        int got = 0; size_t lens[16]; uint8_t first[16]; int evs[32]; int nev = 0;
        for (size_t i = 0; i < sn; i++) {
            int ev = cobs_rx_feed(&rx, st[i]);
            if (ev != RX_NONE && nev < 32) evs[nev++] = ev;
            if (ev == RX_FRAME && got < 16) { lens[got] = rx.frame_len; first[got] = rx.frame_len ? rx.buf[0] : 0; got++; }
        }
        printf("scenario: ok=%u cobs=%u crc=%u overflow=%u short=%u empty=%u  events:",
               rx.st.frames_ok, rx.st.cobs_err, rx.st.crc_err, rx.st.overflow, rx.st.short_err, rx.st.empty);
        for (int i = 0; i < nev; i++) printf(" %d", evs[i]);
        printf("\n");
        CHECK(got == 4 && lens[0] == 5 && first[0] == 'h' && lens[1] == 4 && first[1] == 0x00);
        CHECK(got == 4 && lens[2] == 0 && lens[3] == 64 && first[3] == a[0]);
        CHECK(rx.st.frames_ok == 4 && rx.st.overflow == 2 && rx.st.short_err == 1 && rx.st.empty == 3);
        CHECK(rx.st.crc_err + rx.st.cobs_err == 3 && rx.st.crc_err >= 1);   /* drop-me, flip-me, 쓰레기 */
        CHECK(nev == 10 && evs[2] == RX_ERR_COBS && evs[3] == RX_ERR_CRC && evs[4] == RX_ERR_OVERFLOW);
    }

    /* ===== Part C: 랜덤 고장 스트림 — 멀쩡한 프레임은 하나도 빠짐없이, 순서대로 ===== */
    {
        static uint8_t sent[3000][RX_PAYLOAD_MAX]; static size_t sent_len[3000];
        memset(&g_rx, 0, sizeof g_rx);
        int nsent = 0, damaged = 0, rescued = 0; bool open_tail = false;
        g_nrecv = 0; g_mism = 0; g_sent = sent; g_sent_len = sent_len; g_nsent = &nsent;
        for (int t = 0; t < 3000; t++) {
            uint8_t pl[RX_PAYLOAD_MAX], f[300], g[24];
            size_t n = xs32(&seed) % (RX_PAYLOAD_MAX + 1);
            fill(pl, n, (int)(xs32(&seed) % 3u), &seed);
            int fl = frame_build(pl, n, f, sizeof f);
            uint32_t r = xs32(&seed) % 10u;
            if (fl <= 0) { fl = 0; r = 9; }                 /* (시작 코드의 스텁이 0 을 돌려줄 때 멈추지 않게) */
            if (open_tail && r == 0) r = 9;                 /* 끝 0x00 을 잃은 프레임 뒤에 쓰레기가 끼면 그 프레임도 잃는다 — 구제 경우만 본다 */
            open_tail = false;
            int ok = 1;
            if (r == 0) {                                   /* 앞에 쓰레기 (0 포함 가능) */
                size_t gn = 1 + xs32(&seed) % 20u; fill(g, gn, 0, &seed);
                for (size_t i = 0; i < gn; i++) feed_check(g[i]);
            } else if (r == 1) {                            /* 바이트 하나 빠짐 */
                size_t k = xs32(&seed) % (size_t)fl;
                memmove(f + k, f + k + 1, (size_t)fl - k - 1); fl--;
                if (k == (size_t)fl) { rescued++; open_tail = true; } else ok = 0; /* 끝 0x00 이 빠지면 다음 앞 구분자가 살린다 */
            } else if (r == 2) {                            /* 바이트 하나 깨짐 (0x00 으로 바뀔 수도) */
                size_t k = xs32(&seed) % (size_t)fl;
                f[k] ^= (uint8_t)(1 + xs32(&seed) % 255u); ok = 0;
            }
            if (ok) { memcpy(sent[nsent], pl, n); sent_len[nsent] = n; nsent++; } else damaged++;
            feed_check(0x00);                               /* 송신 측 앞 구분자: 앞의 쓰레기/잘린 프레임을 끊는다 */
            for (int i = 0; i < fl; i++) feed_check(f[i]);
        }
        feed_check(0x00);
        printf("fault stream: sent ok=%d (delimiter-lost rescued %d) damaged=%d recv=%d mismatch=%d\n"
               "              stats ok=%u cobs=%u crc=%u ovf=%u short=%u empty=%u\n",
               nsent, rescued, damaged, g_nrecv, g_mism, g_rx.st.frames_ok, g_rx.st.cobs_err, g_rx.st.crc_err,
               g_rx.st.overflow, g_rx.st.short_err, g_rx.st.empty);
        CHECK(g_mism == 0 && g_nrecv == nsent && (int)g_rx.st.frames_ok == nsent);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
