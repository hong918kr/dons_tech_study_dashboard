/* ARCH-B01 수 체계: 2진·16진·2의 보수 변환기
 *
 *   Part A  n비트(1..64) 기본기: mask_n · trunc_n · sign_extend · to_str (2/10/16진, 무부호/부호 보기)
 *   Part B  경계: umax_n · smin_n · smax_n · fits_s · fits_u · neg_n · resize,
 *           n = 1..64 경계값(최소/최대/-1) 표 + 손계산 10문제
 *   Part C  펌웨어: 12비트 온도 레지스터 · 5비트 부호 트림 필드 · AArch64 B/BL imm26 · 디버그 셸 숫자 파서
 *
 * 규칙: 모든 비트 조작은 uint64_t 위에서 한다 (signed 시프트·오버플로 UB 회피).
 *       n = 64 에서 1ull << 64 를 만들면 안 된다 (C11 6.5.7p3: 시프트 양 >= 폭 → UB).
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o st && ./st    (정답: solution.c)
 */
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define TO_STR_MAX 68   /* "-0b" + 64자리 + NUL = 68 */

/* 비트 패턴(uint64) → int64 값. (int64_t)u 는 u > INT64_MAX 일 때 구현 정의(C11 6.3.1.3p3)라서
 * 이식 가능한 형태로 쓴다. u > INT64_MAX 이면 ~u <= INT64_MAX 라 변환·부정 모두 안전. (주어짐) */
static int64_t u64_as_i64(uint64_t u) {
    return u <= (uint64_t)INT64_MAX ? (int64_t)u : -(int64_t)(~u) - 1;
}

/* ======================= Part A — n비트 기본기 ======================= */
/* 하위 n 비트가 1인 마스크. n = 1..64. n = 64 → 0xFFFF_FFFF_FFFF_FFFF (1ull << 64 금지) */
static uint64_t mask_n(unsigned n) {
    return n >= 64 ? UINT64_MAX : (UINT64_C(1) << n) - 1;
}

/* v 의 하위 n 비트만 남긴다 (n비트 레지스터에 쓰면 일어나는 일) */
static uint64_t trunc_n(uint64_t v, unsigned n) {
    return v & mask_n(n);
}

/* v 의 하위 bits 비트를 bits비트 2의 보수로 읽어 int64 로. 상위 비트는 무시. bits = 1..64 */
static int64_t sign_extend(uint64_t v, unsigned bits) {
    uint64_t x = trunc_n(v, bits);
    uint64_t sign = UINT64_C(1) << (bits - 1);
    return u64_as_i64((x ^ sign) - sign);     /* 부호 비트를 뒤집고 빼면 위쪽이 모두 부호로 채워진다 */
}

/* v 의 하위 n 비트를 base(2/10/16) 문자열로.
 *   is_signed == false : 무부호 보기 = 비트 패턴. 2진 "0b"+n자리, 16진 "0x"+ceil(n/4)자리(대문자), 10진은 패딩 없음
 *   is_signed == true  : 부호 보기 = 값. "-" + 크기, 2/16진도 패딩 없이 최소 자리 ("-0x80", "0x7F", "0x0")
 * 반환: 쓴 길이(NUL 제외). n/base 잘못 → -1, cap 부족 → -1 (cap > 0 이면 buf[0] = '\0') */
static int to_str(uint64_t v, unsigned n, unsigned base, bool is_signed, char *buf, size_t cap) {
    if (cap > 0) buf[0] = '\0';
    if (n < 1 || n > 64 || (base != 2 && base != 10 && base != 16)) return -1;
    uint64_t x = trunc_n(v, n), mag = x;
    bool neg = false;
    if (is_signed) {
        int64_t s = sign_extend(x, n);
        neg = s < 0;
        mag = neg ? (uint64_t)0 - (uint64_t)s : (uint64_t)s;   /* -INT64_MIN 을 int 로 하지 않는다 */
    }
    unsigned width = 1;                                         /* 최소 자리수 */
    if (!is_signed && base == 2) width = n;
    if (!is_signed && base == 16) width = (n + 3) / 4;
    char tmp[TO_STR_MAX];
    unsigned k = 0;
    do { tmp[k++] = "0123456789ABCDEF"[mag % base]; mag /= base; } while (mag != 0);
    while (k < width) tmp[k++] = '0';
    const char *pre = base == 2 ? "0b" : base == 16 ? "0x" : "";
    size_t len = (neg ? 1 : 0) + strlen(pre) + k;
    if (len + 1 > cap) return -1;
    size_t o = 0;
    if (neg) buf[o++] = '-';
    for (const char *p = pre; *p; p++) buf[o++] = *p;
    while (k > 0) buf[o++] = tmp[--k];
    buf[o] = '\0';
    return (int)o;
}

/* ======================= Part B — 경계와 폭 바꾸기 ======================= */
static uint64_t umax_n(unsigned n) { return mask_n(n); }                 /* 2^n - 1          */
static int64_t  smax_n(unsigned n) { return (int64_t)(mask_n(n) >> 1); } /* 2^(n-1) - 1      */
static int64_t  smin_n(unsigned n) { return -smax_n(n) - 1; }            /* -2^(n-1)         */

/* x 가 n비트 부호/무부호 범위에 들어가나 */
static bool fits_s(int64_t x, unsigned n)  { return x >= smin_n(n) && x <= smax_n(n); }
static bool fits_u(uint64_t x, unsigned n) { return x <= umax_n(n); }

/* n비트 안에서 2의 보수 부정 (-x). 결과도 n비트 패턴. neg_n(0x80, 8) == 0x80 */
static uint64_t neg_n(uint64_t v, unsigned n) {
    return trunc_n(~v + 1, n);
}

/* from 비트 패턴을 to 비트 패턴으로: 넓히면 부호(또는 0) 확장, 좁히면 잘라냄. from,to = 1..64 */
static uint64_t resize(uint64_t v, unsigned from, unsigned to, bool is_signed) {
    uint64_t wide = is_signed ? (uint64_t)sign_extend(v, from) : trunc_n(v, from);
    return trunc_n(wide, to);
}

/* ======================= Part C — 펌웨어 현실 ======================= */
/* (1) 12비트 2의 보수 온도, 16비트 레지스터의 [15:4] 에 왼쪽 정렬, 1 LSB = 0.0625 °C → µ°C 로 */
static int32_t temp_raw_to_udeg(uint16_t reg) {
    return (int32_t)sign_extend((uint64_t)(reg >> 4), 12) * 62500;
}

/* (2) 가상의 CAL 레지스터: [12:8] = 5비트 부호 있는 트림(-16..15). 나머지 비트 보존 */
#define TRIM_SHIFT 8u
#define TRIM_BITS  5u
static int32_t trim_get(uint32_t reg) {
    return (int32_t)sign_extend((uint64_t)(reg >> TRIM_SHIFT), TRIM_BITS);
}
static uint32_t trim_set(uint32_t reg, int32_t val, bool *ok) {
    *ok = fits_s(val, TRIM_BITS);
    if (!*ok) return reg;                                  /* 범위 밖 → 레지스터 그대로, 에러 보고 */
    uint32_t m = (uint32_t)mask_n(TRIM_BITS) << TRIM_SHIFT;
    uint32_t f = (uint32_t)trunc_n((uint64_t)(int64_t)val, TRIM_BITS) << TRIM_SHIFT;
    return (reg & ~m) | f;
}

/* (3) AArch64 B / BL: [31]=link, [30:26]=00101, [25:0]=imm26, 바이트 오프셋 = sext(imm26) * 4 (PC 기준) */
static bool a64_b_decode(uint32_t insn, int64_t *off, bool *link) {
    if ((insn & 0x7C000000u) != 0x14000000u) return false;
    *link = (insn >> 31) != 0;
    *off = sign_extend(insn, 26) * 4;                      /* << 2 는 음수 왼쪽 시프트 = UB → * 4 */
    return true;
}
static bool a64_b_encode(uint64_t pc, uint64_t target, bool link, uint32_t *out) {
    int64_t off = u64_as_i64(target - pc);
    if (off % 4 != 0 || !fits_s(off / 4, 26)) return false;  /* 정렬 + ±128 MiB */
    *out = (link ? 0x94000000u : 0x14000000u) | (uint32_t)trunc_n((uint64_t)(off / 4), 26);
    return true;
}

/* (4) 디버그 셸 숫자 파서: [-](0x|0X|0b|0B)?digits. 결과는 n비트 패턴.
 *     '-' 없으면 무부호 n비트에 들어가야 하고(0xFF, 255 → 8비트 OK), '-' 있으면 부호 n비트에(-128 OK, -129 X).
 *     빈 자릿수, 잘못된 문자, 64비트 넘침 → false */
static bool parse_num(const char *s, unsigned n, uint64_t *out) {
    if (n < 1 || n > 64) return false;
    bool neg = false;
    if (*s == '-') { neg = true; s++; }
    unsigned base = 10;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) { base = 16; s += 2; }
    else if (s[0] == '0' && (s[1] == 'b' || s[1] == 'B')) { base = 2; s += 2; }
    if (*s == '\0') return false;
    uint64_t acc = 0;
    for (; *s; s++) {
        unsigned d;
        if (*s >= '0' && *s <= '9') d = (unsigned)(*s - '0');
        else if (*s >= 'a' && *s <= 'f') d = (unsigned)(*s - 'a' + 10);
        else if (*s >= 'A' && *s <= 'F') d = (unsigned)(*s - 'A' + 10);
        else return false;
        if (d >= base) return false;
        if (acc > (UINT64_MAX - d) / base) return false;  /* acc*base + d 가 64비트를 넘기 전에 막는다 */
        acc = acc * base + d;
    }
    if (!neg) {
        if (!fits_u(acc, n)) return false;
        *out = acc;
    } else {
        if (acc > (uint64_t)smax_n(n) + 1) return false;  /* 크기 <= 2^(n-1) */
        *out = trunc_n((uint64_t)0 - acc, n);
    }
    return true;
}

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint64_t rng = 0x9E3779B97F4A7C15ull;          /* xorshift64, 고정 시드 → 매번 같은 수열 */
static uint64_t xs64(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return rng; }

static bool str_is(uint64_t v, unsigned n, unsigned base, bool sg, const char *want) {
    char b[TO_STR_MAX];
    int len = to_str(v, n, base, sg, b, sizeof b);
    return len == (int)strlen(want) && strcmp(b, want) == 0;
}

/* 오라클: 시프트 트릭. (int64_t) 변환과 음수 >> 는 구현 정의지만 clang/gcc 는 모듈러·산술 시프트로 문서화 → 테스트에서만 사용 */
static int64_t oracle_sext(uint64_t v, unsigned n) {
    return (int64_t)(v << (64 - n)) >> (64 - n);
}

int main(void) {
    char b[TO_STR_MAX];

    /* ---------- Part A ---------- */
    CHECK(mask_n(1) == 0x1u);
    CHECK(mask_n(8) == 0xFFu);
    CHECK(mask_n(63) == 0x7FFFFFFFFFFFFFFFull);
    CHECK(mask_n(64) == UINT64_MAX);
    CHECK(trunc_n(0x1FFu, 8) == 0xFFu);
    CHECK(sign_extend(0x80u, 8) == -128);
    CHECK(sign_extend(0x7Fu, 8) == 127);
    CHECK(sign_extend(0xFFFu, 12) == -1);
    CHECK(sign_extend(0x1FFu, 8) == -1);                      /* 상위 비트(bit 8)는 무시 */
    CHECK(sign_extend(1u, 1) == -1 && sign_extend(0u, 1) == 0);
    CHECK(sign_extend(0x8000000000000000ull, 64) == INT64_MIN);
    CHECK(sign_extend(UINT64_MAX, 64) == -1);
    CHECK(str_is(0x80, 8, 16, false, "0x80"));
    CHECK(str_is(0x80, 8, 16, true, "-0x80"));
    CHECK(str_is(0x80, 8, 10, false, "128"));
    CHECK(str_is(0x80, 8, 10, true, "-128"));
    CHECK(str_is(0x80, 8, 2, false, "0b10000000"));
    CHECK(str_is(5, 4, 2, false, "0b0101"));
    CHECK(str_is(3, 5, 16, false, "0x03"));                   /* ceil(5/4) = 2자리 */
    CHECK(str_is(UINT64_MAX, 12, 16, false, "0xFFF"));
    CHECK(str_is(UINT64_MAX, 12, 10, true, "-1"));
    CHECK(str_is(0, 7, 16, true, "0x0"));
    CHECK(str_is(0x8000000000000000ull, 64, 10, true, "-9223372036854775808"));
    CHECK(str_is(UINT64_MAX, 64, 10, false, "18446744073709551615"));
    CHECK(to_str(0x8000000000000000ull, 64, 2, true, b, sizeof b) == 67);  /* "-0b1" + 63개 0 */
    CHECK(to_str(0x80, 8, 16, false, b, 4) == -1 && b[0] == '\0');         /* "0x80" + NUL = 5 > 4 */
    CHECK(to_str(1, 0, 10, false, b, sizeof b) == -1);
    CHECK(to_str(1, 65, 10, false, b, sizeof b) == -1);
    CHECK(to_str(1, 8, 8, false, b, sizeof b) == -1);
    {   /* 무작위 20000 개: 10진·16진은 printf, 2진은 비트 하나씩, sign_extend 는 시프트 트릭 오라클과 */
        int bad = 0;
        for (int i = 0; i < 20000; i++) {
            uint64_t v = xs64();
            unsigned n = (unsigned)(xs64() % 64) + 1;
            uint64_t x = v & (n == 64 ? UINT64_MAX : (UINT64_C(1) << n) - 1);
            char want[TO_STR_MAX];
            if (sign_extend(v, n) != oracle_sext(v, n)) bad++;
            snprintf(want, sizeof want, "%" PRIu64, x);
            if (!str_is(v, n, 10, false, want)) bad++;
            snprintf(want, sizeof want, "%" PRId64, oracle_sext(v, n));
            if (!str_is(v, n, 10, true, want)) bad++;
            snprintf(want, sizeof want, "0x%0*" PRIX64, (int)((n + 3) / 4), x);
            if (!str_is(v, n, 16, false, want)) bad++;
            if (to_str(v, n, 2, false, b, sizeof b) != (int)n + 2) bad++;
            else for (unsigned k = 0; k < n; k++)
                if (b[2 + k] != (char)('0' + ((x >> (n - 1 - k)) & 1))) { bad++; break; }
        }
        CHECK(bad == 0);
    }

    /* ---------- Part B ---------- */
    CHECK(smin_n(1) == -1 && smax_n(1) == 0 && umax_n(1) == 1);
    CHECK(smin_n(8) == -128 && smax_n(8) == 127 && umax_n(8) == 255);
    CHECK(smin_n(64) == INT64_MIN && smax_n(64) == INT64_MAX && umax_n(64) == UINT64_MAX);
    {   /* n = 1..64 경계값 표: 최소/최대/-1 */
        int bad = 0;
        for (unsigned n = 1; n <= 64; n++) {
            uint64_t um = n == 64 ? UINT64_MAX : (UINT64_C(1) << n) - 1;
            int64_t sM = n == 64 ? INT64_MAX : (INT64_C(1) << (n - 1)) - 1;
            int64_t sm = -sM - 1;
            if (umax_n(n) != um || smax_n(n) != sM || smin_n(n) != sm) bad++;
            if (sign_extend(um, n) != -1) bad++;                                 /* 모두 1 = -1 */
            if (sign_extend((uint64_t)sm, n) != sm) bad++;                       /* 1000..0 = 최소 */
            if (sign_extend((uint64_t)sM, n) != sM) bad++;                       /* 0111..1 = 최대 */
            if (!fits_s(sm, n) || !fits_s(sM, n) || !fits_u(um, n)) bad++;
            if (n < 64 && (fits_s(sm - 1, n) || fits_s(sM + 1, n) || fits_u(um + 1, n))) bad++;
            if (neg_n((uint64_t)sm, n) != trunc_n((uint64_t)sm, n)) bad++;       /* -최소 = 최소 */
            if (resize(um, n, 64, true) != UINT64_MAX || resize(um, n, 64, false) != um) bad++;
        }
        CHECK(bad == 0);
    }
    printf("  n | umax                 | smin                 | smax                 | -1 (16진 패턴)\n");
    static const unsigned show[] = {1, 4, 8, 12, 16, 32, 64};
    for (size_t i = 0; i < sizeof show / sizeof show[0]; i++) {
        unsigned n = show[i];
        char u[TO_STR_MAX], lo[TO_STR_MAX], hi[TO_STR_MAX], m1[TO_STR_MAX];
        to_str(umax_n(n), n, 10, false, u, sizeof u);
        to_str((uint64_t)smin_n(n), n, 10, true, lo, sizeof lo);
        to_str((uint64_t)smax_n(n), n, 10, true, hi, sizeof hi);
        to_str(UINT64_MAX, n, 16, false, m1, sizeof m1);
        printf(" %2u | %-20s | %-20s | %-20s | %s\n", n, u, lo, hi, m1);
    }
    CHECK(fits_s(-128, 8) && !fits_s(-129, 8) && fits_s(127, 8) && !fits_s(128, 8));
    CHECK(fits_s(-1, 1) && !fits_s(1, 1) && fits_s(INT64_MIN, 64));
    CHECK(fits_u(255, 8) && !fits_u(256, 8) && fits_u(UINT64_MAX, 64));
    CHECK(neg_n(0x80, 8) == 0x80 && neg_n(1, 8) == 0xFF && neg_n(0, 8) == 0 && neg_n(1, 64) == UINT64_MAX);
    CHECK(resize(0x80, 8, 16, true) == 0xFF80 && resize(0x80, 8, 16, false) == 0x0080);
    CHECK(resize(0xFF80, 16, 8, true) == 0x80 && resize(0x7F, 8, 32, true) == 0x7F);
    CHECK(resize(0xFFFFFFFFu, 32, 64, true) == UINT64_MAX && resize(0xFFFFFFFFu, 32, 64, false) == 0xFFFFFFFFull);

    /* 손계산 10문제 (본문 §1 Part B 표) — 답은 md 의 <details> 에 */
    CHECK(sign_extend(0xB4, 8) == -76);                                        /* H1 */
    CHECK(str_is((uint64_t)(int64_t)-1, 12, 16, false, "0xFFF"));              /* H2 */
    CHECK(sign_extend(0x3A, 6) == -6);                                         /* H3 */
    CHECK(str_is((uint64_t)(int64_t)-37, 8, 2, false, "0b11011011"));          /* H4 */
    CHECK(str_is(200, 8, 10, true, "-56") && str_is(200, 8, 16, false, "0xC8")); /* H5 */
    CHECK(sign_extend(trunc_n(0x7FFF + 1, 16), 16) == -32768);                 /* H6 */
    CHECK(sign_extend(0x2F0, 10) == -272);                                     /* H7 */
    CHECK(resize(0xFFFFFFFFu, 32, 64, false) == 0x00000000FFFFFFFFull &&
          resize(0xFFFFFFFFu, 32, 64, true) == 0xFFFFFFFFFFFFFFFFull);         /* H8 */
    CHECK(sign_extend(1, 1) == -1 && smax_n(1) == 0);                          /* H9 */
    CHECK(neg_n(0x80, 8) == 0x80 && sign_extend(neg_n(0x80, 8), 8) == -128);   /* H10 */

    /* ---------- Part C ---------- */
    CHECK(temp_raw_to_udeg(0x7FF0) == 127937500);     /* +127.9375 °C */
    CHECK(temp_raw_to_udeg(0x1900) == 25000000);      /* 0x190 = 400 → 25 °C */
    CHECK(temp_raw_to_udeg(0x0010) == 62500);         /* 1 LSB */
    CHECK(temp_raw_to_udeg(0xFFF0) == -62500);        /* -1 LSB */
    CHECK(temp_raw_to_udeg(0xFFFF) == -62500);        /* 하위 4비트는 무시 */
    CHECK(temp_raw_to_udeg(0xE700) == -25000000);     /* 0xE70 = -400 → -25 °C */
    CHECK(temp_raw_to_udeg(0x8000) == -128000000);    /* 12비트 최소 */
    {
        bool ok;
        CHECK(trim_set(0x00000000u, -1, &ok) == 0x00001F00u && ok);
        CHECK(trim_set(0xFFFFFFFFu, 0, &ok) == 0xFFFFE0FFu && ok);
        CHECK(trim_set(0x00000000u, -16, &ok) == 0x00001000u && ok);
        CHECK(trim_set(0x12345678u, 16, &ok) == 0x12345678u && !ok);
        CHECK(trim_set(0x12345678u, -17, &ok) == 0x12345678u && !ok);
        CHECK(trim_get(0x00001F00u) == -1 && trim_get(0x00000F00u) == 15 && trim_get(0x00001000u) == -16);
        CHECK(trim_get(0xFFFFEFFFu) == 15);
        int bad = 0;
        for (int32_t t = -16; t <= 15; t++) {
            uint32_t r = (uint32_t)xs64();
            uint32_t w = trim_set(r, t, &ok);
            if (!ok || trim_get(w) != t || (w & ~0x1F00u) != (r & ~0x1F00u)) bad++;
        }
        CHECK(bad == 0);
    }
    {   /* demo_branch.s 를 실제로 어셈블해 llvm-objdump 로 본 인코딩 (pc, target, link, word) */
        static const struct { uint64_t pc, tgt; bool link; uint32_t w; } real[] = {
            {0x04, 0x00, false, 0x17FFFFFFu}, {0x08, 0x00, true, 0x97FFFFFEu},
            {0x0C, 0x14, false, 0x14000002u}, {0x14, 0x14, false, 0x14000000u},
            {0x18, 0x100018, false, 0x14040000u}, {0x1C, 0x00, false, 0x17FFFFF9u},
        };
        int bad = 0;
        for (size_t i = 0; i < sizeof real / sizeof real[0]; i++) {
            int64_t off; bool lk; uint32_t w;
            if (!a64_b_decode(real[i].w, &off, &lk) || lk != real[i].link ||
                (uint64_t)((int64_t)real[i].pc + off) != real[i].tgt) bad++;
            if (!a64_b_encode(real[i].pc, real[i].tgt, real[i].link, &w) || w != real[i].w) bad++;
        }
        CHECK(bad == 0);
        int64_t off; bool lk; uint32_t w;
        CHECK(!a64_b_decode(0xD503201Fu, &off, &lk));                         /* nop */
        CHECK(!a64_b_decode(0xD65F03C0u, &off, &lk));                         /* ret */
        CHECK(a64_b_encode(0x1000, 0x1000 + 0x7FFFFFC, false, &w) && w == 0x15FFFFFFu);  /* +128MiB-4 */
        CHECK(!a64_b_encode(0x1000, 0x1000 + 0x8000000, false, &w));                     /* +128MiB  */
        CHECK(a64_b_encode(0x9000000, 0x9000000 - 0x8000000, false, &w) && w == 0x16000000u);
        CHECK(!a64_b_encode(0x9000000, 0x9000000 - 0x8000004, false, &w));
        CHECK(!a64_b_encode(0x1000, 0x1002, false, &w));                                /* 정렬 X */
    }
    {
        uint64_t v = 0;
        CHECK(parse_num("0x80", 8, &v) && v == 0x80);
        CHECK(parse_num("-128", 8, &v) && v == 0x80);
        CHECK(parse_num("128", 8, &v) && v == 0x80);
        CHECK(parse_num("-0x80", 8, &v) && v == 0x80);
        CHECK(!parse_num("-129", 8, &v) && !parse_num("256", 8, &v));
        CHECK(parse_num("0b1010", 4, &v) && v == 0xA);
        CHECK(parse_num("-1", 64, &v) && v == UINT64_MAX);
        CHECK(parse_num("18446744073709551615", 64, &v) && v == UINT64_MAX);
        CHECK(!parse_num("18446744073709551616", 64, &v));                 /* 64비트 넘침 */
        CHECK(parse_num("-9223372036854775808", 64, &v) && v == 0x8000000000000000ull);
        CHECK(!parse_num("-9223372036854775809", 64, &v));
        CHECK(!parse_num("", 8, &v) && !parse_num("0x", 8, &v) && !parse_num("-", 8, &v));
        CHECK(!parse_num("12a", 16, &v) && !parse_num("0x1G", 16, &v) && !parse_num("0b102", 8, &v));
        CHECK(!parse_num("--1", 8, &v) && !parse_num(" 1", 8, &v));
        int bad = 0;   /* 왕복: to_str (3진법 × 2보기) → parse_num → 같은 패턴 */
        for (int i = 0; i < 5000; i++) {
            unsigned n = (unsigned)(xs64() % 64) + 1;
            uint64_t x = trunc_n(xs64(), n);
            static const unsigned bases[] = {2, 10, 16};
            for (int bi = 0; bi < 3; bi++)
                for (int sg = 0; sg < 2; sg++) {
                    uint64_t back = ~x;
                    if (to_str(x, n, bases[bi], sg, b, sizeof b) < 0 || !parse_num(b, n, &back) || back != x) bad++;
                }
        }
        CHECK(bad == 0);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
