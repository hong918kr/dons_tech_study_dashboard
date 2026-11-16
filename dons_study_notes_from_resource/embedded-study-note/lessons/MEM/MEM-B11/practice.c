/* MEM-B11 추가 연습 — 한 번에 검증
 *
 * 연습 1  write-allocate 가설 판별: 버스가 포화일 때 커널 간 "보고 대역폭 비율" 예측 (WA 있음/없음) → 측정 비율에 가까운 쪽
 * 연습 2  실제 버스 트래픽으로 환산: reported × actual/STREAM, 그리고 피크 대비 효율 두 가지
 * 연습 3  Little 의 법칙: 대역폭 × 지연 / 라인 = 날아다니는 라인 수, 코어당 한계로 필요한 스레드 수
 * 연습 4  산술 강도(flop/byte)와 roofline 상한 미리보기 (MEM-A08, AIPERF-B04)
 * 연습 5  경계 캐시라인 공유(false sharing) 개수: 정렬 안 한 분할 vs 128 B 정렬 분할
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c && ./a.out
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <math.h>

enum { K_COPY, K_SCALE, K_ADD, K_TRIAD, K_SUM, K_FILL, K_N };
static const char *KNAME[K_N] = { "Copy", "Scale", "Add", "Triad", "Sum", "Fill" };
/* 원소당 읽기 / 쓰기 / flop (STREAM 4 커널 + 읽기 전용 Sum, 쓰기 전용 Fill) */
static const unsigned RD[K_N] = { 1, 1, 2, 2, 1, 0 }, WR[K_N] = { 1, 1, 1, 1, 0, 1 }, FL[K_N] = { 0, 1, 1, 2, 1, 0 };

typedef struct { uint64_t num, den; } frac_t;
static uint64_t gcd64(uint64_t a, uint64_t b) { while (b) { uint64_t t = a % b; a = b; b = t; } return a; }
static frac_t mkfrac(uint64_t n, uint64_t d) { uint64_t g = gcd64(n, d); return (frac_t){ n / g, d / g }; }

/* 원소당 바이트: STREAM 규약 (RFO 없음) / write-allocate 포함 */
static unsigned bytes_stream(int k) { return 8u * (RD[k] + WR[k]); }
static unsigned bytes_actual(int k, bool wa) { return 8u * (RD[k] + WR[k] + (wa ? WR[k] : 0)); }

/* ---- 연습 1 ---- 버스가 같은 총 트래픽 X 로 포화라면 보고값 = X · stream/actual.
 * 두 커널의 보고 비율 k1/k2 = (s1/a1) / (s2/a2). */
static frac_t predicted_ratio(int k1, int k2, bool wa) {
    return mkfrac((uint64_t)bytes_stream(k1) * bytes_actual(k2, wa), (uint64_t)bytes_actual(k1, wa) * bytes_stream(k2));
}
/* 측정 비율이 어느 가설에 가까운가: 1 = WA, 0 = no-WA (로그 거리 비교) */
static int wa_verdict(int k1, int k2, double measured_ratio) {
    frac_t w = predicted_ratio(k1, k2, true), o = predicted_ratio(k1, k2, false);
    double dw = fabs(log(measured_ratio / ((double)w.num / w.den)));
    double dn = fabs(log(measured_ratio / ((double)o.num / o.den)));
    return dw < dn ? 1 : 0;
}

/* ---- 연습 2 ---- */
static double bus_gbs(int k, double reported, bool wa) { return reported * bytes_actual(k, wa) / bytes_stream(k); }
static double eff_pct(double g, double peak_gbs) { return peak_gbs > 0 ? 100.0 * g / peak_gbs : 0.0; }

/* ---- 연습 3 ---- GB/s × ns = B (10^9 · 10^-9 이 상쇄) */
static double lines_in_flight(double gbs, double lat_ns, unsigned line) { return gbs * lat_ns / line; }
static unsigned threads_needed(double target_gbs, double lat_ns, unsigned line, double lines_per_core) {
    if (lines_per_core <= 0) return 0;
    return (unsigned)ceil(lines_in_flight(target_gbs, lat_ns, line) / lines_per_core);
}

/* ---- 연습 4 ---- */
static frac_t intensity(int k, bool wa) { return mkfrac(FL[k], bytes_actual(k, wa)); }   /* flop / byte */
static double attainable_gflops(double peak_gflops, double bw_gbs, frac_t ai) {
    double m = bw_gbs * (double)ai.num / (double)ai.den;
    return m < peak_gflops ? m : peak_gflops;
}

/* ---- 연습 5 ---- solution.c 의 분할 (축약판) + 경계 라인 공유 개수 */
static size_t boundary(size_t n, unsigned t, unsigned j, size_t align) {
    if (j == 0) return 0;
    if (j >= t) return n;
    size_t x = (size_t)((uint64_t)n * j / t);
    if (align > 1) x -= x % align;
    return x;
}
/* 쓰기 배열이 라인 정렬(원소 0 = 라인 시작)일 때, 두 스레드 이상이 쓰는 "서로 다른" 라인 수.
 * 내부 경계 b 가 라인 시작이 아니면 원소 b-1 과 b (다른 스레드) 가 같은 라인 b/line 에 있다. */
static unsigned shared_lines(size_t n, unsigned t, size_t align, size_t line_elems) {
    unsigned cnt = 0;
    size_t last = SIZE_MAX;
    for (unsigned j = 1; j < t; j++) {
        size_t b = boundary(n, t, j, align);
        if (b == 0 || b >= n || b % line_elems == 0) continue;
        if (b / line_elems != last) { cnt++; last = b / line_elems; }
    }
    return cnt;
}

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main(void) {
    /* 연습 1 */
    frac_t r = predicted_ratio(K_COPY, K_TRIAD, true);  CHECK(r.num == 8 && r.den == 9);
    r = predicted_ratio(K_COPY, K_TRIAD, false);        CHECK(r.num == 1 && r.den == 1);
    r = predicted_ratio(K_FILL, K_SUM, true);           CHECK(r.num == 1 && r.den == 2);
    r = predicted_ratio(K_FILL, K_SUM, false);          CHECK(r.num == 1 && r.den == 1);
    r = predicted_ratio(K_ADD, K_TRIAD, true);          CHECK(r.num == 1 && r.den == 1);  /* 같은 모양 → 구별 불가 */
    CHECK(wa_verdict(K_FILL, K_SUM, 0.55) == 1 && wa_verdict(K_FILL, K_SUM, 0.95) == 0);
    CHECK(wa_verdict(K_COPY, K_TRIAD, 0.87) == 1 && wa_verdict(K_COPY, K_TRIAD, 1.02) == 0);
    for (int k = 0; k < K_N; k++) {
        frac_t f = mkfrac(bytes_actual(k, true), bytes_stream(k));
        printf("  %-5s stream %2u B, +RFO %2u B  (x%llu/%llu)\n", KNAME[k], bytes_stream(k), bytes_actual(k, true),
               (unsigned long long)f.num, (unsigned long long)f.den);
    }

    /* 연습 2 */
    CHECK(fabs(bus_gbs(K_TRIAD, 120.0, true) - 160.0) < 1e-9);
    CHECK(fabs(bus_gbs(K_COPY, 100.0, true) - 150.0) < 1e-9 && bus_gbs(K_COPY, 100.0, false) == 100.0);
    CHECK(fabs(eff_pct(bus_gbs(K_TRIAD, 120.0, true), 170.0) - 94.117647) < 1e-5);
    CHECK(fabs(eff_pct(120.0, 170.0) - 70.588235) < 1e-5 && eff_pct(1, 0) == 0.0);

    /* 연습 3 */
    CHECK(fabs(lines_in_flight(170.0, 100.0, 128) - 132.8125) < 1e-9);    /* 17,000 B 가 날아다녀야 */
    CHECK(fabs(lines_in_flight(100.0, 100.0, 64) - 156.25) < 1e-9);
    CHECK(threads_needed(170.0, 100.0, 128, 40.0) == 4);                  /* 132.8 / 40 = 3.3 → 4 */
    CHECK(threads_needed(170.0, 100.0, 128, 0) == 0);

    /* 연습 4 */
    frac_t ai = intensity(K_TRIAD, false); CHECK(ai.num == 1 && ai.den == 12);
    ai = intensity(K_TRIAD, true);         CHECK(ai.num == 1 && ai.den == 16);
    ai = intensity(K_COPY, false);         CHECK(ai.num == 0);
    ai = intensity(K_SCALE, false);        CHECK(ai.num == 1 && ai.den == 16);
    CHECK(fabs(attainable_gflops(1000.0, 120.0, intensity(K_TRIAD, false)) - 10.0) < 1e-9);
    CHECK(attainable_gflops(5.0, 120.0, intensity(K_TRIAD, false)) == 5.0);   /* 연산이 천장 */

    /* 연습 5 */
    CHECK(shared_lines(1000, 3, 1, 16) == 2);       /* 333, 666 → 라인 20, 41 을 두 스레드가 같이 씀 */
    CHECK(shared_lines(1000, 3, 16, 16) == 0);
    CHECK(shared_lines(1000, 8, 1, 16) == 7);       /* 125·j 는 16 의 배수가 하나도 없다 */
    CHECK(shared_lines(1024, 8, 1, 16) == 0);       /* 128·j → 전부 라인 경계 */
    CHECK(shared_lines((1u << 20) + 3, 8, 1, 16) == 5 && shared_lines((1u << 20) + 3, 8, 16, 16) == 0);
    CHECK(shared_lines(20, 8, 1, 16) == 2);         /* 경계 2,5,7,10,12,15 → 라인 0 (6 스레드!), 17 → 라인 1 */

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
