/* ARCH-B03 IEEE 754 부동소수점 분해기
 * float32 / FP16(binary16) 분해·조립, float -> FP16 (round-to-nearest-even), FP16 -> float (정확한 확장),
 * 그리고 센서 텐서를 FP16 DMA 버퍼로 포장하는 리포트.
 * 하드웨어 기준(테스트 전용): Apple clang arm64 의 _Float16 캐스트 = FCVT 명령.
 * NaN 정책: 부호 유지, 가수 상위 비트(payload) 유지, quiet 비트를 켠다 (= Arm FCVT, FPCR.DN=0 일 때와 같음).
 */
#include <float.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---------------- 공통 도구 (TODO 아님) ---------------- */
static uint32_t f2u(float f)    { uint32_t u; memcpy(&u, &f, 4); return u; }
static float    u2f(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }

typedef struct { uint32_t sign, exp, frac; } fparts;      /* 원시 필드 (바이어스된 지수 그대로) */
enum fclass { FC_ZERO, FC_SUBNORMAL, FC_NORMAL, FC_INF, FC_QNAN, FC_SNAN };

/* ---------------- Part A: 분해 / 조립 / 분류 / 정확한 값 ---------------- */
/* float32: [31]=sign [30:23]=exp(bias 127) [22:0]=frac */
static fparts f32_split(uint32_t b) {
    fparts p = { 0, 0, 0 };
    (void)b; /* TODO: sign / exp / frac 필드를 꺼낸다 */
    return p;
}
static uint32_t f32_join(fparts p) {
    (void)p;
    return 0; /* TODO: 필드를 제자리로 밀어 OR (넘치는 값은 마스크) */
}
/* FP16: [15]=sign [14:10]=exp(bias 15) [9:0]=frac */
static fparts f16_split(uint16_t h) {
    fparts p = { 0, 0, 0 };
    (void)h; /* TODO: 1 / 5 / 10 비트 */
    return p;
}
static uint16_t f16_join(fparts p) {
    (void)p;
    return 0; /* TODO */
}
static enum fclass f32_classify(uint32_t b) {
    (void)b;
    return FC_NORMAL; /* TODO: exp==0xFF / exp==0 / 나머지, NaN 은 frac 최상위 비트로 quiet/signaling */
}
static enum fclass f16_classify(uint16_t h) {
    (void)h;
    return FC_NORMAL; /* TODO */
}
/* 유한한 float 의 정확한 값 = m * 2^e (m 은 정수, 0 이 아니면 홀수로 정규화. 0 은 m=0,e=0) */
static void f32_exact(uint32_t b, int64_t *m, int *e) {
    (void)b; (void)f32_split;
    *m = 0; *e = 0; /* TODO: 숨은 1 / 비정규 지수 -149 / 끝의 0 비트 제거 / 부호 */
}
/* FP16 -> float32: 항상 정확하다 (모든 FP16 값은 float 로 표현 가능). 비정규 FP16 은 float 에선 정규수. */
static uint32_t f16_to_f32(uint16_t h) {
    (void)h;
    return 0; /* TODO: Inf·NaN(quiet) / ±0 / 비정규 정규화 / 정규(바이어스 +112) */
}

/* ---------------- Part B: float32 -> FP16, round-to-nearest-even ---------------- */
static uint16_t f32_to_f16(uint32_t b) {
    (void)b;
    return 0; /* TODO: 특수값 → 범위 → 남길 비트 수(shift) → RNE → 지수와 합치기 */
}

/* ---------------- Part C: 센서 텐서 → FP16 DMA 버퍼 포장 + 리포트 ---------------- */
typedef struct {
    unsigned exact;      /* 다시 float 로 펴면 입력과 비트가 같음 (±0, ±Inf 포함) */
    unsigned inexact;    /* 유한 → 유한, 반올림 발생 (0 으로 떨어진 것은 제외) */
    unsigned underflow;  /* 0 이 아닌 유한 입력이 ±0 이 됨 */
    unsigned overflow;   /* 유한 입력이 FP16 범위를 넘음 (Inf 또는 포화) */
    unsigned nan;        /* NaN 입력 */
    unsigned subnormal;  /* 결과가 FP16 비정규수 (위 분류와 겹쳐 셈) */
} pack_report;

/* saturate != 0 이면 유한 overflow 를 ±65504(0x7BFF) 로 포화. Inf/NaN 입력은 그대로 Inf/NaN. */
static void f16_pack(const float *in, uint16_t *out, unsigned n, int saturate, pack_report *r) {
    memset(r, 0, sizeof *r);
    for (unsigned i = 0; i < n; i++) out[i] = 0;
    (void)in; (void)saturate; /* TODO: 변환 + 분류 카운트 + 포화 옵션 */
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

/* 하드웨어 기준: _Float16 캐스트 → arm64 FCVT (테스트에서만 사용) */
static uint16_t hw_f16(uint32_t b) { _Float16 h = (_Float16)u2f(b); uint16_t u; memcpy(&u, &h, 2); return u; }
static uint32_t hw_f32(uint16_t h) { _Float16 x; memcpy(&x, &h, 2); return f2u((float)x); }

static uint32_t rng = 0x2026B03u;                          /* xorshift32, 고정 시드 */
static uint32_t xs32(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

static int same_parts(fparts p, uint32_t s, uint32_t e, uint32_t f) { return p.sign == s && p.exp == e && p.frac == f; }

int main(void) {
    /* ---- Part A ---- */
    CHECK(f2u(0.1f) == 0x3DCCCCCDu);                                   /* 사실 확인 (하네스) */
    CHECK(same_parts(f32_split(0x3DCCCCCDu), 0, 123, 0x4CCCCDu));      /* 0.1f */
    CHECK(same_parts(f32_split(f2u(-2.5f)), 1, 128, 0x200000u));       /* -1.01b * 2^1 */
    CHECK(same_parts(f32_split(0x00000001u), 0, 0, 1));                /* 최소 비정규 2^-149 */
    CHECK(f32_join(f32_split(0xC0490FDBu)) == 0xC0490FDBu);            /* -pi */
    {   unsigned bad = 0;
        for (int i = 0; i < 100000; i++) { uint32_t b = xs32(); if (f32_join(f32_split(b)) != b) bad++; }
        CHECK(bad == 0); }
    CHECK(same_parts(f16_split(0x7BFFu), 0, 30, 0x3FFu));              /* 65504 */
    CHECK(same_parts(f16_split(0xBC00u), 1, 15, 0));                   /* -1.0 */
    CHECK(f16_join(f16_split(0x2E66u)) == 0x2E66u);
    CHECK(f32_classify(0x00000000u) == FC_ZERO && f32_classify(0x80000000u) == FC_ZERO);
    CHECK(f32_classify(0x00000001u) == FC_SUBNORMAL);                  /* FLT_TRUE_MIN */
    CHECK(f32_classify(f2u(FLT_MIN)) == FC_NORMAL);                    /* 0x00800000 */
    CHECK(f32_classify(0x7F800000u) == FC_INF && f32_classify(0xFF800000u) == FC_INF);
    CHECK(f32_classify(0x7FC00000u) == FC_QNAN && f32_classify(0x7F800001u) == FC_SNAN);
    CHECK(f16_classify(0x0001u) == FC_SUBNORMAL && f16_classify(0x0400u) == FC_NORMAL);
    CHECK(f16_classify(0x7C00u) == FC_INF && f16_classify(0x7E00u) == FC_QNAN && f16_classify(0x7C01u) == FC_SNAN);
    {   int64_t m; int e;
        f32_exact(f2u(0.1f), &m, &e);   CHECK(m == 13421773 && e == -27);
        CHECK(m * 10 > ((int64_t)1 << 27));            /* 0.1f 는 0.1 보다 크다: m*10 > 2^27 */
        f32_exact(f2u(1.0f), &m, &e);   CHECK(m == 1 && e == 0);
        f32_exact(0x00000001u, &m, &e); CHECK(m == 1 && e == -149);
        f32_exact(f2u(-65504.0f), &m, &e); CHECK(m == -2047 && e == 5);
        f32_exact(0x80000000u, &m, &e); CHECK(m == 0 && e == 0); }
    CHECK(f16_to_f32(0x0001u) == f2u(0x1p-24f));                       /* FP16 최소 비정규 */
    CHECK(f16_to_f32(0x03FFu) == f2u(1023.0f * 0x1p-24f));             /* FP16 최대 비정규 */
    CHECK(f16_to_f32(0x7BFFu) == f2u(65504.0f));
    {   unsigned bad = 0;                                              /* 65536 개 전수: HW 와 비트 일치 */
        for (uint32_t h = 0; h < 0x10000u; h++) if (f16_to_f32((uint16_t)h) != hw_f32((uint16_t)h)) bad++;
        CHECK(bad == 0); }

    /* ---- Part B ---- */
    CHECK(f32_to_f16(f2u(0.1f)) == 0x2E66u);                           /* 0.0999755859375 */
    CHECK(f32_to_f16(f2u(1.0f)) == 0x3C00u && f32_to_f16(f2u(-2.0f)) == 0xC000u);
    CHECK(f32_to_f16(f2u(65504.0f)) == 0x7BFFu);
    CHECK(f32_to_f16(0x477FEFFFu) == 0x7BFFu);                         /* 65519.996 → 65504 */
    CHECK(f32_to_f16(f2u(65520.0f)) == 0x7C00u);                       /* 정확히 반 → 짝수(=Inf 쪽) */
    CHECK(f32_to_f16(f2u(1e5f)) == 0x7C00u && f32_to_f16(0xFF800000u) == 0xFC00u);
    CHECK(f32_to_f16(0x3F801000u) == 0x3C00u);                         /* 1+2^-11: 동점 → 짝수(내림) */
    CHECK(f32_to_f16(0x3F803000u) == 0x3C02u);                         /* 1+3*2^-11: 동점 → 짝수(올림) */
    CHECK(f32_to_f16(0x3F801001u) == 0x3C01u);                         /* 동점보다 1ulp 위 → 올림 */
    CHECK(f32_to_f16(f2u(0x1p-24f)) == 0x0001u);                       /* 최소 비정규 */
    CHECK(f32_to_f16(f2u(0x1p-25f)) == 0x0000u);                       /* 0.5 * 최소 비정규: 동점 → 0 */
    CHECK(f32_to_f16(0x33000001u) == 0x0001u);                         /* 반보다 조금 크면 올림 */
    CHECK(f32_to_f16(f2u(0x1.8p-24f)) == 0x0002u);                     /* 1.5 * 최소 비정규 → 2 (짝수) */
    CHECK(f32_to_f16(f2u(1023.0f * 0x1p-24f)) == 0x03FFu);
    CHECK(f32_to_f16(0x387FFFFFu) == 0x0400u);                         /* 2^-14 바로 아래 → 최소 정규로 올림 */
    CHECK(f32_to_f16(f2u(FLT_MIN)) == 0x0000u && f32_to_f16(0x80000001u) == 0x8000u);
    CHECK(f32_to_f16(0x7FC00000u) == 0x7E00u);                         /* qNaN */
    CHECK(f32_to_f16(0x7F800001u) == 0x7E00u);                         /* sNaN, payload 하위만 → quiet 비트 덕에 NaN 유지 */
    CHECK(f32_to_f16(0x7FA00000u) == 0x7F00u);                         /* sNaN payload 상위 비트 보존 */
    CHECK(f32_to_f16(0xFFFFFFFFu) == 0xFFFFu);
    {   unsigned bad = 0;                                              /* 무작위 비트 패턴 */
        for (int i = 0; i < 200000; i++) { uint32_t b = xs32(); if (f32_to_f16(b) != hw_f16(b)) bad++; }
        CHECK(bad == 0); }
    {   unsigned bad = 0;                                              /* 지수 100..145 (비정규~overflow 경계) 집중 */
        for (int i = 0; i < 200000; i++) {
            uint32_t r = xs32(), b = (r & 0x807FFFFFu) | ((100u + (r >> 23) % 46u) << 23);
            if (f32_to_f16(b) != hw_f16(b)) bad++; }
        CHECK(bad == 0); }
    {   unsigned bad = 0;                                              /* 모든 FP16 값과 그 사이 동점들 */
        for (uint32_t h = 0; h < 0x7C00u; h++) {
            uint32_t b = f16_to_f32((uint16_t)h), tie = b + (h >= 0x400u ? 0x1000u : 0);
            if (f32_to_f16(b) != h) bad++;
            if (h >= 0x400u && f32_to_f16(tie) != hw_f16(tie)) bad++; }
        CHECK(bad == 0); }

    /* ---- Part C ---- */
    {   const float nanv = u2f(0x7FC00000u);
        const float in[12] = { 9.80665f, -9.80665f, 0.0f, -0.0f, 0.5f, 2048.0f, 2049.0f,
                               1e-6f, 1e-9f, 70000.0f, nanv, -1e6f };
        uint16_t out[12];
        pack_report r;
        f16_pack(in, out, 12, 0, &r);
        CHECK(out[0] == 0x48E7u && out[1] == 0xC8E7u);                 /* 9.80665 → 9.8046875 */
        CHECK(out[6] == 0x6800u);                                      /* 2049 → 2048 (FP16 정수 한계) */
        CHECK(out[7] == 0x0011u);                                      /* 1e-6 → 비정규 17 * 2^-24 */
        CHECK(out[8] == 0x0000u && out[9] == 0x7C00u && out[10] == 0x7E00u && out[11] == 0xFC00u);
        CHECK(r.exact == 4 && r.inexact == 4 && r.underflow == 1 && r.overflow == 2 && r.nan == 1);
        CHECK(r.subnormal == 1);
        f16_pack(in, out, 12, 1, &r);
        CHECK(out[9] == 0x7BFFu && out[11] == 0xFBFFu && out[10] == 0x7E00u);
        CHECK(r.overflow == 2);
        const float inf_in[2] = { u2f(0x7F800000u), u2f(0xFF800000u) };
        f16_pack(inf_in, out, 2, 1, &r);                               /* Inf 입력은 포화 대상 아님 */
        CHECK(out[0] == 0x7C00u && out[1] == 0xFC00u && r.exact == 2 && r.overflow == 0);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
