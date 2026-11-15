/* ALGO-B08 비교 없는 정렬: 계수 정렬·기수 정렬
 *
 *   Part A  12비트 ADC 샘플(0..4095) 계수 정렬(counting sort)
 *           A1 맨 키(bare key): 히스토그램 → 카운트에서 배열을 다시 쓴다 (in-place)
 *           A2 레코드: 히스토그램 → 배타적 누적합 → 안정(stable) 배치 src → dst
 *   Part B  uint32 타임스탬프 8비트 LSD 기수 정렬(radix sort)
 *           B1 한 자리(8비트) 안정 계수 패스   B2 4패스 ping-pong (a ↔ tmp, 끝나면 다시 a)
 *   Part C  펌웨어 응용
 *           C1 uint16 카운트(8 KB) 히스토그램으로 ADC 중앙값 — 카운트 넘침 가드
 *           C2 랩어라운드(2^32 µs ≈ 71.6분) 타임스탬프 이벤트 레코드 정렬, 같은 시각은 도착 순서 유지
 *
 * 범위 밖 샘플(> 4095)은 거부(-1)하고 출력 버퍼를 건드리지 않는다. malloc 없음 (작업 공간은 호출자가 준다).
 * 모든 테스트는 qsort / 손 계산 기준과 비교, xorshift32 고정 시드라 결정적이다.
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter   (solution.c 도 같은 명령)
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ADC_BITS   12u
#define ADC_RANGE  (1u << ADC_BITS)   /* 4096 개의 가능한 값 */
#define ADC_MAX    (ADC_RANGE - 1u)   /* 4095 */
#define RADIX_BITS 8u
#define RADIX      (1u << RADIX_BITS) /* 256 버킷 */
#define DIGIT_MASK (RADIX - 1u)

typedef struct { uint16_t code; uint16_t ch; uint32_t seq; } adc_rec_t;  /* 8 B: 샘플 + 채널 + 입력 순번 */
typedef struct { uint32_t t; uint16_t id; uint16_t seq; } event_t;      /* 8 B: µs 타임스탬프 + CAN ID + 순번 */

/* ======================= Part A — 계수 정렬 ======================= */
/* A1. cnt = 호출자가 주는 ADC_RANGE 칸 작업 공간 (uint32 → 16 KB). 함수가 0 으로 지운다.
 * 반환 0 = 성공, -1 = 4095 보다 큰 값이 있음 (이때 a 는 그대로). n ≤ UINT32_MAX 가정. */
static int count_sort_adc_inplace(uint16_t *a, size_t n, uint32_t *cnt) {
    (void)a; (void)n; (void)cnt;
    return 0; /* TODO: (1) cnt 지우기 (2) 히스토그램 + 범위 검사 (3) 값 v 를 cnt[v] 번 다시 쓰기 */
}

/* A2. code 기준 안정 계수 정렬 src → dst (같은 code 끼리는 src 순서 유지).
 * 반환 0 / -1 (범위 밖 code, 이때 dst 는 그대로). */
static int count_sort_adc_stable(const adc_rec_t *src, adc_rec_t *dst, size_t n, uint32_t *cnt) {
    (void)src; (void)dst; (void)n; (void)cnt;
    return 0; /* TODO: 히스토그램 + 범위 검사 → 배타적 누적합(시작 위치) → 앞에서부터 dst[pos[key]++] = src[i] */
}

/* ======================= Part B — LSD 기수 정렬 ======================= */
/* B1. 자리값 d = (x >> shift) & 0xFF 기준 안정 계수 패스 src → dst. 카운트 256 칸 = 1 KB. */
static void radix_pass_u32(const uint32_t *src, uint32_t *dst, size_t n, unsigned shift) {
    (void)src; (void)dst; (void)n; (void)shift;
    /* TODO: 자리값 (x >> shift) & 0xFF 로 256 칸 히스토그램 → 배타적 누적합 → 안정 배치 */
}

/* B2. 4 패스 LSD (shift 0, 8, 16, 24). tmp = n 칸 scratch. 결과는 a 에 남아야 한다. */
static void radix_sort_u32(uint32_t *a, uint32_t *tmp, size_t n) {
    (void)a; (void)tmp; (void)n;
    /* TODO: shift = 0, 8, 16, 24 로 radix_pass_u32 를 4 번, src/dst 포인터를 매번 맞바꾼다 (ping-pong) */
}

/* 주어진 코드 (TODO 아님): 흔한 버그 — "끝 위치"(포함 누적합)를 쓰면서 앞에서부터 훑는다.
 * 같은 자리값끼리 순서가 뒤집힌다 = 불안정 → 앞 패스가 만든 순서가 깨진다 (5. 풀이, 7. 실수 2). */
static void radix_pass_u32_unstable(const uint32_t *src, uint32_t *dst, size_t n, unsigned shift) {
    uint32_t end[RADIX] = {0};
    for (size_t i = 0; i < n; i++) end[(src[i] >> shift) & DIGIT_MASK]++;
    for (uint32_t d = 1; d < RADIX; d++) end[d] += end[d - 1];   /* end[d] = 자리값 ≤ d 인 개수 */
    for (size_t i = 0; i < n; i++)                               /* 앞에서부터 + --end → 역순 배치 */
        dst[--end[(src[i] >> shift) & DIGIT_MASK]] = src[i];
}
static void radix_sort_u32_unstable(uint32_t *a, uint32_t *tmp, size_t n) {
    uint32_t *src = a, *dst = tmp;
    for (unsigned shift = 0; shift < 32u; shift += RADIX_BITS) {
        radix_pass_u32_unstable(src, dst, n, shift);
        uint32_t *t = src; src = dst; dst = t;
    }
}

/* ======================= Part C — 펌웨어 응용 ======================= */
/* C1. 하위 중앙값 = 정렬했을 때 (n-1)/2 번째 값. 정렬하지 않고 히스토그램 누적으로 찾는다.
 * cnt16 = ADC_RANGE 칸 uint16 (8 KB). uint16 카운트는 65535 를 넘으면 0 으로 돌아간다(wrap) →
 * n > UINT16_MAX 이면 거부. 반환 0 / -1 (n == 0, n 너무 큼, 범위 밖 샘플). */
static int adc_median_hist(const uint16_t *a, size_t n, uint16_t *cnt16, uint16_t *median) {
    (void)a; (void)n; (void)cnt16; (void)median;
    return -1; /* TODO: n 가드(0, > UINT16_MAX) → uint16 히스토그램 + 범위 검사 → 누적이 (n-1)/2 를 넘는 첫 v */
}

/* C2. 키 = (t - t_ref) mod 2^32 (= t_ref 이후 경과 µs). 레코드 단위 4 패스 안정 LSD. 결과는 ev 에.
 * 같은 키는 입력 순서 유지 (같은 µs 에 들어온 CAN 프레임의 도착 순서). */
static void radix_sort_events(event_t *ev, event_t *tmp, size_t n, uint32_t t_ref) {
    (void)ev; (void)tmp; (void)n; (void)t_ref;
    /* TODO: 키 = (t - t_ref) (uint32, mod 2^32). B1 과 같은 안정 패스를 레코드에 4 번, 결과는 ev 에 */
}

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t g_rng = 0x2545F491u;
static uint32_t xs32(void) { uint32_t x = g_rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return g_rng = x; }

static int cmp_u16(const void *p, const void *q) {
    uint16_t x = *(const uint16_t *)p, y = *(const uint16_t *)q; return (x > y) - (x < y);
}
static int cmp_u32(const void *p, const void *q) {
    uint32_t x = *(const uint32_t *)p, y = *(const uint32_t *)q; return (x > y) - (x < y);
}
static int cmp_rec(const void *p, const void *q) {     /* (code, seq) = 안정 정렬의 정답 순서 */
    const adc_rec_t *x = p, *y = q;
    if (x->code != y->code) return (x->code > y->code) - (x->code < y->code);
    return (x->seq > y->seq) - (x->seq < y->seq);
}
static uint32_t g_tref;
static int cmp_ev(const void *p, const void *q) {      /* (t - t_ref, seq) */
    const event_t *x = p, *y = q;
    uint32_t kx = x->t - g_tref, ky = y->t - g_tref;
    if (kx != ky) return (kx > ky) - (kx < ky);
    return (x->seq > y->seq) - (x->seq < y->seq);
}

#define NMAX 5000
static uint16_t A16[NMAX], R16[NMAX];
static uint32_t A32[NMAX], R32[NMAX], T32[NMAX];
static uint32_t CNT[ADC_RANGE];
static uint16_t CNT16[ADC_RANGE];
static uint16_t BIG16[65536];
static adc_rec_t RS[NMAX], RD[NMAX], RR[NMAX];
static event_t EV[NMAX], ET[NMAX], ER[NMAX];

enum { PAT_RANDOM, PAT_EQUAL, PAT_SORTED, PAT_REVERSED, PAT_HIGHONLY, PAT_COUNT };
static const char *pat_name[PAT_COUNT] = { "random", "all-equal", "sorted", "reversed", "high-bits-only" };
static const size_t sizes[] = { 0, 1, 2, 3, 17, 256, 1000, 5000 };
#define NSIZES (sizeof sizes / sizeof sizes[0])

/* 값 범위 mask 안에서 패턴 생성. high-bits-only: hi 비트만 무작위, 나머지는 고정 상수. */
static void fill_u32(uint32_t *x, size_t n, int pat, uint32_t mask, uint32_t hi) {
    uint32_t c = xs32() & mask;
    for (size_t i = 0; i < n; i++) {
        switch (pat) {
        case PAT_EQUAL:    x[i] = c; break;
        case PAT_HIGHONLY: x[i] = (xs32() & hi) | (0x5A5C3E1u & mask & ~hi); break;
        default:           x[i] = xs32() & mask; break;
        }
    }
    if (pat == PAT_SORTED || pat == PAT_REVERSED) qsort(x, n, sizeof x[0], cmp_u32);
    if (pat == PAT_REVERSED)
        for (size_t i = 0; i < n / 2; i++) { uint32_t t = x[i]; x[i] = x[n - 1 - i]; x[n - 1 - i] = t; }
}
static bool sorted_u32(const uint32_t *x, size_t n) {
    for (size_t i = 1; i < n; i++) if (x[i - 1] > x[i]) return false;
    return true;
}

/* A1 한 번: 패턴 데이터 → count_sort vs qsort. 틀리면 1 */
static int trial_adc(size_t n, int pat) {
    fill_u32(A32, n, pat, ADC_MAX, 0xF00u);
    for (size_t i = 0; i < n; i++) A16[i] = R16[i] = (uint16_t)A32[i];
    qsort(R16, n, sizeof R16[0], cmp_u16);
    if (count_sort_adc_inplace(A16, n, CNT) != 0) return 1;
    return memcmp(A16, R16, n * sizeof A16[0]) != 0;
}
/* B2 한 번 */
static int trial_radix(size_t n, int pat) {
    fill_u32(A32, n, pat, 0xFFFFFFFFu, 0xFF000000u);
    memcpy(R32, A32, n * sizeof A32[0]);
    qsort(R32, n, sizeof R32[0], cmp_u32);
    radix_sort_u32(A32, T32, n);
    return memcmp(A32, R32, n * sizeof A32[0]) != 0;
}
/* A2 한 번: code 를 [0, range) 에서 뽑아 중복을 많이 만든다 */
static int trial_rec(size_t n, uint32_t range) {
    for (size_t i = 0; i < n; i++) {
        RS[i].code = (uint16_t)(xs32() % range); RS[i].ch = (uint16_t)(xs32() & 7u); RS[i].seq = (uint32_t)i;
    }
    memcpy(RR, RS, n * sizeof RS[0]);
    qsort(RR, n, sizeof RR[0], cmp_rec);
    if (count_sort_adc_stable(RS, RD, n, CNT) != 0) return 1;
    return memcmp(RD, RR, n * sizeof RD[0]) != 0;
}
/* B1 기준: 자리값 기준 안정 삽입 정렬 (brute force) */
static void ref_digit_sort(uint32_t *x, size_t n, unsigned shift) {
    for (size_t i = 1; i < n; i++) {
        uint32_t v = x[i]; size_t j = i;
        while (j > 0 && ((x[j - 1] >> shift) & DIGIT_MASK) > ((v >> shift) & DIGIT_MASK)) { x[j] = x[j - 1]; j--; }
        x[j] = v;
    }
}
static int cmp_median(const uint16_t *a, size_t n) {   /* 정렬 기준 하위 중앙값과 비교, 틀리면 1 */
    memcpy(R16, a, n * sizeof a[0]);
    qsort(R16, n, sizeof R16[0], cmp_u16);
    uint16_t m = 0xFFFF;
    if (adc_median_hist(a, n, CNT16, &m) != 0) return 1;
    return m != R16[(n - 1) / 2];
}

int main(void) {
    /* ---------- Part A1: count_sort_adc_inplace vs qsort ---------- */
    for (int p = 0; p < PAT_COUNT; p++) {
        int bad = 0;
        for (size_t s = 0; s < NSIZES; s++) bad += trial_adc(sizes[s], p);
        if (p == PAT_RANDOM)
            for (int t = 0; t < 300; t++) bad += trial_adc(xs32() % 2001u, p);
        printf("  A1 %-15s bad=%d\n", pat_name[p], bad);
        CHECK(bad == 0);
    }
    {   /* 범위 밖: 마지막 칸 4096 → 거부, 배열 그대로 */
        uint16_t x[6] = { 7, 4095, 0, 12, 7, 4096 }, keep[6];
        memcpy(keep, x, sizeof x);
        CHECK(count_sort_adc_inplace(x, 6, CNT) == -1 && memcmp(x, keep, sizeof x) == 0);
        uint16_t y[4] = { 3, 0xFFFF, 1, 2 }, keep2[4];
        memcpy(keep2, y, sizeof y);
        CHECK(count_sort_adc_inplace(y, 4, CNT) == -1 && memcmp(y, keep2, sizeof y) == 0);
        uint16_t z[5] = { 4095, 0, 4095, 0, 2048 }, zs[5] = { 0, 0, 2048, 4095, 4095 };
        CHECK(count_sort_adc_inplace(z, 5, CNT) == 0 && memcmp(z, zs, sizeof z) == 0);
    }

    /* ---------- Part A2: count_sort_adc_stable vs qsort(code, seq) ---------- */
    {
        int bad_dup = 0, bad_full = 0;
        for (int t = 0; t < 300; t++) bad_dup += trial_rec(xs32() % 2001u, 16);   /* 중복 많음 */
        for (int t = 0; t < 100; t++) bad_full += trial_rec(xs32() % 5001u, ADC_RANGE);
        printf("  A2 stable: dup-heavy bad=%d, full-range bad=%d\n", bad_dup, bad_full);
        CHECK(bad_dup == 0);
        CHECK(bad_full == 0);
        for (size_t i = 0; i < 100; i++) { RS[i].code = 1234; RS[i].ch = (uint16_t)i; RS[i].seq = (uint32_t)i; }
        CHECK(count_sort_adc_stable(RS, RD, 100, CNT) == 0 && memcmp(RD, RS, 100 * sizeof RS[0]) == 0);
        RS[50].code = 4096;
        memset(RD, 0xAB, 100 * sizeof RD[0]);
        memcpy(RR, RD, 100 * sizeof RD[0]);
        CHECK(count_sort_adc_stable(RS, RD, 100, CNT) == -1 && memcmp(RD, RR, 100 * sizeof RD[0]) == 0);
    }

    /* ---------- Part B1: radix_pass_u32 vs 자리값 안정 삽입 정렬 ---------- */
    {
        int bad = 0;
        for (int t = 0; t < 100; t++) {
            size_t n = xs32() % 600u;
            unsigned shift = 8u * (xs32() % 4u);
            for (size_t i = 0; i < n; i++) A32[i] = xs32() & 0x0F0F0F0Fu;  /* 자리값마다 16 종 → 중복 많음 */
            memcpy(R32, A32, n * sizeof A32[0]);
            ref_digit_sort(R32, n, shift);
            radix_pass_u32(A32, T32, n, shift);
            bad += memcmp(T32, R32, n * sizeof T32[0]) != 0;
        }
        printf("  B1 one stable pass bad=%d\n", bad);
        CHECK(bad == 0);
        for (size_t i = 0; i < 300; i++) A32[i] = 0xAB000000u | (xs32() & 0x00FFFFFFu);
        radix_pass_u32(A32, T32, 300, 24);                  /* 모두 같은 자리값 → 그대로 복사 */
        CHECK(memcmp(T32, A32, 300 * sizeof A32[0]) == 0);
    }

    /* ---------- Part B2: radix_sort_u32 vs qsort ---------- */
    for (int p = 0; p < PAT_COUNT; p++) {
        int bad = 0;
        for (size_t s = 0; s < NSIZES; s++) bad += trial_radix(sizes[s], p);
        if (p == PAT_RANDOM)
            for (int t = 0; t < 300; t++) bad += trial_radix(xs32() % 2001u, p);
        printf("  B2 %-15s bad=%d\n", pat_name[p], bad);
        CHECK(bad == 0);
    }
    {
        uint32_t x[8] = { 0xFFFFFFFFu, 0, 0x80000000u, 0x7FFFFFFFu, 1, 0xFFu, 0x100u, 0xFFFFFFFFu };
        uint32_t want[8] = { 0, 1, 0xFFu, 0x100u, 0x7FFFFFFFu, 0x80000000u, 0xFFFFFFFFu, 0xFFFFFFFFu };
        uint32_t tmp[8];
        radix_sort_u32(x, tmp, 8);
        CHECK(memcmp(x, want, sizeof x) == 0);
        uint32_t one[1] = { 0xDEADBEEFu }, t1[1] = { 0 };
        radix_sort_u32(one, t1, 1);
        radix_sort_u32(NULL, NULL, 0);                      /* n == 0: 아무것도 안 건드림 */
        CHECK(one[0] == 0xDEADBEEFu);
    }
    {   /* 주어진 불안정 패스로 돌리면 정렬이 깨진다 (정답 코드와 무관하게 항상 성립해야 하는 시연) */
        uint32_t x[4] = { 0x0102u, 0x0201u, 0x0101u, 0x0202u }, tmp[4];
        radix_sort_u32_unstable(x, tmp, 4);
        printf("  unstable LSD on {0x0102,0x0201,0x0101,0x0202} -> {0x%04X,0x%04X,0x%04X,0x%04X}\n",
               (unsigned)x[0], (unsigned)x[1], (unsigned)x[2], (unsigned)x[3]);
        CHECK(!sorted_u32(x, 4));
    }

    /* ---------- Part C1: adc_median_hist ---------- */
    {
        int bad = 0;
        for (int t = 0; t < 300; t++) {
            size_t n = 1 + xs32() % 3000u;
            uint32_t range = (t & 1) ? ADC_RANGE : 8u;
            for (size_t i = 0; i < n; i++) A16[i] = (uint16_t)(xs32() % range);
            bad += cmp_median(A16, n);
        }
        printf("  C1 median bad=%d\n", bad);
        CHECK(bad == 0);
        uint16_t m = 0;
        CHECK(adc_median_hist(A16, 0, CNT16, &m) == -1);
        for (size_t i = 0; i < 65535; i++) BIG16[i] = 7;
        CHECK(adc_median_hist(BIG16, 65535, CNT16, &m) == 0 && m == 7);   /* 카운트 65535 = 넘치기 직전 */
        BIG16[65535] = 7;
        CHECK(adc_median_hist(BIG16, 65536, CNT16, &m) == -1);            /* 65536 개면 wrap 위험 → 거부 */
        uint16_t bad_in[3] = { 10, 5000, 20 };
        CHECK(adc_median_hist(bad_in, 3, CNT16, &m) == -1);
    }

    /* ---------- Part C2: radix_sort_events (랩어라운드 + 안정성) ---------- */
    {
        int bad = 0;
        for (int t = 0; t < 200; t++) {
            size_t n = xs32() % 3001u;
            g_tref = 0xFFFFF000u + (xs32() & 0xFFFu);          /* 2^32 바로 앞: 랩을 넘나든다 */
            for (size_t i = 0; i < n; i++) {
                EV[i].t = g_tref + (xs32() % 0x3000u);       /* 일부는 0x0000xxxx 로 넘어감 */
                EV[i].id = (uint16_t)(xs32() & 0x7FFu); EV[i].seq = (uint16_t)i;
            }
            memcpy(ER, EV, n * sizeof EV[0]);
            qsort(ER, n, sizeof ER[0], cmp_ev);
            radix_sort_events(EV, ET, n, g_tref);
            bad += memcmp(EV, ER, n * sizeof EV[0]) != 0;
        }
        printf("  C2 wrap+stable bad=%d\n", bad);
        CHECK(bad == 0);
        for (size_t i = 0; i < 64; i++) { EV[i].t = 0x00000010u; EV[i].id = (uint16_t)(0x100 + i); EV[i].seq = (uint16_t)i; }
        memcpy(ER, EV, 64 * sizeof EV[0]);
        radix_sort_events(EV, ET, 64, 0xFFFFFFF0u);
        CHECK(memcmp(EV, ER, 64 * sizeof EV[0]) == 0);            /* 전부 같은 시각 → 도착 순서 그대로 */
        event_t w[4] = { {0x00000005u, 1, 0}, {0xFFFFFFF8u, 2, 1}, {0x00000000u, 3, 2}, {0xFFFFFFFFu, 4, 3} };
        radix_sort_events(w, ET, 4, 0xFFFFFFF0u);                  /* 랩 이전이 먼저 */
        CHECK(w[0].id == 2 && w[1].id == 4 && w[2].id == 3 && w[3].id == 1);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
