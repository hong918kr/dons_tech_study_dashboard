/* ALGO-M10 demo_bench.c — 작업 선택 3가지 구현의 시간 (ns/작업) + 쿨다운 스케줄러 시간 (ns/작업).
 *
 *   naive : 이익 내림차순(힙) + 마감 d 에서 아래로 한 칸씩 빈 슬롯 찾기  — 최악 O(n^2)
 *   dsu   : 이익 내림차순(힙) + slot_find (경로 반감)                 — O(n log n) (힙이 지배)
 *   heap  : 마감 계수 정렬 + 이익 최소 힙                              — O(n log n)
 * 분포: uniform  마감 ~ U[1, n]
 *       all-n    모든 마감 = n (naive 의 최악: k 번째 작업이 k 칸을 훑는다)
 *       tight    마감 ~ U[1, n/16] (대부분 버려짐; naive 는 버리기 전에 d 칸을 다 훑는다)
 * 측정: 1회 워밍업 + 5회 중앙값, clock_gettime(CLOCK_MONOTONIC). 결과 이익은 세 방법이 같은지 확인하고 합을 출력.
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_bench.c -o demo_bench && ./demo_bench
 */
#define _POSIX_C_SOURCE 200809L
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define MAX_JOBS (1u << 18)
#define NO_JOB (-1)
typedef struct { uint32_t deadline; uint32_t profit; } job_t;
typedef int (*job_above_fn)(const job_t *J, uint32_t a, uint32_t b);
static int by_profit_desc(const job_t *J, uint32_t a, uint32_t b) { return J[a].profit > J[b].profit || (J[a].profit == J[b].profit && a < b); }
static int by_profit_asc(const job_t *J, uint32_t a, uint32_t b) { return J[a].profit < J[b].profit || (J[a].profit == J[b].profit && a > b); }
static void ih_sift_down(uint32_t *h, size_t n, size_t i, const job_t *J, job_above_fn above) {
    uint32_t x = h[i];
    for (;;) { size_t c = 2 * i + 1; if (c >= n) break; if (c + 1 < n && above(J, h[c + 1], h[c])) c++; if (!above(J, h[c], x)) break; h[i] = h[c]; i = c; }
    h[i] = x;
}
static void ih_heapify(uint32_t *h, size_t n, const job_t *J, job_above_fn above) { for (size_t i = n / 2; i-- > 0;) ih_sift_down(h, n, i, J, above); }
static void ih_push(uint32_t *h, size_t *n, uint32_t x, const job_t *J, job_above_fn above) {
    size_t i = (*n)++;
    while (i > 0) { size_t p = (i - 1) / 2; if (!above(J, x, h[p])) break; h[i] = h[p]; i = p; }
    h[i] = x;
}
static uint32_t ih_pop(uint32_t *h, size_t *n, const job_t *J, job_above_fn above) {
    uint32_t top = h[0];
    if (--*n) { h[0] = h[*n]; ih_sift_down(h, *n, 0, J, above); }
    return top;
}
static uint32_t g_par[MAX_JOBS + 1], g_heap[MAX_JOBS], g_order[MAX_JOBS], g_bucket[MAX_JOBS + 2];
static uint8_t g_chosen[MAX_JOBS];
static uint32_t slot_find(uint32_t s) { while (g_par[s] != s) { g_par[s] = g_par[g_par[s]]; s = g_par[s]; } return s; }
static uint64_t g_naive_steps;

static uint64_t jobseq_naive(const job_t *J, size_t n, int32_t *slot_job) {
    uint64_t total = 0; size_t hn = n;
    for (size_t s = 0; s <= n; s++) slot_job[s] = NO_JOB;
    for (size_t i = 0; i < n; i++) g_heap[i] = (uint32_t)i;
    ih_heapify(g_heap, hn, J, by_profit_desc);
    while (hn) {
        uint32_t j = ih_pop(g_heap, &hn, J, by_profit_desc);
        uint32_t d = J[j].deadline < n ? J[j].deadline : (uint32_t)n;
        for (uint32_t s = d; s >= 1; s--) {
            g_naive_steps++;
            if (slot_job[s] == NO_JOB) { slot_job[s] = (int32_t)j; total += J[j].profit; break; }
        }
    }
    return total;
}
static uint64_t jobseq_dsu(const job_t *J, size_t n, int32_t *slot_job) {
    uint64_t total = 0; size_t hn = n;
    for (size_t s = 0; s <= n; s++) { g_par[s] = (uint32_t)s; slot_job[s] = NO_JOB; }
    for (size_t i = 0; i < n; i++) g_heap[i] = (uint32_t)i;
    ih_heapify(g_heap, hn, J, by_profit_desc);
    while (hn) {
        uint32_t j = ih_pop(g_heap, &hn, J, by_profit_desc);
        uint32_t d = J[j].deadline < n ? J[j].deadline : (uint32_t)n;
        uint32_t s = slot_find(d);
        if (s == 0) continue;
        slot_job[s] = (int32_t)j; g_par[s] = s - 1; total += J[j].profit;
    }
    return total;
}
static uint64_t jobseq_heap(const job_t *J, size_t n, int32_t *slot_job) {
    uint64_t total = 0; size_t hn = 0;
    memset(g_bucket, 0, (n + 2) * sizeof g_bucket[0]);
    for (size_t i = 0; i < n; i++) { uint32_t d = J[i].deadline < n ? J[i].deadline : (uint32_t)n; g_bucket[d + 1]++; }
    for (size_t d = 1; d <= n + 1; d++) g_bucket[d] += g_bucket[d - 1];
    for (size_t i = 0; i < n; i++) { uint32_t d = J[i].deadline < n ? J[i].deadline : (uint32_t)n; g_order[g_bucket[d]++] = (uint32_t)i; }
    for (size_t k = 0; k < n; k++) {
        uint32_t j = g_order[k], d = J[j].deadline < n ? J[j].deadline : (uint32_t)n;
        if (d == 0) continue;
        ih_push(g_heap, &hn, j, J, by_profit_asc);
        if (hn > d) (void)ih_pop(g_heap, &hn, J, by_profit_asc);
    }
    memset(g_chosen, 0, n);
    for (size_t i = 0; i < hn; i++) { g_chosen[g_heap[i]] = 1; total += J[g_heap[i]].profit; }
    for (size_t s = 0; s <= n; s++) slot_job[s] = NO_JOB;
    uint32_t t = 0;
    for (size_t k = 0; k < n; k++) if (g_chosen[g_order[k]]) slot_job[++t] = (int32_t)g_order[k];
    return total;
}

/* ---- 쿨다운 스케줄러 (solution.c 와 같은 코드) ---- */
#define MAX_TYPES 26
#define IDLE 0xFFu
static int type_above(const uint32_t *cnt, uint8_t a, uint8_t b) { return cnt[a] > cnt[b] || (cnt[a] == cnt[b] && a < b); }
static void th_push(uint8_t *h, size_t *hn, const uint32_t *cnt, uint8_t x) {
    size_t i = (*hn)++;
    while (i > 0) { size_t p = (i - 1) / 2; if (!type_above(cnt, x, h[p])) break; h[i] = h[p]; i = p; }
    h[i] = x;
}
static uint8_t th_pop(uint8_t *h, size_t *hn, const uint32_t *cnt) {
    uint8_t top = h[0], x = h[--*hn]; size_t i = 0, n = *hn;
    for (;;) { size_t c = 2 * i + 1; if (c >= n) break; if (c + 1 < n && type_above(cnt, h[c + 1], h[c])) c++; if (!type_above(cnt, h[c], x)) break; h[i] = h[c]; i = c; }
    if (n) h[i] = x;
    return top;
}
static uint64_t sched_build(const uint8_t *task, size_t n, uint32_t cool, uint8_t *out, size_t cap) {
    uint32_t cnt[MAX_TYPES] = {0}; uint8_t h[MAX_TYPES], round[MAX_TYPES]; size_t hn = 0;
    uint64_t len = 0, width = (uint64_t)cool + 1;
    for (size_t i = 0; i < n; i++) { if (task[i] >= MAX_TYPES) return 0; cnt[task[i]]++; }
    for (uint8_t t = 0; t < MAX_TYPES; t++) if (cnt[t]) th_push(h, &hn, cnt, t);
    while (hn) {
        size_t k = 0;
        while (hn && k < width) round[k++] = th_pop(h, &hn, cnt);
        for (size_t j = 0; j < k; j++) { if (len < cap) out[len] = round[j]; len++; }
        for (size_t j = 0; j < k; j++) if (--cnt[round[j]]) th_push(h, &hn, cnt, round[j]);
        if (hn) { uint64_t pad = width - k; while (pad && len < cap) { out[len++] = IDLE; pad--; } len += pad; }
    }
    return len;
}
static uint64_t sched_min_time(const uint8_t *task, size_t n, uint32_t cool) {
    uint32_t cnt[MAX_TYPES] = {0}, f = 0, m = 0;
    for (size_t i = 0; i < n; i++) { if (task[i] >= MAX_TYPES) return 0; cnt[task[i]]++; }
    if (n == 0) return 0;
    for (int t = 0; t < MAX_TYPES; t++) { if (cnt[t] > f) { f = cnt[t]; m = 1; } else if (cnt[t] == f) m++; }
    uint64_t frame = (uint64_t)(f - 1) * ((uint64_t)cool + 1) + m;
    return frame > n ? frame : (uint64_t)n;
}

static uint32_t g_rng = 0x2545F491u;
static uint32_t xorshift32(void) { uint32_t x = g_rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return g_rng = x; }
static double now_ns(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec; }
static int cmp_d(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return (x > y) - (x < y); }
static double median5(double *v) {   /* 5개 삽입 정렬 */
    for (int i = 1; i < 5; i++) for (int j = i; j > 0 && cmp_d(&v[j - 1], &v[j]) > 0; j--) { double t = v[j]; v[j] = v[j - 1]; v[j - 1] = t; }
    return v[2];
}

static job_t g_jobs[MAX_JOBS];
static int32_t g_slots[MAX_JOBS + 1];
static uint8_t g_task[1u << 20], g_out[1u << 22];
static volatile uint64_t g_sink;

typedef uint64_t (*solver_fn)(const job_t *, size_t, int32_t *);
static double time_solver(solver_fn f, size_t n, uint64_t *result) {
    double v[5];
    *result = f(g_jobs, n, g_slots);                     /* 워밍업 */
    for (int r = 0; r < 5; r++) { double t0 = now_ns(); uint64_t x = f(g_jobs, n, g_slots); v[r] = now_ns() - t0; g_sink += x; }
    return median5(v) / (double)n;
}

int main(void) {
    const char *dist[3] = {"uniform", "all-n", "tight"};
    printf("job sequencing, ns per job (median of 5), Apple Silicon, -O2\n");
    printf("%-8s %8s | %10s %10s %10s | %14s | %s\n", "dist", "n", "naive", "dsu", "heap", "naive steps", "profit equal");
    for (int dd = 0; dd < 3; dd++)
    for (size_t n = 1024; n <= MAX_JOBS; n *= 4) {
        for (size_t i = 0; i < n; i++) {
            g_jobs[i].profit = xorshift32() % 1000000u;
            g_jobs[i].deadline = dd == 0 ? 1 + xorshift32() % (uint32_t)n : dd == 1 ? (uint32_t)n : 1 + xorshift32() % (uint32_t)(n / 16);
        }
        uint64_t rn = 0, rd, rh;
        double tn = -1;
        bool run_naive = !(dd == 1 && n > 65536);            /* all-n 최악은 n^2/2 단계라 너무 길다 */
        g_naive_steps = 0;
        if (run_naive) tn = time_solver(jobseq_naive, n, &rn);
        uint64_t steps = g_naive_steps / 6;                  /* 워밍업 + 5회 */
        double td = time_solver(jobseq_dsu, n, &rd), th = time_solver(jobseq_heap, n, &rh);
        char tns[16];
        if (run_naive) snprintf(tns, sizeof tns, "%.1f", tn); else snprintf(tns, sizeof tns, "(skip)");
        printf("%-8s %8zu | %10s %10.1f %10.1f | %14llu | %s\n", dist[dd], n, tns, td, th,
               (unsigned long long)steps, (!run_naive || rn == rd) && rd == rh ? "yes" : "NO");
    }

    printf("\ncooldown scheduler, n = 2^20 tasks, ns per task (median of 5)\n");
    printf("%-26s %6s | %10s %12s | %10s %10s\n", "distribution", "cool", "slots", "idle", "build", "formula");
    const size_t n = 1u << 20;
    for (int dd = 0; dd < 2; dd++)
    for (uint32_t cool = 0; cool <= 100; cool = cool ? cool * 10 : 1) {
        g_rng = 0x2545F491u;
        for (size_t i = 0; i < n; i++) g_task[i] = (uint8_t)(dd == 0 ? xorshift32() % 26 : (xorshift32() % 3 == 0 ? 0 : xorshift32() % 26));
        double vb[5], vf[5]; uint64_t L = 0, F = 0;
        sched_build(g_task, n, cool, g_out, sizeof g_out);
        for (int r = 0; r < 5; r++) {
            double t0 = now_ns(); L = sched_build(g_task, n, cool, g_out, sizeof g_out); vb[r] = now_ns() - t0;
            t0 = now_ns(); F = sched_min_time(g_task, n, cool); vf[r] = now_ns() - t0;
            g_sink += L + F;
        }
        printf("%-26s %6u | %10llu %12llu | %10.2f %10.2f%s\n", dd == 0 ? "uniform 26 types" : "type A ~36%, rest uniform", cool,
               (unsigned long long)L, (unsigned long long)(L - n), median5(vb) / (double)n, median5(vf) / (double)n, L == F ? "" : "  MISMATCH");
    }
    printf("(sink %llu)\n", (unsigned long long)g_sink);
    return 0;
}
