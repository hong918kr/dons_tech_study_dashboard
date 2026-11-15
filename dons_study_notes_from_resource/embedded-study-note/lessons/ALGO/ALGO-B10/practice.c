/* ALGO-B10 추가 연습 — 한 번에 검증
 *
 *   연습 1  웜업 정책 바꾸기: 첫 샘플로 창을 채워 두는(prefill) 이동 평균 → 처음부터 시프트만
 *   연습 2  16비트 MCU: 32비트 누적합을 반쪽씩 읽으면 창이 꽉 차도 찢어진다 + 시퀀스 카운터로 고치기
 *   연습 3  합 >= S 인 가장 짧은 구간 (축소 템플릿, LeetCode 209 변형)
 *   연습 4  서로 다른 바이트가 K 개 이하인 가장 긴 구간 (개수 표 + 종류 수)
 *   연습 5  나눗셈 없는 과전류 경보: N 샘플 평균 > limit 인 창의 개수 (sum > limit*N)
 *
 * 모두 브루트포스와 고정 시드 xorshift32 로 대조. build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

/* ======================= 연습 1: prefill 이동 평균 (N = 2^k 전용) ======================= */
#define PF_MAX 256u
typedef struct {
    uint16_t buf[PF_MAX];
    uint32_t sum, head, n;
    unsigned shift;
    bool seeded;
} ma_pf_t;

static void pf_init(ma_pf_t *m, unsigned shift) {      /* n = 1 << shift, shift 0..8 */
    memset(m, 0, sizeof *m);
    m->shift = shift; m->n = 1u << shift;
}
static void pf_push(ma_pf_t *m, uint16_t x) {
    if (!m->seeded) {                                  /* 첫 샘플: 창 전체를 x 로 채운다 (O(N), 한 번만) */
        for (uint32_t i = 0; i < m->n; i++) m->buf[i] = x;
        m->sum = (uint32_t)x << m->shift;
        m->seeded = true;
    }
    m->sum = m->sum - m->buf[m->head] + x;             /* 그 다음부터는 항상 O(1) */
    m->buf[m->head] = x;
    m->head = (m->head + 1u) & (m->n - 1u);            /* 2의 거듭제곱 → 마스크 (DS-B04) */
}
static uint16_t pf_avg(const ma_pf_t *m) { return (uint16_t)((m->sum + (m->n >> 1)) >> m->shift); }

/* ======================= 연습 2: 16비트 버스에서 32비트 합 ======================= */
/* 16비트 MCU 에서 uint32_t 는 반쪽 두 개로 따로 읽힌다 — 그 상황을 halves 로 직접 흉내낸다 */
typedef struct {
    uint16_t buf[4];
    uint32_t head, count;
    volatile uint16_t sum_lo, sum_hi;   /* 누적합 (반쪽씩)       */
    volatile uint16_t seq;              /* 16비트 = 한 번에 읽힘  */
} ma16_t;

static void (*g_hook)(void);
static uint16_t ld16(const volatile uint16_t *p) { uint16_t v = *p; if (g_hook) g_hook(); return v; }

static void isr16_push(ma16_t *m, uint16_t x) {
    uint32_t s = ((uint32_t)m->sum_hi << 16) | m->sum_lo;
    if (m->count == 4) s -= m->buf[m->head]; else m->count++;
    m->buf[m->head] = x; s += x;
    m->head = (m->head + 1u) & 3u;
    m->sum_lo = (uint16_t)s; m->sum_hi = (uint16_t)(s >> 16);
    m->seq = (uint16_t)(m->seq + 1u);
}
static uint32_t read16_naive(ma16_t *m) {               /* 하위 → 상위 순서로 두 번 읽기 */
    uint32_t lo = ld16(&m->sum_lo);
    uint32_t hi = ld16(&m->sum_hi);
    return (hi << 16) | lo;
}
static uint32_t read16_seq(ma16_t *m) {
    uint16_t s0; uint32_t lo, hi;
    do {
        s0 = ld16(&m->seq);
        lo = ld16(&m->sum_lo);
        hi = ld16(&m->sum_hi);
    } while (ld16(&m->seq) != s0);
    return (hi << 16) | lo;
}
static ma16_t g_m16; static int g_at, g_cnt; static uint16_t g_x16;
static void fire16(void) { if (++g_cnt == g_at) { g_hook = NULL; isr16_push(&g_m16, g_x16); } }

/* ======================= 연습 3: 합 >= S 인 가장 짧은 구간 ======================= */
/* 값은 비음수. 반환 = 최소 길이 (0 = 없음), *start = 가장 왼쪽. S == 0 이면 빈 구간이 답이지만 여기선 길이 >= 1 로 정의 */
static size_t shortest_sum_ge(const uint16_t *a, size_t n, uint32_t S, size_t *start) {
    uint64_t sum = 0;
    size_t l = 0, best = 0, bs = 0;
    for (size_t r = 0; r < n; r++) {
        sum += a[r];
        while (sum >= S && l <= r) {                   /* 유효한 동안 줄이며 기록 (shortest 템플릿) */
            if (best == 0 || r + 1 - l < best) { best = r + 1 - l; bs = l; }
            sum -= a[l++];
        }
    }
    *start = bs;
    return best;
}
static size_t bf_shortest(const uint16_t *a, size_t n, uint32_t S, size_t *start) {
    size_t best = 0, bs = 0;
    for (size_t l = 0; l < n; l++) {
        uint64_t s = 0;
        for (size_t r = l; r < n; r++) {
            s += a[r];
            if (s >= S && (best == 0 || r + 1 - l < best)) { best = r + 1 - l; bs = l; }
        }
    }
    *start = bs;
    return best;
}

/* ======================= 연습 4: 서로 다른 바이트 <= K 개인 최장 구간 ======================= */
static size_t longest_k_distinct(const uint8_t *s, size_t n, unsigned K, size_t *start) {
    size_t cnt[256] = { 0 };
    unsigned distinct = 0;
    size_t l = 0, best = 0, bs = 0;
    for (size_t r = 0; r < n; r++) {
        if (cnt[s[r]]++ == 0) distinct++;
        while (distinct > K) { if (--cnt[s[l]] == 0) distinct--; l++; }
        if (r + 1 - l > best) { best = r + 1 - l; bs = l; }
    }
    *start = bs;
    return best;
}
static size_t bf_k_distinct(const uint8_t *s, size_t n, unsigned K, size_t *start) {
    size_t best = 0, bs = 0;
    for (size_t l = 0; l < n; l++) {
        bool seen[256] = { false };
        unsigned d = 0;
        for (size_t r = l; r < n; r++) {
            if (!seen[s[r]]) { seen[s[r]] = true; d++; }
            if (d > K) break;
            if (r + 1 - l > best) { best = r + 1 - l; bs = l; }
        }
    }
    *start = bs;
    return best;
}

/* ======================= 연습 5: 나눗셈 없는 경보 ======================= */
/* 길이 N 인 꽉 찬 창 중 (정확한) 평균 > limit 인 창의 개수. 평균 > limit ⇔ sum > limit*N */
static size_t count_alarm_windows(const uint16_t *x, size_t len, uint32_t N, uint16_t limit) {
    if (N == 0 || len < N) return 0;
    uint32_t sum = 0, thr = (uint32_t)limit * N;      /* N <= 65537 이면 thr, sum 모두 32비트 안 */
    size_t c = 0;
    for (size_t i = 0; i < len; i++) {
        sum += x[i];
        if (i >= N) sum -= x[i - N];
        if (i + 1 >= N && sum > thr) c++;
    }
    return c;
}
static size_t bf_alarm(const uint16_t *x, size_t len, uint32_t N, uint16_t limit) {
    size_t c = 0;
    for (size_t i = 0; i + N <= len && N; i++) {
        double s = 0;
        for (size_t j = i; j < i + N; j++) s += x[j];
        if (s / N > limit) c++;
    }
    return c;
}

int main(void) {
    size_t st, st2;

    /* 연습 1 */
    {
        ma_pf_t m; pf_init(&m, 2);                       /* N = 4 */
        pf_push(&m, 100);
        CHECK(pf_avg(&m) == 100 && m.sum == 400);         /* 첫 샘플부터 "꽉 찬" 창 */
        pf_push(&m, 200);
        CHECK(pf_avg(&m) == 125);                         /* (100*3 + 200)/4 — 나눗셈 정책이면 150 */
        uint32_t seed = 4242u; long bad = 0;
        static uint16_t xs[5000];
        for (unsigned sh = 0; sh <= 8; sh++) {
            pf_init(&m, sh);
            uint32_t n = 1u << sh;
            for (size_t i = 0; i < 5000; i++) {
                xs[i] = (uint16_t)xs32(&seed);
                pf_push(&m, xs[i]);
                uint64_t s = 0;                           /* 기준: 앞을 xs[0] 으로 채운 가상 스트림의 창 */
                for (uint32_t j = 0; j < n; j++) s += (i >= j) ? xs[i - j] : xs[0];
                if (pf_avg(&m) != (uint16_t)((2 * s + n) / (2 * n))) bad++;
            }
        }
        CHECK(bad == 0);
    }

    /* 연습 2: {0x4000,0x4000,0x3FFF,0x4000} 합 0xFFFF → 0x4004 를 넣으면 0x10003 (자리올림이 상위 반쪽으로) */
    {
        memset(&g_m16, 0, sizeof g_m16);
        isr16_push(&g_m16, 0x4000); isr16_push(&g_m16, 0x4000); isr16_push(&g_m16, 0x3FFF); isr16_push(&g_m16, 0x4000);
        ma16_t save = g_m16;
        g_cnt = 0; g_at = 1; g_x16 = 0x4004; g_hook = fire16;
        uint32_t torn = read16_naive(&g_m16);
        printf("16-bit bus: naive sum read = 0x%05X (before 0x0FFFF, after 0x10003) -> avg %u instead of 16384/16385\n",
               (unsigned)torn, (unsigned)((torn + 2) / 4));
        CHECK(torn == 0x1FFFFu);                          /* 창은 꽉 차 있는데도 찢어짐 */
        int ok = 1;
        for (int k = 1; k <= 6; k++) {
            g_m16 = save; g_cnt = 0; g_at = k; g_x16 = 0x4004; g_hook = fire16;
            uint32_t v = read16_seq(&g_m16);
            g_hook = NULL;
            if (v != 0xFFFFu && v != 0x10003u) ok = 0;
        }
        CHECK(ok);
    }

    /* 연습 3 */
    {
        const uint16_t a[6] = { 2, 3, 1, 2, 4, 3 };
        CHECK(shortest_sum_ge(a, 6, 7, &st) == 2 && st == 4);          /* [4,3] */
        CHECK(shortest_sum_ge(a, 6, 100, &st) == 0);                    /* 없음 */
        CHECK(shortest_sum_ge(a, 6, 4, &st) == 1 && st == 4);           /* [4] */
        CHECK(shortest_sum_ge(a, 0, 1, &st) == 0);
        uint32_t seed = 99u; long bad = 0;
        uint16_t x[40];
        for (int t = 0; t < 3000; t++) {
            size_t n = xs32(&seed) % 40;
            for (size_t i = 0; i < n; i++) x[i] = (uint16_t)(xs32(&seed) % 12);
            uint32_t S = 1 + xs32(&seed) % 80;
            size_t l1 = shortest_sum_ge(x, n, S, &st), l2 = bf_shortest(x, n, S, &st2);
            if (l1 != l2 || st != st2) bad++;
        }
        CHECK(bad == 0);
    }

    /* 연습 4 */
    {
        CHECK(longest_k_distinct((const uint8_t *)"eceba", 5, 2, &st) == 3 && st == 0);   /* "ece" */
        CHECK(longest_k_distinct((const uint8_t *)"aa", 2, 1, &st) == 2);
        CHECK(longest_k_distinct((const uint8_t *)"abc", 3, 0, &st) == 0);
        uint32_t seed = 31337u; long bad = 0;
        uint8_t s[200];
        for (int t = 0; t < 3000; t++) {
            size_t n = xs32(&seed) % 200;
            uint32_t alpha = 1 + xs32(&seed) % 12;
            for (size_t i = 0; i < n; i++) s[i] = (uint8_t)(0xF0 + xs32(&seed) % alpha);
            unsigned K = xs32(&seed) % 6;
            size_t l1 = longest_k_distinct(s, n, K, &st), l2 = bf_k_distinct(s, n, K, &st2);
            if (l1 != l2 || st != st2) bad++;
        }
        CHECK(bad == 0);
    }

    /* 연습 5 */
    {
        const uint16_t x[4] = { 10, 10, 10, 12 };          /* 평균 10.5: 반올림 평균(11) 도 정확한 평균도 > 10 */
        CHECK(count_alarm_windows(x, 4, 4, 10) == 1);
        const uint16_t y[4] = { 10, 10, 10, 11 };          /* 평균 10.25: 반올림하면 10 → "> 10" 를 놓친다 */
        CHECK(count_alarm_windows(y, 4, 4, 10) == 1);
        CHECK(count_alarm_windows(y, 3, 4, 10) == 0);      /* 꽉 찬 창이 없음 */
        uint32_t seed = 2026u; long bad = 0;
        static uint16_t z[3000];
        for (int t = 0; t < 40; t++) {
            uint32_t N = 1 + xs32(&seed) % 64;
            for (size_t i = 0; i < 3000; i++) z[i] = (uint16_t)(1000 + xs32(&seed) % 50);
            uint16_t lim = (uint16_t)(1015 + xs32(&seed) % 20);
            if (count_alarm_windows(z, 3000, N, lim) != bf_alarm(z, 3000, N, lim)) bad++;
        }
        CHECK(bad == 0);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
