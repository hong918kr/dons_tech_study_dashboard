/* ARCH-A11 추가 연습 — 한 번에 검증
 *   연습 1  Gustafson 확장 속도 향상과 Amdahl 의 관계 (같은 기계, 다른 질문)
 *   연습 2  동기화 오버헤드가 있는 Amdahl: 최적 스레드 수, Karp–Flatt 가 p 와 함께 커지는 것
 *   연습 3  계층 루프라인 (L1 / L2 / DRAM 지붕) — 어느 지붕이 묶나
 *   연습 4  엣지 NPU (대표값 4 TOPS INT8, LPDDR 8.5 GB/s): 3×3 conv 층 vs FC 층 (배치 1 / 32)
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr && ./pr
 */
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; printf("PASS  %s\n", #expr); } \
                         else printf("FAIL  %s  (line %d)\n", #expr, __LINE__); } while (0)
static bool near(double a, double b, double tol) { return fabs(a - b) <= tol * (fabs(b) > 1 ? fabs(b) : 1); }

static double amdahl_speedup(double f, int p) { return p < 1 ? 0.0 : 1.0 / (f + (1.0 - f) / p); }
static double karp_flatt(double S, int p) { return (p <= 1 || !(S > 0)) ? NAN : (1.0 / S - 1.0 / p) / (1.0 - 1.0 / p); }

/* ---- 연습 1: Gustafson ----
 * s = p 코어 실행 시간 중 직렬 부분의 비율 (병렬 실행에서 잰 값). 문제 크기를 p 에 맞춰 키운다면
 * 같은 일을 1 코어로 하면 s + (1-s)·p → 확장 속도 향상 = s + (1-s)·p = p − s·(p−1). */
static double gustafson_speedup(double s, int p) { return p - s * (p - 1); }
/* 같은 확장 문제를 Amdahl 로 보면: 1 코어 시간 기준 직렬 비율 f = s / (s + (1-s)·p) */
static double amdahl_f_of_scaled(double s, int p) { return s / (s + (1.0 - s) * p); }

/* ---- 연습 2: 오버헤드 c 가 스레드마다 붙는 모델 T(p) = f + (1-f)/p + c·(p-1) (T1 = 1) ---- */
static double speedup_ovh(double f, double c, int p) { return 1.0 / (f + (1.0 - f) / p + c * (p - 1)); }
static int best_p(double f, double c, int pmax) {
    int bp = 1;
    for (int p = 2; p <= pmax; p++)
        if (speedup_ovh(f, c, p) > speedup_ovh(f, c, bp)) bp = p;
    return bp;
}

/* ---- 연습 3: 계층 루프라인 ----
 * 레벨 i 마다 그 레벨에서 오가는 바이트로 잰 AI_i, 대역폭 BW_i. attainable = min(peak, min_i AI_i·BW_i).
 * 반환값의 *which = 묶는 지붕 (-1 = 연산 지붕). */
static double roof_hier(double peak, int nl, const double *ai, const double *bw, int *which) {
    double best = peak;
    *which = -1;
    for (int i = 0; i < nl; i++)
        if (ai[i] * bw[i] < best) { best = ai[i] * bw[i]; *which = i; }
    return best;
}

/* ---- 연습 4: NPU 층 ---- */
typedef struct { double ops, bytes; } cost_t;
/* 3×3 conv, same padding, stride 1, INT8 (1 B/원소), 가중치는 한 번 읽음. ops = 2·MAC */
static cost_t conv3x3_int8(int H, int W, int Cin, int Cout) {
    double mac = (double)H * W * Cin * Cout * 9;
    double bytes = (double)H * W * Cin + (double)H * W * Cout + 9.0 * Cin * Cout;
    return (cost_t){ 2 * mac, bytes };
}
/* FC (행렬-벡터 / 작은 배치): out[b][o] = Σ_i W[o][i]·x[b][i]. 가중치 Cin·Cout B 를 배치 B 가 공유 */
static cost_t fc_int8(int Cin, int Cout, int B) {
    double mac = (double)Cin * Cout * B;
    double bytes = (double)Cin * Cout + (double)B * Cin + (double)B * Cout;
    return (cost_t){ 2 * mac, bytes };
}
static double t_lower_bound(cost_t c, double peak_ops, double bw) {   /* 초: max(연산 시간, 메모리 시간) */
    double tc = c.ops / peak_ops, tm = c.bytes / bw;
    return tc > tm ? tc : tm;
}

int main(void) {
    printf("-- 연습 1: Gustafson vs Amdahl --\n");
    printf("      s=f=0.05, p=12: Amdahl %.3f  Gustafson %.3f\n", amdahl_speedup(0.05, 12), gustafson_speedup(0.05, 12));
    CHECK(near(gustafson_speedup(0.05, 12), 11.45, 1e-12));
    CHECK(gustafson_speedup(0.0, 12) == 12.0 && gustafson_speedup(1.0, 12) == 1.0);
    {
        bool same = true;                      /* 같은 실행을 두 관점으로: 수치가 정확히 같아야 */
        for (int p = 1; p <= 64; p++)
            for (int a = 0; a <= 10; a++) {
                double s = a / 10.0;
                if (!near(amdahl_speedup(amdahl_f_of_scaled(s, p), p), gustafson_speedup(s, p), 1e-12)) same = false;
            }
        CHECK(same);
    }
    CHECK(amdahl_f_of_scaled(0.05, 12) < 0.005);   /* 문제를 키우면 1 코어 기준 직렬 비율이 1/10 미만으로 */

    printf("-- 연습 2: overhead model, best p, Karp-Flatt growth --\n");
    {
        double f = 0.05, c = 0.002;
        int bp = best_p(f, c, 64);
        printf("      f=%.2f c=%.3f: best p=%d (sqrt((1-f)/c)=%.2f) S=%.3f; S(12)=%.3f S(64)=%.3f\n", f, c, bp,
               sqrt((1 - f) / c), speedup_ovh(f, c, bp), speedup_ovh(f, c, 12), speedup_ovh(f, c, 64));
        CHECK(abs(bp - (int)lround(sqrt((1 - f) / c))) <= 1);
        CHECK(speedup_ovh(f, c, 64) < speedup_ovh(f, c, bp));
        CHECK(best_p(f, 0.0, 64) == 64);
        bool grows = true, flat = true;
        for (int p = 3; p <= 32; p++) {
            if (!(karp_flatt(speedup_ovh(f, c, p), p) > karp_flatt(speedup_ovh(f, c, p - 1), p - 1))) grows = false;
            if (!near(karp_flatt(amdahl_speedup(f, p), p), f, 1e-9)) flat = false;
        }
        CHECK(grows);                          /* 오버헤드 → e 가 p 와 함께 증가 */
        CHECK(flat);                           /* 순수 Amdahl → e 는 상수 f */
        printf("      e(KF) with overhead: p=2 %.4f  p=12 %.4f  p=32 %.4f\n", karp_flatt(speedup_ovh(f, c, 2), 2),
               karp_flatt(speedup_ovh(f, c, 12), 12), karp_flatt(speedup_ovh(f, c, 32), 32));
    }

    printf("-- 연습 3: hierarchical roofline --\n");
    {
        /* 지붕 숫자는 연습용 대표값 (본문 실측의 크기 순서와 비슷하게): peak 100, L1 400, L2 150, DRAM 60 GB/s */
        double bw[3] = { 400, 150, 60 };
        int w;
        double ai_tiled[3] = { 0.2, 4.0, 170.0 };       /* 타일 matmul: L1 에서는 FMA 당 load 2+store 1 → 낮다 */
        double r1 = roof_hier(100, 3, ai_tiled, bw, &w);
        printf("      tiled-like: attainable %.1f, binding level %d\n", r1, w);
        CHECK(near(r1, 80.0, 1e-12) && w == 0);          /* DRAM AI 170 인데도 L1 지붕 (0.2 × 400 = 80) 이 묶는다 */
        double ai_vadd[3] = { 1.0 / 12, 1.0 / 12, 1.0 / 12 };
        double r2 = roof_hier(100, 3, ai_vadd, bw, &w);
        CHECK(near(r2, 5.0, 1e-12) && w == 2);           /* 스트리밍: DRAM 지붕 */
        double ai_l2res[3] = { 1.0 / 12, 1.0 / 12, 1e9 }; /* 작업 집합이 L2 안 → DRAM 트래픽 ≈ 0 */
        double r3 = roof_hier(100, 3, ai_l2res, bw, &w);
        CHECK(near(r3, 12.5, 1e-12) && w == 1);
        double ai_reg[3] = { 8.0, 40.0, 200.0 };          /* 레지스터 블로킹 */
        CHECK(roof_hier(100, 3, ai_reg, bw, &w) == 100.0 && w == -1);
    }

    printf("-- 연습 4: edge NPU, 4 TOPS INT8 / 8.5 GB/s (representative) --\n");
    {
        const double peak = 4e12, bw = 8.5e9, ridge = peak / bw;
        cost_t cv = conv3x3_int8(56, 56, 64, 64), f1 = fc_int8(4096, 4096, 1), f32 = fc_int8(4096, 4096, 32);
        printf("      ridge %.1f op/B | conv3x3 56x56x64->64: %.1f Mop, %.0f B, AI %.1f, t_lb %.1f us\n", ridge, cv.ops / 1e6,
               cv.bytes, cv.ops / cv.bytes, 1e6 * t_lower_bound(cv, peak, bw));
        printf("      FC 4096x4096 B=1: AI %.2f, t_lb %.0f us (compute alone %.1f us) | B=32: AI %.1f, t_lb %.0f us\n",
               f1.ops / f1.bytes, 1e6 * t_lower_bound(f1, peak, bw), 1e6 * f1.ops / peak, f32.ops / f32.bytes,
               1e6 * t_lower_bound(f32, peak, bw));
        CHECK(near(ridge, 470.588, 1e-5));
        CHECK(cv.ops == 2.0 * 56 * 56 * 64 * 64 * 9 && cv.bytes == 438272.0);
        CHECK(cv.ops / cv.bytes > ridge);                 /* conv: 연산 바운드 (간신히) */
        CHECK(f1.ops / f1.bytes < 2.0 && f1.ops / f1.bytes > 1.99);
        CHECK(f32.ops / f32.bytes < ridge);               /* 배치 32 도 여전히 메모리 바운드 */
        CHECK(t_lower_bound(f1, peak, bw) > 200 * f1.ops / peak);   /* 배치 1 FC 는 연산 시간의 200배 넘게 메모리에 묶인다 */
        CHECK(near(t_lower_bound(f32, peak, bw) / t_lower_bound(f1, peak, bw), (16777216.0 + 32 * 8192) / (16777216.0 + 8192), 1e-12));
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
