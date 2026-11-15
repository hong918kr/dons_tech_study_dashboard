/* ARCH-B01 추가 연습 — 한 번에 검증
 *
 *   연습 1  다른 부호 표현: 부호-크기(sign-magnitude) · 1의 보수 · 바이어스(excess-K) 와 2의 보수 비교
 *   연습 2  BCD: RTC 시각 레지스터 (0x00HHMMSS 형태) 읽고 쓰기 + 유효성 검사
 *   연습 3  Thumb 16비트 B (T2, imm11): PC+4 기준 오프셋 — demo_thumb_branch.s 의 실제 인코딩으로 검증
 *   연습 4  n비트 부호 포화(saturation) 와 Q15: +1.0 은 표현 못 한다
 *   연습 5  산술 시프트(ASR) vs 나눗셈: -7 >> 1 = -4, -7 / 2 = -3 — 패턴 위에서 이식 가능하게
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr && ./pr
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint64_t rng = 0x2545F4914F6CDD1Dull;
static uint64_t xs64(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return rng; }

/* 본문과 같은 도구 */
static int64_t u64_as_i64(uint64_t u) { return u <= (uint64_t)INT64_MAX ? (int64_t)u : -(int64_t)(~u) - 1; }
static uint64_t mask_n(unsigned n) { return n >= 64 ? UINT64_MAX : (UINT64_C(1) << n) - 1; }
static uint64_t trunc_n(uint64_t v, unsigned n) { return v & mask_n(n); }
static int64_t sign_extend(uint64_t v, unsigned bits) {
    uint64_t x = trunc_n(v, bits), sign = UINT64_C(1) << (bits - 1);
    return u64_as_i64((x ^ sign) - sign);
}
static int64_t smax_n(unsigned n) { return (int64_t)(mask_n(n) >> 1); }
static int64_t smin_n(unsigned n) { return -smax_n(n) - 1; }

/* ---------- 연습 1: 다른 부호 표현 (n = 2..63) ---------- */
static int64_t sm_decode(uint64_t v, unsigned n) {            /* 부호-크기: MSB = 부호, 나머지 = 크기 */
    uint64_t x = trunc_n(v, n), mag = x & mask_n(n - 1);
    return (x >> (n - 1)) ? -(int64_t)mag : (int64_t)mag;
}
static int64_t oc_decode(uint64_t v, unsigned n) {            /* 1의 보수: 음수 = 크기의 비트 반전 */
    uint64_t x = trunc_n(v, n);
    return (x >> (n - 1)) ? -(int64_t)trunc_n(~x, n) : (int64_t)x;
}
static int64_t bias_decode(uint64_t v, unsigned n, int64_t bias) { /* excess-K: 값 = 패턴 - K */
    return (int64_t)trunc_n(v, n) - bias;
}

/* ---------- 연습 2: BCD ---------- */
static bool bcd_valid(uint32_t b, unsigned digits) {
    for (unsigned i = 0; i < digits; i++) if (((b >> (4 * i)) & 0xFu) > 9u) return false;
    return true;
}
static uint32_t bcd_to_bin(uint32_t b, unsigned digits) {
    uint32_t r = 0;
    for (unsigned i = digits; i-- > 0;) r = r * 10u + ((b >> (4 * i)) & 0xFu);
    return r;
}
static uint32_t bin_to_bcd(uint32_t x, unsigned digits) {
    uint32_t r = 0;
    for (unsigned i = 0; i < digits; i++) { r |= (x % 10u) << (4 * i); x /= 10u; }
    return r;
}
/* 0x00HHMMSS (각 바이트가 2자리 BCD) → 초. 잘못된 BCD 나 범위 밖이면 -1 */
static int32_t rtc_to_seconds(uint32_t tr) {
    uint32_t hh = (tr >> 16) & 0xFFu, mm = (tr >> 8) & 0xFFu, ss = tr & 0xFFu;
    if (!bcd_valid(hh, 2) || !bcd_valid(mm, 2) || !bcd_valid(ss, 2)) return -1;
    uint32_t h = bcd_to_bin(hh, 2), m = bcd_to_bin(mm, 2), s = bcd_to_bin(ss, 2);
    if (h > 23 || m > 59 || s > 59) return -1;
    return (int32_t)(h * 3600u + m * 60u + s);
}
static uint32_t seconds_to_rtc(uint32_t sec) {
    return bin_to_bcd(sec / 3600u, 2) << 16 | bin_to_bcd(sec / 60u % 60u, 2) << 8 | bin_to_bcd(sec % 60u, 2);
}

/* ---------- 연습 3: Thumb B (T2) = 11100 imm11, 오프셋 = sext(imm11:'0'), target = pc + 4 + off ---------- */
static bool t2_b_decode(uint16_t insn, uint32_t pc, uint32_t *target) {
    if ((insn & 0xF800u) != 0xE000u) return false;
    int64_t off = sign_extend(insn, 11) * 2;
    *target = (uint32_t)((int64_t)pc + 4 + off);
    return true;
}
static bool t2_b_encode(uint32_t pc, uint32_t target, uint16_t *out) {
    int64_t off = (int64_t)target - ((int64_t)pc + 4);
    if (off % 2 != 0 || off / 2 < smin_n(11) || off / 2 > smax_n(11)) return false;  /* -2048..+2046 */
    *out = (uint16_t)(0xE000u | (uint32_t)trunc_n((uint64_t)(off / 2), 11));
    return true;
}

/* ---------- 연습 4: 포화 + Q15 ---------- */
static int64_t sat_s(int64_t x, unsigned n) {
    return x < smin_n(n) ? smin_n(n) : x > smax_n(n) ? smax_n(n) : x;
}
static uint16_t q15_from_milli(int32_t milli) {               /* -1000..1000 → Q15 패턴 (버림 후 포화) */
    int64_t q = (int64_t)milli * 32768 / 1000;
    return (uint16_t)trunc_n((uint64_t)sat_s(q, 16), 16);
}

/* ---------- 연습 5: n비트 패턴 위의 ASR 과 0 쪽으로 자르는 나눗셈 ---------- */
static uint64_t asr_n(uint64_t v, unsigned n, unsigned k) {   /* k < n */
    uint64_t x = trunc_n(v, n);
    bool neg = (x >> (n - 1)) & 1u;
    uint64_t r = neg ? ~(trunc_n(~x, n) >> k) : x >> k;       /* 음수: n비트 안에서 반전 → 논리 시프트 → 반전 */
    return trunc_n(r, n);
}
static int64_t div_pow2(int64_t x, unsigned k) {              /* x / 2^k, C 의 '/' 처럼 0 쪽으로. k = 0..62 */
    uint64_t u = (uint64_t)x;
    if (x < 0) u += mask_n(k);                                /* 음수면 2^k - 1 을 더하고 내림 → 올림 효과 */
    return sign_extend(asr_n(u, 64, k), 64);
}

int main(void) {
    /* 연습 1 */
    CHECK(sm_decode(0x80, 8) == 0 && oc_decode(0x80, 8) == -127 && sign_extend(0x80, 8) == -128 && bias_decode(0x80, 8, 128) == 0);
    CHECK(sm_decode(0xFF, 8) == -127 && oc_decode(0xFF, 8) == 0 && sign_extend(0xFF, 8) == -1 && bias_decode(0xFF, 8, 128) == 127);
    CHECK(sm_decode(0x00, 8) == 0 && oc_decode(0x00, 8) == 0);           /* 0 이 두 개: +0 과 -0 */
    CHECK(bias_decode(0x7F, 8, 127) == 0 && bias_decode(0x01, 8, 127) == -126);  /* float32 지수 바이어스 (ARCH-B03) */
    {
        int bad = 0;   /* excess-2^(n-1) == MSB 를 뒤집은 2의 보수 */
        for (unsigned n = 2; n <= 12; n++)
            for (uint64_t v = 0; v <= mask_n(n); v++)
                if (bias_decode(v, n, (int64_t)1 << (n - 1)) != sign_extend(v ^ (UINT64_C(1) << (n - 1)), n)) bad++;
        CHECK(bad == 0);
    }

    /* 연습 2 */
    CHECK(bcd_to_bin(0x59, 2) == 59 && bin_to_bcd(59, 2) == 0x59 && !bcd_valid(0x5A, 2));
    CHECK(rtc_to_seconds(0x00235959u) == 86399 && rtc_to_seconds(0x00000000u) == 0);
    CHECK(rtc_to_seconds(0x00240000u) == -1 && rtc_to_seconds(0x00126A00u) == -1);
    {
        int bad = 0;
        for (uint32_t s = 0; s < 86400; s++) if (rtc_to_seconds(seconds_to_rtc(s)) != (int32_t)s) bad++;
        CHECK(bad == 0);
    }

    /* 연습 3: demo_thumb_branch.s → llvm-objdump 실제 결과 (pc, target, word) */
    {
        static const struct { uint32_t pc, tgt; uint16_t w; } real[] = {
            {0x2, 0x0, 0xE7FD}, {0x4, 0xA, 0xE001}, {0xA, 0xA, 0xE7FE},
        };
        int bad = 0;
        for (int i = 0; i < 3; i++) {
            uint32_t t; uint16_t w;
            if (!t2_b_decode(real[i].w, real[i].pc, &t) || t != real[i].tgt) bad++;
            if (!t2_b_encode(real[i].pc, real[i].tgt, &w) || w != real[i].w) bad++;
        }
        CHECK(bad == 0);
        uint16_t w; uint32_t t;
        CHECK(t2_b_encode(0x1000, 0x1000 + 4 + 2046, &w) && w == 0xE3FF);
        CHECK(!t2_b_encode(0x1000, 0x1000 + 4 + 2048, &w));
        CHECK(t2_b_encode(0x1000, 0x1000 + 4 - 2048, &w) && w == 0xE400);
        CHECK(!t2_b_encode(0x1000, 0x1000 + 4 - 2050, &w) && !t2_b_encode(0x1000, 0x1005, &w));
        CHECK(!t2_b_decode(0xBF00, 0, &t));                                   /* nop */
    }

    /* 연습 4 */
    CHECK(sat_s(200, 8) == 127 && sat_s(-200, 8) == -128 && sat_s(-5, 8) == -5);
    CHECK(sat_s(INT64_MAX, 64) == INT64_MAX && sat_s(1, 1) == 0 && sat_s(-7, 1) == -1);
    CHECK(q15_from_milli(500) == 0x4000 && q15_from_milli(-500) == 0xC000);
    CHECK(q15_from_milli(1000) == 0x7FFF && q15_from_milli(-1000) == 0x8000);   /* +1.0 → 포화 */

    /* 연습 5 */
    CHECK(asr_n(0xF9, 8, 1) == 0xFC && sign_extend(asr_n(0xF9, 8, 1), 8) == -4);   /* -7 >> 1 = -4 */
    CHECK(div_pow2(-7, 1) == -3 && div_pow2(7, 1) == 3 && div_pow2(-8, 3) == -1 && div_pow2(-1, 4) == 0);
    {
        int bad = 0;
        for (int i = 0; i < 100000; i++) {
            int64_t x = u64_as_i64(xs64());
            unsigned k = (unsigned)(xs64() % 63);
            if (div_pow2(x, k) != x / ((int64_t)1 << k)) bad++;
            /* 오라클: clang 의 음수 >> 는 산술 시프트 (구현 정의, 문서화됨) */
            if (sign_extend(asr_n((uint64_t)x, 64, k), 64) != (x >> k)) bad++;
        }
        CHECK(bad == 0);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
