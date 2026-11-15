/* ALGO-M10 demo_wrong.c — "그럴듯한" 틀린 그리디들을 최적값과 대조한다.
 *
 * 쿨다운 스케줄러: 최적 길이 = 공식 max(n, (f-1)(c+1)+m) (solution.c 에서 브루트포스로 검증된 값).
 *   W1 라운드마다 id 순서로 서로 다른 종류 c+1 개 (개수 무시)
 *   W2 칸마다 "준비된 종류 중 id 가 가장 작은 것" (FIFO 처럼, 개수 무시)
 *   OK 칸마다 "준비된 종류 중 남은 개수가 가장 많은 것" (연습 1의 이벤트 루프판)
 * 작업 선택: 최적 이익 = 부분집합 브루트포스.
 *   W3 이익 내림차순 + 마감 이하 "가장 이른" 빈 슬롯
 *   W4 이익 내림차순 + 슬롯 d 가 비었을 때만 (아래로 찾지 않음)
 *   W5 마감 오름차순 + 들어가면 넣기, 꽉 차면 새 작업을 버림 (최솟값과 교체 안 함)
 *   OK 마감 오름차순 + 꽉 차면 최솟값과 교체 (jobseq_heap 과 같은 규칙, 선형 최솟값 탐색판)
 * 각 방법의 가장 작은 반례(작은 n 부터 전수)와 무작위 오답 비율을 출력한다.
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_wrong.c -o demo_wrong && ./demo_wrong
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define K 4
static uint32_t g_rng = 0x2545F491u;
static uint32_t xorshift32(void) { uint32_t x = g_rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return g_rng = x; }

static uint64_t formula(const uint32_t *cnt, uint32_t c) {
    uint32_t f = 0, m = 0; uint64_t n = 0;
    for (int i = 0; i < K; i++) { n += cnt[i]; if (cnt[i] > f) { f = cnt[i]; m = 1; } else if (cnt[i] == f && f) m++; }
    if (!n) return 0;
    uint64_t a = (uint64_t)(f - 1) * (c + 1) + m;
    return a > n ? a : n;
}
/* mode 1 = W1 (id 순서 라운드) */
static uint64_t w1_round_id(const uint32_t *cnt0, uint32_t c, char *seq) {
    uint32_t cnt[K]; uint64_t left = 0, len = 0;
    for (int i = 0; i < K; i++) { cnt[i] = cnt0[i]; left += cnt[i]; }
    while (left) {
        uint32_t k = 0;
        for (int i = 0; i < K && k < c + 1; i++) if (cnt[i]) { cnt[i]--; left--; k++; if (seq) seq[len] = (char)('A' + i); len++; }
        if (left) for (; k < c + 1; k++) { if (seq) seq[len] = '_'; len++; }
    }
    if (seq) seq[len] = 0;
    return len;
}
/* mode: 0 = 남은 개수 최대 (OK), 1 = id 최소 (W2) */
static uint64_t per_slot(const uint32_t *cnt0, uint32_t c, int smallest_id, char *seq) {
    uint32_t cnt[K]; int64_t ready[K]; uint64_t left = 0, t = 0, last = 0;
    for (int i = 0; i < K; i++) { cnt[i] = cnt0[i]; ready[i] = 0; left += cnt[i]; }
    while (left) {
        int best = -1;
        for (int i = 0; i < K; i++)
            if (cnt[i] && ready[i] <= (int64_t)t && (best < 0 || (!smallest_id && cnt[i] > cnt[best]))) best = i;
        if (best >= 0) { cnt[best]--; ready[best] = (int64_t)(t + c + 1); left--; last = t + 1; }
        if (seq) seq[t] = best >= 0 ? (char)('A' + best) : '_';
        t++;
    }
    if (seq) seq[last] = 0;
    return last;
}

/* ---------------- 작업 선택 ---------------- */
#define NJ 12
typedef struct { uint32_t d, p; } job_t;
static uint64_t brute(const job_t *J, int n) {
    uint64_t best = 0;
    for (uint32_t mask = 0; mask < (1u << n); mask++) {
        uint32_t c[NJ + 2] = {0}; uint64_t s = 0; bool ok = true;
        for (int i = 0; i < n; i++) if (mask >> i & 1) { uint32_t d = J[i].d < (uint32_t)n ? J[i].d : (uint32_t)n; if (!d) ok = false; c[d]++; s += J[i].p; }
        uint32_t run = 0;
        for (int t = 1; t <= n && ok; t++) { run += c[t]; if (run > (uint32_t)t) ok = false; }
        if (ok && s > best) best = s;
    }
    return best;
}
static void order_by_profit(const job_t *J, int n, int *o) {      /* 이익 내림차순, 같으면 id 순 (삽입 정렬) */
    for (int i = 0; i < n; i++) o[i] = i;
    for (int i = 1; i < n; i++) for (int j = i; j > 0 && (J[o[j]].p > J[o[j - 1]].p); j--) { int t = o[j]; o[j] = o[j - 1]; o[j - 1] = t; }
}
static void order_by_deadline(const job_t *J, int n, int *o) {
    for (int i = 0; i < n; i++) o[i] = i;
    for (int i = 1; i < n; i++) for (int j = i; j > 0 && (J[o[j]].d < J[o[j - 1]].d); j--) { int t = o[j]; o[j] = o[j - 1]; o[j - 1] = t; }
}
/* mode 3 = 가장 이른 빈 슬롯, 4 = 정확히 d 만 */
static uint64_t w_profit(const job_t *J, int n, int mode) {
    int o[NJ]; bool used[NJ + 1] = {false}; uint64_t s = 0;
    order_by_profit(J, n, o);
    for (int k = 0; k < n; k++) {
        uint32_t d = J[o[k]].d < (uint32_t)n ? J[o[k]].d : (uint32_t)n;
        if (mode == 3) { for (uint32_t t = 1; t <= d; t++) if (!used[t]) { used[t] = true; s += J[o[k]].p; break; } }
        else if (d && !used[d]) { used[d] = true; s += J[o[k]].p; }
    }
    return s;
}
/* mode 5 = 들어가면 넣기(교체 없음), 7 = 꽉 차면 최솟값과 교체 (올바른 규칙) */
static uint64_t w_deadline(const job_t *J, int n, int mode) {
    int o[NJ], chosen[NJ], k = 0; uint64_t s = 0;
    order_by_deadline(J, n, o);
    for (int i = 0; i < n; i++) {
        uint32_t d = J[o[i]].d < (uint32_t)n ? J[o[i]].d : (uint32_t)n;
        if (!d) continue;
        if ((uint32_t)k < d) { chosen[k++] = o[i]; continue; }
        if (mode == 7) {                               /* 최솟값보다 크면 교체 */
            int mi = 0;
            for (int q = 1; q < k; q++) if (J[chosen[q]].p < J[chosen[mi]].p) mi = q;
            if (J[chosen[mi]].p < J[o[i]].p) chosen[mi] = o[i];
        }
    }
    for (int q = 0; q < k; q++) s += J[chosen[q]].p;
    return s;
}

int main(void) {
    /* ---- 쿨다운: 가장 작은 반례 (n 오름차순, 개수 벡터 전수, c ≤ 3) ---- */
    const char *tname[3] = {"W1 round, id order", "W2 per-slot, smallest id", "OK per-slot, max remaining"};
    for (int mth = 0; mth < 3; mth++) {
        bool found = false;
        for (uint32_t n = 1; n <= 10 && !found; n++)
        for (uint32_t c = 0; c <= 3 && !found; c++)
        for (uint32_t a = 0; a <= n && !found; a++) for (uint32_t b = 0; a + b <= n && !found; b++)
        for (uint32_t d = 0; a + b + d <= n && !found; d++) {
            uint32_t cnt[K] = {a, b, d, n - a - b - d};
            uint64_t got = mth == 0 ? w1_round_id(cnt, c, NULL) : per_slot(cnt, c, mth == 1, NULL);
            if (got != formula(cnt, c)) {
                char s[256];
                if (mth == 0) w1_round_id(cnt, c, s); else per_slot(cnt, c, mth == 1, s);
                printf("%-28s smallest counterexample: counts A..D={%u,%u,%u,%u} c=%u -> %s (len %llu, optimum %llu)\n",
                       tname[mth], a, b, d, n - a - b - d, c, s, (unsigned long long)got, (unsigned long long)formula(cnt, c));
                found = true;
            }
        }
        if (!found) printf("%-28s no counterexample with n <= 10, c <= 3 (all count vectors)\n", tname[mth]);
    }
    /* ---- 쿨다운: 무작위 오답 비율 ---- */
    {
        long cases = 100000, wrong[3] = {0, 0, 0}; uint64_t extra[3] = {0, 0, 0};
        for (long it = 0; it < cases; it++) {
            uint32_t cnt[K], c = xorshift32() % 6;
            for (int i = 0; i < K; i++) cnt[i] = xorshift32() % 8;
            uint64_t opt = formula(cnt, c);
            uint64_t g[3] = {w1_round_id(cnt, c, NULL), per_slot(cnt, c, 1, NULL), per_slot(cnt, c, 0, NULL)};
            for (int m = 0; m < 3; m++) if (g[m] != opt) { wrong[m]++; extra[m] += g[m] - opt; }
        }
        printf("cooldown random (4 types, counts 0..7, c 0..5): %ld cases\n", cases);
        for (int m = 0; m < 3; m++) printf("  %-28s wrong %6ld (%.1f%%), extra slots total %llu\n", tname[m], wrong[m], 100.0 * wrong[m] / cases, (unsigned long long)extra[m]);
    }

    /* ---- 작업 선택: 가장 작은 반례 (n ≤ 3, 마감 0..n, 이익 1..3 전수) ---- */
    const char *jname[4] = {"W3 profit, earliest slot", "W4 profit, exact slot d", "W5 deadline, accept-if-fits", "OK deadline + min-heap swap"};
    const int jmode[4] = {3, 4, 5, 7};
    for (int mth = 0; mth < 4; mth++) {
        bool found = false;
        for (int n = 1; n <= 3 && !found; n++) {
            uint32_t tot = 1; for (int i = 0; i < n; i++) tot *= (uint32_t)(n + 1) * 3;
            for (uint32_t code = 0; code < tot && !found; code++) {
                job_t J[NJ]; uint32_t x = code;
                for (int i = 0; i < n; i++) { J[i].d = x % (uint32_t)(n + 1); x /= (uint32_t)(n + 1); J[i].p = 1 + x % 3; x /= 3; }
                uint64_t got = jmode[mth] <= 4 ? w_profit(J, n, jmode[mth]) : w_deadline(J, n, jmode[mth]);
                uint64_t opt = brute(J, n);
                if (got != opt) {
                    printf("%-28s smallest counterexample: (d,p) =", jname[mth]);
                    for (int i = 0; i < n; i++) printf(" (%u,%u)", J[i].d, J[i].p);
                    printf(" -> %llu, optimum %llu\n", (unsigned long long)got, (unsigned long long)opt);
                    found = true;
                }
            }
        }
        if (!found) printf("%-28s no counterexample with n <= 3\n", jname[mth]);
    }
    /* ---- 작업 선택: 무작위 오답 비율 ---- */
    {
        long cases = 20000, wrong[4] = {0, 0, 0, 0}; uint64_t loss[4] = {0, 0, 0, 0}, optsum = 0;
        for (long it = 0; it < cases; it++) {
            int n = 1 + (int)(xorshift32() % NJ); job_t J[NJ];
            for (int i = 0; i < n; i++) { J[i].d = xorshift32() % (uint32_t)(n + 1); J[i].p = 1 + xorshift32() % 100; }
            uint64_t opt = brute(J, n); optsum += opt;
            for (int m = 0; m < 4; m++) {
                uint64_t got = jmode[m] <= 4 ? w_profit(J, n, jmode[m]) : w_deadline(J, n, jmode[m]);
                if (got != opt) { wrong[m]++; loss[m] += opt - got; }
            }
        }
        printf("job sequencing random (n 1..12, d 0..n, p 1..100): %ld cases\n", cases);
        for (int m = 0; m < 4; m++) printf("  %-28s wrong %6ld (%.1f%%), profit lost %.2f%% of optimum\n", jname[m], wrong[m], 100.0 * wrong[m] / cases, 100.0 * (double)loss[m] / (double)optsum);
    }
    return 0;
}
