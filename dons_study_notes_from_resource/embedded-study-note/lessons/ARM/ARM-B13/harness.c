/* ENV: qemu */
/* ARM-B13 테스트 하네스 (펌웨어) — Cortex-M3 on qemu-system-arm -M mps2-an385, 출력은 세미호스팅.
 * starter.S / solution.S 의 Thumb-2 함수를 C 참조 모델과 비교한다. 종료 코드 = 0 (전부 PASS) / 1.
 * build+run (VM): bash run_qemu.sh starter|solution
 *   sources = harness.c + <v>.S + shared_startup.c + shared_regcheck.S, -T arm_b13.ld
 * C 에서 UB 인 값(abs(INT32_MIN), 부호 있는 합 오버플로)은 C 로 계산하지 않는다:
 * 손으로 쓴 기대값과 비교하거나, 참조 모델을 uint32_t (mod 2^32, UB 없음) 로 계산한다.
 */
#include <stdint.h>

void sh_puts(const char *s);
void sh_hex(uint32_t v);
void sh_dec(int32_t v);

/* 어셈블리가 만족해야 할 C 프로토타입 (AAPCS: r0–r3 인자, r0 반환) */
int32_t max_s32(int32_t a, int32_t b);
int32_t abs_s32(int32_t x);
int32_t sum_array_s32(const int32_t *a, uint32_t n);
uint32_t max_u32(uint32_t a, uint32_t b);
int32_t clamp_s32(int32_t x, int32_t lo, int32_t hi);
int32_t sum_clamped_s32(const int32_t *a, uint32_t n, int32_t lo, int32_t hi);
typedef void (*anyfn_t)(void);                       /* 범용 함수 포인터 (-Wcast-function-type 예외) */
int32_t rc_call4(anyfn_t fn, const int32_t args[4], uint32_t *bad);   /* shared_regcheck.S */

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; sh_puts("PASS " #expr "\n"); } \
    else { sh_puts("FAIL harness.c:"); sh_dec(__LINE__); sh_puts("  " #expr "\n"); } } while (0)

/* ---------------- C 참조 모델 (UB 없는 식만) ---------------- */
static int32_t ref_max(int32_t a, int32_t b) { return a > b ? a : b; }
static uint32_t ref_max_u(uint32_t a, uint32_t b) { return a > b ? a : b; }
static uint32_t ref_abs_u(int32_t x) { return x < 0 ? 0u - (uint32_t)x : (uint32_t)x; }
static int32_t ref_clamp(int32_t x, int32_t lo, int32_t hi) { return x < lo ? lo : (x > hi ? hi : x); }
static uint32_t ref_sum_u(const int32_t *a, uint32_t n) {
    uint32_t s = 0;
    for (uint32_t i = 0; i < n; i++) s += (uint32_t)a[i];
    return s;
}
static uint32_t ref_sum_clamped_u(const int32_t *a, uint32_t n, int32_t lo, int32_t hi) {
    uint32_t s = 0;
    for (uint32_t i = 0; i < n; i++) s += (uint32_t)ref_clamp(a[i], lo, hi);
    return s;
}

static uint32_t g_rng = 0x2545F491u;                 /* xorshift32, 고정 시드 → 결정적 */
static uint32_t rnd(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }
static int32_t rnd_s(void) {                         /* 가끔 경계값이 나오게 섞는다 */
    uint32_t k = rnd() & 7u, r = rnd();
    return k == 0 ? (int32_t)(r & 0xFFu) - 128 : k == 1 ? INT32_MIN + (int32_t)(r & 3u)
         : k == 2 ? INT32_MAX - (int32_t)(r & 3u) : (int32_t)r;
}

static const int32_t E[] = { 0, 1, -1, 2, -2, 100, -100, 4095, 4096, 0x7FFF,
                             INT32_MAX, INT32_MAX - 1, INT32_MIN, INT32_MIN + 1 };
#define NE ((int)(sizeof E / sizeof E[0]))

static int32_t g_buf[64];

static void part_a(void) {
    sh_puts("--- Part A: max_s32 / abs_s32 / sum_array_s32 ---\n");
    CHECK(max_s32(3, 7) == 7);
    CHECK(max_s32(-5, -9) == -5);
    CHECK(max_s32(INT32_MIN, INT32_MAX) == INT32_MAX);
    CHECK(max_s32(-1, 1) == 1);                      /* 부호 있는 비교: -1 < 1 */
    int bad_max = 0;
    for (int i = 0; i < NE; i++)
        for (int j = 0; j < NE; j++) bad_max += max_s32(E[i], E[j]) != ref_max(E[i], E[j]);
    for (int k = 0; k < 2000; k++) { int32_t a = rnd_s(), b = rnd_s(); bad_max += max_s32(a, b) != ref_max(a, b); }
    CHECK(bad_max == 0 /* max_s32: 14x14 edge + 2000 random */);

    CHECK(abs_s32(-5) == 5);
    CHECK(abs_s32(0) == 0);
    CHECK(abs_s32(INT32_MAX) == INT32_MAX);
    CHECK(abs_s32(-INT32_MAX) == INT32_MAX);
    CHECK(abs_s32(INT32_MIN) == INT32_MIN);          /* 손으로 쓴 기대값: ISA 는 래핑 (C 의 -INT32_MIN 은 UB) */
    int bad_abs = 0;
    for (int i = 0; i < NE; i++) bad_abs += (uint32_t)abs_s32(E[i]) != ref_abs_u(E[i]);
    for (int k = 0; k < 2000; k++) { int32_t x = rnd_s(); bad_abs += (uint32_t)abs_s32(x) != ref_abs_u(x); }
    CHECK(bad_abs == 0 /* abs_s32: edge + 2000 random, compared as uint32 */);

    static const int32_t a4[4] = { 1, 2, 3, 4 }, m3[3] = { -1, -2, -3 }, ov[2] = { INT32_MAX, 1 };
    CHECK(sum_array_s32(a4, 4) == 10);
    CHECK(sum_array_s32(m3, 3) == -6);
    CHECK(sum_array_s32(ov, 2) == INT32_MIN);        /* 손으로 쓴 기대값: 0x7FFFFFFF + 1 → 0x80000000 */
    CHECK(sum_array_s32(a4, 0) == 0);
    CHECK(sum_array_s32((const int32_t *)0xFFFFFFF0u, 0) == 0);  /* n == 0 이면 포인터를 읽으면 안 된다 */
    int bad_sum = 0;
    for (uint32_t n = 0; n <= 64; n++) {
        for (uint32_t i = 0; i < n; i++) g_buf[i] = rnd_s();
        bad_sum += (uint32_t)sum_array_s32(g_buf, n) != ref_sum_u(g_buf, n);
    }
    CHECK(bad_sum == 0 /* sum_array_s32: n = 0..64 random, mod 2^32 */);
}

static void part_b(void) {
    sh_puts("--- Part B: unsigned 조건, Thumb 비트, callee-saved ---\n");
    CHECK(max_u32(0xFFFFFFFFu, 1u) == 0xFFFFFFFFu);  /* 같은 비트를 max_s32 에 주면 1 (위 Part A) */
    CHECK(max_u32(0x80000000u, 0x7FFFFFFFu) == 0x80000000u);
    CHECK(max_u32(0u, 0u) == 0u);
    int bad_maxu = 0;
    for (int i = 0; i < NE; i++)
        for (int j = 0; j < NE; j++)
            bad_maxu += max_u32((uint32_t)E[i], (uint32_t)E[j]) != ref_max_u((uint32_t)E[i], (uint32_t)E[j]);
    for (int k = 0; k < 2000; k++) { uint32_t a = rnd(), b = rnd(); bad_maxu += max_u32(a, b) != ref_max_u(a, b); }
    CHECK(bad_maxu == 0 /* max_u32: edge + 2000 random */);

    /* 함수 주소의 bit0 = 1 (Thumb). .type %function / .thumb_func 가 없으면 0 이 되고 blx 에서 폴트 */
    CHECK(((uintptr_t)max_s32 & 1u) == 1u);
    CHECK(((uintptr_t)abs_s32 & (uintptr_t)sum_array_s32 & (uintptr_t)max_u32 & 1u) == 1u);
    CHECK(((uintptr_t)clamp_s32 & (uintptr_t)sum_clamped_s32 & 1u) == 1u);

    /* r4–r11, sp 를 지켰나: rc_call4 가 표지값으로 검사 */
    uint32_t m = 0xFFu;
    const int32_t a2[4] = { -3, 9, 0, 0 };
    CHECK(rc_call4((anyfn_t)max_s32, a2, &m) == 9 && m == 0u);
    const int32_t a1[4] = { -42, 0, 0, 0 };
    CHECK(rc_call4((anyfn_t)abs_s32, a1, &m) == 42 && m == 0u);
}

static void part_c(void) {
    sh_puts("--- Part C: clamp_s32 (ITE) / sum_clamped_s32 (non-leaf) ---\n");
    CHECK(clamp_s32(5000, 0, 4095) == 4095);         /* ADC 12비트 범위로 */
    CHECK(clamp_s32(-7, 0, 4095) == 0);
    CHECK(clamp_s32(100, 0, 4095) == 100);
    CHECK(clamp_s32(0, 0, 4095) == 0 && clamp_s32(4095, 0, 4095) == 4095);
    CHECK(clamp_s32(9, 3, 3) == 3 && clamp_s32(-9, 3, 3) == 3);
    CHECK(clamp_s32(INT32_MIN, INT32_MIN, INT32_MAX) == INT32_MIN);
    int bad_clamp = 0;
    for (int i = 0; i < NE; i++)
        for (int j = 0; j < NE; j++)
            for (int k = 0; k < NE; k++)
                if (E[j] <= E[k]) bad_clamp += clamp_s32(E[i], E[j], E[k]) != ref_clamp(E[i], E[j], E[k]);
    for (int t = 0; t < 2000; t++) {
        int32_t x = rnd_s(), lo = rnd_s(), hi = rnd_s();
        if (lo > hi) { int32_t s = lo; lo = hi; hi = s; }
        bad_clamp += clamp_s32(x, lo, hi) != ref_clamp(x, lo, hi);
    }
    CHECK(bad_clamp == 0 /* clamp_s32: 14^3 edge (lo<=hi) + 2000 random */);

    /* ADC 프레임: 12비트 샘플 + 글리치(-32768, 65535) → [0, 4095] 로 자르고 합 */
    static const int32_t adc[5] = { 100, 4095, 65535, -32768, 2048 };
    CHECK(sum_clamped_s32(adc, 5, 0, 4095) == 10338);   /* 100 + 4095 + 4095 + 0 + 2048 */
    CHECK(sum_clamped_s32(adc, 0, 0, 4095) == 0);
    int bad_sumc = 0;
    for (uint32_t n = 0; n <= 64; n++) {
        int32_t lo = rnd_s(), hi = rnd_s();
        if (lo > hi) { int32_t s = lo; lo = hi; hi = s; }
        for (uint32_t i = 0; i < n; i++) g_buf[i] = rnd_s();
        bad_sumc += (uint32_t)sum_clamped_s32(g_buf, n, lo, hi) != ref_sum_clamped_u(g_buf, n, lo, hi);
    }
    CHECK(bad_sumc == 0 /* sum_clamped_s32: n = 0..64 random */);
    uint32_t m = 0xFFu;
    const int32_t a3[4] = { 5000, 0, 4095, 0 };
    CHECK(rc_call4((anyfn_t)clamp_s32, a3, &m) == 4095 && m == 0u);
    const int32_t a4[4] = { (int32_t)(uintptr_t)adc, 5, 0, 4095 };    /* 포인터도 r0 로 (32비트) */
    CHECK(rc_call4((anyfn_t)sum_clamped_s32, a4, &m) == 10338 && m == 0u);
}

int main(void) {
    sh_puts("ARM-B13 Thumb-2 on Cortex-M3 (QEMU mps2-an385)\n");
    part_a();
    part_b();
    part_c();
    sh_dec(g_pass); sh_puts("/"); sh_dec(g_total); sh_puts(" passed\n");
    return g_pass == g_total ? 0 : 1;
}
