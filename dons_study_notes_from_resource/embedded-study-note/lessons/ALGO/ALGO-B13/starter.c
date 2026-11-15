/* ALGO-B13 비트 트릭 2: 비트 반전·니블 스왑·패리티
 *   Part A: reverse bits 8/32 — 루프, 마스크-시프트 분할정복(D&C), 256-entry LUT (+ 표 만들기)
 *   Part B: 니블 두 가지 뜻(바이트 안 swap / 니블 순서 역순), 홀짝 비트 교환, 패리티 3가지
 *   Part C: SPI LSB-first 송신 버퍼 준비, UART 8E1 프레임 만들기/검사
 * 모든 함수는 인덱스 기준 참조 구현 ref_xor_perm()(비트 i → 비트 i^m)과 대조한다.
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---------------- 주어진 것: 컴파일 타임 256-entry 표 (const → flash / __TEXT,__const) ----------------
 * 인덱스를 2비트 숫자 4개(d3 d2 d1 d0)로 보고, 각 숫자를 뒤집어(0,2,1,3) 거울 위치에 놓는다. */
#define R2(n) (n), (n) + 2 * 64, (n) + 1 * 64, (n) + 3 * 64
#define R4(n) R2(n), R2((n) + 2 * 16), R2((n) + 1 * 16), R2((n) + 3 * 16)
#define R6(n) R4(n), R4((n) + 2 * 4), R4((n) + 1 * 4), R4((n) + 3 * 4)
static const uint8_t REV8[256] = { R6(0), R6(2), R6(1), R6(3) };

/* 패리티 표: P[i] = popcount(i) & 1. 인덱스를 둘로 나누면 P[2k+b] = P[k] ^ b 와 같은 재귀. */
#define P2(n) (n), (n) ^ 1, (n) ^ 1, (n)
#define P4(n) P2(n), P2((n) ^ 1), P2((n) ^ 1), P2(n)
#define P6(n) P4(n), P4((n) ^ 1), P4((n) ^ 1), P4(n)
static const uint8_t PARITY8[256] = { P6(0), P6(1), P6(1), P6(0) };

/* ---------------- Part A: 비트 반전 ---------------- */
/* 비트 i → 비트 7-i. 한 비트씩 꺼내(LSB) 결과에 밀어 넣는다(왼쪽으로). */
static uint8_t rev8_loop(uint8_t b) {
    (void)b;
    return 0; /* TODO: 한 비트씩 꺼내 r 에 밀어 넣기 */
}

/* 분할정복: 니블 swap → 2비트 쌍 swap → 1비트 swap (순서는 무관) */
static uint8_t rev8_dc(uint8_t b) {
    return b; /* TODO: 니블 swap → 2비트 swap → 1비트 swap */
}

static uint32_t rev32_loop(uint32_t x) {
    return x; /* TODO */
}

/* 5단계: 거리 1, 2, 4, 8, 16 짝끼리 맞바꾼다. 마스크는 "아래쪽 짝" 자리. */
static uint32_t rev32_dc(uint32_t x) {
    return x; /* TODO: 거리 1, 2, 4, 8, 16 짝 바꾸기 5단계 */
}

/* 바이트마다 표로 뒤집고, 바이트 순서도 뒤집는다 (byte k → byte 3-k). */
static uint32_t rev32_lut(uint32_t x) {
    (void)REV8;
    return x; /* TODO: REV8[] 로 바이트마다 뒤집고 바이트 순서도 뒤집기 */
}

/* 런타임에 표 만들기(RAM 256 B). 점화식: i 를 반 나눈 값(i>>1)의 반전을 한 칸 내리고, i 의 LSB 를 MSB 로. */
static void rev8_table_build(uint8_t t[256]) {
    (void)t; /* TODO: t[i] 를 t[i >> 1] 로부터 만든다 */
}

/* ---------------- Part B: 니블 · 홀짝 · 패리티 ---------------- */
/* 뜻 1: 한 바이트 안의 두 니블 맞바꾸기 0xAB → 0xBA */
static uint8_t swap_nibbles8(uint8_t b) {
    return b; /* TODO */
}

/* 뜻 1 (워드): 각 바이트 안에서 swap, 바이트 순서는 그대로. 0x12345678 → 0x21436587 */
static uint32_t nib_swap_bytes32(uint32_t x) {
    return x; /* TODO: 바이트 순서는 그대로 */
}

/* 뜻 2: 니블 8개의 순서를 뒤집기 (hex 자리 역순). 0x12345678 → 0x87654321 */
static uint32_t rev_nibbles32_loop(uint32_t x) {
    return x; /* TODO: 한 니블(hex 한 자리)씩 */
}

/* 뜻 2 (64비트, 실제 스크린 문제): 0x1234567890ABCDEF → 0xFEDCBA0987654321.
 * 니블 swap(거리 4) + 바이트 역순(거리 8, 16, 32). */
static uint64_t rev_nibbles64_dc(uint64_t x) {
    return x; /* TODO: 니블 swap + 바이트 역순 (반복문 없이) */
}

/* 짝수 비트 ↔ 홀수 비트: bit 2k ↔ bit 2k+1 */
static uint32_t swap_odd_even32(uint32_t x) {
    return x; /* TODO */
}

/* 패리티 = 1 의 개수가 홀수면 1 */
static unsigned parity32_loop(uint32_t x) {
    (void)x;
    return 0; /* TODO */
}

static unsigned parity32_fold(uint32_t x) {
    (void)x;
    return 0; /* TODO: x ^= x >> 16 ... */
}

static unsigned parity32_lut(uint32_t x) {
    (void)PARITY8;
    return x & 1u; /* TODO: 접어서 8비트로 만든 뒤 PARITY8[] */
}

/* ---------------- Part C: 펌웨어 ---------------- */
/* SPI 주변장치는 MSB-first 고정, 장치는 LSB-first. 바이트 순서는 그대로 두고 각 바이트의 비트만 뒤집는다.
 * dst == src (제자리) 허용. 받은 데이터도 같은 함수로 되돌린다(두 번 하면 원래대로). */
static void spi_prepare_lsb_first(uint8_t *dst, const uint8_t *src, size_t n) {
    for (size_t i = 0; i < n; i++)
        dst[i] = src[i]; /* TODO: 지금은 그냥 복사한다 */
}

/* UART 8E1 프레임 (선 위의 순서 = 비트 0 부터):
 *   bit 0 = start(0), bit 1..8 = d0..d7 (LSB 먼저), bit 9 = 짝수 패리티, bit 10 = stop(1). bit 15..11 = 0 */
static uint16_t uart_frame_8e1(uint8_t d) {
    (void)d;
    return 0; /* TODO */
}

/* 0 = OK(*out 에 데이터), -1 = framing(start≠0 또는 stop≠1), -2 = parity. bit 15..11 은 무시한다. */
static int uart_decode_8e1(uint16_t f, uint8_t *out) {
    (void)f; (void)out;
    return -1; /* TODO */
}

/* ---------------- 테스트 하네스 ---------------- */
#ifndef ALGO_B13_NO_MAIN
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t rng = 0x2545F491u;
static uint32_t xs32(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static uint64_t xs64(void) { uint64_t h = xs32(); return (h << 32) | xs32(); }

/* 참조: n 비트 워드에서 비트 i 를 비트 i^m 으로 옮긴다 (m < n, n 은 2의 거듭제곱).
 * m = n-1 → 비트 반전, 1 → 홀짝 교환, 4 → 바이트 안 니블 swap, n-4 → 니블 역순, 7 → 바이트마다 비트 반전 */
static uint64_t ref_xor_perm(uint64_t x, unsigned n, unsigned m) {
    uint64_t r = 0;
    for (unsigned i = 0; i < n; i++)
        if ((x >> i) & 1u)
            r |= 1ull << (i ^ m);
    return r;
}
static unsigned ref_parity(uint32_t x) { return (unsigned)__builtin_popcount(x) & 1u; }

/* 첫 불일치만 출력하고 개수를 센다 */
static long g_bad;
static void mism(const char *what, uint64_t in, uint64_t got, uint64_t want) {
    if (g_bad++ == 0)
        printf("  first mismatch %s: in=0x%llx got=0x%llx want=0x%llx\n", what,
               (unsigned long long)in, (unsigned long long)got, (unsigned long long)want);
}
#define EXPECT(what, in, got, want) do { uint64_t g_ = (got), w_ = (want); \
    if (g_ != w_) mism(what, (in), g_, w_); } while (0)

/* SPI 선 위의 비트 순서를 흉내낸다: 바이트 0 부터, 각 바이트는 msb_first 면 b7..b0, 아니면 b0..b7 */
static size_t wire_bits(const uint8_t *buf, size_t n, bool msb_first, uint8_t *bits) {
    size_t k = 0;
    for (size_t i = 0; i < n; i++)
        for (int j = 0; j < 8; j++)
            bits[k++] = (uint8_t)((buf[i] >> (msb_first ? 7 - j : j)) & 1u);
    return k;
}

int main(void) {
    /* ===== Part A ===== */
    CHECK(rev8_loop(0x01u) == 0x80u);
    CHECK(rev8_loop(0xB4u) == 0x2Du);
    CHECK(rev8_dc(0x0Fu) == 0xF0u);
    CHECK(rev8_dc(0xB4u) == 0x2Du);
    CHECK(rev32_loop(1u) == 0x80000000u);
    CHECK(rev32_dc(0x12345678u) == 0x1E6A2C48u);
    CHECK(rev32_lut(0x80000000u) == 1u);
    CHECK(rev32_lut(0x12345678u) == 0x1E6A2C48u);
    CHECK(rev32_dc(0u) == 0u && rev32_dc(0xFFFFFFFFu) == 0xFFFFFFFFu);

    g_bad = 0;                                   /* 8비트 전수: 세 방법 + 주어진 표 */
    for (unsigned b = 0; b < 256; b++) {
        uint64_t want = ref_xor_perm(b, 8, 7);
        EXPECT("rev8_loop", b, rev8_loop((uint8_t)b), want);
        EXPECT("rev8_dc", b, rev8_dc((uint8_t)b), want);
        EXPECT("REV8[]", b, REV8[b], want);
    }
    CHECK(g_bad == 0);

    uint8_t built[256];
    memset(built, 0xEE, sizeof built);
    rev8_table_build(built);
    CHECK(memcmp(built, REV8, sizeof built) == 0);

    const char *n32[3] = { "rev32_loop", "rev32_dc", "rev32_lut" };
    uint32_t (*f32[3])(uint32_t) = { rev32_loop, rev32_dc, rev32_lut };
    for (int k = 0; k < 3; k++) {                /* 16비트 전수(아래 반, 위 반, 양쪽) + 무작위 20만 + 걷는 1 */
        g_bad = 0;
        for (uint32_t v = 0; v < 65536u; v++) {
            uint32_t in[3] = { v, v << 16, v | (v << 16) };
            for (int j = 0; j < 3; j++) EXPECT(n32[k], in[j], f32[k](in[j]), ref_xor_perm(in[j], 32, 31));
        }
        for (int i = 0; i < 200000; i++) {
            uint32_t x = xs32();
            EXPECT(n32[k], x, f32[k](x), ref_xor_perm(x, 32, 31));
            EXPECT(n32[k], x, f32[k](f32[k](x)), x);            /* 두 번 = 원래대로 */
        }
        for (int i = 0; i < 32; i++) {
            EXPECT(n32[k], 1u << i, f32[k](1u << i), 1u << (31 - i));
            EXPECT(n32[k], ~(1u << i), f32[k](~(1u << i)), ~(1u << (31 - i)));
        }
        CHECK(g_bad == 0);
    }
#if defined(__has_builtin)
#if __has_builtin(__builtin_bitreverse32)
    g_bad = 0;                                   /* 컴파일러 builtin 과도 대조 (테스트 쪽에서만) */
    for (int i = 0; i < 100000; i++) {
        uint32_t x = xs32();
        EXPECT("dc vs builtin", x, rev32_dc(x), __builtin_bitreverse32(x));
    }
    CHECK(g_bad == 0);
#endif
#endif

    /* ===== Part B ===== */
    CHECK(swap_nibbles8(0xABu) == 0xBAu);
    CHECK(nib_swap_bytes32(0x12345678u) == 0x21436587u);
    CHECK(rev_nibbles32_loop(0x12345678u) == 0x87654321u);
    CHECK(rev_nibbles64_dc(0x1234567890ABCDEFull) == 0xFEDCBA0987654321ull);   /* 실제 스크린 예시 */
    CHECK(rev_nibbles64_dc(0x1234567890ABCDEFull) != __builtin_bswap64(0x1234567890ABCDEFull));
    CHECK(swap_odd_even32(0xAAAAAAAAu) == 0x55555555u);
    CHECK(swap_odd_even32(0x80000001u) == 0x40000002u);

    g_bad = 0;
    for (unsigned b = 0; b < 256; b++) EXPECT("swap_nibbles8", b, swap_nibbles8((uint8_t)b), ref_xor_perm(b, 8, 4));
    for (uint32_t v = 0; v < 65536u; v++) {
        uint32_t in[3] = { v, v << 16, v * 0x10001u ^ 0x5A5A0000u };
        for (int j = 0; j < 3; j++) {
            uint32_t x = in[j];
            EXPECT("nib_swap_bytes32", x, nib_swap_bytes32(x), ref_xor_perm(x, 32, 4));
            EXPECT("rev_nibbles32_loop", x, rev_nibbles32_loop(x), ref_xor_perm(x, 32, 28));
            EXPECT("swap_odd_even32", x, swap_odd_even32(x), ref_xor_perm(x, 32, 1));
        }
    }
    CHECK(g_bad == 0);

    g_bad = 0;                                   /* 64비트: 참조 + 32비트 루프 두 개로 조립한 값 + 관계식 */
    for (int i = 0; i < 200000; i++) {
        uint64_t x = xs64();
        uint64_t two = ((uint64_t)rev_nibbles32_loop((uint32_t)x) << 32) | rev_nibbles32_loop((uint32_t)(x >> 32));
        EXPECT("rev_nibbles64_dc", x, rev_nibbles64_dc(x), ref_xor_perm(x, 64, 60));
        EXPECT("rev_nibbles64_dc vs 2x32", x, rev_nibbles64_dc(x), two);
        uint32_t y = (uint32_t)x;
        EXPECT("nibrev = bswap o nibswap", y, rev_nibbles32_loop(y), __builtin_bswap32(nib_swap_bytes32(y)));
        EXPECT("bitrev = nibrev o (rev in nibble)", y, rev32_dc(y), rev_nibbles32_loop((uint32_t)ref_xor_perm(y, 32, 3)));
    }
    CHECK(g_bad == 0);

    CHECK(parity32_loop(0u) == 0 && parity32_loop(7u) == 1 && parity32_loop(0xFFFFFFFFu) == 0);
    CHECK(parity32_fold(0x80000000u) == 1 && parity32_fold(3u) == 0);
    CHECK(parity32_lut(0x80000001u) == 0 && parity32_lut(0x00010000u) == 1);
    const char *np[3] = { "parity32_loop", "parity32_fold", "parity32_lut" };
    unsigned (*fp[3])(uint32_t) = { parity32_loop, parity32_fold, parity32_lut };
    for (int k = 0; k < 3; k++) {
        g_bad = 0;
        for (uint32_t v = 0; v < 65536u; v++) {
            uint32_t in[3] = { v, v << 16, v ^ (xs32() & 0xFFFF0000u) };
            for (int j = 0; j < 3; j++) EXPECT(np[k], in[j], fp[k](in[j]), ref_parity(in[j]));
        }
        for (int i = 0; i < 200000; i++) { uint32_t x = xs32(); EXPECT(np[k], x, fp[k](x), ref_parity(x)); }
        CHECK(g_bad == 0);
    }

    /* ===== Part C ===== */
    {
        const uint8_t msg[7] = { 0x01, 0xA5, 0x3C, 0x80, 0x00, 0xFF, 0x12 };
        uint8_t tx[7], w_dev[56], w_mcu[56];
        spi_prepare_lsb_first(tx, msg, 7);
        CHECK(tx[0] == 0x80u && tx[3] == 0x01u && tx[6] == 0x48u);   /* 바이트 순서는 그대로 */
        wire_bits(msg, 7, false, w_dev);         /* 장치가 기대하는 선 위 순서 (LSB-first) */
        wire_bits(tx, 7, true, w_mcu);           /* MSB-first 주변장치가 실제로 내보내는 순서 */
        CHECK(memcmp(w_dev, w_mcu, sizeof w_dev) == 0);

        uint8_t buf[1000], orig[1000], wa[8000], wb[8000];
        for (int i = 0; i < 1000; i++) orig[i] = buf[i] = (uint8_t)xs32();
        spi_prepare_lsb_first(buf, buf, 1000);   /* 제자리 */
        wire_bits(orig, 1000, false, wa);
        wire_bits(buf, 1000, true, wb);
        CHECK(memcmp(wa, wb, sizeof wa) == 0);
        spi_prepare_lsb_first(buf, buf, 1000);   /* 수신 쪽: 한 번 더 = 원래 값 */
        CHECK(memcmp(buf, orig, sizeof buf) == 0);
        spi_prepare_lsb_first(NULL, NULL, 0);    /* n = 0: 아무것도 안 건드림 */
    }
    {
        CHECK(uart_frame_8e1(0x55u) == 0x4AAu);  /* 1 이 4개 → 패리티 0 */
        CHECK(uart_frame_8e1(0x01u) == 0x602u);  /* 1 이 1개 → 패리티 1 */
        uint8_t out = 0;
        CHECK(uart_decode_8e1(0x602u, &out) == 0 && out == 0x01u);
        CHECK(uart_decode_8e1(0x603u, &out) == -1);   /* start 비트가 1 */
        CHECK(uart_decode_8e1(0x202u, &out) == -1);   /* stop 비트가 0 */
        CHECK(uart_decode_8e1(0x402u, &out) == -2);   /* 패리티 틀림 */

        long ok = 0, par1 = 0, frm1 = 0, undetected2 = 0;
        for (unsigned d = 0; d < 256; d++) {
            uint16_t f = uart_frame_8e1((uint8_t)d);
            out = 0;
            if (uart_decode_8e1(f, &out) == 0 && out == d && ((f >> 10) == 1)) ok++;
            for (int bit = 0; bit <= 10; bit++) {          /* 1비트 오류: 데이터·패리티는 -2, start/stop 은 -1 */
                int rc = uart_decode_8e1((uint16_t)(f ^ (1u << bit)), &out);
                if (bit >= 1 && bit <= 9 && rc == -2) par1++;
                if ((bit == 0 || bit == 10) && rc == -1) frm1++;
            }
            for (int a = 1; a <= 8; a++)                   /* 데이터 2비트 오류: 패리티는 못 잡는다 */
                for (int b = a + 1; b <= 8; b++)
                    if (uart_decode_8e1((uint16_t)(f ^ (1u << a) ^ (1u << b)), &out) == 0) undetected2++;
        }
        printf("uart 8E1: roundtrip %ld/256, 1-bit parity err %ld/2304, framing %ld/512, 2-bit undetected %ld/7168\n",
               ok, par1, frm1, undetected2);
        CHECK(ok == 256 && par1 == 2304 && frm1 == 512);
        CHECK(undetected2 == 256 * 28);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
#endif
