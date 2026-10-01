/* 15_fixed_point_filter.c — Q15 고정소수점 필터 (starter)
 *
 * float 유닛이 없는(또는 있어도 ISR에서 쓰면 안 되는) MCU에서 센서 값을
 * 매끄럽게 만드는 두 가지 기본 필터를 정수만으로 구현한다.
 *
 *   1) 이동평균(moving average, box filter)  — 창 N개의 평균
 *   2) 1차 IIR(EWMA)  y += alpha * (x - y)   — 메모리 1개로 끝나는 저역통과
 *
 * 표현: 샘플은 Q15 분수다. 정수 v가 실수 v / 32768 을 뜻한다.
 *   범위      [-1.0, +0.999969482421875]
 *   해상도    1 / 32768 = 3.0517578125e-05
 *   +1.0은 표현할 수 없다.
 *
 * IIR 상태는 Q30(int32)으로 들고 있다. acc = y_q15 * 32768.
 *
 * float은 테스트의 '정답' 계산에만 쓴다. 필터 본체에는 한 줄도 쓰지 말 것.
 *
 * 할 일: sat16 / q15_mul / ma_init / ma_push / iir_init / iir_push 의 TODO를
 *        채우고 make run N=15 가 ALL TESTS PASSED를 찍게 만든다.
 *        테스트 코드(아래쪽)는 건드리지 않는다.
 *
 * build: cc -std=c11 -Wall -Wextra -O2 15_fixed_point_filter.c -o my_15
 */

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* ------------------------------------------------------------------ */
/* Q15 상수                                                            */
/* ------------------------------------------------------------------ */

typedef int16_t q15_t;

#define Q15_SHIFT  15
#define Q15_SCALE  32768           /* 1.0에 해당하는 배율 */
#define Q15_MAX    ((q15_t)32767)  /* +0.99997 */
#define Q15_MIN    ((q15_t)-32768) /* -1.0 */

#define MA_N       8u              /* 이동평균 창 크기 (2의 거듭제곱) */

/* ------------------------------------------------------------------ */
/* 타입                                                                 */
/* ------------------------------------------------------------------ */

typedef struct {
    int16_t buf[MA_N];   /* 창 안의 샘플 */
    int32_t sum;         /* buf에 담긴 n개의 합 (러닝 합) */
    uint8_t idx;         /* 다음에 덮어쓸 위치 */
    uint8_t n;           /* 지금까지 담은 개수 (<= MA_N) */
} ma_t;

typedef struct {
    int32_t acc;         /* y를 Q30으로 들고 있다 (= y_q15 << 15) */
    q15_t   alpha;       /* 0 <= alpha <= Q15_MAX. 클수록 빠르고 거칠다 */
} iir_t;

/* ------------------------------------------------------------------ */
/* 구현 — 여기를 채운다                                                  */
/* ------------------------------------------------------------------ */

/* int32 -> int16 포화. wrap 대신 한계값에서 멈춘다. */
int16_t sat16(int32_t v)
{
    /* TODO: 32767보다 크면 32767, -32768보다 작으면 -32768, 그 외는 그대로. */
    (void)v;
    return 0;
}

/* (a/32768) * (b/32768) 을 Q15로 돌려준다. 반올림 + 포화. */
q15_t q15_mul(q15_t a, q15_t b)
{
    /* TODO:
     *  - 곱을 int32로 넓혀서 한다(int16끼리 곱하면 Q30 값이 잘린다).
     *  - >> 15 전에 반올림 항을 더한다(그냥 시프트하면 DC offset이 쌓인다).
     *  - 오버플로하는 유일한 조합이 무엇인지 찾아 포화시킨다. */
    (void)a;
    (void)b;
    return 0;
}

void ma_init(ma_t *f)
{
    /* 안전하게 0으로 채워 둔다. */
    for (unsigned i = 0u; i < MA_N; i++) { f->buf[i] = 0; }
    f->sum = 0;
    f->idx = 0u;
    f->n   = 0u;
    /* TODO: 이 상태가 '창이 비어 있음'을 뜻하도록 ma_push를 설계한다. */
}

/* 샘플 하나를 넣고 현재 창의 평균(반올림)을 돌려준다. O(1)이어야 한다. */
int16_t ma_push(ma_t *f, int16_t x)
{
    /* TODO:
     *  - 러닝 합을 유지한다(매번 N개를 다시 더하지 않는다).
     *  - 창이 아직 안 찼으면 지금까지 들어온 개수로 나눈다(0으로 희석 금지).
     *  - 반올림을 0에서 먼 방향으로 대칭시킨다(C의 / 는 0쪽으로 자른다).
     *  - MA_N이 2의 거듭제곱이라는 점을 인덱스 갱신에 쓸 수 있다. */
    (void)f;
    (void)x;
    return 0;
}

void iir_init(iir_t *f, q15_t alpha, q15_t y0)
{
    /* 안전하게 0으로 채워 둔다. */
    f->alpha = 0;
    f->acc   = 0;
    (void)alpha;
    (void)y0;
    /* TODO: alpha를 유효 범위로 클램프하고, y0를 Q30 상태로 옮긴다.
     *       음수의 좌시프트는 C11에서 undefined behavior다. 무엇을 쓸까? */
}

/* y_new = y + alpha * (x - y), 전부 정수. */
q15_t iir_push(iir_t *f, q15_t x)
{
    /* TODO:
     *  - 현재 출력 y(Q15)를 acc(Q30)에서 꺼낸다.
     *  - err = x - y 를 구한다. 범위가 얼마인지 먼저 계산해 볼 것.
     *  - acc를 갱신한다. 시프트가 상쇄되어 곱 한 번으로 끝나는 형태가 있다.
     *  - 그 곱이 int32를 넘지 않는지 손으로 확인하고 주석에 적는다. */
    (void)f;
    (void)x;
    return 0;
}

/* ================================================================== */
/* 여기부터는 테스트 코드                                                */
/* ================================================================== */

/* math.h를 쓰지 않는다(-lm 의존을 피한다). 필요한 건 절댓값뿐이다. */
static float absf(float v) { return (v < 0.0f) ? -v : v; }
static int32_t abs32(int32_t v) { return (v < 0) ? -v : v; }

/* 결정론적 LCG. 테스트 입력을 재현 가능하게 만든다. */
static uint32_t lcg_state = 12345u;
static void lcg_reset(void) { lcg_state = 12345u; }
static int32_t lcg_next(int32_t amp)
{
    lcg_state = lcg_state * 1103515245u + 12345u;
    int32_t r = (int32_t)((lcg_state >> 16) & 0xFFFFu);   /* 0..65535 */
    return ((r * (2 * amp + 1)) >> 16) - amp;             /* -amp..+amp */
}

/* 삼각파 + 노이즈. 실제 센서 신호를 정수만으로 흉내낸 것. */
static q15_t test_signal(unsigned k)
{
    unsigned phase = k % 200u;
    int32_t tri = (phase < 100u) ? (int32_t)phase * 300 - 15000
                                 : 15000 - (int32_t)(phase - 100u) * 300;
    return (q15_t)sat16(tri + lcg_next(2000));
}

static void test_sat16(void)
{
    assert(sat16(0) == 0);
    assert(sat16(32767) == 32767);
    assert(sat16(32768) == 32767);          /* 한 칸 넘으면 멈춘다 */
    assert(sat16(-32768) == -32768);
    assert(sat16(-32769) == -32768);
    assert(sat16(2147483647L) == 32767);
    assert(sat16(-2147483647L - 1L) == -32768);
    printf("  ok  sat16 boundaries: 32767/32768/-32768/-32769/INT32 extremes\n");
}

static void test_q15_mul_identities(void)
{
    assert(q15_mul(0, 0) == 0);
    assert(q15_mul(Q15_MAX, 0) == 0);
    /* 0.99997 * 0.99997 = 0.99994 -> 32766 */
    assert(q15_mul(Q15_MAX, Q15_MAX) == 32766);
    /* x * 0.99997 은 x보다 1 LSB 작거나 같다 */
    assert(q15_mul(16384, Q15_MAX) == 16384);      /* 반올림으로 그대로 */
    /* -1.0 * 0.99997 = -0.99997 */
    assert(q15_mul(Q15_MIN, Q15_MAX) == -32767);
    /* (-1.0) * (-1.0) = +1.0 은 Q15에 없다 -> 포화 */
    assert(q15_mul(Q15_MIN, Q15_MIN) == 32767);
    /* 0.5 * 0.5 = 0.25 */
    assert(q15_mul(16384, 16384) == 8192);
    /* -0.5 * 0.5 = -0.25 */
    assert(q15_mul(-16384, 16384) == -8192);
    printf("  ok  q15_mul identities incl. the (-1.0)x(-1.0) saturation case\n");
}

static void test_q15_mul_vs_float(void)
{
    /* float 기준값과 1 LSB 이내로 같아야 한다(round-half-up 차이 1칸 허용). */
    int32_t worst = 0;
    for (int32_t a = -32768; a <= 32767; a += 257) {
        for (int32_t b = -32768; b <= 32767; b += 263) {
            q15_t got = q15_mul((q15_t)a, (q15_t)b);
            float  ref = ((float)a / 32768.0f) * ((float)b / 32768.0f) * 32768.0f;
            if (ref > 32767.0f) { ref = 32767.0f; }
            int32_t d = abs32((int32_t)got - (int32_t)(ref < 0.0f ? ref - 0.5f
                                                                 : ref + 0.5f));
            if (d > worst) { worst = d; }
        }
    }
    assert(worst <= 1);
    printf("  ok  q15_mul vs float reference: max error %d LSB (bound 1)\n", worst);
}

static void test_ma_warmup_and_window(void)
{
    ma_t f;
    ma_init(&f);

    /* 워밍업 구간에서는 지금까지 들어온 개수로 나눈다.
     * 1000 하나만 들어오면 평균은 1000이어야 한다(0으로 희석하지 않는다). */
    assert(ma_push(&f, 1000) == 1000);
    assert(ma_push(&f, 2000) == 1500);
    assert(ma_push(&f, 3000) == 2000);

    /* 창을 다 채운 뒤 상수 입력이면 정확히 그 상수가 나온다. */
    ma_init(&f);
    for (unsigned i = 0u; i < 3u * MA_N; i++) {
        int16_t y = ma_push(&f, 1234);
        assert(y == 1234);
    }

    /* 창 길이만큼 지나면 옛 샘플은 완전히 사라진다(FIR = 유한 기억). */
    ma_init(&f);
    (void)ma_push(&f, 32767);
    for (unsigned i = 0u; i < MA_N; i++) { (void)ma_push(&f, 0); }
    assert(ma_push(&f, 0) == 0);
    printf("  ok  moving average: warm-up divides by n, window forgets exactly\n");
}

static void test_ma_rounding_and_extremes(void)
{
    ma_t f;

    /* 0에서 먼 쪽으로 반올림: 합 4, n 8 -> 0.5 -> 1 */
    ma_init(&f);
    int16_t y = 0;
    for (unsigned i = 0u; i < MA_N; i++) { y = ma_push(&f, (i < 4u) ? 1 : 0); }
    assert(y == 1);
    /* 대칭: 합 -4, n 8 -> -0.5 -> -1 */
    ma_init(&f);
    for (unsigned i = 0u; i < MA_N; i++) { y = ma_push(&f, (i < 4u) ? -1 : 0); }
    assert(y == -1);

    /* 전부 최대값: 러닝 합은 8 * 32767 = 262136 으로 int32에 넉넉히 들어간다.
     * int16로 합을 들고 있었다면 첫 두 샘플에서 이미 깨진다. */
    ma_init(&f);
    for (unsigned i = 0u; i < 2u * MA_N; i++) { y = ma_push(&f, Q15_MAX); }
    assert(y == Q15_MAX);
    assert(f.sum == 8L * 32767L);

    /* 전부 최소값 */
    ma_init(&f);
    for (unsigned i = 0u; i < 2u * MA_N; i++) { y = ma_push(&f, Q15_MIN); }
    assert(y == Q15_MIN);
    assert(f.sum == -8L * 32768L);
    printf("  ok  moving average: away-from-zero rounding, full-scale sum safe\n");
}

static void test_ma_vs_float(void)
{
    /* 창이 찬 뒤로는 float 평균과 1 LSB 이내여야 한다(반올림 차이뿐). */
    ma_t f;
    ma_init(&f);
    lcg_reset();

    float win[MA_N];
    for (unsigned i = 0u; i < MA_N; i++) { win[i] = 0.0f; }
    unsigned wi = 0u;
    float worst = 0.0f;

    for (unsigned k = 0u; k < 2000u; k++) {
        q15_t x = test_signal(k);
        int16_t got = ma_push(&f, x);
        win[wi] = (float)x;
        wi = (wi + 1u) % MA_N;
        if (k >= MA_N) {
            float s = 0.0f;
            for (unsigned i = 0u; i < MA_N; i++) { s += win[i]; }
            float ref = s / (float)MA_N;
            float d = absf((float)got - ref);
            if (d > worst) { worst = d; }
        }
    }
    assert(worst <= 0.5f + 1e-6f);
    printf("  ok  moving average vs float: max error %.3f LSB (bound 0.5)\n",
           (double)worst);
}

static void test_iir_step_response(void)
{
    iir_t f;

    /* alpha = 0: 완전히 얼어붙는다 */
    iir_init(&f, 0, 100);
    for (int i = 0; i < 50; i++) { assert(iir_push(&f, 20000) == 100); }

    /* alpha = 1.0(에 가장 가까운 Q15_MAX): 거의 즉시 따라간다 */
    iir_init(&f, Q15_MAX, 0);
    q15_t y = iir_push(&f, 20000);
    assert(y >= 19999 && y <= 20000);

    /* alpha = 0.5: 첫 스텝에 절반, 두 번째에 3/4 */
    iir_init(&f, 16384, 0);
    y = iir_push(&f, 20000);
    assert(y == 10000);
    y = iir_push(&f, 20000);
    assert(y == 15000);

    /* 상수 입력을 오래 넣으면 dead band 없이 정확히 그 값에 도달한다.
     * (y += (x-y) >> k 형태는 여기서 x-1, x-2에 멈춰 선다) */
    iir_init(&f, 328, 0);            /* alpha = 0.01 */
    for (int i = 0; i < 5000; i++) { y = iir_push(&f, 12345); }
    assert(y == 12345);
    printf("  ok  IIR: alpha=0 frozen, 0.5 halves the gap, converges exactly\n");
}

static void test_iir_extremes_no_overflow(void)
{
    /* 최악의 조합: alpha 최대, 입력이 매 샘플 +full <-> -full로 튄다.
     * err가 ±65535까지 가고 alpha가 32767이라 곱이 int32 한계에 가장 가깝다. */
    iir_t f;
    iir_init(&f, Q15_MAX, Q15_MIN);
    for (int i = 0; i < 1000; i++) {
        q15_t x = (i & 1) ? Q15_MAX : Q15_MIN;
        q15_t y = iir_push(&f, x);
        assert(y >= Q15_MIN && y <= Q15_MAX);
        /* acc가 Q30 범위를 크게 벗어나지 않는지도 본다 */
        assert(f.acc <= 1073741824L && f.acc >= -1073741824L);
    }
    /* 초기값이 full-scale인 채로 반대 full-scale을 넣어도 안전 */
    iir_init(&f, Q15_MAX, Q15_MAX);
    (void)iir_push(&f, Q15_MIN);
    iir_init(&f, Q15_MIN + 1, Q15_MIN);   /* alpha 음수는 0으로 클램프됨 */
    assert(f.alpha == 0);
    printf("  ok  IIR extremes: full-scale ping-pong stays in range, no overflow\n");
}

static void test_iir_vs_float(void)
{
    /* 고정소수점 IIR을 float 기준 IIR과 나란히 돌린다.
     * 오차 한계: 누적 오차의 정상상태 상한은 출력 1 LSB 수준이다.
     *   acc는 정확한 정수 연산이고, 유일한 근사는 err를 구할 때 acc를 Q15로
     *   내림(acc >> 15)하는 것뿐이다. 그 오차는 항상 [0, 1) LSB이고,
     *   e_{n+1} = e_n (1 - alpha) + alpha * delta 로 감쇠하므로
     *   정상상태 |e| <= max|delta| = 1 LSB.
     * 실측 여유를 두어 2 LSB로 못박는다. */
    const q15_t alphas[4] = { 328, 3277, 16384, 29491 };  /* 0.01 0.1 0.5 0.9 */
    float worst_all = 0.0f;

    for (unsigned a = 0u; a < 4u; a++) {
        iir_t f;
        iir_init(&f, alphas[a], 0);
        float yref = 0.0f;
        float af = (float)alphas[a] / 32768.0f;
        lcg_reset();
        float worst = 0.0f;

        for (unsigned k = 0u; k < 4000u; k++) {
            q15_t x = test_signal(k);
            q15_t got = iir_push(&f, x);
            yref += af * ((float)x - yref);
            float d = absf((float)got - yref);
            if (d > worst) { worst = d; }
        }
        assert(worst <= 2.0f);
        if (worst > worst_all) { worst_all = worst; }
    }
    printf("  ok  IIR vs float (alpha 0.01/0.1/0.5/0.9): max error %.3f LSB"
           " (bound 2.0)\n", (double)worst_all);
}

static void test_iir_is_lowpass(void)
{
    /* 노이즈가 실제로 줄어드는지 숫자로 확인한다.
     * 같은 신호에 대해 |y[k] - y[k-1]|의 평균이 입력보다 훨씬 작아야 한다. */
    iir_t f;
    iir_init(&f, 3277, 0);           /* alpha = 0.1 */
    lcg_reset();

    int32_t in_var = 0, out_var = 0;
    q15_t prev_x = 0, prev_y = 0;
    for (unsigned k = 0u; k < 2000u; k++) {
        q15_t x = test_signal(k);
        q15_t y = iir_push(&f, x);
        if (k > 0u) {
            in_var  += abs32((int32_t)x - prev_x);
            out_var += abs32((int32_t)y - prev_y);
        }
        prev_x = x;
        prev_y = y;
    }
    assert(out_var * 4 < in_var);     /* 적어도 4배는 매끄러워야 한다 */
    printf("  ok  IIR alpha=0.1 smooths: sum|dy| = %d vs sum|dx| = %d\n",
           out_var, in_var);
}

static void test_arithmetic_shift_assumption(void)
{
    /* 이 파일은 음수의 >>가 산술 시프트(부호 유지)라고 가정한다.
     * C 표준은 implementation-defined라고만 말하므로 여기서 못박는다. */
    assert((-2 >> 1) == -1);
    assert((-1 >> 1) == -1);
    int32_t q30 = -1073741824;                   /* = -1.0을 Q30으로 */
    assert((q30 >> 15) == -32768);
    printf("  ok  platform uses arithmetic right shift for negative values\n");
}

int main(void)
{
    printf("15_fixed_point_filter (starter)\n");
    test_arithmetic_shift_assumption();
    test_sat16();
    test_q15_mul_identities();
    test_q15_mul_vs_float();
    test_ma_warmup_and_window();
    test_ma_rounding_and_extremes();
    test_ma_vs_float();
    test_iir_step_response();
    test_iir_extremes_no_overflow();
    test_iir_vs_float();
    test_iir_is_lowpass();
    printf("ALL TESTS PASSED\n");
    return 0;
}
