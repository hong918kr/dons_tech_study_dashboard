/* ARM-M06 추가 연습 검증 — practice.S 의 4개 함수
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c practice.S shared_bugs.S -o pr && ./pr
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern float   dot_f32_asm4(const float *a, const float *b, size_t n);
extern int32_t dot_s8_nodot(const int8_t *a, const int8_t *b, size_t n);
extern float   maxabs_f32(const float *x, size_t n);
extern void    saxpy_f32_asm(float *y, float a, const float *x, size_t n);

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint64_t g_rng = 0x9E3779B97F4A7C15ull;
static uint64_t xs(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 7; g_rng ^= g_rng << 17; return g_rng; }
static float rnd_f(float lo, float hi) { return lo + (hi - lo) * (float)(xs() >> 40) * 0x1p-24f; }
static uint32_t f_bits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }

/* |ŝ − s| <= γ_n Σ|a_i b_i| (+ double 참조 오차) — solution.c 와 같은 한계 */
static int dot_ok(float got, const float *a, const float *b, size_t n) {
    double s = 0, sa = 0;
    for (size_t i = 0; i < n; i++) { double p = (double)a[i] * b[i]; s += p; sa += fabs(p); }
    double k = (double)n, bound = (k * 0x1p-24 / (1 - k * 0x1p-24) + k * 0x1p-53 / (1 - k * 0x1p-53)) * sa;
    return isfinite(got) && fabs((double)got - s) <= bound;
}

int main(void) {
    enum { MAXN = 140 };
    static float a[MAXN + 4], b[MAXN + 4], y[MAXN + 12], yref[MAXN + 12];
    static int8_t p[MAXN + 16], q[MAXN + 16];

    /* 연습 1: 누산기 4개 — n = 0..140 × 오프셋 0..3, 무작위 + 작은 정수(정확) */
    int bad1 = 0, bad1x = 0;
    for (size_t off = 0; off < 4; off++)
    for (size_t n = 0; n <= MAXN; n++) {
        for (size_t i = 0; i < n + off; i++) { a[i] = rnd_f(-1, 1); b[i] = rnd_f(-1, 1); }
        bad1 += !dot_ok(dot_f32_asm4(a + off, b + off, n), a + off, b + off, n);
        int64_t ex = 0;
        for (size_t i = 0; i < n + off; i++) { a[i] = (float)((int)(xs() % 9) - 4); b[i] = (float)((int)(xs() % 9) - 4); }
        for (size_t i = 0; i < n; i++) ex += (int64_t)a[off + i] * (int64_t)b[off + i];
        bad1x += dot_f32_asm4(a + off, b + off, n) != (float)ex;
    }
    CHECK(bad1 == 0);
    CHECK(bad1x == 0);
    { float o[20]; for (int i = 0; i < 20; i++) o[i] = 1.0f;
      CHECK(dot_f32_asm4(o, o, 20) == 20.0f && dot_f32_asm4(o, o, 0) == 0.0f && dot_f32_asm4(o, o, 19) == 19.0f); }

    /* 연습 2: SDOT 없는 int8 내적 — int64 참조와 정확히 */
    int bad2 = 0;
    for (size_t off = 0; off < 16; off++)
    for (size_t n = 0; n <= MAXN; n++) {
        for (size_t i = 0; i < n + off; i++) { p[i] = (int8_t)(uint8_t)(xs() >> 56); q[i] = (int8_t)(uint8_t)(xs() >> 56); }
        int64_t ref = 0;
        for (size_t i = 0; i < n; i++) ref += (int64_t)p[off + i] * q[off + i];
        bad2 += (int64_t)dot_s8_nodot(p + off, q + off, n) != ref;
    }
    CHECK(bad2 == 0);
    {
        size_t n = 131071;
        int8_t *u = malloc(n), *v = malloc(n);
        memset(u, 0x80, n); memset(v, 0x80, n);
        CHECK(dot_s8_nodot(u, v, n) == 2147467264);           /* SMULL 곱 16384 도 int16 에 들어간다 */
        memset(v, 0x7f, n);
        CHECK(dot_s8_nodot(u, v, n) == -2130690176);
        free(u); free(v);
    }

    /* 연습 3: max |x| — 비트까지 같아야 한다 (fmax/fabs 는 반올림이 없다) */
    int bad3 = 0;
    for (size_t off = 0; off < 4; off++)
    for (size_t n = 0; n <= MAXN; n++) {
        for (size_t i = 0; i < n + off; i++) a[i] = rnd_f(-100, 100);
        if (n) a[off + xs() % n] = (xs() & 1) ? -1e30f : 0x1p-149f;   /* 큰 음수 / 비정규수 */
        float ref = 0.0f;
        for (size_t i = 0; i < n; i++) ref = fmaxf(ref, fabsf(a[off + i]));
        bad3 += f_bits(maxabs_f32(a + off, n)) != f_bits(ref);
    }
    CHECK(bad3 == 0);
    { float z[5] = { -0.0f, -0.0f, -0.0f, -0.0f, -INFINITY };
      CHECK(f_bits(maxabs_f32(z, 4)) == 0 && maxabs_f32(z, 5) == INFINITY && f_bits(maxabs_f32(z, 0)) == 0); }

    /* 연습 4: saxpy — fmaf 와 비트 동일 + y[n..] canary 보존 */
    int bad4 = 0;
    for (size_t off = 0; off < 4; off++)
    for (size_t n = 0; n <= MAXN; n++) {
        float al = rnd_f(-3, 3);
        for (size_t i = 0; i < n + off; i++) b[i] = rnd_f(-10, 10);
        for (size_t i = 0; i < MAXN + 12; i++) y[i] = yref[i] = rnd_f(-10, 10);
        for (size_t i = 0; i < n; i++) yref[off + i] = fmaf(al, b[off + i], yref[off + i]);
        saxpy_f32_asm(y + off, al, b + off, n);
        bad4 += memcmp(y, yref, sizeof y) != 0;
    }
    CHECK(bad4 == 0);
    { float yy[6] = { 1, 1, 1, 1, 1, 1 }, xx[6] = { 1, 2, 3, 4, 5, 6 };
      saxpy_f32_asm(yy, 2.0f, xx, 5);
      CHECK(yy[0] == 3 && yy[3] == 9 && yy[4] == 11 && yy[5] == 1); }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
