/* ARCH-B10 추가 연습 — 한 번에 검증
 *
 *   연습 1  목표 AMAT 를 맞추려면 L1 히트율이 얼마여야 하나 (역산)
 *   연습 2  큰 L1(느림) vs 작은 L1(빠름): 손익분기 히트율
 *   연습 3  히트 "시간" 1 cycle 단축의 민감도 = reach_k
 *   연습 4  PMU 카운터 (access / refill) → 로컬 히트율 → AMAT
 *   연습 5  dirty victim write-back 이 DRAM 비용을 늘린다
 *   연습 6  메모리 수준 병렬성(MLP): 독립 미스는 겹친다, 포인터 체이싱은 못 겹친다
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr && ./pr
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>

#define AMAT_MAX_LEVELS 8

typedef struct {
    const char *name;
    double hit_time;
    double hit_rate;  /* LOCAL */
} level_t;

/* 본문 Part A 그대로 */
static double amat(const level_t *L, int n) {
    double acc = L[n - 1].hit_time;
    for (int k = n - 2; k >= 0; k--)
        acc = L[k].hit_time + (1.0 - L[k].hit_rate) * acc;
    return acc;
}
static double reach_frac(const level_t *L, int k) {
    double r = 1.0;
    for (int j = 0; j < k; j++) r *= 1.0 - L[j].hit_rate;
    return r;
}

/* 연습 1: target = t0 + (1-h0) * below  →  h0 = 1 - (target - t0) / below.
 * target < t0 이면 불가능(-1). 계산한 h0 < 0 이면 이미 충분(0). */
static double required_l1_hit(const level_t *L, int n, double target) {
    if (target < L[0].hit_time) return -1.0;
    double below = amat(L + 1, n - 1);
    if (below == 0.0) return 0.0;
    double h = 1.0 - (target - L[0].hit_time) / below;
    return h < 0.0 ? 0.0 : h;
}

/* 연습 2: 새 L1 (히트 시간 t_new) 이 기존 AMAT 와 같아지는 히트율 */
static double breakeven_hit(double t_new, double below, double amat_old) {
    return 1.0 - (amat_old - t_new) / below;
}

/* 연습 3: 계층 k 의 히트 시간을 dt 만큼 바꿀 때 ΔAMAT (유한 차분) */
static double sens_hit_time(const level_t *L, int n, int k, double dt) {
    level_t tmp[AMAT_MAX_LEVELS];
    for (int j = 0; j < n; j++) tmp[j] = L[j];
    tmp[k].hit_time += dt;
    return amat(tmp, n) - amat(L, n);
}

/* 연습 4: accesses = L1 접근 수, refills[k] = 계층 k 의 refill(미스) 수.
 * 계층 k+1 의 접근 수 = 계층 k 의 refill 수 (프리페치/write-back 트래픽은 무시한 근사). */
static void levels_from_counters(uint64_t accesses, const uint64_t *refills, int nc,
                                 const double *times, level_t *out) {
    uint64_t prev = accesses;
    for (int k = 0; k < nc; k++) {
        out[k].name = "cache";
        out[k].hit_time = times[k];
        out[k].hit_rate = prev == 0 ? 1.0 : 1.0 - (double)refills[k] / (double)prev;
        prev = refills[k];
    }
    out[nc].name = "DRAM";
    out[nc].hit_time = times[nc];
    out[nc].hit_rate = 1.0;
}

/* 연습 5: DRAM 까지 간 미스의 p_dirty 비율은 먼저 dirty victim 을 DRAM 에 써야 한다
 * (write buffer 없음 가정) → DRAM 비용 = t_mem * (1 + p_dirty) */
static double amat_with_dirty(const level_t *L, int n, double p_dirty) {
    level_t tmp[AMAT_MAX_LEVELS];
    for (int j = 0; j < n; j++) tmp[j] = L[j];
    tmp[n - 1].hit_time *= 1.0 + p_dirty;
    return amat(tmp, n);
}

/* 연습 6: 독립된 DRAM 미스 mlp 개가 겹치면 미스당 체감 지연 = t_mem / mlp.
 * 포인터 체이싱은 다음 주소가 이번 load 결과라 mlp = 1 (MEM-B10 이 이걸로 지연을 잰다). */
static double amat_with_mlp(const level_t *L, int n, double mlp) {
    level_t tmp[AMAT_MAX_LEVELS];
    for (int j = 0; j < n; j++) tmp[j] = L[j];
    tmp[n - 1].hit_time /= mlp;
    return amat(tmp, n);
}

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; printf("PASS  %s\n", #expr); } \
                         else printf("FAIL  %s  (line %d)\n", #expr, __LINE__); } while (0)
#define NEAR(a, b) (fabs((a) - (b)) < 1e-9)

static const level_t HA[] = {
    {"L1", 4, 0.90}, {"L2", 12, 0.70}, {"L3", 40, 0.60}, {"DRAM", 200, 1.0},
};

int main(void) {
    const int n = 4;

    /* 연습 1 */
    CHECK(NEAR(required_l1_hit(HA, n, 6.4), 0.95));     /* 1 - 2.4/48 */
    CHECK(required_l1_hit(HA, n, 3.0) == -1.0);          /* L1 히트 시간보다 작게는 불가능 */
    CHECK(required_l1_hit(HA, n, 100.0) == 0.0);         /* 이미 충족 */

    /* 연습 2: L1 4cyc/90% vs 5cyc/93% */
    const level_t big[] = {{"L1", 5, 0.93}, {"L2", 12, 0.70}, {"L3", 40, 0.60}, {"DRAM", 200, 1.0}};
    CHECK(NEAR(amat(big, n), 8.36));                     /* 큰 L1 이 이긴다 */
    double be = breakeven_hit(5.0, 48.0, 8.8);
    CHECK(NEAR(be, 1.0 - 3.8 / 48.0));                   /* 0.92083… */
    const level_t big92[] = {{"L1", 5, 0.92}, {"L2", 12, 0.70}, {"L3", 40, 0.60}, {"DRAM", 200, 1.0}};
    CHECK(amat(big92, n) > amat(HA, n));                 /* 92% 면 진다 (8.84 > 8.8) */

    /* 연습 3: -1 cycle 의 효과 = -reach_k → L1 이 항상 최대 */
    for (int k = 0; k < n; k++)
        CHECK(NEAR(sens_hit_time(HA, n, k, -1.0), -reach_frac(HA, k)));
    CHECK(NEAR(sens_hit_time(HA, n, 3, -1.0), -0.012));

    /* 연습 4 */
    const uint64_t refills[3] = {100000, 30000, 12000};
    const double times[4] = {4, 12, 40, 200};
    level_t got[4];
    levels_from_counters(1000000, refills, 3, times, got);
    CHECK(NEAR(got[0].hit_rate, 0.90) && NEAR(got[1].hit_rate, 0.70) && NEAR(got[2].hit_rate, 0.60));
    CHECK(NEAR(amat(got, 4), 8.8));

    /* 연습 5 */
    CHECK(NEAR(amat_with_dirty(HA, n, 0.0), 8.8));
    CHECK(NEAR(amat_with_dirty(HA, n, 0.25), 9.4));      /* DRAM 250: 4 + .1*(12 + .3*(40 + .4*250)) */

    /* 연습 6 */
    CHECK(NEAR(amat_with_mlp(HA, n, 1.0), 8.8));         /* 포인터 체이싱 */
    CHECK(NEAR(amat_with_mlp(HA, n, 4.0), 7.0));         /* 독립 미스 4개 겹침: DRAM 50 */

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
