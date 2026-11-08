/* C-M05 엔디안 독립 직렬화: get_le16 / put_be32
 *
 * 텔레메트리 패킷(헤더 + 페이로드 + CRC)을 바이트 버퍼로 직렬화/역직렬화하는 "계층"을 만든다.
 * 규칙: 포인터 캐스트(*(uint32_t *)p) 금지, 호스트 엔디안 검사(#if __BYTE_ORDER__) 금지,
 *       컴파일러 내장 함수(__builtin_bswap32 등) 금지. 다바이트 값은 uint8_t 한 개씩 시프트로
 *       조립/분해한다 → 호스트 엔디안과 무관하고, 어느 주소(홀수 주소 포함)에서도 동작한다.
 *   Part A: get/put_{le,be}{16,32,64}
 *   Part B: 부호(int16), float(memcpy), 경계 검사 커서(sticky error)
 *   Part C: telem_encode / telem_decode — 고정 바이트 벡터, 왕복(round-trip), 손상 검출
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

_Static_assert(sizeof(float) == 4, "float 은 32비트(IEEE 754 binary32)라고 가정");

/* ======================= Part A: 고정 폭 헬퍼 ======================= */
/* 예시 (그대로 둔다): le = 바이트 0 이 최하위 바이트(LSB) */
static inline uint16_t get_le16(const uint8_t *p) {
    return (uint16_t)(p[0] | (p[1] << 8));       /* int 로 승격돼도 최대 0xFFFF → 안전 */
}
static inline void put_le16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}
/* TODO: 나머지 10개. be = 바이트 0 이 최상위 바이트(MSB). 승격(C-B02)에 주의 */
static inline uint16_t get_be16(const uint8_t *p) {
    (void)p; return 0; /* TODO */
}
static inline uint32_t get_le32(const uint8_t *p) {
    (void)p; return 0; /* TODO */
}
static inline uint32_t get_be32(const uint8_t *p) {
    (void)p; return 0; /* TODO */
}
static inline uint64_t get_le64(const uint8_t *p) {
    (void)p; return 0; /* TODO */
}
static inline uint64_t get_be64(const uint8_t *p) {
    (void)p; return 0; /* TODO */
}
static inline void put_be16(uint8_t *p, uint16_t v) {
    (void)p; (void)v; /* TODO */
}
static inline void put_le32(uint8_t *p, uint32_t v) {
    (void)p; (void)v; /* TODO */
}
static inline void put_be32(uint8_t *p, uint32_t v) {
    (void)p; (void)v; /* TODO */
}
static inline void put_le64(uint8_t *p, uint64_t v) {
    (void)p; (void)v; /* TODO */
}
static inline void put_be64(uint8_t *p, uint64_t v) {
    (void)p; (void)v; /* TODO */
}

/* ======================= Part B: 부호 · float · 커서 ======================= */
/* u16 → int16. (int16_t)u 는 u > 0x7FFF 일 때 구현 정의(C11 6.3.1.3p3) → 그것 없이 */
static inline int16_t s16_from_u16(uint16_t u) {
    (void)u; return 0; /* TODO */
}
static inline int16_t get_le16s(const uint8_t *p) { return s16_from_u16(get_le16(p)); }
/* 부호 → 무부호 변환은 mod 2^16 으로 "정의됨"(6.3.1.3p2) → 캐스트 한 번이면 2의 보수 비트 */
static inline void put_le16s(uint8_t *p, int16_t v) { put_le16(p, (uint16_t)v); }

/* float 의 비트를 uint32 로 옮겨(memcpy) le32 로 직렬화 — 포인터 캐스트 punning 금지(C-M13) */
static inline float get_le_f32(const uint8_t *p) {
    (void)p; return 0.0f; /* TODO */
}
static inline void put_le_f32(uint8_t *p, float f) {
    (void)p; (void)f; /* TODO */
}

/* 경계 검사 커서: 필드마다 길이 검사를 쓰지 않고, 커서가 한 번 실패하면 err 가 "계속" 선다. */
typedef struct { uint8_t *buf;       size_t cap; size_t pos; bool err; } wcur_t;
typedef struct { const uint8_t *buf; size_t len; size_t pos; bool err; } rcur_t;

/* n 바이트 자리를 예약하고 그 시작 포인터를 돌려준 뒤 pos += n.
 * 공간이 모자라거나 이미 err 면: err = true, NULL 반환, pos 는 그대로. (pos + n 오버플로에 주의) */
static inline uint8_t *w_reserve(wcur_t *w, size_t n) {
    (void)w; (void)n; return NULL; /* TODO */
}
static inline const uint8_t *r_take(rcur_t *r, size_t n) {
    (void)r; (void)n; return NULL; /* TODO */
}
/* (주어진 코드) 커서 위의 얇은 래퍼 — 실패 시 쓰기는 무시, 읽기는 0 */
static inline void w_u8(wcur_t *w, uint8_t v)     { uint8_t *p = w_reserve(w, 1); if (p) p[0] = v; }
static inline void w_be16(wcur_t *w, uint16_t v)  { uint8_t *p = w_reserve(w, 2); if (p) put_be16(p, v); }
static inline void w_le16(wcur_t *w, uint16_t v)  { uint8_t *p = w_reserve(w, 2); if (p) put_le16(p, v); }
static inline void w_le16s(wcur_t *w, int16_t v)  { uint8_t *p = w_reserve(w, 2); if (p) put_le16s(p, v); }
static inline void w_le32(wcur_t *w, uint32_t v)  { uint8_t *p = w_reserve(w, 4); if (p) put_le32(p, v); }
static inline void w_le64(wcur_t *w, uint64_t v)  { uint8_t *p = w_reserve(w, 8); if (p) put_le64(p, v); }
static inline void w_f32(wcur_t *w, float v)      { uint8_t *p = w_reserve(w, 4); if (p) put_le_f32(p, v); }
static inline uint8_t  r_u8(rcur_t *r)   { const uint8_t *p = r_take(r, 1); return p ? p[0] : 0u; }
static inline uint16_t r_be16(rcur_t *r) { const uint8_t *p = r_take(r, 2); return p ? get_be16(p) : 0u; }
static inline uint16_t r_le16(rcur_t *r) { const uint8_t *p = r_take(r, 2); return p ? get_le16(p) : 0u; }
static inline int16_t  r_le16s(rcur_t *r){ const uint8_t *p = r_take(r, 2); return p ? get_le16s(p) : 0; }
static inline uint32_t r_le32(rcur_t *r) { const uint8_t *p = r_take(r, 4); return p ? get_le32(p) : 0u; }
static inline uint64_t r_le64(rcur_t *r) { const uint8_t *p = r_take(r, 8); return p ? get_le64(p) : 0u; }
static inline float    r_f32(rcur_t *r)  { const uint8_t *p = r_take(r, 4); return p ? get_le_f32(p) : 0.0f; }

/* ======================= Part C: 텔레메트리 패킷 ======================= */
/* off  0  sync    u16 BE  EB 90 (선로에 이 순서)        off 15 temp_cC   i16 LE (0.01 °C)
 * off  2  type    u8      0x21 (IMU 텔레메트리)          off 17 vbat_mV   u16 LE
 * off  3  seq     u16 LE                                 off 19 accel_mg  i16 LE x3
 * off  5  len     u16 LE  페이로드 길이 = 26              off 25 pressure  f32 LE (Pa)
 * off  7  time_us u64 LE  (홀수 오프셋! 비정렬)           off 29 status    u32 LE
 * off 33  crc     u16 BE  CRC-16/CCITT-FALSE(바이트 0..32)  → 프레임 35바이트 */
#define TELEM_SYNC        0xEB90u
#define TELEM_TYPE_IMU    0x21u
#define TELEM_HDR_LEN     7u
#define TELEM_PAYLOAD_LEN 26u
#define TELEM_FRAME_LEN   (TELEM_HDR_LEN + TELEM_PAYLOAD_LEN + 2u)

typedef struct {
    uint64_t time_us;
    int16_t  temp_cC;
    uint16_t vbat_mV;
    int16_t  accel_mg[3];
    float    pressure_pa;
    uint32_t status;
} telem_t;

enum { TELEM_ERR_SHORT = -1, TELEM_ERR_SYNC = -2, TELEM_ERR_TYPE = -3,
       TELEM_ERR_LEN = -4, TELEM_ERR_CRC = -5 };

/* (주어진 코드) CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF, 반사 없음, xorout 0.
 * 검사값(check): "123456789" → 0x29B1. 원리·테이블 버전은 ALGO-M01/M02. */
static uint16_t crc16_ccitt_false(const uint8_t *p, size_t n) {
    uint16_t crc = 0xFFFFu;
    for (size_t i = 0; i < n; i++) {
        crc ^= (uint16_t)(p[i] << 8);
        for (int b = 0; b < 8; b++)
            crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u) : (uint16_t)(crc << 1);
    }
    return crc;
}

/* 프레임을 out 에 직렬화하고 길이(35)를 반환. cap 이 모자라면 0 (cap 밖에는 한 바이트도 안 쓴다). */
static size_t telem_encode(const telem_t *t, uint16_t seq, uint8_t *out, size_t cap) {
    (void)t; (void)seq; (void)out; (void)cap;
    (void)w_be16; (void)w_le16s; (void)w_le64; (void)w_f32;   /* 미사용 경고 막기 — 풀 때 지운다 */
    return 0; /* TODO: wcur_t 로 헤더 → 페이로드 → CRC(BE) */
}

/* in[0..n) 맨 앞의 프레임을 해석. 성공: 소비한 바이트 수(35), *t·*seq 채움.
 * 실패: 음수 에러, *t·*seq 는 건드리지 않는다.
 * 검사 순서: 헤더 길이 → sync → type → len → 프레임 길이 → CRC → 필드 */
static int telem_decode(const uint8_t *in, size_t n, telem_t *t, uint16_t *seq) {
    (void)in; (void)n; (void)t; (void)seq;
    (void)r_be16; (void)r_le16s; (void)r_le64; (void)r_f32;   /* 미사용 경고 막기 — 풀 때 지운다 */
    return TELEM_ERR_SHORT; /* TODO */
}

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t g_rng = 0x12345678u;              /* xorshift32 — 고정 시드, 재현 가능 */
static uint32_t rnd32(void) {
    uint32_t x = g_rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return g_rng = x;
}
static uint64_t rnd64(void) { uint64_t hi = rnd32(); return (hi << 32) | rnd32(); }
static uint32_t f32_bits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static float bits_f32(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }
static bool telem_eq(const telem_t *a, const telem_t *b) {   /* float 은 비트로 비교 (NaN 포함) */
    return a->time_us == b->time_us && a->temp_cC == b->temp_cC && a->vbat_mV == b->vbat_mV &&
           a->accel_mg[0] == b->accel_mg[0] && a->accel_mg[1] == b->accel_mg[1] &&
           a->accel_mg[2] == b->accel_mg[2] &&
           f32_bits(a->pressure_pa) == f32_bits(b->pressure_pa) && a->status == b->status;
}

/* 고정 바이트 벡터 — Python struct.pack('<QhH3hfI') + binascii.crc_hqx(…, 0xFFFF) 로 독립 생성 */
static const uint8_t GOLDEN[TELEM_FRAME_LEN] = {
    0xEB, 0x90, 0x21, 0x34, 0x12, 0x1A, 0x00,                       /* sync type seq len   */
    0xEF, 0xCD, 0xAB, 0x89, 0x67, 0x45, 0x23, 0x01,                 /* time_us             */
    0x2E, 0xFB, 0x74, 0x0E,                                         /* temp -1234, 3700 mV */
    0x18, 0xFC, 0x02, 0x00, 0xE8, 0x03,                             /* accel -1000, 2, 1000 */
    0x80, 0xE6, 0xC5, 0x47,                                         /* 101325.0f           */
    0xEF, 0xBE, 0xAD, 0xDE,                                         /* status              */
    0xCE, 0x65,                                                     /* CRC (BE)            */
};
static const telem_t GOLDEN_T = { 0x0123456789ABCDEFull, -1234, 3700, { -1000, 2, 1000 },
                                  101325.0f, 0xDEADBEEFu };

int main(void) {
    /* ---- Part A: 고정 벡터 ---- */
    const uint8_t seq8[9]  = { 0xAA, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08 };
    const uint8_t *s = seq8 + 1;                   /* 홀수 주소에서 시작 */
    const uint8_t hi8[8]   = { 0xFF, 0xFE, 0xFD, 0xFC, 0xFB, 0xFA, 0xF9, 0xF8 };
    CHECK(get_le16(s) == 0x0201u);
    CHECK(get_be16(s) == 0x0102u);
    CHECK(get_le32(s) == 0x04030201u);
    CHECK(get_be32(s) == 0x01020304u);
    CHECK(get_le64(s) == 0x0807060504030201ull);
    CHECK(get_be64(s) == 0x0102030405060708ull);
    CHECK(get_be16(hi8) == 0xFFFEu && get_le16(hi8) == 0xFEFFu);
    CHECK(get_be32(hi8) == 0xFFFEFDFCu);           /* 0xFF << 24 승격 함정 */
    CHECK(get_le32(hi8 + 4) == 0xF8F9FAFBu);
    CHECK(get_be64(hi8) == 0xFFFEFDFCFBFAF9F8ull); /* 부호 확장되면 상위가 엉킨다 */
    CHECK(get_le64(hi8) == 0xF8F9FAFBFCFDFEFFull);

    uint8_t b[12];
    memset(b, 0xCC, sizeof b);
    put_be32(b + 1, 0xA1B2C3D4u);
    CHECK(b[0] == 0xCC && b[1] == 0xA1 && b[2] == 0xB2 && b[3] == 0xC3 && b[4] == 0xD4 && b[5] == 0xCC);
    memset(b, 0xCC, sizeof b);
    put_le32(b + 3, 0xA1B2C3D4u);
    CHECK(b[2] == 0xCC && b[3] == 0xD4 && b[4] == 0xC3 && b[5] == 0xB2 && b[6] == 0xA1 && b[7] == 0xCC);
    memset(b, 0xCC, sizeof b);
    put_be16(b + 1, 0xBEEFu);
    CHECK(b[0] == 0xCC && b[1] == 0xBE && b[2] == 0xEF && b[3] == 0xCC);
    memset(b, 0xCC, sizeof b);
    put_le64(b + 1, 0x1122334455667788ull);
    const uint8_t want_le64[10] = { 0xCC, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, 0xCC };
    CHECK(memcmp(b, want_le64, 10) == 0);
    memset(b, 0xCC, sizeof b);
    put_be64(b + 3, 0x1122334455667788ull);
    const uint8_t want_be64[10] = { 0xCC, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0xCC };
    CHECK(memcmp(b + 2, want_be64, 10) == 0);

    /* ---- Part A: 왕복 1000회 x 임의 오프셋 ---- */
    int rt_ok = 0, cross_ok = 0;
    for (int i = 0; i < 1000; i++) {
        uint8_t buf[16];
        size_t off = rnd32() % 8u;
        uint64_t v = rnd64();
        int ok = 1;
        put_le16(buf + off, (uint16_t)v); ok &= get_le16(buf + off) == (uint16_t)v;
        put_be16(buf + off, (uint16_t)v); ok &= get_be16(buf + off) == (uint16_t)v;
        put_le32(buf + off, (uint32_t)v); ok &= get_le32(buf + off) == (uint32_t)v;
        put_be32(buf + off, (uint32_t)v); ok &= get_be32(buf + off) == (uint32_t)v;
        put_le64(buf + off, v);           ok &= get_le64(buf + off) == v;
        put_be64(buf + off, v);           ok &= get_be64(buf + off) == v;
        rt_ok += ok;
        /* be 로 쓰고 le 로 읽으면 바이트 역순: 바이트 k 가 자리 7-k 로 */
        uint64_t le = get_le64(buf + off), rev = 0;
        for (int k = 0; k < 8; k++) rev |= ((v >> (8 * k)) & 0xFFu) << (8 * (7 - k));
        cross_ok += (le == rev);
    }
    CHECK(rt_ok == 1000);
    CHECK(cross_ok == 1000);

    /* ---- Part B: 부호 · float ---- */
    CHECK(s16_from_u16(0x0000u) == 0 && s16_from_u16(0x7FFFu) == 32767);
    CHECK(s16_from_u16(0x8000u) == -32768 && s16_from_u16(0xFFFFu) == -1);
    CHECK(s16_from_u16(0xFB2Eu) == -1234);
    const uint8_t neg[2] = { 0x18, 0xFC };
    CHECK(get_le16s(neg) == -1000);
    memset(b, 0, sizeof b);
    put_le_f32(b, 1.0f);                           /* 0x3F800000 */
    CHECK(b[0] == 0x00 && b[1] == 0x00 && b[2] == 0x80 && b[3] == 0x3F);
    put_le_f32(b + 1, -2.5f);                      /* 0xC0200000, 홀수 주소 */
    CHECK(b[1] == 0x00 && b[2] == 0x00 && b[3] == 0x20 && b[4] == 0xC0);
    CHECK(get_le_f32(b + 1) == -2.5f);
    const uint8_t pa[4] = { 0x80, 0xE6, 0xC5, 0x47 };
    CHECK(get_le_f32(pa) == 101325.0f);
    int f_ok = 0;
    for (int i = 0; i < 1000; i++) {               /* 임의 비트 (NaN·Inf·비정규 포함) */
        uint32_t u = rnd32();
        put_le_f32(b + 2, bits_f32(u));
        f_ok += (get_le32(b + 2) == u) && (f32_bits(get_le_f32(b + 2)) == u);
    }
    CHECK(f_ok == 1000);

    /* ---- Part B: 커서 ---- */
    memset(b, 0xCC, sizeof b);
    wcur_t w = { b, 5, 0, false };
    w_le32(&w, 0x11223344u);
    CHECK(!w.err && w.pos == 4);
    w_le16(&w, 0xABCDu);                           /* 2바이트 필요, 1바이트 남음 → 실패 */
    CHECK(w.err && w.pos == 4 && b[4] == 0xCC);
    w_u8(&w, 0x55u);                               /* 1바이트는 들어가지만 err 는 sticky */
    CHECK(w.err && w.pos == 4 && b[4] == 0xCC);
    wcur_t w2 = { b, 8, 4, false };
    CHECK(w_reserve(&w2, SIZE_MAX) == NULL && w2.err && w2.pos == 4);  /* pos+n 랩어라운드 */
    wcur_t w3 = { b, 8, 0, false };
    CHECK(w_reserve(&w3, 8) == b && !w3.err && w3.pos == 8);           /* 꼭 맞으면 OK */
    CHECK(w_reserve(&w3, 0) == b + 8 && !w3.err);                      /* 0바이트는 항상 OK */

    const uint8_t rd[5] = { 0x01, 0x02, 0x03, 0x04, 0x05 };
    rcur_t r = { rd, 5, 0, false };
    CHECK(r_le32(&r) == 0x04030201u && !r.err && r.pos == 4);
    CHECK(r_le16(&r) == 0 && r.err && r.pos == 4);
    CHECK(r_u8(&r) == 0 && r.err && r.pos == 4);   /* sticky */
    rcur_t r2 = { rd, 5, 3, false };
    CHECK(r_take(&r2, SIZE_MAX - 1u) == NULL && r2.err && r2.pos == 3);

    /* ---- Part C: 고정 바이트 벡터 ---- */
    uint8_t fr[40];
    memset(fr, 0xCC, sizeof fr);
    CHECK(crc16_ccitt_false((const uint8_t *)"123456789", 9) == 0x29B1u);
    CHECK(telem_encode(&GOLDEN_T, 0x1234u, fr, sizeof fr) == TELEM_FRAME_LEN);
    CHECK(memcmp(fr, GOLDEN, TELEM_FRAME_LEN) == 0);
    CHECK(fr[TELEM_FRAME_LEN] == 0xCC);
    memset(fr, 0xCC, sizeof fr);
    CHECK(telem_encode(&GOLDEN_T, 0x1234u, fr, TELEM_FRAME_LEN - 1u) == 0);
    CHECK(fr[TELEM_FRAME_LEN - 1u] == 0xCC);       /* cap 밖은 안 건드림 */

    telem_t t;
    uint16_t sq = 0;
    CHECK(telem_decode(GOLDEN, TELEM_FRAME_LEN, &t, &sq) == (int)TELEM_FRAME_LEN);
    CHECK(sq == 0x1234u && telem_eq(&t, &GOLDEN_T));
    CHECK(t.temp_cC == -1234 && t.accel_mg[0] == -1000 && t.pressure_pa == 101325.0f);

    /* ---- Part C: 에러 경로 ---- */
    uint8_t bad[TELEM_FRAME_LEN];
    telem_t keep;
    memset(&keep, 0x5A, sizeof keep);
    t = keep; sq = 0x7777u;
    CHECK(telem_decode(GOLDEN, 6, &t, &sq) == TELEM_ERR_SHORT);
    CHECK(telem_decode(GOLDEN, TELEM_FRAME_LEN - 1u, &t, &sq) == TELEM_ERR_SHORT);
    memcpy(bad, GOLDEN, sizeof bad); bad[1] = 0x91;
    CHECK(telem_decode(bad, sizeof bad, &t, &sq) == TELEM_ERR_SYNC);
    memcpy(bad, GOLDEN, sizeof bad); bad[2] = 0x22;
    CHECK(telem_decode(bad, sizeof bad, &t, &sq) == TELEM_ERR_TYPE);
    memcpy(bad, GOLDEN, sizeof bad); bad[6] = 0x01;          /* len = 0x011A */
    CHECK(telem_decode(bad, sizeof bad, &t, &sq) == TELEM_ERR_LEN);
    memcpy(bad, GOLDEN, sizeof bad); bad[20] ^= 0x01;        /* 페이로드 1비트 */
    CHECK(telem_decode(bad, sizeof bad, &t, &sq) == TELEM_ERR_CRC);
    memcpy(bad, GOLDEN, sizeof bad); bad[34] ^= 0x80;        /* CRC 자체 1비트 */
    CHECK(telem_decode(bad, sizeof bad, &t, &sq) == TELEM_ERR_CRC);
    CHECK(memcmp(&t, &keep, sizeof t) == 0 && sq == 0x7777u);  /* 실패 시 출력 불변 */

    int flips_caught = 0;                          /* 35바이트 x 8비트 = 280가지 1비트 오류 */
    for (unsigned i = 0; i < TELEM_FRAME_LEN; i++)
        for (int k = 0; k < 8; k++) {
            memcpy(bad, GOLDEN, sizeof bad);
            bad[i] ^= (uint8_t)(1u << k);
            flips_caught += telem_decode(bad, sizeof bad, &t, &sq) < 0;
        }
    CHECK(flips_caught == 280);

    /* ---- Part C: 왕복 500회 (임의 필드, 임의 seq, 뒤에 쓰레기 바이트) ---- */
    int trip_ok = 0;
    for (int i = 0; i < 500; i++) {
        telem_t a, d;
        a.time_us = rnd64();
        a.temp_cC = s16_from_u16((uint16_t)rnd32());
        a.vbat_mV = (uint16_t)rnd32();
        for (int k = 0; k < 3; k++) a.accel_mg[k] = s16_from_u16((uint16_t)rnd32());
        a.pressure_pa = bits_f32(rnd32());
        a.status = rnd32();
        uint16_t sq_in = (uint16_t)rnd32(), sq_out = 0;
        memset(fr, (int)(rnd32() & 0xFFu), sizeof fr);
        size_t n = telem_encode(&a, sq_in, fr, sizeof fr);
        int rc = telem_decode(fr, sizeof fr, &d, &sq_out);   /* 뒤 5바이트는 무시돼야 */
        trip_ok += n == TELEM_FRAME_LEN && rc == (int)TELEM_FRAME_LEN && sq_out == sq_in &&
                   telem_eq(&a, &d);
    }
    CHECK(trip_ok == 500);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
