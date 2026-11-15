/* ALGO-B10 슬라이딩 윈도우: 이동 평균과 최장 구간
 *
 *   Part A  고정 윈도우: N 샘플 이동 평균 = 링 버퍼 + 정수 누적합, 샘플당 O(1)
 *           웜업(받은 샘플 k < N) = 지금까지 받은 k 개의 평균, 샘플 0 개면 0
 *           반올림 = 0.5 올림(round half up): floor(sum/k + 1/2)
 *           N 이 2의 거듭제곱이고 창이 꽉 찼으면 나눗셈 대신 시프트
 *   Part B  가변 윈도우: 합 <= S 인 최장 구간(값은 비음수) / 중복 없는 최장 바이트 구간(256칸 표)
 *           + 음수가 섞이면 같은 축소 윈도우가 틀리는 반례 (window_sum_le_signed, 주어진 코드)
 *   Part C  ADC 완료 ISR 이 넣고 태스크가 읽는 이동 평균: 찢어진 읽기(torn read) vs 시퀀스 카운터
 *
 * 모든 TODO 는 브루트포스(창 합을 매번 새로 계산 / 모든 구간 검사)와 고정 시드 xorshift32 무작위로 대조한다.
 * malloc 없음. build: cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o st && ./st   (solution.c 도 동일)
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ======================= Part A: 이동 평균 (고정 윈도우) ======================= */
#define MA_MAX_N 256u

typedef struct {
    uint16_t buf[MA_MAX_N]; /* 최근 n 개 샘플 (링 버퍼, DS-B03/DS-B04)                         */
    uint32_t n;             /* 창 크기 1..MA_MAX_N                                              */
    uint32_t head;          /* 다음에 쓸 칸. 창이 꽉 찬 뒤에는 = 가장 오래된 샘플이 있는 칸      */
    uint32_t count;         /* 지금까지 받은 샘플 수, n 에서 멈춘다                              */
    uint32_t sum;           /* 창 안 샘플의 합. sum <= n*65535 <= 256*65535 < 2^32 (본문 증명)  */
    int      shift;         /* n == 2^shift 이면 shift, 아니면 -1                                */
} ma_t;

/* 0 = 성공, -1 = n 이 0 이거나 MA_MAX_N 보다 큼 */
static int ma_init(ma_t *m, uint32_t n) {
    if (n == 0 || n > MA_MAX_N) return -1;
    m->n = n; m->head = 0; m->count = 0; m->sum = 0; m->shift = -1;
    if ((n & (n - 1u)) == 0) { int k = 0; while ((1u << k) != n) k++; m->shift = k; }
    return 0;
}

/* 샘플 하나 넣기: O(1). 꽉 찼으면 가장 오래된 값을 빼고, 새 값을 더한다 */
static void ma_push(ma_t *m, uint16_t x) {
    if (m->count == m->n) m->sum -= m->buf[m->head];
    else m->count++;
    m->buf[m->head] = x;
    m->sum += x;
    if (++m->head == m->n) m->head = 0;
}

static bool ma_ready(const ma_t *m) { return m->n != 0 && m->count == m->n; }

/* 창 안 k = count 개 샘플의 평균, 0.5 올림. count == 0 이면 0 */
static uint16_t ma_avg(const ma_t *m) {
    if (m->count == 0) return 0;
    return (uint16_t)((m->sum + m->count / 2u) / m->count);
}

/* 전제: n 이 2의 거듭제곱(shift >= 0) 이고 창이 꽉 참. 나눗셈 없이 ma_avg 와 같은 값 */
static uint16_t ma_avg_shift(const ma_t *m) {
    return (uint16_t)((m->sum + (m->n >> 1)) >> m->shift);
}

/* ======================= Part B: 가변 윈도우 ======================= */
/* 합 <= S 인 가장 긴 연속 구간 a[l..r]. 값은 비음수(uint16_t). 같은 길이가 여럿이면 가장 왼쪽.
 * 반환 = 길이 (0 = 그런 구간 없음), *start = 시작 인덱스 (길이 0 이면 0) */
static size_t longest_sum_le(const uint16_t *a, size_t n, uint32_t S, size_t *start) {
    uint64_t sum = 0;                    /* 잠깐 S + 65535 까지 커질 수 있다 → 64비트 */
    size_t l = 0, best = 0, bs = 0;
    for (size_t r = 0; r < n; r++) {
        sum += a[r];
        while (sum > S) sum -= a[l++];   /* 비음수라서: 왼쪽을 빼면 합이 줄어든다 */
        if (r + 1 - l > best) { best = r + 1 - l; bs = l; }
    }
    *start = bs;
    return best;
}

/* 같은 바이트가 두 번 나오지 않는 가장 긴 구간 s[l..r]. 0x00 도 보통 바이트 (C 문자열 아님).
 * 같은 길이가 여럿이면 가장 왼쪽. 반환 = 길이, *start = 시작 인덱스 */
static size_t longest_unique_run(const uint8_t *s, size_t n, size_t *start) {
    size_t last[256];                    /* last[c] = c 가 마지막으로 나온 위치 + 1, 0 = 아직 없음 */
    for (int c = 0; c < 256; c++) last[c] = 0;
    size_t l = 0, best = 0, bs = 0;
    for (size_t r = 0; r < n; r++) {
        uint8_t c = s[r];
        if (last[c] > l) l = last[c];    /* c 가 창 [l, r) 안에 있었다 → 그 다음 칸부터. l 은 뒤로 안 감 */
        last[c] = r + 1;
        if (r + 1 - l > best) { best = r + 1 - l; bs = l; }
    }
    *start = bs;
    return best;
}

/* (주어진 코드) 똑같은 축소 윈도우를 부호 있는 값에 그대로 쓴 버전 — 음수가 있으면 틀린다 (본문 반례) */
static size_t window_sum_le_signed(const int16_t *a, size_t n, int32_t S, size_t *start) {
    int64_t sum = 0;
    size_t l = 0, best = 0, bs = 0;
    for (size_t r = 0; r < n; r++) {
        sum += a[r];
        while (l <= r && sum > S) sum -= a[l++];
        if (r + 1 - l > best) { best = r + 1 - l; bs = l; }
    }
    *start = bs;
    return best;
}

/* ======================= Part C: ISR 가 넣고 태스크가 읽는다 ======================= */
typedef struct {
    ma_t ma;                    /* ISR 만 쓴다 (작성자 하나)                         */
    volatile uint32_t pub_sum;  /* ISR 가 갱신을 마치고 공개하는 (sum, count) 스냅샷 */
    volatile uint32_t pub_cnt;
    volatile uint32_t seq;      /* 공개할 때마다 +1 (시퀀스 카운터)                  */
} shared_ma_t;

/* 테스트 장치: 태스크 쪽 공유 변수 읽기는 전부 ld32() 를 거친다. 훅이 걸려 있으면 그 읽기 "직후"
 * 에 인터럽트가 끼어든 것처럼 ISR 을 실행한다 → 모든 끼어들기 지점을 결정적으로 재현. */
static void (*g_preempt_hook)(void);
static uint32_t ld32(const volatile uint32_t *p) {
    uint32_t v = *p;
    if (g_preempt_hook) g_preempt_hook();
    return v;
}

/* ADC 변환 완료 인터럽트 핸들러가 부른다. O(1), 루프 없음 */
static void isr_adc_sample(shared_ma_t *sh, uint16_t x) {
    ma_push(&sh->ma, x);
    sh->pub_sum = sh->ma.sum;
    sh->pub_cnt = sh->ma.count;
    sh->seq = sh->seq + 1u;
}

/* 태스크가 부른다. 공유 변수는 ld32() 로만 읽는다. 찢어지지 않은 (sum, count) 쌍으로 평균 계산 */
static uint16_t task_read_avg(shared_ma_t *sh) {
    uint32_t s0, sum, cnt;
    do {
        s0  = ld32(&sh->seq);
        sum = ld32(&sh->pub_sum);
        cnt = ld32(&sh->pub_cnt);
    } while (ld32(&sh->seq) != s0);     /* 그 사이 ISR 이 돌았으면 다시 */
    if (cnt == 0) return 0;
    return (uint16_t)((sum + cnt / 2u) / cnt);
}

/* (주어진 코드) 잠금도 시퀀스도 없이 두 번 읽는 버전 — 웜업 중에 찢어진다 */
static uint16_t task_read_avg_naive(shared_ma_t *sh) {
    uint32_t sum = ld32(&sh->pub_sum);
    uint32_t cnt = ld32(&sh->pub_cnt);
    if (cnt == 0) return 0;
    return (uint16_t)((sum + cnt / 2u) / cnt);
}

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

/* 기준(reference): 0.5 올림을 다른 식으로 — floor((2*sum + k) / (2k)) */
static uint32_t ref_round(uint64_t sum, uint64_t k) { return k ? (uint32_t)((2u * sum + k) / (2u * k)) : 0u; }

/* 브루트포스: 스트림 x[0..i] 에서 마지막 min(i+1, n) 개를 매번 새로 더한다 — O(n)/샘플 */
static uint64_t bf_window_sum(const uint16_t *x, size_t i, uint32_t n, uint32_t *k) {
    size_t lo = (i + 1 >= n) ? i + 1 - n : 0;
    uint64_t s = 0;
    for (size_t j = lo; j <= i; j++) s += x[j];
    *k = (uint32_t)(i + 1 - lo);
    return s;
}

/* 브루트포스: 모든 (l, r) — 비음수 가정 없이, 가장 왼쪽 최장 */
static size_t bf_sum_le(const int32_t *a, size_t n, int64_t S, size_t *start) {
    size_t best = 0, bs = 0;
    for (size_t l = 0; l < n; l++) {
        int64_t s = 0;
        for (size_t r = l; r < n; r++) {
            s += a[r];
            if (s <= S && r + 1 - l > best) { best = r + 1 - l; bs = l; }
        }
    }
    *start = bs;
    return best;
}

static size_t bf_unique(const uint8_t *s, size_t n, size_t *start) {
    size_t best = 0, bs = 0;
    for (size_t l = 0; l < n; l++) {
        bool seen[256] = { false };
        size_t r = l;
        while (r < n && !seen[s[r]]) { seen[s[r]] = true; r++; }
        if (r - l > best) { best = r - l; bs = l; }
    }
    *start = bs;
    return best;
}

/* Part C 테스트용: k 번째 ld32 직후에 ISR 을 한 번 실행 */
static shared_ma_t g_sh;
static int g_fire_at, g_loads;
static uint16_t g_isr_sample;
static void fire_once(void) {
    if (++g_loads == g_fire_at) { g_preempt_hook = NULL; isr_adc_sample(&g_sh, g_isr_sample); }
}
static void arm(int k, uint16_t x) { g_loads = 0; g_fire_at = k; g_isr_sample = x; g_preempt_hook = fire_once; }

#define STREAM_LEN 200000u
static uint16_t g_stream[STREAM_LEN];

int main(void) {
    ma_t m;
    size_t st;

    /* ---------- Part A: 이름 붙은 경우 ---------- */
    memset(&m, 0, sizeof m);
    CHECK(ma_init(&m, 0) == -1);
    CHECK(ma_init(&m, MA_MAX_N + 1) == -1);
    CHECK(ma_init(&m, 256) == 0 && m.shift == 8);
    CHECK(ma_init(&m, 100) == 0 && m.shift == -1);
    CHECK(ma_init(&m, 1) == 0 && m.shift == 0);

    memset(&m, 0, sizeof m);
    CHECK(ma_init(&m, 4) == 0 && ma_avg(&m) == 0 && !ma_ready(&m));     /* 샘플 0개 → 0 */
    {   /* 웜업 추적: 10 → 10, 20 → 15, 31 → 20.33→20, 40 → 25.25→25, 50 (10 밀려남) → 35.25→35 */
        const uint16_t in[5] = { 10, 20, 31, 40, 50 }, want[5] = { 10, 15, 20, 25, 35 };
        int ok = 1;
        for (int i = 0; i < 5; i++) { ma_push(&m, in[i]); if (ma_avg(&m) != want[i]) ok = 0; }
        CHECK(ok);
        CHECK(ma_ready(&m) && m.sum == 141 && ma_avg_shift(&m) == 35);
    }
    memset(&m, 0, sizeof m);
    ma_init(&m, 2); ma_push(&m, 1); ma_push(&m, 2);
    CHECK(ma_avg(&m) == 2 && ma_avg_shift(&m) == 2);                     /* 1.5 → 2 (0.5 올림) */
    memset(&m, 0, sizeof m);
    ma_init(&m, 4); ma_push(&m, 0); ma_push(&m, 0); ma_push(&m, 0); ma_push(&m, 2);
    CHECK(ma_avg(&m) == 1);                                             /* 0.5 → 1 */
    ma_push(&m, 0); ma_push(&m, 0); ma_push(&m, 0);                     /* 창 = {2,0,0,0} → 0.5 → 1 */
    CHECK(ma_avg(&m) == 1 && ma_avg_shift(&m) == 1);
    ma_push(&m, 1);                                                     /* 창 = {0,0,0,1} → 0.25 → 0 */
    CHECK(ma_avg(&m) == 0 && ma_avg_shift(&m) == 0);
    memset(&m, 0, sizeof m);
    ma_init(&m, 1); ma_push(&m, 7); ma_push(&m, 65535);
    CHECK(ma_avg(&m) == 65535 && m.sum == 65535);                       /* N=1: 마지막 샘플 그대로 */
    memset(&m, 0, sizeof m);
    ma_init(&m, MA_MAX_N);
    for (int i = 0; i < 1000; i++) ma_push(&m, 65535);
    CHECK(m.sum == 256u * 65535u && ma_avg(&m) == 65535 && ma_avg_shift(&m) == 65535);  /* 최대값 경계 */

    /* ---------- Part A: 긴 무작위 스트림 vs 매번 새로 더하기 ---------- */
    {
        uint32_t seed = 0xB10A5EEDu;
        for (size_t i = 0; i < STREAM_LEN; i++) {
            uint32_t r = xs32(&seed);
            /* 섞어서: 전 범위 / 12비트 ADC / 최대값 근처 */
            g_stream[i] = (uint16_t)((i / 5000) % 3 == 0 ? r : (i / 5000) % 3 == 1 ? (r & 0x0FFFu) : 65535u - (r & 7u));
        }
        const uint32_t ns[] = { 1, 2, 3, 5, 8, 16, 64, 100, 255, 256 };
        long bad_sum = 0, bad_avg = 0, bad_shift = 0, shift_checked = 0;
        for (size_t t = 0; t < sizeof ns / sizeof ns[0]; t++) {
            memset(&m, 0, sizeof m);
            ma_init(&m, ns[t]);
            for (size_t i = 0; i < STREAM_LEN; i++) {
                uint32_t k;
                ma_push(&m, g_stream[i]);
                uint64_t s = bf_window_sum(g_stream, i, ns[t], &k);
                if (m.sum != s) bad_sum++;
                if (ma_avg(&m) != ref_round(s, k)) bad_avg++;
                if (m.shift >= 0 && ma_ready(&m)) { shift_checked++; if (ma_avg_shift(&m) != ref_round(s, k)) bad_shift++; }
            }
        }
        printf("Part A: %u samples x 10 window sizes: sum mismatches=%ld avg mismatches=%ld shift mismatches=%ld (checked %ld)\n",
               STREAM_LEN, bad_sum, bad_avg, bad_shift, shift_checked);
        CHECK(bad_sum == 0);              /* 정수 누적합은 20만 샘플 뒤에도 정확 (드리프트 없음) */
        CHECK(bad_avg == 0);
        CHECK(bad_shift == 0 && shift_checked > 1000000);
    }

    /* ---------- Part B: 합 <= S 최장 구간 ---------- */
    {
        const uint16_t a[7] = { 1, 6, 3, 1, 2, 4, 5 };
        CHECK(longest_sum_le(a, 7, 10, &st) == 4 && st == 2);             /* [3,1,2,4] */
        CHECK(longest_sum_le(a, 0, 10, &st) == 0 && st == 0);             /* 빈 입력 */
        CHECK(longest_sum_le(a, 7, 0, &st) == 0);                         /* 모든 원소 > S */
        CHECK(longest_sum_le(a, 7, 1000, &st) == 7 && st == 0);           /* 전체 */
        const uint16_t z[5] = { 5, 0, 0, 7, 0 };
        CHECK(longest_sum_le(z, 5, 0, &st) == 2 && st == 1);              /* S = 0, 0 들만 */
        const uint16_t big[4] = { 65535, 65535, 65535, 65535 };
        CHECK(longest_sum_le(big, 4, 4294967295u, &st) == 4);             /* S 최대: 합이 2^32 넘지 않게 */
        CHECK(longest_sum_le(big, 4, 131070, &st) == 2 && st == 0);
    }
    {
        uint32_t seed = 12345u;
        long bad = 0, cases = 0;
        uint16_t a[48]; int32_t w[48];
        for (int t = 0; t < 4000; t++) {
            size_t n = xs32(&seed) % 48;
            uint32_t maxv = (t % 4 == 0) ? 65535u : (t % 4 == 1) ? 1u : 9u + (xs32(&seed) % 100);
            for (size_t i = 0; i < n; i++) { a[i] = (uint16_t)(xs32(&seed) % (maxv + 1)); w[i] = a[i]; }
            uint32_t S = (t % 4 == 0) ? xs32(&seed) % 400000u : xs32(&seed) % (uint32_t)(maxv * 8 + 2);
            size_t s1, s2;
            size_t l1 = longest_sum_le(a, n, S, &s1), l2 = bf_sum_le(w, n, S, &s2);
            cases++;
            if (l1 != l2 || s1 != s2) bad++;
        }
        printf("Part B sum<=S: %ld random arrays, mismatches vs brute force = %ld\n", cases, bad);
        CHECK(bad == 0);
    }
    {   /* 음수 반례: {3,-5,3}, S=1. 정답은 전체(합 1) 길이 3, 축소 윈도우는 3 을 버려서 2 */
        const int16_t a[3] = { 3, -5, 3 };
        const int32_t w[3] = { 3, -5, 3 };
        size_t s1, s2;
        size_t sw = window_sum_le_signed(a, 3, 1, &s1), bf = bf_sum_le(w, 3, 1, &s2);
        printf("counterexample {3,-5,3} S=1: sliding window=%zu brute force=%zu\n", sw, bf);
        CHECK(sw == 2 && bf == 3);
        uint32_t seed = 777u;
        long bad_neg = 0, bad_nonneg = 0;
        int16_t x[32]; int32_t y[32];
        for (int t = 0; t < 2000; t++) {
            size_t n = 1 + xs32(&seed) % 31;
            bool neg = t & 1;
            for (size_t i = 0; i < n; i++) {
                x[i] = (int16_t)(neg ? (int)(xs32(&seed) % 21) - 10 : (int)(xs32(&seed) % 11));
                y[i] = x[i];
            }
            int32_t S = (int32_t)(xs32(&seed) % 30);
            size_t l1 = window_sum_le_signed(x, n, S, &s1), l2 = bf_sum_le(y, n, S, &s2);
            if (l1 != l2) { if (neg) bad_neg++; else bad_nonneg++; }
        }
        printf("signed sliding window vs brute force: with negatives %ld/1000 wrong, non-negative %ld/1000 wrong\n",
               bad_neg, bad_nonneg);
        CHECK(bad_nonneg == 0 && bad_neg > 0);   /* 주어진 코드의 성질 확인 (TODO 와 무관) */
    }

    /* ---------- Part B: 중복 없는 최장 바이트 구간 ---------- */
    {
        CHECK(longest_unique_run((const uint8_t *)"abcabcbb", 8, &st) == 3 && st == 0);
        CHECK(longest_unique_run((const uint8_t *)"bbbbb", 5, &st) == 1 && st == 0);
        CHECK(longest_unique_run((const uint8_t *)"pwwkew", 6, &st) == 3 && st == 2);   /* "wke" */
        CHECK(longest_unique_run((const uint8_t *)"abba", 4, &st) == 2 && st == 0);     /* l 이 뒤로 가면 3 이 나온다 */
        CHECK(longest_unique_run((const uint8_t *)"", 0, &st) == 0 && st == 0);
        const uint8_t bin[5] = { 0x00, 0xFF, 0x80, 0x00, 0x7F };                       /* 0x00 과 0x80 이상 */
        CHECK(longest_unique_run(bin, 5, &st) == 4 && st == 1);
        uint8_t all[257];
        for (int i = 0; i < 256; i++) all[i] = (uint8_t)(255 - i);
        all[256] = 255;
        CHECK(longest_unique_run(all, 256, &st) == 256 && st == 0);
        CHECK(longest_unique_run(all, 257, &st) == 256 && st == 0);
    }
    {
        uint32_t seed = 0xC0FFEEu;
        long bad = 0, cases = 0;
        uint8_t s[600];
        for (int t = 0; t < 3000; t++) {
            size_t n = xs32(&seed) % 600;
            uint32_t alpha = 1 + xs32(&seed) % 256;              /* 알파벳 크기 1..256 */
            for (size_t i = 0; i < n; i++) s[i] = (uint8_t)(xs32(&seed) % alpha * (256 / alpha) + (t & 1) * 0x80);
            size_t s1, s2;
            size_t l1 = longest_unique_run(s, n, &s1), l2 = bf_unique(s, n, &s2);
            cases++;
            if (l1 != l2 || s1 != s2) bad++;
        }
        printf("Part B unique: %ld random byte strings, mismatches vs brute force = %ld\n", cases, bad);
        CHECK(bad == 0);
    }

    /* ---------- Part C: ISR push / 태스크 read ---------- */
    {   /* 웜업 중: 10, 20 이 들어가 있고(평균 15) ISR 이 90 을 넣으면 평균 40. */
        memset(&g_sh, 0, sizeof g_sh); ma_init(&g_sh.ma, 8);
        isr_adc_sample(&g_sh, 10); isr_adc_sample(&g_sh, 20);
        CHECK(task_read_avg(&g_sh) == 15);
        arm(1, 90);                                   /* sum 을 읽은 직후 ISR → (30, 3) → 10 */
        uint16_t torn = task_read_avg_naive(&g_sh);
        printf("naive read with ISR between loads: %u (before=15, after=40)\n", torn);
        CHECK(torn == 10);                            /* 15 도 40 도 아닌 값 = 찢어진 읽기 */
        CHECK(task_read_avg(&g_sh) == 40);
        int ok = 1;
        for (int k = 1; k <= 8; k++) {                /* 모든 읽기 지점 뒤에 ISR 을 끼워 본다 */
            memset(&g_sh, 0, sizeof g_sh); ma_init(&g_sh.ma, 8);
            isr_adc_sample(&g_sh, 10); isr_adc_sample(&g_sh, 20);
            arm(k, 90);
            uint16_t v = task_read_avg(&g_sh);
            g_preempt_hook = NULL;
            if (k <= 3 ? v != 40 : v != 15) ok = 0;   /* 1~3: 재시도 후 새 값, 4~: 이미 끝난 읽기 = 옛 값 */
        }
        CHECK(ok);
    }
    {   /* 무작위: 20000 번 읽고, 매번 무작위 지점에 ISR 을 끼운다. 결과는 반드시 직전 or 직후 평균 */
        uint32_t seed = 0xADC0u;
        ma_t ref;
        memset(&g_sh, 0, sizeof g_sh); memset(&ref, 0, sizeof ref);
        ma_init(&g_sh.ma, 16); ma_init(&ref, 16);
        long bad = 0, torn_naive = 0;
        for (int t = 0; t < 20000; t++) {
            uint16_t x = (uint16_t)(xs32(&seed) & 0x0FFF);
            if (t % 50 == 0) { memset(&g_sh, 0, sizeof g_sh); ma_init(&g_sh.ma, 16); memset(&ref, 0, sizeof ref); ma_init(&ref, 16); }
            uint16_t before = ma_avg(&ref);
            ma_push(&ref, x);
            uint16_t after = ma_avg(&ref);
            int k = 1 + (int)(xs32(&seed) % 6);
            shared_ma_t save = g_sh;
            arm(k, x);
            uint16_t v = task_read_avg(&g_sh);
            if (g_preempt_hook) { g_preempt_hook = NULL; isr_adc_sample(&g_sh, x); }   /* 안 끼었으면 지금 */
            if (v != before && v != after) bad++;
            g_sh = save;                              /* 같은 상황에서 naive 도 */
            arm(k, x);
            v = task_read_avg_naive(&g_sh);
            if (g_preempt_hook) { g_preempt_hook = NULL; isr_adc_sample(&g_sh, x); }
            if (v != before && v != after) torn_naive++;
        }
        printf("Part C: 20000 reads with random ISR point: seq-read inconsistent=%ld, naive inconsistent=%ld\n",
               bad, torn_naive);
        CHECK(bad == 0);
        CHECK(task_read_avg(&g_sh) == ma_avg(&ref));
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
