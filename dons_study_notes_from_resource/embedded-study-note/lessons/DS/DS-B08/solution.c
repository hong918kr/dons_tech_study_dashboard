/* DS-B08 비트맵(Bitset): 할당 상태 관리
 * uint32_t 워드 배열 비트맵. 비트 i = 1 이면 "사용 중", 0 이면 "비어 있음".
 * 찾는 함수는 못 찾으면 n 을 돌려준다 (Linux find_*_bit 관례).
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define BM_WORDS(n) (((n) + 31u) / 32u)    /* n 비트에 필요한 워드 수 */
#define NB 200u                            /* 테스트 비트 수: 32의 배수가 아님 → 꼬리 처리 확인 */

/* ---------------- Part A: set / clear / test ---------------- */
static void bm_set(uint32_t *bm, unsigned i)   { bm[i / 32u] |= 1u << (i % 32u); }
static void bm_clear(uint32_t *bm, unsigned i) { bm[i / 32u] &= ~(1u << (i % 32u)); }
static bool bm_test(const uint32_t *bm, unsigned i) { return (bm[i / 32u] >> (i % 32u)) & 1u; }

/* ---------------- Part B-1: 단순 루프 버전 (한 비트씩) ---------------- */
static unsigned ffz_loop(const uint32_t *bm, unsigned n) {
    for (unsigned i = 0; i < n; i++)
        if (!bm_test(bm, i)) return i;
    return n;
}
static unsigned fns_loop(const uint32_t *bm, unsigned n, unsigned from) {
    for (unsigned i = from; i < n; i++)
        if (bm_test(bm, i)) return i;
    return n;
}
static unsigned fnz_loop(const uint32_t *bm, unsigned n, unsigned from) {
    for (unsigned i = from; i < n; i++)
        if (!bm_test(bm, i)) return i;
    return n;
}

/* ---------------- Part B-2: __builtin_ctz 버전 (한 워드씩) ---------------- */
/* from 이상에서 첫 1비트. 워드가 0이면 32비트를 한 번에 건너뛴다 */
static unsigned fns_ctz(const uint32_t *bm, unsigned n, unsigned from) {
    if (from >= n) return n;
    unsigned w = from / 32u;
    uint32_t x = bm[w] & (0xFFFFFFFFu << (from % 32u));      /* from 아래 비트는 버린다 */
    for (;;) {
        if (x) {                                             /* ctz(0) 은 UB → 반드시 x != 0 확인 */
            unsigned i = w * 32u + (unsigned)__builtin_ctz(x);
            return i < n ? i : n;                            /* 마지막 워드의 n 이상 비트는 무시 */
        }
        if (++w >= BM_WORDS(n)) return n;
        x = bm[w];
    }
}
/* from 이상에서 첫 0비트 = 반전한 워드에서 첫 1비트 */
static unsigned fnz_ctz(const uint32_t *bm, unsigned n, unsigned from) {
    if (from >= n) return n;
    unsigned w = from / 32u;
    uint32_t x = ~bm[w] & (0xFFFFFFFFu << (from % 32u));
    for (;;) {
        if (x) {
            unsigned i = w * 32u + (unsigned)__builtin_ctz(x);
            return i < n ? i : n;
        }
        if (++w >= BM_WORDS(n)) return n;
        x = ~bm[w];                                          /* 0xFFFFFFFF(꽉 참) 이면 x == 0 → 건너뜀 */
    }
}
static unsigned ffz_ctz(const uint32_t *bm, unsigned n) { return fnz_ctz(bm, n, 0); }

/* ---------------- Part C: 연속 k개 빈 비트 (first fit), k >= 1 ---------------- */
static unsigned zrun_loop(const uint32_t *bm, unsigned n, unsigned k) {
    unsigned run = 0;
    for (unsigned i = 0; i < n; i++) {
        if (bm_test(bm, i)) run = 0;                         /* 끊기면 처음부터 */
        else if (++run == k) return i + 1u - k;
    }
    return n;
}
/* 빈 구간 [s, e) 단위로 점프: s = 다음 0, e = 그 뒤 다음 1 */
static unsigned zrun_ctz(const uint32_t *bm, unsigned n, unsigned k) {
    unsigned s = fnz_ctz(bm, n, 0);
    while (s < n) {
        unsigned e = fns_ctz(bm, n, s);
        if (e - s >= k) return s;
        s = fnz_ctz(bm, n, e);                               /* e == n 이면 n → 종료 */
    }
    return n;
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t rng = 0xB17A5u;                              /* 고정 시드 xorshift32 */
static uint32_t xs32(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

/* 정답지(oracle): 하네스 전용, 비트를 직접 읽는다 */
static unsigned raw(const uint32_t *bm, unsigned i) { return (bm[i / 32u] >> (i % 32u)) & 1u; }
static unsigned ref_next(const uint32_t *bm, unsigned n, unsigned from, unsigned want) {
    for (unsigned i = from; i < n; i++) if (raw(bm, i) == want) return i;
    return n;
}
static unsigned ref_zrun(const uint32_t *bm, unsigned n, unsigned k) {
    for (unsigned s = 0; s + k <= n; s++) {
        unsigned j = 0;
        while (j < k && !raw(bm, s + j)) j++;
        if (j == k) return s;
    }
    return n;
}
static void fill(uint32_t *bm, unsigned words, uint32_t v) { for (unsigned w = 0; w < words; w++) bm[w] = v; }

int main(void) {
    uint32_t bm[BM_WORDS(NB)];                               /* 7 워드 = 224 비트 중 200 비트 사용 */

    /* Part A */
    fill(bm, BM_WORDS(NB), 0);
    bm_set(bm, 0); bm_set(bm, 31); bm_set(bm, 32); bm_set(bm, 199);
    CHECK(bm[0] == 0x80000001u && bm[1] == 0x1u && bm[6] == (1u << 7));
    CHECK(bm_test(bm, 31) && bm_test(bm, 199) && !bm_test(bm, 30));
    bm_clear(bm, 31);
    CHECK(bm[0] == 0x1u && !bm_test(bm, 31));
    bm_clear(bm, 31);                                        /* 두 번 clear 해도 같음 */
    CHECK(bm[0] == 0x1u);

    /* Part B: 정해진 경우 */
    fill(bm, BM_WORDS(NB), 0);
    CHECK(ffz_loop(bm, NB) == 0 && ffz_ctz(bm, NB) == 0);
    CHECK(fns_loop(bm, NB, 0) == NB && fns_ctz(bm, NB, 0) == NB);
    for (unsigned i = 0; i < 70; i++) bm_set(bm, i);
    bm_set(bm, 150);
    CHECK(ffz_loop(bm, NB) == 70 && ffz_ctz(bm, NB) == 70);
    CHECK(fns_loop(bm, NB, 70) == 150 && fns_ctz(bm, NB, 70) == 150);
    CHECK(fns_ctz(bm, NB, 33) == 33);                        /* from 이 워드 중간 */
    CHECK(fns_ctz(bm, NB, 151) == NB);
    CHECK(fnz_ctz(bm, NB, 150) == 151 && fnz_loop(bm, NB, 150) == 151);
    CHECK(fns_ctz(bm, NB, NB) == NB && fnz_ctz(bm, NB, 500) == NB);
    fill(bm, BM_WORDS(NB), 0xFFFFFFFFu);                     /* 꽉 참 + 꼬리(200..223)도 1 */
    CHECK(ffz_loop(bm, NB) == NB && ffz_ctz(bm, NB) == NB);
    bm[6] = 0x00000FFFu;                                     /* 192..203 은 1, 꼬리 204.. 는 0 */
    CHECK(ffz_loop(bm, NB) == NB && ffz_ctz(bm, NB) == NB);  /* 204 를 돌려주면 안 된다 */
    fill(bm, BM_WORDS(NB), 0);
    bm[6] = 0xFFFFF000u;                                     /* 꼬리 쓰레기 비트만 1 (204..223) */
    CHECK(fns_loop(bm, NB, 0) == NB && fns_ctz(bm, NB, 0) == NB);

    /* Part C: 정해진 경우 */
    fill(bm, BM_WORDS(NB), 0xFFFFFFFFu);
    for (unsigned i = 10; i <= 12; i++) bm_clear(bm, i);     /* 빈 구간 3개   [10,13)  */
    for (unsigned i = 40; i <= 47; i++) bm_clear(bm, i);     /* 빈 구간 8개   [40,48)  */
    for (unsigned i = 100; i < NB; i++) bm_clear(bm, i);     /* 빈 구간 100개 [100,200) */
    CHECK(zrun_loop(bm, NB, 1) == 10 && zrun_ctz(bm, NB, 1) == 10);
    CHECK(zrun_loop(bm, NB, 3) == 10 && zrun_ctz(bm, NB, 3) == 10);
    CHECK(zrun_loop(bm, NB, 4) == 40 && zrun_ctz(bm, NB, 4) == 40);
    CHECK(zrun_loop(bm, NB, 9) == 100 && zrun_ctz(bm, NB, 9) == 100);
    CHECK(zrun_loop(bm, NB, 100) == 100 && zrun_ctz(bm, NB, 100) == 100);
    CHECK(zrun_loop(bm, NB, 101) == NB && zrun_ctz(bm, NB, 101) == NB);
    fill(bm, BM_WORDS(NB), 0xFFFFFFFFu);
    for (unsigned i = 28; i <= 36; i++) bm_clear(bm, i);     /* 워드 경계를 걸친 9칸 */
    CHECK(zrun_loop(bm, NB, 9) == 28 && zrun_ctz(bm, NB, 9) == 28);

    /* 랜덤 대조: 밀도 1/8 ~ 7/8, n = 1..200, 꼬리 쓰레기 비트 포함 */
    unsigned bad_loop = 0, bad_ctz = 0, diff = 0;
    for (unsigned t = 0; t < 5000; t++) {
        unsigned n = 1u + xs32() % NB, dens = 1u + xs32() % 7u;
        for (unsigned w = 0; w < BM_WORDS(NB); w++) {
            uint32_t v = 0;
            for (unsigned b = 0; b < 32; b++) if (xs32() % 8u < dens) v |= 1u << b;
            bm[w] = v;
        }
        unsigned from = xs32() % (n + 2u), k = 1u + xs32() % 12u;
        unsigned e1 = ref_next(bm, n, 0, 0), e2 = ref_next(bm, n, from, 1);
        unsigned e3 = ref_next(bm, n, from, 0), e4 = ref_zrun(bm, n, k);
        unsigned l1 = ffz_loop(bm, n), l2 = fns_loop(bm, n, from), l3 = fnz_loop(bm, n, from), l4 = zrun_loop(bm, n, k);
        unsigned c1 = ffz_ctz(bm, n),  c2 = fns_ctz(bm, n, from),  c3 = fnz_ctz(bm, n, from),  c4 = zrun_ctz(bm, n, k);
        bad_loop += (l1 != e1) + (l2 != e2) + (l3 != e3) + (l4 != e4);
        bad_ctz  += (c1 != e1) + (c2 != e2) + (c3 != e3) + (c4 != e4);
        diff     += (l1 != c1) + (l2 != c2) + (l3 != c3) + (l4 != c4);
    }
    CHECK(bad_loop == 0);
    CHECK(bad_ctz == 0);
    CHECK(diff == 0);                                        /* 두 구현이 서로 일치 */

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
