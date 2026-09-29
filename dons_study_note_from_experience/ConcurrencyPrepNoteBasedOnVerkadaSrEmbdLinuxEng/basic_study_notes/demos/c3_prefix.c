/* c3_prefix.c — C3 노트의 코드 조각 검증
 * cc -std=c11 -O2 -Wall -Wextra -o c3 c3_prefix.c -lm && ./c3
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>

/* ---------- 1. 가장 단순한 누적합 ---------- */
static void build_prefix(const int *a, unsigned n, long long *P)
{
    P[0] = 0;                                  /* P[0] 은 "아무것도 더하지 않음" */
    for (unsigned i = 0; i < n; i++)
        P[i + 1] = P[i] + a[i];                /* P[i+1] = a[0..i] 의 합 */
}
static long long range_sum(const long long *P, unsigned i, unsigned j)
{ return P[j + 1] - P[i]; }                    /* a[i..j] 포함-포함 */
static long long bf_sum(const int *a, unsigned i, unsigned j)
{
    long long s = 0;
    for (unsigned k = i; k <= j; k++) s += a[k];
    return s;
}

/* ---------- 2. 링버퍼 + 절대 누적값 ---------- */
#define CAP  8u
#define MASK (CAP - 1u)
typedef struct { uint64_t ts; double seg; double cum; } entry_t;  /* seg: 직전 칸에서 여기까지 */
typedef struct { entry_t e[CAP]; uint32_t head, tail; } ring_t;

static entry_t *R(ring_t *r, uint32_t off) { return &r->e[(r->tail + off) & MASK]; }
static uint32_t Rn(const ring_t *r) { return r->head - r->tail; }

/* 새 점을 넣는다. cum 은 "맨 처음부터 여기까지"의 절대값이라 eviction 과 무관하다. */
static void ring_append(ring_t *r, uint64_t ts, double step)
{
    double cum = 0.0, seg = 0.0;
    if (Rn(r) > 0) { seg = step; cum = R(r, Rn(r) - 1u)->cum + step; }
    if (Rn(r) == CAP) r->tail++;               /* 가장 오래된 것을 버린다 */
    entry_t *e = &r->e[r->head & MASK];
    e->ts = ts; e->seg = seg; e->cum = cum; r->head++;
}

/* off 구간 [k0, k1] 의 합 = cum[k1] - cum[k0].  뺄셈 한 번. */
static double ring_range(ring_t *r, uint32_t k0, uint32_t k1)
{ return R(r, k1)->cum - R(r, k0)->cum; }
static double bf_ring_range(ring_t *r, uint32_t k0, uint32_t k1)
{
    double s = 0.0;
    for (uint32_t k = k0 + 1u; k <= k1; k++) s += R(r, k)->seg;
    return s;
}

/* ---------- 3. 사다리꼴 적분 + 양 끝 조각 (08_battery_energy_pipeline 과 같은 모양) ---------- */
#define NMAX 64
static uint64_t H_ts[NMAX];
static double   H_p[NMAX];
static double   H_cum[NMAX];
static unsigned H_n;

static void hist_push(uint64_t ts, double p)
{
    double cum = 0.0;
    if (H_n > 0) {
        double dt_s = (double)(ts - H_ts[H_n - 1]) / 1e6;         /* 초 단위! */
        cum = H_cum[H_n - 1] + 0.5 * (H_p[H_n - 1] + p) * dt_s;   /* 사다리꼴 */
    }
    H_ts[H_n] = ts; H_p[H_n] = p; H_cum[H_n] = cum; H_n++;
}

/* H_ts[i] <= t 인 가장 큰 i (t >= H_ts[0] 가정) */
static unsigned floor_index(uint64_t t)
{
    unsigned lo = 0, hi = H_n - 1u;
    while (lo < hi) {
        unsigned mid = lo + (hi - lo + 1u) / 2u;   /* 위로 치우친 중간 */
        if (H_ts[mid] <= t) lo = mid; else hi = mid - 1u;
    }
    return lo;
}

/* 구간 [i, i+1] 안의 시각 t 에서의 전력 — 선형 보간 */
static double p_at(unsigned i, uint64_t t)     /* 구간 [i, i+1] 안의 t 에서의 전력 */
{
    if (t <= H_ts[i])     return H_p[i];
    if (t >= H_ts[i + 1]) return H_p[i + 1];
    return H_p[i] + (H_p[i+1] - H_p[i]) * ((double)(t - H_ts[i]) / (double)(H_ts[i+1] - H_ts[i]));
}

static double energy_between(uint64_t t0, uint64_t t1)
{
    if (t1 <= t0 || H_n < 2) return 0.0;
    uint64_t first = H_ts[0], last = H_ts[H_n - 1];
    if (t0 < first) t0 = first;                 /* 측정한 구간으로 clamp */
    if (t1 > last)  t1 = last;
    if (t1 <= t0) return 0.0;

    unsigned i = floor_index(t0), j = floor_index(t1);
    if (i == j)                                  /* 양 끝이 한 구간 안 */
        return 0.5 * (p_at(i, t0) + p_at(i, t1)) * ((double)(t1 - t0) / 1e6);

    double head = 0.5 * (p_at(i,t0) + H_p[i+1]) * ((double)(H_ts[i+1] - t0) / 1e6);  /* 앞 조각 */
    double mid  = H_cum[j] - H_cum[i + 1];                    /* 통째 구간들: 뺄셈 한 번 */
    double tail = 0.0;                                        /* 뒤 조각 */
    if (t1 > H_ts[j] && j + 1u < H_n)
        tail = 0.5 * (H_p[j] + p_at(j, t1)) * ((double)(t1 - H_ts[j]) / 1e6);
    return head + mid + tail;
}

/* brute force: 각 구간을 [t0,t1] 로 잘라 하나씩 적분한다 (O(n), 독립 구현) */
static double bf_energy(uint64_t t0, uint64_t t1)
{
    if (t1 <= t0 || H_n < 2) return 0.0;
    double e = 0.0;
    for (unsigned k = 0; k + 1u < H_n; k++) {
        uint64_t s = H_ts[k] > t0 ? H_ts[k] : t0;          /* 구간을 질의로 자른다 */
        uint64_t f = H_ts[k+1] < t1 ? H_ts[k+1] : t1;
        if (f > s) e += 0.5 * (p_at(k, s) + p_at(k, f)) * ((double)(f - s) / 1e6);
    }
    return e;
}

/* ---------- 4. 부동소수점 누적 오차 ---------- */
static double kahan_sum_f(const float *a, unsigned n)
{
    float s = 0.0f, c = 0.0f;                 /* c = 지금까지 잃어버린 몫 */
    for (unsigned i = 0; i < n; i++) {
        float y = a[i] - c;                   /* 잃어버린 몫을 먼저 되돌린다 */
        float t = s + y;
        c = (t - s) - y;                      /* 이번 덧셈에서 잘려나간 양 */
        s = t;
    }
    return (double)s;
}

int main(void)
{
    puts("== 1. 누적합의 기본 ==");
    const int a[] = {3, 1, 4, 1, 5, 9, 2, 6};
    long long P[9];
    build_prefix(a, 8, P);
    printf("a  =");  for (unsigned i = 0; i < 8; i++) printf(" %2d", a[i]);       puts("");
    printf("P  =");  for (unsigned i = 0; i < 9; i++) printf(" %2lld", P[i]);     puts("");
    unsigned fails = 0;
    for (unsigned i = 0; i < 8; i++)
        for (unsigned j = i; j < 8; j++)
            if (range_sum(P, i, j) != bf_sum(a, i, j)) fails++;
    printf("sum(2..5) = P[6] - P[2] = %lld - %lld = %lld  (brute force %lld)\n",
           P[6], P[2], range_sum(P, 2, 5), bf_sum(a, 2, 5));
    printf("모든 (i,j) 36쌍 검사: 불일치 %u -> %s\n", fails, fails ? "FAIL" : "PASS");

    puts("\n== 2. 링버퍼에서 절대 누적값 (CAP=8, 12개 넣어 4개를 버린다) ==");
    ring_t r = {{{0, 0, 0}}, 0, 0};
    for (int i = 1; i <= 12; i++) ring_append(&r, (uint64_t)i * 100u, (double)i);
    printf("count=%u  (ts, seg, cum):\n", Rn(&r));
    for (uint32_t k = 0; k < Rn(&r); k++)
        printf("  off=%u  ts=%4llu  seg=%5.1f  cum=%6.1f\n", k,
               (unsigned long long)R(&r, k)->ts, R(&r, k)->seg, R(&r, k)->cum);
    printf("range(off 1..5) = %.1f - %.1f = %.1f   (brute force %.1f)\n",
           R(&r, 5)->cum, R(&r, 1)->cum, ring_range(&r, 1, 5), bf_ring_range(&r, 1, 5));
    printf("-> cum 은 버려진 4개의 몫을 그대로 품고 있다. 차이만 읽으니 상관없다.\n");

    puts("\n== 3. 불규칙 간격 사다리꼴 적분 ==");
    /* 간격이 제각각인 전력 샘플 (us, W) */
    hist_push(1000000u, 100.0);
    hist_push(1250000u, 200.0);
    hist_push(1300000u, 200.0);
    hist_push(2000000u,  50.0);
    hist_push(2400000u,  50.0);
    printf("샘플: ");
    for (unsigned i = 0; i < H_n; i++)
        printf("(t=%.2fs, p=%.0fW) ", (double)H_ts[i] / 1e6, H_p[i]);
    puts("");
    printf("cum :");
    for (unsigned i = 0; i < H_n; i++) printf(" %8.1f", H_cum[i]);
    puts("  [J]");
    struct { uint64_t t0, t1; const char *what; } q[] = {
        {1000000u, 2400000u, "전체"},
        {1100000u, 1200000u, "한 구간 안 (양쪽 보간)"},
        {1125000u, 2000000u, "앞 조각 + 통째 + 정확히 일치"},
        {1300000u, 2200000u, "샘플에서 시작, 구간 중간에서 끝"},
        { 500000u, 3000000u, "양쪽 다 창 밖 -> clamp"},
        {2400000u, 2400000u, "폭 0"},
        {2000000u, 1000000u, "거꾸로"},
    };
    double worst = 0.0;
    for (unsigned k = 0; k < sizeof q / sizeof q[0]; k++) {
        double e = energy_between(q[k].t0, q[k].t1), b = bf_energy(q[k].t0, q[k].t1);
        double d = fabs(e - b);
        if (d > worst) worst = d;
        printf("  [%.3f, %.3f] = %9.3f J   brute %9.3f J   차이 %.2e   %s\n",
               (double)q[k].t0 / 1e6, (double)q[k].t1 / 1e6, e, b, d, q[k].what);
    }
    printf("최대 차이 %.2e J -> %s\n", worst, worst < 1e-9 ? "PASS" : "FAIL");

    puts("\n== 3b. 가산성 E(a,m) + E(m,b) == E(a,b) ==");
    unsigned add_fail = 0;
    for (uint64_t m = 1000000u; m <= 2400000u; m += 70000u) {
        double l = energy_between(1000000u, m), rr = energy_between(m, 2400000u);
        double whole = energy_between(1000000u, 2400000u);
        if (fabs(l + rr - whole) > 1e-9) add_fail++;
    }
    printf("21개 분할점 검사: 불일치 %u -> %s\n", add_fail, add_fail ? "FAIL" : "PASS");

    puts("\n== 4. float 로 누적하면 안 되는 이유 ==");
    /* 300만 번, 한 번에 13.7 J 씩 -> 4.1e7 J. 마지막 10 스텝만 물어본다. */
    const unsigned N = 3000000u;
    const double step = 13.7;
    float  cf = 0.0f, cf_mark = 0.0f;
    double cd = 0.0,  cd_mark = 0.0;
    for (unsigned i = 0; i < N; i++) {
        if (i == N - 10u) { cf_mark = cf; cd_mark = cd; }
        cf += (float)step;
        cd += step;
    }
    printf("정답(창 10스텝)        = %.6f J\n", 10.0 * step);
    printf("double cum 차이        = %.6f J   (오차 %.2e)\n",
           cd - cd_mark, fabs((cd - cd_mark) - 10.0 * step));
    printf("float  cum 차이        = %.6f J   (오차 %.2e)\n",
           (double)(cf - cf_mark), fabs((double)(cf - cf_mark) - 10.0 * step));
    printf("float  cum 총합        = %.1f  (double %.1f, 차이 %.1f)\n",
           (double)cf, cd, fabs((double)cf - cd));

    puts("\n== 4b. Kahan 합 ==");
    static float ones[1000000];
    for (unsigned i = 0; i < 1000000u; i++) ones[i] = 0.1f;
    float naive = 0.0f;
    for (unsigned i = 0; i < 1000000u; i++) naive += ones[i];
    double dsum = 0.0;
    for (unsigned i = 0; i < 1000000u; i++) dsum += (double)ones[i];
    printf("0.1f 를 100만 번:  naive float = %.4f   Kahan float = %.4f   double = %.4f\n",
           (double)naive, kahan_sum_f(ones, 1000000u), dsum);
    printf("-> 참값은 100000.0 (0.1f 자체의 표현 오차까지 포함하면 %.4f)\n", dsum);

    puts("\n== 4c. 정수(밀리 단위)로 다루기 ==");
    long long mj = 0;                            /* milli-joule */
    for (unsigned i = 0; i < N; i++) mj += 13700;   /* 13.7 J = 13700 mJ, 정확 */
    printf("정수 누적 = %lld mJ = %.3f J  (오차 0, 단 단위를 넘칠 때만 주의)\n",
           mj, (double)mj / 1000.0);
    return 0;
}
