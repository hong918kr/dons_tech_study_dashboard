/* MEM-B10 추가 연습 — 한 번에 검증 (결정적, 타이밍 없음)
 *
 * 연습 1: Fisher–Yates 로 섞으면 0 에서 출발한 체인이 도는 원소 수의 기댓값 = (n+1)/2
 * 연습 2: Sattolo 의 균등성 — n = 4 의 (n-1)! = 6 개 순환 순열이 각각 1/6
 * 연습 3: 혼합 근사 모델과 "반쯤 오른 점" — 곡선이 두 레벨의 중간을 지나는 W 를 로그 보간으로 찾기
 * 연습 4: k 개 독립 커서 (MLP) — 한 사이클을 n/k 간격으로 나눈 출발점, Little 의 법칙
 * 연습 5: 페이지 배치의 칸 회전 — 페이지 안 칸이 고르게 퍼지나, 오프셋 고정이면 한 칸에 몰린다
 *
 * cc -std=c11 -Wall -Wextra -Werror -O2 practice.c && ./a.out
 */
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---- solution.c 축약판: 난수 · Sattolo · Fisher–Yates · 크기 목록 ---- */
static uint64_t xs64(uint64_t *s) { uint64_t x = *s; x ^= x << 13; x ^= x >> 7; x ^= x << 17; return *s = x; }
static uint64_t rand_below(uint64_t *s, uint64_t bound) {
    uint64_t lim = UINT64_MAX - UINT64_MAX % bound, r;
    do { r = xs64(s); } while (r >= lim);
    return r % bound;
}
static void shuffle(uint32_t *next, size_t n, uint64_t *seed, bool sattolo) {
    for (size_t i = 0; i < n; i++) next[i] = (uint32_t)i;
    for (size_t i = n; i-- > 1;) {
        size_t j = (size_t)rand_below(seed, sattolo ? i : i + 1);
        uint32_t t = next[i]; next[i] = next[j]; next[j] = t;
    }
}
static size_t ws_sizes(size_t lo, size_t hi, size_t node, size_t *out, size_t cap) {
    size_t cnt = 0;
    for (size_t p = lo; p <= hi && cnt < cap; p <<= 1) {
        out[cnt++] = p;
        unsigned k = (unsigned)__builtin_ctzll((unsigned long long)p);
        size_t h = (size_t)sqrt((double)((uint64_t)1 << (2 * k + 1)));   /* 2^(2k+1) 는 double 로 정확 */
        h -= h % node;
        if (h > p && h <= hi && cnt < cap) out[cnt++] = h;
    }
    return cnt;
}

/* ---- 연습 1 ---- */
static size_t cycle_len_from(const uint32_t *next, size_t start) {
    size_t len = 1;
    for (uint32_t i = next[start]; i != start; i = next[i]) len++;
    return len;
}

/* ---- 연습 2: 순열 → 번호 (n = 4, 기수 4 로 충분) ---- */
static unsigned perm_code4(const uint32_t *p) { return p[0] | p[1] << 2 | p[2] << 4 | p[3] << 6; }

/* ---- 연습 3 ---- */
/* 단순 혼합 근사: W ≤ C 면 t_in, 아니면 줄이 캐시에 있을 확률을 C/W 로 보고 섞는다. */
static double mix_latency(double W, double C, double t_in, double t_out) {
    if (W <= C) return t_in;
    double h = C / W;
    return h * t_in + (1.0 - h) * t_out;
}
/* y 가 처음으로 mid = (lo+hi)/2 이상이 되는 구간 [x[i-1], x[i]] 에서 log2(x) 를 선형 보간. 없으면 0. */
static double half_rise_size(const size_t *x, const double *y, size_t n, double lo, double hi) {
    double mid = 0.5 * (lo + hi);
    for (size_t i = 1; i < n; i++) {
        if (y[i - 1] < mid && y[i] >= mid) {
            double a = log2((double)x[i - 1]), b = log2((double)x[i]);
            double f = (mid - y[i - 1]) / (y[i] - y[i - 1]);
            return exp2(a + f * (b - a));
        }
    }
    return 0.0;
}

/* ---- 연습 4 ---- */
/* 원소 0 에서 출발해 n/k 걸음마다 출발점 하나 (k | n 가정). out[j] = j 번째 커서의 출발 원소. */
static void spaced_starts(const uint32_t *next, size_t n, unsigned k, uint32_t *out) {
    uint32_t p = 0;
    for (unsigned j = 0; j < k; j++) {
        out[j] = p;
        for (size_t s = 0; s < n / k; s++) p = next[p];
    }
}
/* Little 의 법칙: 동시에 날아다녀야 하는 줄 수 = 대역폭(B/ns = GB/s) × 지연(ns) / 줄 크기 */
static double lines_in_flight(double gbps, double latency_ns, double line_bytes) {
    return gbps * latency_ns / line_bytes;
}

/* ---- 연습 5 ---- */
static size_t page_slot(size_t i, size_t node, size_t page, bool rotate) {
    return rotate ? i % (page / node) : 0;                   /* 페이지 안 몇 번째 칸인가 */
}

/* ---- 하네스 ---- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main(void) {
    static uint32_t nx[4096];

    /* 연습 1 */
    {
        const size_t n = 1000;
        double sum = 0; int half = 0;
        for (uint64_t sd = 1; sd <= 4000; sd++) {
            uint64_t seed = sd * 0x9E3779B97F4A7C15ull;
            shuffle(nx, n, &seed, false);
            size_t L = cycle_len_from(nx, 0);
            sum += (double)L; half += L <= n / 2;
        }
        double mean = sum / 4000;
        printf("ex1: fisher-yates n=1000, cycle through 0: mean %.1f (theory 500.5), P(len<=500) %.3f (theory 0.500)\n",
               mean, half / 4000.0);
        CHECK(mean > 485 && mean < 516);                     /* σ_mean = 288.7/√4000 = 4.6 → ±3.3σ */
        CHECK(half > 1880 && half < 2120);                    /* σ = √(4000/4) = 31.6 → ±3.8σ */
        uint64_t seed = 3;
        shuffle(nx, n, &seed, true);
        CHECK(cycle_len_from(nx, 0) == n);                    /* Sattolo 는 언제나 n */
    }

    /* 연습 2 */
    {
        int cnt[256] = { 0 }, distinct = 0, cyclic_ok = 1;
        uint64_t seed = 11;
        const int R = 60000;
        for (int r = 0; r < R; r++) { shuffle(nx, 4, &seed, true); cnt[perm_code4(nx)]++; }
        for (unsigned c = 0; c < 256; c++) {
            if (!cnt[c]) continue;
            distinct++;
            uint32_t p[4] = { c & 3, c >> 2 & 3, c >> 4 & 3, c >> 6 & 3 };
            if (cycle_len_from(p, 0) != 4) cyclic_ok = 0;
            if (cnt[c] < 10000 - 400 || cnt[c] > 10000 + 400) cyclic_ok = 0;   /* σ = 91 → ±4.4σ */
        }
        printf("ex2: sattolo n=4 x %d: %d distinct permutations, all 4-cycles and within 10000±400: %s\n",
               R, distinct, cyclic_ok ? "yes" : "no");
        CHECK(distinct == 6);
        CHECK(cyclic_ok);
    }

    /* 연습 3 */
    {
        size_t x[64];
        size_t n = ws_sizes(4096, (size_t)1 << 30, 128, x, 64);
        CHECK(n == 37 && x[1] == 5760 && x[3] == 11520);
        double y[64];
        for (size_t i = 0; i < n; i++) y[i] = mix_latency((double)x[i], 131072.0, 0.9, 9.0);
        double h = half_rise_size(x, y, n, 0.9, 9.0);
        printf("ex3: mix model C=128 KiB: half-rise at %.0f B (2C = 262144)\n", h);
        CHECK(fabs(h - 262144.0) < 1.0);                      /* 중간 = C/W = 1/2 → W = 2C, 목록에 정확히 있음 */
        for (size_t i = 0; i < n; i++) y[i] = mix_latency((double)x[i], 16777216.0, 9.0, 120.0);
        h = half_rise_size(x, y, n, 9.0, 120.0);
        CHECK(fabs(h - 33554432.0) < 1.0);
        CHECK(mix_latency(100.0, 128.0, 1.0, 9.0) == 1.0);
        CHECK(half_rise_size(x, y, n, 200.0, 300.0) == 0.0);  /* 그 레벨을 지나지 않음 */
    }

    /* 연습 4 */
    {
        const size_t n = 4096;
        uint64_t seed = 77;
        shuffle(nx, n, &seed, true);
        static const unsigned ks[5] = { 1, 2, 4, 8, 16 };
        int ok = 1;
        for (int t = 0; t < 5; t++) {
            unsigned k = ks[t];
            uint32_t st[16];
            static uint8_t seen[4096];
            memset(seen, 0, sizeof seen);
            spaced_starts(nx, n, k, st);
            size_t visits = 0;
            for (unsigned j = 0; j < k; j++) {               /* 커서 j 가 n/k 걸음 동안 밟는 원소 */
                uint32_t p = st[j];
                for (size_t s = 0; s < n / k; s++) { if (seen[p]++) ok = 0; visits++; p = nx[p]; }
                if (p != st[(j + 1) % k]) ok = 0;             /* 다음 커서의 출발점에서 끝난다 */
            }
            if (visits != n) ok = 0;
        }
        printf("ex4: spaced starts k=1..16 partition the cycle: %s\n", ok ? "yes" : "no");
        CHECK(ok);
        CHECK(lines_in_flight(1.28, 100.0, 128.0) == 1.0);
        double need = lines_in_flight(100.0, 120.0, 128.0);   /* 100 GB/s 를 120 ns 지연으로 */
        printf("ex4: 100 GB/s at 120 ns with 128 B lines needs %.2f lines in flight\n", need);
        CHECK(need > 93.7 && need < 93.8);
    }

    /* 연습 5 */
    {
        const size_t P = 1000, node = 128, page = 16384;
        size_t slots = page / node, cnt[256] = { 0 }, cnt0[256] = { 0 };
        for (size_t i = 0; i < P; i++) { cnt[page_slot(i, node, page, true)]++; cnt0[page_slot(i, node, page, false)]++; }
        size_t mn = SIZE_MAX, mx = 0;
        for (size_t s = 0; s < slots; s++) { if (cnt[s] < mn) mn = cnt[s]; if (cnt[s] > mx) mx = cnt[s]; }
        printf("ex5: P=1000 pages, 128 slots: rotate min %zu max %zu | fixed offset: slot0 %zu\n", mn, mx, cnt0[0]);
        CHECK(mn == 7 && mx == 8);                            /* 1000 = 128·7 + 104 */
        CHECK(cnt0[0] == P);
        CHECK(P * node == 128000 && P * page == 16384000);    /* 데이터 125 KiB vs 주소 범위 15.6 MiB */
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
