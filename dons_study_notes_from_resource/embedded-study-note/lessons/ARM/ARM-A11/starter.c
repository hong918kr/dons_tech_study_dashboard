/* ARM-A11 컴파일러 최적화 출력 판독: 벡터화와 언롤 — 커널 + 테스트 하네스
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o /tmp/ARM-A11_solution && /tmp/ARM-A11_solution   (starter 는 이름만 바꿔서)
 * 벡터화 판정: bash check_vec.sh   (이 파일을 -O2/-O3/-Os 로 다시 컴파일해 리마크·asm 을 함수별로 확인)
 * 규칙: 정수 커널은 참조와 정확히 같아야 한다. 순서를 지키는 float 커널도 비트까지 같아야 한다.
 *       재결합(reassociate)한 float 합만 오차 한계 γ_n·Σ|선택된 a_i| 로 비교한다.
 *       restrict 커널에 겹친 버퍼를 넣는 것은 UB → 테스트에서 절대 부르지 않는다 (demo_restrict_lie.c 참고).
 */
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* 커널은 인라인 금지: 테스트가 check_vec.sh 가 보는 "바로 그 함수 본체"를 돌리게 한다 */
#define KERNEL __attribute__((noinline))
/* 참조 구현: 벡터화·인터리브를 끈 순차 루프 (C 의미 그대로 한 원소씩) */
#define NOVEC _Pragma("clang loop vectorize(disable) interleave(disable)")

/* ======================= 주어진 커널 (고치지 않는다) ======================= */

/* (1) saxpy: y = a*x + y. x 와 y 가 겹칠 수 있다 → 컴파일러는 런타임 겹침 검사(vector.memcheck)를 넣는다 */
KERNEL void saxpy_plain(float *y, const float *x, float a, size_t n) {
    for (size_t i = 0; i < n; i++) y[i] = a * x[i] + y[i];
}

/* (2) 히스토그램: h[p[i]]++ — 같은 칸이 연속으로 나오면 앞 반복의 저장을 읽어야 한다 (메모리 의존) */
KERNEL void hist_plain(uint32_t *h, const uint8_t *p, size_t n) {
    for (size_t i = 0; i < n; i++) h[p[i]]++;
}

/* (3) 조건부 합 (float, if 형태) — 순서를 지켜야 하고 -0.0 도 지켜야 한다 */
KERNEL float csum_f32_if(const float *a, size_t n, float t) {
    float s = 0.0f;
    for (size_t i = 0; i < n; i++)
        if (a[i] > t) s += a[i];
    return s;
}

/* (4) 조건부 합 (int32 → int64) — 정수는 재결합 자유 */
KERNEL int64_t csum_i32(const int32_t *a, size_t n, int32_t t) {
    int64_t s = 0;
    for (size_t i = 0; i < n; i++)
        if (a[i] > t) s += a[i];
    return s;
}

/* (5) int16 내적 → int32 (계약: 결과와 모든 부분합이 int32 범위 — 넘치면 C 에서 UB) */
KERNEL int32_t dot_i16(const int16_t *a, const int16_t *b, size_t n) {
    int32_t s = 0;
    for (size_t i = 0; i < n; i++) s += a[i] * b[i];
    return s;
}

/* (6) Part C 의 "고치기 전" 버전: 히스토그램·합·밝은 픽셀 수를 한 루프에 → 루프 전체가 스칼라 */
struct ae_stats { uint32_t hist[256]; uint64_t sum; uint32_t bright; };
KERNEL void ae_fused(struct ae_stats *st, const uint8_t *img, size_t n, uint8_t thr) {
    memset(st, 0, sizeof *st);
    uint64_t sum = 0;
    uint32_t bright = 0;
    for (size_t i = 0; i < n; i++) {
        st->hist[img[i]]++;
        sum += img[i];
        bright += img[i] > thr;
    }
    st->sum = sum;
    st->bright = bright;
}

/* =============================== TODO =============================== */

/* Part A-1: saxpy_plain 과 같은 계산. restrict 로 "겹치지 않는다"를 약속 → memcheck 블록이 사라져야 한다 */
KERNEL void saxpy_restrict(float *restrict y, const float *restrict x, float a, size_t n) {
    (void)y; (void)x; (void)a; (void)n;   /* TODO */
}

/* Part A-2: csum_f32_if 와 비트까지 같은 결과 + 벡터화 리마크. (힌트: if 대신 선택식) */
KERNEL float csum_f32_sel(const float *a, size_t n, float t) {
    (void)a; (void)n; (void)t;   /* TODO */
    return -1.0f;
}

/* Part A-3: 재결합을 허락해 lane 별 누산(fadd.4s)으로 → 오차 한계 안에서만 같다 */
KERNEL float csum_f32_fast(const float *a, size_t n, float t) {
    (void)a; (void)n; (void)t;   /* TODO */
    return -1.0f;
}

/* Part B: 하위 히스토그램 4개 + 병합. h 에 "더한다"(hist_plain 과 같은 계약) */
KERNEL void hist4(uint32_t *h, const uint8_t *p, size_t n) {
    (void)h; (void)p; (void)n;   /* TODO */
}

/* Part C-1: DMA 버퍼 제자리 보정 buf = g*buf + b. 포인터 1개 → 겹침 검사 자체가 필요 없다 */
KERNEL void gain_inplace(float *buf, float g, float b, size_t n) {
    (void)buf; (void)g; (void)b; (void)n;   /* TODO */
}

/* Part C-2: ae_fused 와 같은 결과. 히스토그램(스칼라, hist4)과 합·카운트(벡터)를 루프 분리 */
KERNEL void ae_frame_stats(struct ae_stats *st, const uint8_t *img, size_t n, uint8_t thr) {
    memset(st, 0, sizeof *st);
    (void)img; (void)n; (void)thr;   /* TODO */
}

/* ============================ 테스트 하네스 ============================ */

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint64_t g_rng = 0x2545F4914F6CDD1Dull;          /* 고정 시드 xorshift64 */
static uint64_t xs(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 7; g_rng ^= g_rng << 17; return g_rng; }
static float rnd_f(float lo, float hi) { return lo + (hi - lo) * (float)(xs() >> 40) * 0x1p-24f; }

/* ---- 참조 (순차, 벡터화 금지) ---- */
static void ref_saxpy(float *y, const float *x, float a, size_t n) {
    NOVEC for (size_t i = 0; i < n; i++) y[i] = a * x[i] + y[i];   /* 같은 식 → 같은 fmadd 축약 */
}
static void ref_hist(uint32_t *h, const uint8_t *p, size_t n) {
    NOVEC for (size_t i = 0; i < n; i++) h[p[i]]++;
}
static float ref_csum_f32(const float *a, size_t n, float t) {
    float s = 0.0f;
    NOVEC for (size_t i = 0; i < n; i++) if (a[i] > t) s += a[i];
    return s;
}
static int64_t ref_csum_i32(const int32_t *a, size_t n, int32_t t) {
    int64_t s = 0;
    NOVEC for (size_t i = 0; i < n; i++) if (a[i] > t) s += a[i];
    return s;
}
static int64_t ref_dot_i16(const int16_t *a, const int16_t *b, size_t n) {   /* int64: 넘침 없음 */
    int64_t s = 0;
    NOVEC for (size_t i = 0; i < n; i++) s += (int64_t)a[i] * b[i];
    return s;
}
static int same_bits(float a, float b) { uint32_t x, y; memcpy(&x, &a, 4); memcpy(&y, &b, 4); return x == y; }

/* 재결합 합의 오차 한계: |s - exact| <= γ_m · Σ|선택된 a_i|,  γ_m = m·u/(1-m·u), u = 2^-24 */
static double g_worst_ratio;
static int within_bound(float got, const float *a, size_t n, float t) {
    double exact = 0, abs_sum = 0;
    size_t m = 0;
    for (size_t i = 0; i < n; i++) if (a[i] > t) { exact += a[i]; abs_sum += fabs((double)a[i]); m++; }
    double u = 0x1p-24, gam = (double)m * u / (1 - (double)m * u);
    double bound = gam * abs_sum + 0x1p-60 * abs_sum;   /* 뒤 항: double 참조 자체의 오차 여유 */
    double err = fabs((double)got - exact);
    if (bound > 0 && err / bound > g_worst_ratio) g_worst_ratio = err / bound;
    return err <= bound;
}

#define BIG 4096
static float fx[BIG + 128], fy[BIG + 128], fr[BIG + 128], buf[2 * BIG + 256], bref[2 * BIG + 256];
static uint8_t px[1 << 16];
static int32_t ia[BIG];
static int16_t sa[BIG], sb[BIG];

/* saxpy_plain 을 "겹친" 버퍼에서: y = base + oy, x = base + ox (둘 다 같은 배열 안) */
static int overlap_case(ptrdiff_t oy, ptrdiff_t ox, size_t n, float a) {
    for (size_t i = 0; i < sizeof buf / sizeof buf[0]; i++) buf[i] = bref[i] = rnd_f(-4, 4);
    float *b0 = buf + 128, *r0 = bref + 128;
    saxpy_plain(b0 + oy, b0 + ox, a, n);
    ref_saxpy(r0 + oy, r0 + ox, a, n);
    return memcmp(buf, bref, sizeof buf) == 0;   /* 범위 밖까지 같아야 한다 (쓰기 넘침 없음) */
}

int main(void) {
    /* ---------- Part A-1: saxpy (겹치지 않는 버퍼) ---------- */
    {
        int bad = 0;
        for (size_t n = 0; n <= 70; n++)
            for (size_t off = 0; off < 4; off++) {       /* 16바이트 정렬 깨기 */
                float a = rnd_f(-2, 2);
                for (size_t i = 0; i < n + 8; i++) { fx[i] = rnd_f(-4, 4); fy[i] = fr[i] = rnd_f(-4, 4); }
                float y2[80];
                memcpy(y2, fy, sizeof y2);
                saxpy_plain(fy + off, fx + 1, a, n);
                ref_saxpy(fr + off, fx + 1, a, n);
                bad += memcmp(fy, fr, sizeof y2) != 0;
                memcpy(fy, y2, sizeof y2);
                saxpy_restrict(fy + off, fx + 1, a, n);
                bad += memcmp(fy, fr, sizeof y2) != 0;
            }
        CHECK(bad == 0);
        float x4[5] = { 1, 2, 3, 4, 5 }, y4[5] = { 10, 20, 30, 40, 50 };
        saxpy_restrict(y4, x4, 2.0f, 5);
        CHECK(y4[0] == 12 && y4[3] == 48 && y4[4] == 60);
        for (size_t i = 0; i < BIG; i++) { fx[i] = rnd_f(-4, 4); fy[i] = fr[i] = rnd_f(-4, 4); }
        saxpy_restrict(fy, fx, 0.75f, BIG);
        ref_saxpy(fr, fx, 0.75f, BIG);
        CHECK(memcmp(fy, fr, BIG * sizeof(float)) == 0);   /* fmla(벡터) 와 fmadd(스칼라) 는 같은 단일 반올림 */
    }

    /* ---------- Part B: saxpy_plain 은 겹쳐도 순차 의미 그대로 (memcheck → 스칼라 경로) ---------- */
    {
        int bad = 0;
        for (ptrdiff_t d = -40; d <= 40; d++)
            for (size_t n = 0; n <= 64; n += (n < 20 ? 1 : 11))
                bad += !overlap_case(d, 0, n, 1.5f);
        CHECK(bad == 0);
        CHECK(overlap_case(0, 0, BIG, -0.5f));          /* 제자리 y == x */
        CHECK(overlap_case(1, 0, BIG, 2.0f));           /* y = x+1: 앞 반복의 결과를 읽어야 한다 */
        CHECK(overlap_case(0, 1, BIG, 2.0f));           /* x = y+1: 아직 안 바뀐 값을 읽어야 한다 */
        /* y = x+1, a = 1, x = {1,1,1,...}: 순차 의미라면 y[i] = i+2 (누적), 벡터로 잘못 읽으면 2 */
        for (int i = 0; i < 40; i++) buf[i] = 1.0f;
        saxpy_plain(buf + 1, buf, 1.0f, 32);
        CHECK(buf[1] == 2.0f && buf[2] == 3.0f && buf[32] == 33.0f);
    }

    /* ---------- Part A-2/A-3: 조건부 합 ---------- */
    {
        int bad_sel = 0, bad_fast = 0, bad_i = 0;
        for (size_t n = 0; n <= 300; n++) {
            float t = rnd_f(-1, 1);
            for (size_t i = 0; i < n; i++) fx[i] = rnd_f(-1000, 1000);
            float r = ref_csum_f32(fx, n, t);
            bad_sel += !same_bits(csum_f32_if(fx, n, t), r);
            bad_sel += !same_bits(csum_f32_sel(fx, n, t), r);
            bad_fast += !within_bound(csum_f32_fast(fx, n, t), fx, n, t);
            int32_t ti = (int32_t)(xs() >> 33) - (1 << 30);
            for (size_t i = 0; i < n; i++) ia[i] = (int32_t)(uint32_t)xs();
            bad_i += csum_i32(ia, n, ti) != ref_csum_i32(ia, n, ti);
        }
        for (size_t i = 0; i < BIG; i++) fx[i] = rnd_f(-1e6f, 1e6f);
        bad_sel += !same_bits(csum_f32_sel(fx, BIG, 0.0f), ref_csum_f32(fx, BIG, 0.0f));
        bad_fast += !within_bound(csum_f32_fast(fx, BIG, -1e5f), fx, BIG, -1e5f);
        CHECK(bad_sel == 0);
        CHECK(bad_fast == 0);
        CHECK(bad_i == 0);
        CHECK(g_worst_ratio <= 1.0);
        /* 특수값: NaN 은 비교가 거짓 → 빠진다, t = NaN 이면 아무것도 안 고른다, -0 은 +0 합을 바꾸지 않는다 */
        float sp[9] = { NAN, 1.0f, -0.0f, INFINITY, 2.0f, NAN, -INFINITY, 0.5f, -3.0f };
        CHECK(same_bits(csum_f32_sel(sp, 9, -1.0f), ref_csum_f32(sp, 9, -1.0f)));   /* +Inf */
        CHECK(csum_f32_sel(sp, 9, NAN) == 0.0f && csum_f32_fast(sp, 9, NAN) == 0.0f);
        CHECK(csum_f32_fast(sp, 9, -1.0f) == INFINITY);
        CHECK(same_bits(csum_f32_sel(sp + 2, 1, -1.0f), 0.0f));   /* +0 + (-0) = +0 */
        CHECK(csum_f32_sel(fx, 0, 0.0f) == 0.0f && csum_f32_fast(fx, 0, 0.0f) == 0.0f);
        float eq[6] = { 1, 2, 3, 2, 2, 5 };                       /* a == t 는 고르지 않는다 (> 이지 >= 가 아님) */
        CHECK(csum_f32_sel(eq, 6, 2.0f) == 8.0f && csum_f32_fast(eq, 6, 2.0f) == 8.0f);
        int32_t e[5] = { INT32_MAX, INT32_MAX, INT32_MIN, 5, INT32_MAX };   /* 합이 int32 를 넘는다 → int64 누산 */
        CHECK(csum_i32(e, 5, 0) == 3LL * INT32_MAX + 5);
        CHECK(csum_i32(e, 5, INT32_MAX) == 0 && csum_i32(e, 5, INT32_MIN) == 3LL * INT32_MAX + 5);
        /* 재결합은 순서만 바꾼다: 작은 정수 값이면 어떤 순서도 정확 → 비트까지 같다 */
        for (size_t i = 0; i < BIG; i++) fx[i] = (float)(int)(xs() % 2001) - 1000.0f;
        CHECK(csum_f32_fast(fx, BIG, 0.0f) == ref_csum_f32(fx, BIG, 0.0f));
    }

    /* ---------- dot_i16 (계약 범위 안에서 정확) ---------- */
    {
        int bad = 0;
        for (size_t n = 0; n <= 200; n++) {
            for (size_t i = 0; i < n; i++) { sa[i] = (int16_t)((int)(xs() % 4001) - 2000); sb[i] = (int16_t)((int)(xs() % 4001) - 2000); }
            bad += dot_i16(sa, sb, n) != ref_dot_i16(sa, sb, n);   /* |합| <= 200·4e6 < 2^31 */
        }
        CHECK(bad == 0);
        for (int i = 0; i < 2; i++) { sa[i] = INT16_MIN; sb[i] = INT16_MIN; }
        CHECK(dot_i16(sa, sb, 1) == 1073741824 && dot_i16(sa, sb, 0) == 0);   /* (-32768)^2 = 2^30, 2개면 넘침(UB) */
    }

    /* ---------- Part B: 히스토그램 ---------- */
    {
        uint32_t h1[256], h2[256], h3[256];
        int bad = 0;
        for (size_t n = 0; n <= 300; n++) {
            for (size_t i = 0; i < n; i++) px[i] = (uint8_t)xs();
            if (n % 3 == 0) for (size_t i = 0; i < n; i++) px[i] = (uint8_t)(xs() & 3);   /* 충돌 많은 입력 */
            for (int b = 0; b < 256; b++) h1[b] = h2[b] = h3[b] = (uint32_t)b * 7;      /* "더한다" 계약 */
            hist_plain(h1, px, n);
            hist4(h2, px, n);
            ref_hist(h3, px, n);
            bad += memcmp(h1, h3, sizeof h1) != 0;
            bad += memcmp(h2, h3, sizeof h2) != 0;
        }
        CHECK(bad == 0);
        memset(px, 0xAB, sizeof px);                                   /* 전부 같은 값: 최악의 의존 사슬 */
        memset(h2, 0, sizeof h2);
        hist4(h2, px, sizeof px);
        CHECK(h2[0xAB] == sizeof px && h2[0xAA] == 0 && h2[0xAC] == 0);
        for (size_t i = 0; i < sizeof px; i++) px[i] = (uint8_t)i;     /* 0..255 반복 */
        memset(h2, 0, sizeof h2);
        hist4(h2, px, sizeof px - 3);                                  /* 꼬리 1개(n%4 = 1) */
        CHECK(h2[0] == 256 && h2[252] == 256 && h2[253] == 255 && h2[255] == 255);
    }

    /* ---------- Part C: DMA 버퍼 제자리 보정 + 자동 노출 통계 ---------- */
    {
        int bad = 0;
        for (size_t n = 0; n <= 70; n++) {
            for (size_t i = 0; i < n + 4; i++) fy[i] = fr[i] = rnd_f(-2048, 2048);
            gain_inplace(fy, 1.25f, -3.5f, n);
            NOVEC for (size_t i = 0; i < n; i++) fr[i] = 1.25f * fr[i] + -3.5f;
            bad += memcmp(fy, fr, (n + 4) * sizeof(float)) != 0;
        }
        CHECK(bad == 0);
        static struct ae_stats s1, s2;
        bad = 0;
        size_t lens[] = { 0, 1, 3, 4, 15, 16, 17, 63, 64, 65, 1000, 65536 };
        for (size_t k = 0; k < sizeof lens / sizeof lens[0]; k++) {
            for (size_t i = 0; i < lens[k]; i++) px[i] = (uint8_t)(xs() >> 56);
            uint8_t thr = (uint8_t)(k * 37);
            ae_fused(&s1, px, lens[k], thr);
            ae_frame_stats(&s2, px, lens[k], thr);
            bad += memcmp(s1.hist, s2.hist, sizeof s1.hist) != 0 || s1.sum != s2.sum || s1.bright != s2.bright;
        }
        CHECK(bad == 0);
        memset(px, 255, sizeof px);                                    /* 포화된 프레임 */
        ae_frame_stats(&s2, px, sizeof px, 254);
        CHECK(s2.sum == 255u * 65536u && s2.bright == 65536 && s2.hist[255] == 65536);
        ae_frame_stats(&s2, px, sizeof px, 255);
        CHECK(s2.bright == 0);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
