/* C-M13 Strict Aliasing과 Type Punning
 *
 * 규칙 (C11 6.5p7): 객체는 "자기 유효 타입(effective type)과 호환되는 타입" 또는 "문자 타입"의
 * lvalue 로만 읽고 써야 한다. 그래서
 *   *(uint32_t *)&f          ← float 객체를 uint32_t 로 읽음: 위반 (UB)
 *   *(uint32_t *)&rx[1]      ← uint8_t 배열 객체를 uint32_t 로 읽음: 위반 (+ 정렬 문제, C-M04)
 *   ((unsigned char *)&f)[0] ← 아무 객체나 문자 타입으로 읽음: 허용
 * 이 파일의 모든 함수는 memcpy (또는 union) 로만 비트를 옮긴다 → -O2 에서도, 새니타이저에서도 안전.
 * memcpy 는 -O2 에서 fmov / ldr 한 개로 컴파일된다 (demo_punning_asm.c 참고).
 */
#include <float.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---------------- Part A: float ⇄ 비트, 바이트 버퍼 ⇄ uint32_t ---------------- */
static uint32_t float_bits(float f) {
    (void)f;
    /* TODO: f 의 비트 패턴. return *(uint32_t *)&f; 는 6.5p7 위반 — 다른 방법으로 */
    return 0;
}
static float bits_to_float(uint32_t u) {
    (void)u;
    /* TODO */
    return 0.0f;
}
/* p: 정렬 보장 없음. 호스트 바이트 순서로 해석 (엔디언 독립 버전은 C-M05) */
static uint32_t load_u32(const void *p) {
    (void)p;
    /* TODO: return *(const uint32_t *)p; 는 aliasing + 정렬 위반 */
    return 0;
}
static void store_u32(void *p, uint32_t v) {
    (void)p; (void)v; /* TODO */
}

/* ---------------- Part B: IEEE-754 binary32 필드 ---------------- */
#define FP32_SIGN_MASK 0x80000000u
#define FP32_EXP_MASK  0x7F800000u
#define FP32_FRAC_MASK 0x007FFFFFu
#define FP32_EXP_SHIFT 23

typedef struct { unsigned sign; unsigned exp; uint32_t frac; } fp32_fields;
typedef enum { FPC_ZERO, FPC_SUBNORMAL, FPC_NORMAL, FPC_INF, FPC_NAN } fp_class;

static fp32_fields fp32_decode(float f) {
    uint32_t u = float_bits(f);
    fp32_fields r = { 0, 0, 0 };  (void)u;  /* TODO: sign 1비트, exp 8비트, frac 23비트 */
    return r;
}
/* 범위를 넘는 입력은 잘라낸다 (sign & 1, exp & 0xFF, frac & 0x7FFFFF) */
static float fp32_encode(unsigned sign, unsigned exp, uint32_t frac) {
    (void)sign; (void)exp; (void)frac;
    uint32_t u = 0;               /* TODO: 각 필드를 마스크한 뒤 제자리로 */
    return bits_to_float(u);
}
/* <math.h> 의 fpclassify 를 쓰지 않고 비트만으로 분류 */
static fp_class fp32_classify(float f) {
    fp32_fields x = fp32_decode(f);
    (void)x;              /* TODO: exp == 0 / exp == 0xFF / 그 외, frac == 0 여부로 5가지 */
    return FPC_NORMAL;
}

/* ---------------- Part C: IMU 프레임 파싱 + 빠른 역제곱근 ---------------- */
/* 프레임 (18바이트, 리틀 엔디언 = Cortex-M/arm64 호스트 순서):
 *   [0] 0xA5 sync  [1..4] u32 timestamp  [5..8] float ax  [9..12] float ay  [13..16] float az
 *   [17] XOR checksum of bytes [0..16]
 * 필드는 전부 비정렬 오프셋(1,5,9,13)에 있다. */
#define IMU_SYNC      0xA5u
#define IMU_FRAME_LEN 18u
typedef struct { uint32_t ts; float ax, ay, az; } imu_sample;

static bool imu_parse(const uint8_t *frame, size_t len, imu_sample *out) {
    if (len < IMU_FRAME_LEN || frame[0] != IMU_SYNC) return false;
    uint8_t x = 0;
    for (size_t i = 0; i < IMU_FRAME_LEN - 1; i++) x ^= frame[i];
    if (x != frame[IMU_FRAME_LEN - 1]) return false;
    imu_sample s;
    s.ts = 0;             /* TODO: 오프셋 1, 5, 9, 13 의 필드 (전부 비정렬!) */
    s.ax = 0.0f;
    s.ay = 0.0f;
    s.az = 0.0f;
    fp_class c[3] = { FPC_NORMAL, FPC_NORMAL, FPC_NORMAL };
    for (int i = 0; i < 3; i++)   /* TODO: NaN / Inf 값이 있으면 false */
        if (c[i] == FPC_NAN || c[i] == FPC_INF) return false;   /* 센서 쓰레기 값 거부 */
    *out = s;                                                    /* 성공했을 때만 출력 */
    return true;
}

/* Quake III 의 0x5F3759DF 트릭을 aliasing-안전하게. 전제: x 는 양의 정규(normal) 수 */
static float fast_rsqrt(float x) {
    uint32_t i = float_bits(x);
    i = 0;                             /* TODO: 매직 상수 0x5F3759DF - (i >> 1) */
    float y = bits_to_float(i);
    return y;                          /* TODO: 뉴턴 1회 y * (1.5 - 0.5 x y^2) */
}
/* 길이 1로 정규화. 0 벡터(또는 비정상 길이)면 false, v 는 그대로 */
static bool vec3_normalize(float v[3]) {
    float n2 = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
    if (fp32_classify(n2) != FPC_NORMAL) return false;
    float k = fast_rsqrt(n2);
    v[0] *= k; v[1] *= k; v[2] *= k;
    return true;
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t xorshift32(uint32_t *s) {      /* 고정 시드 → 재현 가능한 "랜덤" */
    uint32_t x = *s;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return *s = x;
}
/* 기대값을 독립적으로 계산: uint32_t 객체에 "문자 타입"으로 바이트를 써 넣는다 (6.5p7 허용 방향) */
static uint32_t ref_load(const uint8_t *p) {
    uint32_t e = 0;
    unsigned char *ep = (unsigned char *)&e;
    for (int i = 0; i < 4; i++) ep[i] = p[i];
    return e;
}
static bool near(float a, float b, float rel) { return fabsf(a - b) <= rel * fabsf(b); }

int main(void) {
    /* Part A */
    CHECK(float_bits(1.0f) == 0x3F800000u);
    CHECK(float_bits(-2.0f) == 0xC0000000u);
    CHECK(float_bits(0.0f) == 0x00000000u);
    CHECK(float_bits(-0.0f) == 0x80000000u);            /* -0.0 == 0.0 이지만 비트는 다르다 */
    CHECK(float_bits(0.1f) == 0x3DCCCCCDu);
    CHECK(float_bits(FLT_MAX) == 0x7F7FFFFFu);
    CHECK(float_bits(FLT_MIN) == 0x00800000u);
    CHECK(float_bits(FLT_TRUE_MIN) == 0x00000001u);
    CHECK(float_bits(INFINITY) == 0x7F800000u);
    CHECK(bits_to_float(0x40490FDBu) == 3.14159274f);
    CHECK(bits_to_float(0xC0200000u) == -2.5f);
    {   /* 비-NaN 비트 패턴 10000개 왕복 */
        uint32_t s = 0xC0FFEEu; int ok = 0, n = 0;
        while (n < 10000) {
            uint32_t u = xorshift32(&s);
            if ((u & FP32_EXP_MASK) == FP32_EXP_MASK && (u & FP32_FRAC_MASK)) continue;
            n++; ok += float_bits(bits_to_float(u)) == u;
        }
        CHECK(ok == 10000);
    }
    {   /* 비정렬 오프셋 0..3 에서 읽기 — 버퍼는 uint8_t 배열 (DMA 버퍼처럼 선언된 타입이 바이트) */
        static const uint8_t buf[8] = { 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88 };
        int ok = 0;
        for (int off = 0; off < 4; off++) ok += load_u32(buf + off) == ref_load(buf + off);
        CHECK(ok == 4);
        CHECK(load_u32(buf + 1) == 0x55443322u);        /* 리틀 엔디언 호스트 (arm64/Cortex-M) */
    }
    {   /* 홀수 주소에 쓰기, 이웃 바이트(카나리)는 그대로 */
        uint8_t b[8] = { 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE };
        store_u32(b + 3, 0xA1B2C3D4u);
        CHECK(b[2] == 0xEE && b[7] == 0xEE);
        CHECK(load_u32(b + 3) == 0xA1B2C3D4u && b[3] == 0xD4 && b[6] == 0xA1);
    }

    /* Part B */
    {
        fp32_fields a = fp32_decode(1.0f);
        CHECK(a.sign == 0 && a.exp == 127 && a.frac == 0);
        fp32_fields b = fp32_decode(-0.0f);
        CHECK(b.sign == 1 && b.exp == 0 && b.frac == 0);
        fp32_fields c = fp32_decode(0.1f);
        CHECK(c.sign == 0 && c.exp == 123 && c.frac == 0x4CCCCDu);
        fp32_fields d = fp32_decode(FLT_TRUE_MIN);
        CHECK(d.sign == 0 && d.exp == 0 && d.frac == 1);
        fp32_fields e = fp32_decode(-INFINITY);
        CHECK(e.sign == 1 && e.exp == 255 && e.frac == 0);
        fp32_fields n = fp32_decode(NAN);
        CHECK(n.exp == 255 && n.frac != 0);
    }
    CHECK(fp32_encode(0, 127, 0) == 1.0f);
    CHECK(fp32_encode(1, 128, 0x490FDBu) == -3.14159274f);
    CHECK(fp32_encode(2u, 0x17Fu, 0xFF800000u) == 1.0f);          /* 넘치는 입력은 마스크 */
    CHECK(float_bits(fp32_encode(1, 0, 0)) == 0x80000000u);         /* -0.0 */
    CHECK(fp32_classify(0.0f) == FPC_ZERO && fp32_classify(-0.0f) == FPC_ZERO);
    CHECK(fp32_classify(FLT_TRUE_MIN) == FPC_SUBNORMAL);
    CHECK(fp32_classify(FLT_MIN) == FPC_NORMAL);
    CHECK(fp32_classify(FLT_MIN / 2.0f) == FPC_SUBNORMAL);
    CHECK(fp32_classify(FLT_MAX) == FPC_NORMAL);
    CHECK(fp32_classify(-INFINITY) == FPC_INF);
    CHECK(fp32_classify(NAN) == FPC_NAN);
    CHECK(fp32_classify(bits_to_float(0x7F800001u)) == FPC_NAN);     /* signaling NaN */
    {   /* libm fpclassify 와 10000개 대조 + decode→encode 비트 왕복 */
        uint32_t s = 12345u; int agree = 0, round = 0;
        for (int k = 0; k < 10000; k++) {
            uint32_t u = xorshift32(&s);
            if (k % 4 == 0) u &= ~FP32_EXP_MASK;                     /* 서브노멀/0 도 섞는다 */
            if (k % 7 == 0) u |= FP32_EXP_MASK;                      /* inf/NaN 도 섞는다 */
            float f = bits_to_float(u);
            int lib = fpclassify(f);
            fp_class me = fp32_classify(f);
            agree += (lib == FP_ZERO && me == FPC_ZERO) || (lib == FP_SUBNORMAL && me == FPC_SUBNORMAL)
                  || (lib == FP_NORMAL && me == FPC_NORMAL) || (lib == FP_INFINITE && me == FPC_INF)
                  || (lib == FP_NAN && me == FPC_NAN);
            fp32_fields x = fp32_decode(f);
            round += float_bits(fp32_encode(x.sign, x.exp, x.frac)) == u;
        }
        CHECK(agree == 10000);
        CHECK(round == 10000);
    }

    /* Part C */
    {
        static uint8_t rx_dma[32];                       /* 선언된 타입: uint8_t[32] */
        static const uint8_t frame[IMU_FRAME_LEN] = {
            0xA5, 0x78, 0x56, 0x34, 0x12,                /* ts = 0x12345678 */
            0x00, 0x00, 0x80, 0x3F,                      /* ax = 1.0f       */
            0x00, 0x00, 0x20, 0xC0,                      /* ay = -2.5f      */
            0xC3, 0xF5, 0x1C, 0x41,                      /* az = 9.81f      */
            0x00 };
        uint8_t *f = rx_dma + 3;                         /* 홀수 주소에서 시작 → 필드는 전부 비정렬 */
        memcpy(f, frame, sizeof frame);
        uint8_t x = 0;
        for (unsigned i = 0; i < IMU_FRAME_LEN - 1; i++) x ^= f[i];
        f[IMU_FRAME_LEN - 1] = x;
        imu_sample s = { 0, 0, 0, 0 };
        CHECK(imu_parse(f, IMU_FRAME_LEN, &s) == true);
        CHECK(s.ts == 0x12345678u);
        CHECK(s.ax == 1.0f && s.ay == -2.5f && s.az == 9.81f);
        imu_sample keep = { 7, 7, 7, 7 };
        CHECK(imu_parse(f, IMU_FRAME_LEN - 1, &keep) == false && keep.ts == 7);   /* 짧음 */
        f[6] ^= 0x01;                                                             /* 1비트 손상 */
        CHECK(imu_parse(f, IMU_FRAME_LEN, &keep) == false && keep.ts == 7);
        f[6] ^= 0x01;
        f[0] = 0x5A; f[17] ^= 0xA5 ^ 0x5A;                                       /* sync 틀림 (체크섬은 맞춤) */
        CHECK(imu_parse(f, IMU_FRAME_LEN, &keep) == false);
        f[0] = 0xA5; f[17] ^= 0xA5 ^ 0x5A;
        store_u32(f + 9, 0x7FC00000u);                                           /* ay = NaN */
        x = 0;
        for (unsigned i = 0; i < IMU_FRAME_LEN - 1; i++) x ^= f[i];
        f[17] = x;
        CHECK(imu_parse(f, IMU_FRAME_LEN, &keep) == false && keep.ax == 7.0f);
    }
    CHECK(near(fast_rsqrt(1.0f), 1.0f, 2e-3f));
    CHECK(near(fast_rsqrt(4.0f), 0.5f, 2e-3f));
    CHECK(near(fast_rsqrt(0.01f), 10.0f, 2e-3f));
    {   /* [1e-3, 1e3) 에서 2000개: 상대 오차 < 0.2% (뉴턴 1회 이론 최대 ≈ 0.175%) */
        uint32_t s = 2024u; int ok = 0;
        for (int k = 0; k < 2000; k++) {
            float xx = 1e-3f + (float)(xorshift32(&s) % 1000000u) * 1e-3f;
            ok += near(fast_rsqrt(xx), 1.0f / sqrtf(xx), 2e-3f);
        }
        CHECK(ok == 2000);
    }
    {
        float v[3] = { 3.0f, 4.0f, 0.0f };
        CHECK(vec3_normalize(v) == true);
        CHECK(near(v[0], 0.6f, 2e-3f) && near(v[1], 0.8f, 2e-3f) && v[2] == 0.0f);
        float z[3] = { 0.0f, 0.0f, 0.0f };
        CHECK(vec3_normalize(z) == false && z[0] == 0.0f);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
