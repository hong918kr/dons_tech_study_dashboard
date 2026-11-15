/* ENV: qemu */
/* ARM-B13 추가 연습 검증 (펌웨어) — practice.S 의 4 함수 + 연습 5 (xPSR 의 IT 상태 해독, C).
 * build+run (VM): bash run_qemu.sh practice   (practice.c + practice.S + shared_*.c/.S, -T arm_b13.ld)
 */
#include <stdint.h>

void sh_puts(const char *s);
void sh_dec(int32_t v);

int32_t sign_s32(int32_t x);
int32_t max_array_s32(const int32_t *a, uint32_t n);
int32_t sat_add_s32(int32_t a, int32_t b);
uint32_t count_in_range_s32(const int32_t *a, uint32_t n, int32_t lo, int32_t hi);
typedef void (*anyfn_t)(void);
int32_t rc_call4(anyfn_t fn, const int32_t args[4], uint32_t *bad);

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; sh_puts("PASS " #expr "\n"); } \
    else { sh_puts("FAIL practice.c:"); sh_dec(__LINE__); sh_puts("  " #expr "\n"); } } while (0)

static uint32_t g_rng = 0x9E3779B9u;
static uint32_t rnd(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }
static int32_t rnd_s(void) {
    uint32_t k = rnd() & 7u, r = rnd();
    return k == 0 ? (int32_t)(r & 0xFFu) - 128 : k == 1 ? INT32_MIN + (int32_t)(r & 3u)
         : k == 2 ? INT32_MAX - (int32_t)(r & 3u) : (int32_t)r;
}

/* C 참조 (UB 없음: 합은 int64_t 로) */
static int32_t ref_sign(int32_t x) { return (x > 0) - (x < 0); }
static int32_t ref_sat_add(int32_t a, int32_t b) {
    int64_t s = (int64_t)a + b;
    return s > INT32_MAX ? INT32_MAX : s < INT32_MIN ? INT32_MIN : (int32_t)s;
}

/* 연습 5: xPSR 에서 IT 상태 해독. ITSTATE[7:0] = { xPSR[15:10], xPSR[26:25] }.
 * ITSTATE[3:0] == 0 이면 IT 블록 밖 → -1, 아니면 다음 명령의 조건 = ITSTATE[7:4] (0..14). */
static int it_next_cond(uint32_t xpsr) {
    uint32_t it = ((xpsr >> 8) & 0xFCu) | ((xpsr >> 25) & 0x3u);
    return (it & 0xFu) ? (int)(it >> 4) : -1;
}
static uint32_t it_insns_left(uint32_t xpsr) {        /* 남은 명령 수 = ITSTATE[3:0] 의 맨 아래 1 의 위치로 */
    uint32_t it = ((xpsr >> 8) & 0xFCu) | ((xpsr >> 25) & 0x3u), low = it & 0xFu, n = 4u;
    if (low == 0u) return 0u;                         /* 끝표시 1 이 bit0 → 4개, bit1 → 3개, bit2 → 2개, bit3 → 1개 */
    while (!(low & 1u)) { low >>= 1; n--; }
    return n;
}

int main(void) {
    sh_puts("ARM-B13 practice\n");
    /* 연습 1 */
    CHECK(sign_s32(42) == 1 && sign_s32(0) == 0 && sign_s32(-42) == -1);
    CHECK(sign_s32(INT32_MIN) == -1 && sign_s32(INT32_MAX) == 1);
    int bad_sign = 0;
    for (int k = 0; k < 2000; k++) { int32_t x = rnd_s(); bad_sign += sign_s32(x) != ref_sign(x); }
    CHECK(bad_sign == 0);

    /* 연습 2 */
    static const int32_t v[5] = { -9, 3, INT32_MIN, 3, -1 };
    CHECK(max_array_s32(v, 5) == 3);
    CHECK(max_array_s32(v, 0) == INT32_MIN);
    CHECK(max_array_s32(&v[2], 1) == INT32_MIN);
    static int32_t buf[64];
    int bad_maxa = 0;
    for (uint32_t n = 1; n <= 64; n++) {
        int32_t best = INT32_MIN;
        for (uint32_t i = 0; i < n; i++) { buf[i] = rnd_s(); if (buf[i] > best) best = buf[i]; }
        bad_maxa += max_array_s32(buf, n) != best;
    }
    CHECK(bad_maxa == 0);

    /* 연습 3 */
    CHECK(sat_add_s32(INT32_MAX, 1) == INT32_MAX);
    CHECK(sat_add_s32(INT32_MIN, -1) == INT32_MIN);
    CHECK(sat_add_s32(INT32_MIN, INT32_MIN) == INT32_MIN && sat_add_s32(INT32_MAX, INT32_MAX) == INT32_MAX);
    CHECK(sat_add_s32(-5, 3) == -2 && sat_add_s32(INT32_MAX, INT32_MIN) == -1);
    int bad_sat = 0;
    for (int k = 0; k < 2000; k++) { int32_t a = rnd_s(), b = rnd_s(); bad_sat += sat_add_s32(a, b) != ref_sat_add(a, b); }
    CHECK(bad_sat == 0);

    /* 연습 4 */
    static const int32_t s[8] = { -1, 0, 1, 4095, 4096, 2048, INT32_MIN, INT32_MAX };
    CHECK(count_in_range_s32(s, 8, 0, 4095) == 4u);      /* 0, 1, 4095, 2048 */
    CHECK(count_in_range_s32(s, 0, 0, 4095) == 0u);
    CHECK(count_in_range_s32(s, 8, INT32_MIN, INT32_MAX) == 8u);
    CHECK(count_in_range_s32(s, 8, 5000, 5000) == 0u);   /* x < lo 쪽: 비교 순서가 틀리면 여기서 깨진다 */
    int bad_cnt = 0;
    for (uint32_t n = 0; n <= 64; n++) {
        int32_t lo = rnd_s(), hi = rnd_s();
        if (lo > hi) { int32_t t = lo; lo = hi; hi = t; }
        uint32_t ref = 0;
        for (uint32_t i = 0; i < n; i++) {
            buf[i] = (rnd() & 1u) ? rnd_s() : lo + (int32_t)(rnd() & 7u) - 4;   /* 경계 근처를 섞는다 */
            ref += (lo <= buf[i] && buf[i] <= hi);
        }
        bad_cnt += count_in_range_s32(buf, n, lo, hi) != ref;
    }
    CHECK(bad_cnt == 0);
    uint32_t m = 0xFFu;
    const int32_t args[4] = { (int32_t)(uintptr_t)s, 8, 0, 4095 };
    CHECK(rc_call4((anyfn_t)count_in_range_s32, args, &m) == 4 && m == 0u);

    /* 연습 5: demo_it.gdb 에서 실제로 본 xPSR 값들 */
    CHECK(it_next_cond(0x2100b400u) == 0xB && it_insns_left(0x2100b400u) == 2u);  /* ite lt 직후: LT, 2개 */
    CHECK(it_next_cond(0x2100a800u) == 0xA && it_insns_left(0x2100a800u) == 1u);  /* 다음: GE (else 칸) */
    CHECK(it_next_cond(0x2100c800u) == 0xC && it_insns_left(0x2100c800u) == 1u);  /* it gt 직후: GT */
    CHECK(it_next_cond(0x21000000u) == -1 && it_insns_left(0xa1000000u) == 0u);   /* IT 밖 */

    sh_dec(g_pass); sh_puts("/"); sh_dec(g_total); sh_puts(" passed\n");
    return g_pass == g_total ? 0 : 1;
}
