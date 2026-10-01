/* 09_saturating_math.c — 포화 연산, 오버플로 검출, Q15 곱셈, 나눗셈 없는 평균 (모범답안)
 *
 * 빌드: cc -std=c11 -Wall -Wextra -O2 -g 09_saturating_math.c -o sol && ./sol
 *       (또는 coding/ 에서 make sol N=09)
 *
 * 레벨 L0. 이 파일의 규칙은 딱 하나다:
 *
 *   signed 정수는 절대 오버플로시키지 않는다. 오버플로는 UB 이고,
 *   -O2 컴파일러는 "UB 는 일어나지 않는다"를 전제로 검사 코드를 지워 버린다.
 *
 * 그래서 오버플로가 날 수 있는 산술은 전부 uint32_t 에서 하고
 * (unsigned 는 2^32 로 랩어라운드하도록 표준이 정의해 두었다),
 * 결과를 u32_to_s32() 로 안전하게 되돌린다.
 *
 * 흔한 함정 코드:
 *     if (a + b > INT32_MAX) ...      <- a + b 자체가 이미 UB. 검사가 늦다.
 *     if (a > INT32_MAX - b) ...      <- 이건 맞다(b >= 0 일 때).
 */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define SIGN32 0x80000000u

/* ------------------------------------------------------------------ */
/* 0. unsigned -> signed 안전 변환                                      */
/* ------------------------------------------------------------------ */
/* C11 에서 범위를 벗어난 unsigned -> signed 변환은 "구현 정의"다
 * (UB 는 아니지만 표준이 값을 정해 주지 않는다. C23 에서 2의 보수로 확정됐다).
 * 실무에서 gcc/clang 은 2의 보수로 자르지만, 표준만 믿고 쓰려면 이렇게 쓴다.
 *
 *   u <= INT32_MAX          -> 그대로 캐스트 (범위 안)
 *   그 외                    -> u - 2^31 은 [0, INT32_MAX] 이므로 캐스트 안전,
 *                              거기에 INT32_MIN 을 더하면 [INT32_MIN, -1]
 */
static int32_t u32_to_s32(uint32_t u)
{
    if (u <= (uint32_t)INT32_MAX) {
        return (int32_t)u;
    }
    return (int32_t)(u - SIGN32) + INT32_MIN;
}

/* 잘 정의된 산술 오른쪽 시프트(부호 확장). 0 <= n <= 31.
 * C11 은 음수의 >> 를 구현 정의로 남겨 두었다. DSP 명령(ASR)과 같은 의미를
 * 표준 안에서 재현하려면 unsigned 로 밀고 위쪽을 1 로 채운다. */
static int32_t asr32(int32_t x, unsigned n)
{
    assert(n < 32u);
    uint32_t u = (uint32_t)x >> n;
    if (x < 0) {
        u |= ~(0xFFFFFFFFu >> n);       /* 상위 n 비트를 1 로 = 부호 확장 */
    }
    return u32_to_s32(u);
}

/* ------------------------------------------------------------------ */
/* 1. 무부호 포화                                                       */
/* ------------------------------------------------------------------ */

/* unsigned 덧셈은 랩어라운드가 정의되어 있으므로 결과를 보고 판정한다.
 * a + b 가 a 보다 작아졌다 = 자리올림이 밖으로 나갔다. */
static uint32_t sat_add_u32(uint32_t a, uint32_t b)
{
    uint32_t s = a + b;
    return (s < a) ? UINT32_MAX : s;
}

static uint32_t sat_sub_u32(uint32_t a, uint32_t b)
{
    return (a < b) ? 0u : (a - b);      /* 빌림이 나면 0 에서 멈춘다 */
}

static bool u32_add_overflow(uint32_t a, uint32_t b, uint32_t *out)
{
    uint32_t s = a + b;
    if (out != NULL) {
        *out = s;                        /* 넘쳤더라도 랩된 값은 정의되어 있다 */
    }
    return s < a;
}

/* ------------------------------------------------------------------ */
/* 2. 부호 있는 오버플로 검출                                            */
/* ------------------------------------------------------------------ */

/* 같은 부호끼리 더했는데 결과 부호가 달라졌다 = 오버플로.
 *
 *   (a ^ s) & (b ^ s) & 0x80000000
 *    ^^^^^^^   ^^^^^^^
 *    a 와 s 의 부호가 다르고, b 와 s 의 부호도 다를 때만 최상위 비트가 1.
 *    a 와 b 의 부호가 다르면 절대 오버플로할 수 없으므로 자동으로 0 이 된다.
 *
 * 계산은 전부 uint32_t 에서 하므로 어느 단계에서도 UB 가 없다. */
static bool s32_add_overflow(int32_t a, int32_t b, int32_t *out)
{
    uint32_t ua = (uint32_t)a;
    uint32_t ub = (uint32_t)b;
    uint32_t us = ua + ub;
    bool ovf = (((ua ^ us) & (ub ^ us) & SIGN32) != 0u);
    if (!ovf && out != NULL) {
        *out = u32_to_s32(us);
    }
    return ovf;
}

/* 뺄셈: 부호가 서로 다른 값을 뺐는데 결과 부호가 a 와 달라졌다 = 오버플로.
 * b 를 부정해서 덧셈으로 돌리는 방법은 b == INT32_MIN 에서 무너진다
 * (-INT32_MIN 은 표현 불가). 그래서 뺄셈은 따로 쓴다. */
static bool s32_sub_overflow(int32_t a, int32_t b, int32_t *out)
{
    uint32_t ua = (uint32_t)a;
    uint32_t ub = (uint32_t)b;
    uint32_t ud = ua - ub;
    bool ovf = (((ua ^ ub) & (ua ^ ud) & SIGN32) != 0u);
    if (!ovf && out != NULL) {
        *out = u32_to_s32(ud);
    }
    return ovf;
}

/* ------------------------------------------------------------------ */
/* 3. 부호 있는 포화                                                    */
/* ------------------------------------------------------------------ */

/* 넘쳤으면 넘친 방향의 끝값으로 clamp. a 의 부호가 곧 넘친 방향이다
 * (덧셈에서 오버플로가 났다면 a 와 b 의 부호가 같으므로). */
static int32_t sat_add_s32(int32_t a, int32_t b)
{
    int32_t s;
    if (!s32_add_overflow(a, b, &s)) {
        return s;
    }
    return (a < 0) ? INT32_MIN : INT32_MAX;
}

static int32_t sat_sub_s32(int32_t a, int32_t b)
{
    int32_t d;
    if (!s32_sub_overflow(a, b, &d)) {
        return d;
    }
    return (a < 0) ? INT32_MIN : INT32_MAX;
}

/* 16비트 오디오 샘플 믹싱. 폭이 절반이라 더 넓은 타입으로 올려 계산하면
 * 오버플로가 원천적으로 불가능하다. 이게 가장 읽기 쉬운 포화 구현이다. */
static int16_t sat_add_s16(int16_t a, int16_t b)
{
    int32_t s = (int32_t)a + (int32_t)b;     /* 최대 |s| = 65536, int32 여유 충분 */
    if (s > INT16_MAX) {
        return INT16_MAX;
    }
    if (s < INT16_MIN) {
        return INT16_MIN;
    }
    return (int16_t)s;
}

/* ------------------------------------------------------------------ */
/* 4. Q15 고정소수점                                                    */
/* ------------------------------------------------------------------ */
/* Q15: int16_t 를 2^15 로 나눈 값으로 읽는다. 표현 범위는
 *   [-32768/32768, 32767/32768] = [-1.0, +0.999969...]
 *
 *   0x4000 =  16384 =  0.5
 *   0x7FFF =  32767 =  0.99997
 *   0x8000 = -32768 = -1.0
 *
 * Q15 x Q15 = Q30 이므로 결과를 다시 Q15 로 만들려면 15비트 내려야 한다.
 *
 *   비트 그림:
 *     a (Q15)  s.fffffffffffffff              16비트
 *     b (Q15)  s.fffffffffffffff              16비트
 *     a*b      ss.ffffffffffffffffffffffffffffff   32비트 = Q30 (부호 비트 2개)
 *              ^^ 여기서 15비트 내리면 Q15, 나머지 하위 15비트는 버려진다
 *
 * 곱은 int32 안에서 끝난다: |a*b| <= 32768 * 32768 = 2^30 < 2^31.
 * 유일한 포화 지점은 (-1.0) x (-1.0) = +1.0 인데, +1.0 은 Q15 에 없다. */
static int16_t q15_mul(int16_t a, int16_t b)
{
    int32_t p = (int32_t)a * (int32_t)b;     /* Q30. UB 없음 (|p| <= 2^30) */
    /* 반올림: 버릴 15비트의 절반(2^14)을 미리 더한 뒤 내린다.
     * 이걸 빼면 항상 0 쪽이 아니라 -무한대 쪽으로 깎여 DC offset 이 생긴다. */
    int32_t r = asr32(p + (1 << 14), 15);
    if (r > INT16_MAX) {
        return INT16_MAX;                    /* (-1.0) x (-1.0) 만 여기로 온다 */
    }
    if (r < INT16_MIN) {
        return INT16_MIN;
    }
    return (int16_t)r;
}

/* 반올림 없는(버림) 버전. 두 구현을 비교하면 반올림의 효과가 보인다. */
static int16_t q15_mul_trunc(int16_t a, int16_t b)
{
    int32_t p = (int32_t)a * (int32_t)b;
    int32_t r = asr32(p, 15);
    if (r > INT16_MAX) {
        return INT16_MAX;
    }
    return (int16_t)r;
}

/* ------------------------------------------------------------------ */
/* 5. 나눗셈 없는 평균                                                  */
/* ------------------------------------------------------------------ */
/* (a + b) / 2 는 a + b 단계에서 넘친다. 아래 항등식은 넘치지 않는다.
 *
 *   a + b = (a & b) * 2 + (a ^ b)
 *   => (a + b) / 2 = (a & b) + (a ^ b) / 2
 *
 *   (a & b)  = 두 수가 공통으로 가진 비트 = 자리올림이 확실한 부분
 *   (a ^ b)  = 한쪽에만 있는 비트
 *
 * 결과는 내림(floor) 평균이다. Cortex-M0 처럼 나눗셈 명령이 없는 코어에서
 * 나눗셈 라이브러리 호출을 피하려고 쓴다. */
static uint32_t avg_u32(uint32_t a, uint32_t b)
{
    return (a & b) + ((a ^ b) >> 1);
}

/* 부호 있는 버전은 (a ^ b) 를 산술 시프트해야 한다.
 * 논리 시프트를 쓰면 a = -1, b = 1 에서 0 대신 INT32_MIN 이 나온다. */
static int32_t avg_s32(int32_t a, int32_t b)
{
    uint32_t ua = (uint32_t)a;
    uint32_t ub = (uint32_t)b;
    uint32_t common = ua & ub;
    int32_t  half   = asr32(u32_to_s32(ua ^ ub), 1);
    return u32_to_s32(common + (uint32_t)half);
}

/* ------------------------------------------------------------------ */
/* 6. 테스트                                                            */
/* ------------------------------------------------------------------ */
#define OK(msg) printf("  ok  %s\n", (msg))

static void test_u32_to_s32(void)
{
    assert(u32_to_s32(0u) == 0);
    assert(u32_to_s32(1u) == 1);
    assert(u32_to_s32(0x7FFFFFFFu) == INT32_MAX);
    assert(u32_to_s32(0x80000000u) == INT32_MIN);
    assert(u32_to_s32(0x80000001u) == INT32_MIN + 1);
    assert(u32_to_s32(0xFFFFFFFFu) == -1);
    assert(u32_to_s32(0xFFFFFFFEu) == -2);

    /* 왕복: 모든 값에 대해 (uint32_t)u32_to_s32(u) == u */
    uint32_t s = 0x2545F491u;
    for (int i = 0; i < 4000; i++) {
        s = (s * 1664525u) + 1013904223u;
        assert((uint32_t)u32_to_s32(s) == s);
    }
    OK("u32_to_s32: 0, INT32_MAX/MIN 경계, -1, 왕복");
}

static void test_asr32(void)
{
    assert(asr32(0, 0) == 0);
    assert(asr32(8, 3) == 1);
    assert(asr32(7, 1) == 3);
    assert(asr32(-8, 3) == -1);
    assert(asr32(-7, 1) == -4);            /* floor(-3.5) = -4 */
    assert(asr32(-1, 31) == -1);           /* -1 은 아무리 내려도 -1 */
    assert(asr32(INT32_MIN, 31) == -1);
    assert(asr32(INT32_MAX, 31) == 0);
    assert(asr32(INT32_MIN, 1) == INT32_MIN / 2);
    assert(asr32(-5, 0) == -5);            /* n = 0 은 항등 */

    /* x >= 0 일 때는 논리 시프트와 같다 */
    for (int32_t x = 0; x < 1000; x++) {
        for (unsigned n = 0; n < 10u; n++) {
            assert(asr32(x, n) == (int32_t)((uint32_t)x >> n));
        }
    }
    /* floor 성질: asr32(x,1) * 2 <= x < asr32(x,1) * 2 + 2 */
    for (int32_t x = -1000; x < 1000; x++) {
        int32_t h = asr32(x, 1);
        assert(h * 2 <= x && x < h * 2 + 2);
    }
    OK("asr32: 부호 확장, floor 성질, n=0 과 n=31 경계");
}

static void test_sat_u32(void)
{
    assert(sat_add_u32(0u, 0u) == 0u);
    assert(sat_add_u32(1u, 2u) == 3u);
    assert(sat_add_u32(UINT32_MAX, 0u) == UINT32_MAX);
    assert(sat_add_u32(UINT32_MAX, 1u) == UINT32_MAX);      /* 포화 */
    assert(sat_add_u32(UINT32_MAX, UINT32_MAX) == UINT32_MAX);
    assert(sat_add_u32(0x80000000u, 0x7FFFFFFFu) == 0xFFFFFFFFu);  /* 딱 맞음 */
    assert(sat_add_u32(0x80000000u, 0x80000000u) == UINT32_MAX);   /* 한 칸 넘침 */

    assert(sat_sub_u32(0u, 0u) == 0u);
    assert(sat_sub_u32(0u, 1u) == 0u);                      /* 포화 (랩 안 함) */
    assert(sat_sub_u32(5u, 3u) == 2u);
    assert(sat_sub_u32(3u, 5u) == 0u);
    assert(sat_sub_u32(UINT32_MAX, UINT32_MAX) == 0u);
    assert(sat_sub_u32(UINT32_MAX, 1u) == 0xFFFFFFFEu);

    uint32_t o;
    assert(!u32_add_overflow(1u, 2u, &o) && o == 3u);
    assert(u32_add_overflow(UINT32_MAX, 1u, &o) && o == 0u);   /* 랩된 값 */
    assert(!u32_add_overflow(0u, 0u, NULL));
    OK("sat_add_u32 / sat_sub_u32 / u32_add_overflow: 0, 최대값, 경계 한 칸");
}

static void test_s32_overflow_detect(void)
{
    int32_t r;

    assert(!s32_add_overflow(1, 2, &r) && r == 3);
    assert(!s32_add_overflow(INT32_MAX, 0, &r) && r == INT32_MAX);
    assert(!s32_add_overflow(INT32_MAX, -1, &r) && r == INT32_MAX - 1);
    assert(!s32_add_overflow(INT32_MIN, 0, &r) && r == INT32_MIN);
    assert(!s32_add_overflow(INT32_MIN, INT32_MAX, &r) && r == -1);
    assert(s32_add_overflow(INT32_MAX, 1, NULL));            /* 양수 오버플로 */
    assert(s32_add_overflow(INT32_MIN, -1, NULL));           /* 음수 오버플로 */
    assert(s32_add_overflow(INT32_MAX, INT32_MAX, NULL));
    assert(s32_add_overflow(INT32_MIN, INT32_MIN, NULL));
    /* 부호가 다르면 절대 오버플로하지 않는다 */
    assert(!s32_add_overflow(INT32_MAX, INT32_MIN, &r) && r == -1);

    assert(!s32_sub_overflow(3, 1, &r) && r == 2);
    assert(!s32_sub_overflow(0, INT32_MAX, &r) && r == -INT32_MAX);
    assert(s32_sub_overflow(0, INT32_MIN, NULL));            /* -INT32_MIN 은 불가 */
    assert(s32_sub_overflow(INT32_MAX, -1, NULL));
    assert(s32_sub_overflow(INT32_MIN, 1, NULL));
    assert(!s32_sub_overflow(INT32_MIN, INT32_MIN, &r) && r == 0);
    assert(!s32_sub_overflow(-1, INT32_MAX, &r) && r == INT32_MIN);
    OK("s32_add_overflow / s32_sub_overflow: INT32_MIN 을 부정하는 함정 포함");
}

static void test_sat_s32(void)
{
    assert(sat_add_s32(0, 0) == 0);
    assert(sat_add_s32(1, -1) == 0);
    assert(sat_add_s32(INT32_MAX, 1) == INT32_MAX);
    assert(sat_add_s32(INT32_MAX, INT32_MAX) == INT32_MAX);
    assert(sat_add_s32(INT32_MIN, -1) == INT32_MIN);
    assert(sat_add_s32(INT32_MIN, INT32_MIN) == INT32_MIN);
    assert(sat_add_s32(INT32_MAX, INT32_MIN) == -1);
    assert(sat_add_s32(INT32_MAX - 1, 1) == INT32_MAX);      /* 딱 맞음, 포화 아님 */

    assert(sat_sub_s32(0, 0) == 0);
    assert(sat_sub_s32(INT32_MAX, -1) == INT32_MAX);         /* 포화 */
    assert(sat_sub_s32(INT32_MIN, 1) == INT32_MIN);          /* 포화 */
    assert(sat_sub_s32(0, INT32_MIN) == INT32_MAX);          /* 포화 */
    assert(sat_sub_s32(0, INT32_MAX) == -INT32_MAX);
    assert(sat_sub_s32(-1, INT32_MAX) == INT32_MIN);         /* 딱 맞음 */

    /* 작은 값에서는 평범한 덧셈과 같아야 한다 */
    for (int32_t a = -50; a <= 50; a++) {
        for (int32_t b = -50; b <= 50; b++) {
            assert(sat_add_s32(a, b) == a + b);
            assert(sat_sub_s32(a, b) == a - b);
        }
    }
    OK("sat_add_s32 / sat_sub_s32: 양쪽 끝 포화, 0, 작은 값 일치");
}

static void test_sat_s16(void)
{
    assert(sat_add_s16(0, 0) == 0);
    assert(sat_add_s16(100, -100) == 0);
    assert(sat_add_s16(INT16_MAX, 1) == INT16_MAX);
    assert(sat_add_s16(INT16_MAX, INT16_MAX) == INT16_MAX);
    assert(sat_add_s16(INT16_MIN, -1) == INT16_MIN);
    assert(sat_add_s16(INT16_MIN, INT16_MIN) == INT16_MIN);
    assert(sat_add_s16(INT16_MAX, INT16_MIN) == -1);
    assert(sat_add_s16(INT16_MAX - 1, 1) == INT16_MAX);

    /* 오디오 믹싱 시나리오: 큰 샘플 두 개를 더해도 클리핑만 되고 부호가 뒤집히지 않는다.
     * 랩어라운드였다면 +32767 두 개가 -2 가 되어 "딱" 소리로 들린다. */
    int16_t mixed = sat_add_s16(30000, 20000);
    assert(mixed == INT16_MAX);
    mixed = sat_add_s16(-30000, -20000);
    assert(mixed == INT16_MIN);

    /* 전 범위 스윕: 결과는 항상 int16 범위 안이고, 부호가 뒤집히지 않는다 */
    for (int32_t a = INT16_MIN; a <= INT16_MAX; a += 257) {
        for (int32_t b = INT16_MIN; b <= INT16_MAX; b += 263) {
            int32_t exact = a + b;
            int16_t got = sat_add_s16((int16_t)a, (int16_t)b);
            if (exact > INT16_MAX) {
                assert(got == INT16_MAX);
            } else if (exact < INT16_MIN) {
                assert(got == INT16_MIN);
            } else {
                assert(got == (int16_t)exact);
            }
        }
    }
    OK("sat_add_s16: 오디오 샘플 클리핑, 전 범위 스윕");
}

static void test_q15(void)
{
    /* 0 과 항등원 */
    assert(q15_mul(0, 32767) == 0);
    assert(q15_mul(32767, 0) == 0);
    assert(q15_mul(0, -32768) == 0);

    /* 0.5 x 0.5 = 0.25 */
    assert(q15_mul(16384, 16384) == 8192);
    /* 1.0(근사) x 0.5 = 0.5 (반올림으로 정확히 16384) */
    assert(q15_mul(32767, 16384) == 16384);
    /* -1.0 x 0.5 = -0.5 */
    assert(q15_mul(-32768, 16384) == -16384);
    /* -1.0 x -1.0 = +1.0 -> Q15 에 없으므로 포화 */
    assert(q15_mul(-32768, -32768) == INT16_MAX);
    /* -1.0 x 1.0(근사) */
    assert(q15_mul(-32768, 32767) == -32767);
    /* 0.99997 x 0.99997 = 0.99994 */
    assert(q15_mul(32767, 32767) == 32766);
    /* 아주 작은 값: 1/32768 x 1/32768 은 Q15 로 0 (언더플로) */
    assert(q15_mul(1, 1) == 0);
    /* 부호 규칙 */
    assert(q15_mul(-16384, 16384) == -8192);
    assert(q15_mul(-16384, -16384) == 8192);

    /* 반올림 vs 버림: 반올림 버전이 float 참값에 더 가깝다 */
    int rounded_better = 0;
    for (int32_t i = -32768; i <= 32767; i += 97) {
        for (int32_t j = -32768; j <= 32767; j += 101) {
            double exact = ((double)i / 32768.0) * ((double)j / 32768.0) * 32768.0;
            int16_t r = q15_mul((int16_t)i, (int16_t)j);
            int16_t t = q15_mul_trunc((int16_t)i, (int16_t)j);
            double er = (double)r - exact;
            double et = (double)t - exact;
            if (er < 0) { er = -er; }
            if (et < 0) { et = -et; }
            assert(er <= 1.0);                  /* 반올림 오차는 1 LSB 이내 */
            if (er < et) { rounded_better++; }
            /* 두 구현의 차이는 최대 1 LSB */
            assert(r - t == 0 || r - t == 1);
        }
    }
    assert(rounded_better > 0);

    /* float 참값과 비교 (포화 구간 제외) */
    for (int32_t i = -32768; i <= 32767; i += 313) {
        for (int32_t j = -32768; j <= 32767; j += 317) {
            if (i == -32768 && j == -32768) {
                continue;
            }
            double exact = ((double)i * (double)j) / 32768.0;
            double got = (double)q15_mul((int16_t)i, (int16_t)j);
            double d = got - exact;
            if (d < 0) { d = -d; }
            assert(d <= 1.0);
        }
    }
    OK("q15_mul: 0.5x0.5, -1.0x-1.0 포화, 언더플로, 반올림이 버림보다 정확");
}

static void test_avg(void)
{
    assert(avg_u32(0u, 0u) == 0u);
    assert(avg_u32(0u, 1u) == 0u);              /* floor(0.5) */
    assert(avg_u32(1u, 1u) == 1u);
    assert(avg_u32(2u, 4u) == 3u);
    assert(avg_u32(3u, 4u) == 3u);              /* floor(3.5) */
    assert(avg_u32(UINT32_MAX, UINT32_MAX) == UINT32_MAX);   /* 넘치지 않는다 */
    assert(avg_u32(UINT32_MAX, 0u) == 0x7FFFFFFFu);
    assert(avg_u32(0x80000000u, 0x80000000u) == 0x80000000u);
    assert(avg_u32(0xFFFFFFFFu, 0xFFFFFFFEu) == 0xFFFFFFFEu);

    /* (a + b) / 2 를 64비트로 계산한 참값과 일치 */
    uint32_t s = 0x9E3779B9u;
    for (int i = 0; i < 4000; i++) {
        s = (s * 1664525u) + 1013904223u;
        uint32_t a = s;
        s = (s * 1664525u) + 1013904223u;
        uint32_t b = s;
        uint64_t exact = ((uint64_t)a + (uint64_t)b) / 2u;
        assert(avg_u32(a, b) == (uint32_t)exact);
    }

    assert(avg_s32(0, 0) == 0);
    assert(avg_s32(2, 4) == 3);
    assert(avg_s32(-1, 1) == 0);                /* 논리 시프트로 짜면 여기서 깨진다 */
    assert(avg_s32(-2, -4) == -3);
    assert(avg_s32(-3, -4) == -4);              /* floor(-3.5) = -4 */
    assert(avg_s32(-3, 4) == 0);                /* floor(0.5) = 0 */
    assert(avg_s32(INT32_MAX, INT32_MAX) == INT32_MAX);
    assert(avg_s32(INT32_MIN, INT32_MIN) == INT32_MIN);
    assert(avg_s32(INT32_MAX, INT32_MIN) == -1);
    assert(avg_s32(INT32_MAX, 0) == INT32_MAX / 2);
    assert(avg_s32(INT32_MIN, 0) == INT32_MIN / 2);   /* -1073741824, 정확히 나뉜다 */

    /* 64비트 floor 평균과 일치 */
    s = 0x12345678u;
    for (int i = 0; i < 4000; i++) {
        s = (s * 1664525u) + 1013904223u;
        int32_t a = u32_to_s32(s);
        s = (s * 1664525u) + 1013904223u;
        int32_t b = u32_to_s32(s);
        int64_t sum = (int64_t)a + (int64_t)b;
        int64_t exact = sum >> 1;                /* int64 산술 시프트 = floor */
        assert((int64_t)avg_s32(a, b) == exact);
    }
    OK("avg_u32 / avg_s32: 나눗셈 없이 floor 평균, 최대값에서도 무오버플로");
}

/* 실전 모양: 포화 연산으로 만든 아주 작은 오디오 게인 + 믹서 체인.
 * 두 채널에 각각 Q15 게인을 걸고 더한다. 어디서도 랩어라운드가 없어야 한다. */
static int16_t mix2(int16_t l, int16_t gl, int16_t r, int16_t gr)
{
    return sat_add_s16(q15_mul(l, gl), q15_mul(r, gr));
}

static void test_audio_chain(void)
{
    /* 게인 0.25 씩 걸어 더하면 헤드룸이 남아 클리핑이 없다 */
    assert(q15_mul(32767, 8192) == 8192);
    assert(mix2(32767, 8192, 32767, 8192) == 16384);
    assert(mix2(0, 16384, 0, 16384) == 0);

    /* 게인 0.5 씩이면 16384 + 16384 = 32768 로 딱 1 LSB 넘쳐 클리핑된다.
     * 랩어라운드 구현이었다면 여기서 -32768 (최대 음량)이 나온다. */
    assert(q15_mul(32767, 16384) == 16384);
    assert(mix2(32767, 16384, 32767, 16384) == INT16_MAX);

    /* 게인 1.0(근사)을 두 채널에 걸면 최대 입력에서 클리핑된다 */
    assert(mix2(32767, 32767, 32767, 32767) == INT16_MAX);
    assert(mix2(-32768, 32767, -32768, 32767) == INT16_MIN);

    /* 음소거 */
    assert(mix2(32767, 0, -32768, 0) == 0);

    /* 전 범위 스윕: 결과는 늘 int16 범위 안이고 부호가 뒤집히지 않는다 */
    for (int32_t x = INT16_MIN; x <= INT16_MAX; x += 1021) {
        int16_t hot = mix2((int16_t)x, 32767, (int16_t)x, 32767);
        if (x > 0) {
            assert(hot > 0);                     /* 랩어라운드였다면 음수가 됐다 */
        } else if (x < -1) {
            assert(hot < 0);
        }
        assert(hot >= INT16_MIN && hot <= INT16_MAX);
    }
    OK("오디오 체인: Q15 게인 + 포화 믹싱, 랩어라운드 없음");
}

int main(void)
{
    printf("09_saturating_math (solution)\n");
    test_u32_to_s32();
    test_asr32();
    test_sat_u32();
    test_s32_overflow_detect();
    test_sat_s32();
    test_sat_s16();
    test_q15();
    test_avg();
    test_audio_chain();
    printf("ALL TESTS PASSED\n");
    return 0;
}
