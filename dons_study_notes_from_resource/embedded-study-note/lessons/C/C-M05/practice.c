/* C-M05 추가 연습 — 한 번에 검증
 *   연습 1: 가변 폭 get_uint_le / put_uint_be (24·40·48비트 필드)
 *   연습 2: float → 스케일된 int16 (반올림 · 포화 · NaN 센티널)
 *   연습 3: 바이트 스트림에서 프레임 재동기화(resync) 스캐너
 *   연습 4: 앞으로 호환(forward-compatible) 디코더 — 더 긴 페이로드 허용
 *   연습 5: double 을 빅 엔디안으로 (get/put_be_f64)
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

_Static_assert(sizeof(float) == 4 && sizeof(double) == 8, "IEEE 754 binary32/64 가정");

/* ---- 본문 헬퍼 (solution.c 와 같음) ---- */
static inline uint16_t get_le16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static inline uint16_t get_be16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static inline uint32_t get_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static inline uint32_t get_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}
static inline uint64_t get_le64(const uint8_t *p) { return (uint64_t)get_le32(p) | ((uint64_t)get_le32(p + 4) << 32); }
static inline uint64_t get_be64(const uint8_t *p) { return ((uint64_t)get_be32(p) << 32) | (uint64_t)get_be32(p + 4); }
static inline void put_le16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static inline void put_be16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static inline void put_le32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
static inline void put_be32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}
static inline void put_le64(uint8_t *p, uint64_t v) { put_le32(p, (uint32_t)v); put_le32(p + 4, (uint32_t)(v >> 32)); }
static inline void put_be64(uint8_t *p, uint64_t v) { put_be32(p, (uint32_t)(v >> 32)); put_be32(p + 4, (uint32_t)v); }
static inline int16_t s16_from_u16(uint16_t u) { return u <= 0x7FFFu ? (int16_t)u : (int16_t)((int32_t)u - 65536); }
static inline float get_le_f32(const uint8_t *p) { uint32_t u = get_le32(p); float f; memcpy(&f, &u, 4); return f; }
static inline void put_le_f32(uint8_t *p, float f) { uint32_t u; memcpy(&u, &f, 4); put_le32(p, u); }

static uint16_t crc16_ccitt_false(const uint8_t *p, size_t n) {
    uint16_t crc = 0xFFFFu;
    for (size_t i = 0; i < n; i++) {
        crc ^= (uint16_t)(p[i] << 8);
        for (int b = 0; b < 8; b++)
            crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u) : (uint16_t)(crc << 1);
    }
    return crc;
}

#define TELEM_SYNC        0xEB90u
#define TELEM_TYPE_IMU    0x21u
#define TELEM_HDR_LEN     7u
#define TELEM_PAYLOAD_LEN 26u
#define TELEM_FRAME_LEN   (TELEM_HDR_LEN + TELEM_PAYLOAD_LEN + 2u)
typedef struct {
    uint64_t time_us; int16_t temp_cC; uint16_t vbat_mV; int16_t accel_mg[3];
    float pressure_pa; uint32_t status;
} telem_t;
enum { TELEM_ERR_SHORT = -1, TELEM_ERR_SYNC = -2, TELEM_ERR_TYPE = -3,
       TELEM_ERR_LEN = -4, TELEM_ERR_CRC = -5 };

/* 페이로드 26바이트만 쓰는/읽는 부분 (오프셋 고정이라 커서 없이 직접) */
static void payload_put(uint8_t *p, const telem_t *t) {
    put_le64(p, t->time_us);
    put_le16(p + 8, (uint16_t)t->temp_cC);
    put_le16(p + 10, t->vbat_mV);
    for (int i = 0; i < 3; i++) put_le16(p + 12 + 2 * i, (uint16_t)t->accel_mg[i]);
    put_le_f32(p + 18, t->pressure_pa);
    put_le32(p + 22, t->status);
}
static void payload_get(const uint8_t *p, telem_t *t) {
    t->time_us = get_le64(p);
    t->temp_cC = s16_from_u16(get_le16(p + 8));
    t->vbat_mV = get_le16(p + 10);
    for (int i = 0; i < 3; i++) t->accel_mg[i] = s16_from_u16(get_le16(p + 12 + 2 * i));
    t->pressure_pa = get_le_f32(p + 18);
    t->status = get_le32(p + 22);
}
/* extra 바이트(0xEE)를 덧붙인 "v2" 프레임도 만들 수 있는 인코더. 반환 = 프레임 길이 */
static size_t frame_build(uint8_t *out, const telem_t *t, uint16_t seq, size_t extra) {
    size_t len = TELEM_PAYLOAD_LEN + extra;
    put_be16(out, TELEM_SYNC);
    out[2] = TELEM_TYPE_IMU;
    put_le16(out + 3, seq);
    put_le16(out + 5, (uint16_t)len);
    payload_put(out + TELEM_HDR_LEN, t);
    memset(out + TELEM_HDR_LEN + TELEM_PAYLOAD_LEN, 0xEE, extra);
    put_be16(out + TELEM_HDR_LEN + len, crc16_ccitt_false(out, TELEM_HDR_LEN + len));
    return TELEM_HDR_LEN + len + 2u;
}
/* 본문과 같은 엄격한 디코더 (len == 26 만 허용) */
static int telem_decode(const uint8_t *in, size_t n, telem_t *t, uint16_t *seq) {
    if (n < TELEM_HDR_LEN) return TELEM_ERR_SHORT;
    if (get_be16(in) != TELEM_SYNC) return TELEM_ERR_SYNC;
    if (in[2] != TELEM_TYPE_IMU) return TELEM_ERR_TYPE;
    uint16_t len = get_le16(in + 5);
    if (len != TELEM_PAYLOAD_LEN) return TELEM_ERR_LEN;
    size_t body = TELEM_HDR_LEN + (size_t)len;
    if (n < body + 2u) return TELEM_ERR_SHORT;
    if (crc16_ccitt_false(in, body) != get_be16(in + body)) return TELEM_ERR_CRC;
    payload_get(in + TELEM_HDR_LEN, t);
    *seq = get_le16(in + 3);
    return (int)(body + 2u);
}

/* ===== 연습 1: 가변 폭 (n = 1..8 바이트) ===== */
static uint64_t get_uint_le(const uint8_t *p, unsigned n) {
    uint64_t v = 0;
    for (unsigned i = n; i-- > 0;) v = (v << 8) | p[i];        /* 높은 주소 = 상위 바이트 */
    return v;
}
static uint64_t get_uint_be(const uint8_t *p, unsigned n) {
    uint64_t v = 0;
    for (unsigned i = 0; i < n; i++) v = (v << 8) | p[i];
    return v;
}
static void put_uint_be(uint8_t *p, uint64_t v, unsigned n) {
    for (unsigned i = n; i-- > 0;) { p[i] = (uint8_t)v; v >>= 8; } /* 끝(LSB)부터 채움 */
}
/* 24비트 2의 보수 → int32 (구현 정의 변환 없이) */
static int32_t s24_from_u24(uint32_t u) { return (u & 0x800000u) ? (int32_t)u - 0x1000000 : (int32_t)u; }

/* ===== 연습 2: °C(float) → 0.01 °C int16, 반올림(0에서 먼 쪽) · 포화 · NaN → INT16_MIN ===== */
#define CC_INVALID INT16_MIN                         /* 0x8000 = "값 없음" 예약 */
static int16_t degc_to_cC(float degC) {
    float x = degC * 100.0f;
    if (x != x) return CC_INVALID;                   /* NaN */
    if (x >= 32767.0f) return 32767;                 /* 범위 검사를 먼저: 범위 밖 float→int 는 UB */
    if (x <= -32767.0f) return -32767;
    int32_t t = (int32_t)x;                          /* 0 쪽으로 버림 */
    float frac = x - (float)t;                       /* |x| < 2^15 → 정확 */
    if (frac >= 0.5f) t++;
    else if (frac <= -0.5f) t--;
    return (int16_t)t;
}

/* ===== 연습 3: 스트림 스캐너 — 좋은 프레임 수 반환, 버린 바이트 수는 *skipped ===== */
static size_t scan_frames(const uint8_t *s, size_t n, uint16_t *seqs, size_t max, size_t *skipped) {
    size_t i = 0, got = 0;
    *skipped = 0;
    while (i < n && got < max) {
        telem_t t;
        int rc = telem_decode(s + i, n - i, &t, &seqs[got]);
        if (rc > 0) { got++; i += (size_t)rc; continue; }
        if (rc == TELEM_ERR_SHORT && n - i >= 2 && get_be16(s + i) == TELEM_SYNC)
            break;                                   /* 진짜 프레임이 덜 왔을 수 있음 → 다음 수신을 기다림 */
        i++; (*skipped)++;                           /* 실패면 1바이트만 전진 (35 아님!) */
    }
    return got;
}

/* ===== 연습 4: 앞으로 호환 디코더 — len >= 26 이면 아는 필드만 읽고 나머지는 건너뜀 ===== */
#define TELEM_MAX_PAYLOAD 200u
static int telem_decode_v(const uint8_t *in, size_t n, telem_t *t, uint16_t *seq) {
    if (n < TELEM_HDR_LEN) return TELEM_ERR_SHORT;
    if (get_be16(in) != TELEM_SYNC) return TELEM_ERR_SYNC;
    if (in[2] != TELEM_TYPE_IMU) return TELEM_ERR_TYPE;
    uint16_t len = get_le16(in + 5);
    if (len < TELEM_PAYLOAD_LEN || len > TELEM_MAX_PAYLOAD) return TELEM_ERR_LEN;
    size_t body = TELEM_HDR_LEN + (size_t)len;
    if (n < body + 2u) return TELEM_ERR_SHORT;
    if (crc16_ccitt_false(in, body) != get_be16(in + body)) return TELEM_ERR_CRC;  /* CRC 는 전체 */
    payload_get(in + TELEM_HDR_LEN, t);
    *seq = get_le16(in + 3);
    return (int)(body + 2u);
}

/* ===== 연습 5: double, 빅 엔디안 ===== */
static void put_be_f64(uint8_t *p, double d) { uint64_t u; memcpy(&u, &d, 8); put_be64(p, u); }
static double get_be_f64(const uint8_t *p) { uint64_t u = get_be64(p); double d; memcpy(&d, &u, 8); return d; }

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
static uint32_t g_rng = 0xC0FFEE11u;
static uint32_t rnd32(void) { uint32_t x = g_rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return g_rng = x; }

int main(void) {
    /* 연습 1 */
    const uint8_t mac[6] = { 0x00, 0x11, 0x22, 0x33, 0x44, 0x55 };
    CHECK(get_uint_be(mac, 6) == 0x001122334455ull);
    CHECK(get_uint_le(mac, 3) == 0x221100u && get_uint_be(mac + 1, 3) == 0x112233u);
    const uint8_t adc[3] = { 0xFF, 0xFF, 0xFE };                 /* 24비트 BE ADC = -2 */
    CHECK(s24_from_u24((uint32_t)get_uint_be(adc, 3)) == -2);
    CHECK(s24_from_u24(0x7FFFFFu) == 8388607 && s24_from_u24(0x800000u) == -8388608);
    uint8_t b[16];
    memset(b, 0xCC, sizeof b);
    put_uint_be(b + 1, 0x0102030405ull, 5);                      /* 40비트 */
    CHECK(b[0] == 0xCC && b[1] == 0x01 && b[5] == 0x05 && b[6] == 0xCC);
    int agree = 0;
    for (int i = 0; i < 300; i++) {
        uint64_t v = ((uint64_t)rnd32() << 32) | rnd32();
        put_be64(b + 3, v);
        agree += get_uint_be(b + 3, 8) == get_be64(b + 3) && get_uint_le(b + 3, 8) == get_le64(b + 3) &&
                 get_uint_be(b + 3, 4) == get_be32(b + 3) && get_uint_le(b + 3, 2) == get_le16(b + 3);
    }
    CHECK(agree == 300);

    /* 연습 2 */
    CHECK(degc_to_cC(25.375f) == 2538);                          /* 2537.5 → 2538 */
    CHECK(degc_to_cC(-0.125f) == -13);                           /* -12.5 → -13 (0에서 먼 쪽) */
    CHECK(degc_to_cC(0.0f) == 0 && degc_to_cC(-12.34f) == -1234);
    CHECK(degc_to_cC(1000.0f) == 32767 && degc_to_cC(-400.0f) == -32767);
    float nan; uint32_t qnan = 0x7FC00000u; memcpy(&nan, &qnan, 4);
    CHECK(degc_to_cC(nan) == CC_INVALID);
    float inf; uint32_t pinf = 0x7F800000u; memcpy(&inf, &pinf, 4);
    CHECK(degc_to_cC(inf) == 32767 && degc_to_cC(-inf) == -32767);

    /* 연습 3: 쓰레기 3 + 프레임(1) + 손상 프레임(2) + 가짜 sync + 프레임(3) + 잘린 프레임(4) */
    uint8_t s[3 + 35 + 35 + 2 + 35 + 20];
    size_t n = 0;
    telem_t t0 = { 1000u, 2500, 3700, { 0, 0, 1000 }, 101325.0f, 0u };
    s[n++] = 0x00; s[n++] = 0xEB; s[n++] = 0x13;
    n += frame_build(s + n, &t0, 1, 0);
    size_t f2 = n; n += frame_build(s + n, &t0, 2, 0);
    s[f2 + 20] ^= 0x40;                                          /* 페이로드 1비트 손상 */
    s[n++] = 0xEB; s[n++] = 0x90;                                /* sync 처럼 보이는 쓰레기 */
    n += frame_build(s + n, &t0, 3, 0);
    uint8_t tail[35];
    frame_build(tail, &t0, 4, 0);
    memcpy(s + n, tail, 20); n += 20;                            /* 아직 다 안 온 프레임 */
    uint16_t seqs[8] = { 0 };
    size_t skipped = 0;
    size_t got = scan_frames(s, n, seqs, 8, &skipped);
    CHECK(got == 2 && seqs[0] == 1 && seqs[1] == 3);
    CHECK(skipped == 3 + 35 + 2);                                /* 앞 쓰레기 + 손상 프레임 + 가짜 sync */

    /* 연습 4 */
    uint8_t v2[TELEM_FRAME_LEN + 4];
    telem_t d; uint16_t sq = 0;
    size_t v2n = frame_build(v2, &t0, 77, 4);                    /* len = 30 */
    CHECK(v2n == 39 && telem_decode(v2, v2n, &d, &sq) == TELEM_ERR_LEN);   /* 엄격: 거부 */
    CHECK(telem_decode_v(v2, v2n, &d, &sq) == 39 && sq == 77 && d.temp_cC == 2500 && d.accel_mg[2] == 1000);
    uint8_t v1[TELEM_FRAME_LEN];
    frame_build(v1, &t0, 78, 0);
    CHECK(telem_decode_v(v1, sizeof v1, &d, &sq) == (int)TELEM_FRAME_LEN && sq == 78);
    put_le16(v1 + 5, 25);                                        /* 너무 짧은 len → LEN */
    CHECK(telem_decode_v(v1, sizeof v1, &d, &sq) == TELEM_ERR_LEN);
    put_le16(v1 + 5, 0xFFFF);                                    /* 터무니없이 긴 len → LEN */
    CHECK(telem_decode_v(v1, sizeof v1, &d, &sq) == TELEM_ERR_LEN);

    /* 연습 5 */
    put_be_f64(b, 1.0);
    const uint8_t one_be[8] = { 0x3F, 0xF0, 0, 0, 0, 0, 0, 0 };
    CHECK(memcmp(b, one_be, 8) == 0);
    put_be_f64(b + 1, 3.141592653589793);
    const uint8_t pi_be[8] = { 0x40, 0x09, 0x21, 0xFB, 0x54, 0x44, 0x2D, 0x18 };
    CHECK(memcmp(b + 1, pi_be, 8) == 0 && get_be_f64(b + 1) == 3.141592653589793);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
