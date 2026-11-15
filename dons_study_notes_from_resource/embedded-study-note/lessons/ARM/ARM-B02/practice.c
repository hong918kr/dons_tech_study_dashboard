/* ARM-B02 추가 연습 — practice.S (mystery7..mystery10, REAL clang -O2 output) 역해석 답안 + 테스트
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c practice.S shared_mystery.S -o /tmp/ARM-B02_p && /tmp/ARM-B02_p
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

uint32_t mystery7(uint32_t x);
int32_t  mystery8(int op, int32_t x);
uint32_t mystery9(uint32_t reg, uint32_t v);
long     mystery10(const long *a, size_t n, long (*f)(long));

/* 연습 1 — umull 0xcccccccd >> 35 = /10, msub = %10 : 10진 자릿수 합 */
static uint32_t my_mystery7(uint32_t x) {
    uint32_t s = 0;
    while (x) { s += x % 10u; x /= 10u; }
    return s;
}

/* 연습 2 — 1바이트 점프 테이블. 원본 C 의 x+7, x*3, -x, x-100, x<<4 는 오버플로 시 UB 지만
 * asm 은 32비트 wrap 으로 "정의" 되어 있다 -> 같은 동작을 UB 없이 쓰려면 uint32_t 로 계산한다.
 * (uint32_t -> int32_t 변환은 C11 6.3.1.3p3: implementation-defined, clang/gcc 는 mod 2^32) */
static int32_t my_mystery8(int op, int32_t x) {
    uint32_t u = (uint32_t)x;
    switch (op) {                              /* cmp w0,#9 / b.hi : 음수 op 도 default */
    case 0: return (int32_t)(u + 7u);
    case 1: return (int32_t)(u * 3u);          /* add w0, w1, w1, lsl #1 */
    case 2: return x >> 2;                     /* asr (음수 >> 는 impl-defined, clang = 산술) */
    case 3: return (int32_t)(0u - u);          /* neg */
    case 4: return x ^ 0x55;
    case 5: return x / 8;                      /* +7 bias, csel lt, asr #3 = 0 쪽 절삭 */
    case 6: return x & 0xF0;
    case 7: return (int32_t)(u - 100u);
    case 8: return (int32_t)(u << 4);          /* 음수 << 는 C 에서 UB -> unsigned 로 */
    case 9: return ~x;                         /* mvn */
    default: return 0;
    }
}

/* 연습 3 — bfi w0, w1, #4, #7 : reg 의 [10:4] 필드에 v 의 하위 7비트를 넣는다 */
static uint32_t my_mystery9(uint32_t reg, uint32_t v) {
    return (reg & ~(0x7Fu << 4)) | ((v & 0x7Fu) << 4);
}

/* 연습 4 — callee-saved x19..x22 에 f, n, a, sum 을 두고 blr x19 반복 */
static long my_mystery10(const long *a, size_t n, long (*f)(long)) {
    long s = 0;
    for (size_t i = 0; i < n; i++) s = (long)((unsigned long)s + (unsigned long)f(a[i])); /* add 는 wrap */
    return s;
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint64_t rng = 0x9E3779B97F4A7C15ull;
static uint64_t xorshift64(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return rng; }

static const int32_t E32[] = { 0, 1, -1, 7, -7, -8, -9, 100, -100, INT32_MAX, INT32_MIN, INT32_MAX - 6, INT32_MIN + 99 };
#define N_OF(a) (sizeof(a) / sizeof((a)[0]))

/* 콜백: 호출 순서와 횟수를 기록 (asm 이 a[] 를 순서대로 한 번씩 넘기는지) */
static long g_seen[64]; static size_t g_nseen;
static long cb_record(long v) { if (g_nseen < 64) g_seen[g_nseen] = v; g_nseen++; return 3 * v - 1; }
static long cb_big(long v) { return v == 0 ? INT64_MAX : 1; }       /* 합이 wrap 하는 경우 */

int main(void) {
    long bad;

    /* 연습 1 */
    CHECK(my_mystery7(0) == 0 && mystery7(0) == 0);
    CHECK(my_mystery7(9) == 9);
    CHECK(my_mystery7(10) == 1);
    CHECK(my_mystery7(12345) == 15);
    CHECK(my_mystery7(UINT32_MAX) == 57 && mystery7(UINT32_MAX) == 57);
    bad = 0;
    for (uint32_t x = 0; x < 1000000u; x++) bad += my_mystery7(x) != mystery7(x);
    for (int k = 0; k < 200000; k++) { uint32_t x = (uint32_t)xorshift64(); bad += my_mystery7(x) != mystery7(x); }
    CHECK(bad == 0);

    /* 연습 2 */
    CHECK(mystery8(0, 1) == 8 && my_mystery8(0, 1) == 8);
    CHECK(mystery8(5, -9) == -1 && my_mystery8(5, -9) == -1);      /* -9/8: 0 쪽 절삭 */
    CHECK(mystery8(2, -9) == -3 && my_mystery8(2, -9) == -3);      /* -9>>2: -inf 쪽 */
    CHECK(mystery8(0, INT32_MAX) == INT32_MIN + 6);                /* wrap (C 원본은 UB) */
    CHECK(mystery8(3, INT32_MIN) == INT32_MIN);                    /* neg INT32_MIN = 자기 자신 */
    CHECK(mystery8(-1, 5) == 0 && mystery8(10, 5) == 0);
    bad = 0;
    for (int op = -3; op <= 12; op++) {
        for (size_t i = 0; i < N_OF(E32); i++) bad += my_mystery8(op, E32[i]) != mystery8(op, E32[i]);
        for (int k = 0; k < 20000; k++) { int32_t x = (int32_t)(uint32_t)xorshift64(); bad += my_mystery8(op, x) != mystery8(op, x); }
    }
    CHECK(bad == 0);

    /* 연습 3 */
    CHECK(mystery9(0xFFFFFFFFu, 0) == 0xFFFFF80Fu && my_mystery9(0xFFFFFFFFu, 0) == 0xFFFFF80Fu);
    CHECK(my_mystery9(0, 0x7F) == 0x7F0u);
    CHECK(my_mystery9(0, 0xFF) == 0x7F0u);                         /* v 의 bit7 이상은 버림 */
    bad = 0;
    for (int k = 0; k < 200000; k++) { uint64_t r = xorshift64(); bad += my_mystery9((uint32_t)r, (uint32_t)(r >> 32)) != mystery9((uint32_t)r, (uint32_t)(r >> 32)); }
    CHECK(bad == 0);

    /* 연습 4 */
    {
        long a[40];
        for (int i = 0; i < 40; i++) a[i] = (long)(xorshift64() % 2001) - 1000;
        g_nseen = 0;
        CHECK(mystery10(a, 0, cb_record) == 0 && g_nseen == 0);   /* n=0: 콜백 0번 */
        g_nseen = 0;
        long w = mystery10(a, 40, cb_record);
        int order_ok = g_nseen == 40;
        for (int i = 0; i < 40 && order_ok; i++) order_ok = g_seen[i] == a[i];
        CHECK(order_ok);                                           /* 순서대로 한 번씩 */
        g_nseen = 0;
        CHECK(my_mystery10(a, 40, cb_record) == w);
        long z[3] = { 0, 5, 6 };                                   /* INT64_MAX + 1 + 1 -> wrap */
        CHECK(mystery10(z, 3, cb_big) == INT64_MIN + 1 && my_mystery10(z, 3, cb_big) == INT64_MIN + 1);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
