/* OS-A04 추가 연습 — 한 번에 검증
 *
 *   연습 1  vruntime u64 래핑: 순진한 `a < b` 비교 vs 부호 있는 차이 (int64_t)(a − b) < 0
 *   연습 2  그룹 스케줄링(cgroup cpu.weight / autogroup): 2 단계 vruntime → 태스크 몫 = 그룹 몫 / 그룹 안 수
 *   연습 3  미니 EEVDF: eligible(v_i ≤ V) + 가상 마감 d = v + slice·1024/w → 가장 이른 eligible 마감.
 *           짧은 slice 태스크의 최대 대기(지연)를 CFS 식 pick 과 비교 — 몫은 그대로
 *   연습 4  avg_vruntime 증분 추적 (6.18 fair.c: zero_vruntime + sum_w_vruntime + sum_weight)
 *           = 정확한 가중 평균의 floor, 정수 eligibility 판정 = 유리수 판정
 *   연습 5  place_entity 의 lag 보존 (PLACE_LAG): 넣기 전 lag 를 (W + w)/W 배로 부풀려야
 *           넣은 뒤 실제 lag 가 저장한 값과 같다. entity_lag 의 clamp.
 *
 * 전부 결정적(고정 시드 xorshift32, 시간 측정 없음). 모든 태스크는 항상 runnable(hog) — 깨어남은
 * solution.c 가 다룬다. 여기 시뮬레이터는 1 ms tick 마다만 결정하는 축소판이다.
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr && ./pr
 */
#define _POSIX_C_SOURCE 200809L
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(e) do { g_total++; if (e) g_pass++; \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #e); } while (0)

/* Linux 6.18.55 kernel/sched/core.c:10331 / :10349 */
static const int W[40] = {
 88761, 71755, 56483, 46273, 36291, 29154, 23254, 18705, 14949, 11916,
  9548,  7620,  6100,  4904,  3906,  3121,  2501,  1991,  1586,  1277,
  1024,   820,   655,   526,   423,   335,   272,   215,   172,   137,
   110,    87,    70,    56,    45,    36,    29,    23,    18,    15,
};
static uint32_t weight_of(int nice) { return (uint32_t)W[nice + 20]; }
/* delta × 1024 / w — 연습에서는 정확한 정수 나눗셈 (solution.c 는 커널식 역가중치) */
static uint64_t vdelta(uint64_t delta_ns, uint32_t w) { return delta_ns * 1024u / w; }

static uint32_t xs = 88172645u;
static uint32_t xorshift32(void) { xs ^= xs << 13; xs ^= xs >> 17; xs ^= xs << 5; return xs; }
static double absd(double x) { return x < 0 ? -x : x; }

/* ======================= 축소 시뮬레이터 (tick = 1 ms 결정) ======================= */
enum { M_CFS, M_CFS_NAIVE, M_EEVDF };
#define MAXE 8
#define TICK_NS 1000000ull

typedef struct { int nice; uint32_t slice_us; } pspec_t;   /* slice_us 0 = 기본 */
typedef struct {
    uint32_t w, slice_us;
    uint64_t v, d, vprot, exec_ns, pick_exec_ns, wait_start, max_wait_ns;
} pent_t;
typedef struct { pent_t e[MAXE]; int n, curr; uint64_t now; uint32_t switches; } psim_t;

static bool before_signed(uint64_t a, uint64_t b) { return (int64_t)(a - b) < 0; }

/* 연습 4 의 avg_vruntime 과 같은 식을 '전수 계산' 으로: eligible ⇔ Σ w_j (v_j − v_i) ≥ 0 */
static bool eligible_bf(const psim_t *s, int i)
{
    __int128 acc = 0;
    for (int j = 0; j < s->n; j++) acc += (__int128)s->e[j].w * (int64_t)(s->e[j].v - s->e[i].v);
    return acc >= 0;
}

static int pick_cfs(const psim_t *s, bool naive)
{
    int b = 0;
    for (int i = 1; i < s->n; i++) {
        bool lt = naive ? s->e[i].v < s->e[b].v : before_signed(s->e[i].v, s->e[b].v);
        if (lt) b = i;
    }
    return b;
}

/* 연습 3 답: 가장 이른 가상 마감을 가진 eligible 엔티티 (fair.c:1025 pick_eevdf 의 의미) */
static int pick_eevdf_bf(const psim_t *s)
{
    int b = -1;
    for (int i = 0; i < s->n; i++) {
        if (!eligible_bf(s, i)) continue;
        if (b < 0 || before_signed(s->e[i].d, s->e[b].d)) b = i;
    }
    return b;   /* 가중 평균 이하인 엔티티는 항상 하나 이상 있다 → −1 이 안 나온다 */
}

static uint32_t min_slice_us(const psim_t *s)
{
    uint32_t m = UINT32_MAX;
    for (int i = 0; i < s->n; i++) if (s->e[i].slice_us < m) m = s->e[i].slice_us;
    return m;
}

static void set_next(psim_t *s, int i, int mode)
{
    if (i != s->curr) {
        s->switches++;
        uint64_t w = s->now - s->e[i].wait_start;
        if (w > s->e[i].max_wait_ns) s->e[i].max_wait_ns = w;
        if (s->curr >= 0) s->e[s->curr].wait_start = s->now;
    }
    s->curr = i;
    pent_t *e = &s->e[i];
    e->pick_exec_ns = e->exec_ns;
    if (mode == M_EEVDF) {   /* RUN_TO_PARITY: 큐의 최소 slice 만큼만 보호 (fair.c:973 set_protect_slice) */
        uint32_t ms = min_slice_us(s);
        uint64_t vp = e->v + vdelta((uint64_t)(ms < e->slice_us ? ms : e->slice_us) * 1000u, e->w);
        e->vprot = before_signed(vp, e->d) ? vp : e->d;
    }
}

/* CFS: period = 6 ms (n ≤ 8), slice_i = period·w_i/Σw. EEVDF: base slice 2.8 ms (= 0.7 ms × 4, 8+ CPU 기본) */
static void run_psim(psim_t *s, const pspec_t *sp, int n, int mode, uint64_t vr0, uint64_t sim_ns)
{
    memset(s, 0, sizeof *s);
    s->n = n; s->curr = -1;
    uint64_t Wsum = 0;
    for (int i = 0; i < n; i++) {
        pent_t *e = &s->e[i];
        e->w = weight_of(sp[i].nice);
        e->slice_us = sp[i].slice_us ? sp[i].slice_us : 2800;
        e->v = vr0;
        e->d = e->v + vdelta((uint64_t)e->slice_us * 1000u, e->w);
        Wsum += e->w;
    }
    set_next(s, mode == M_EEVDF ? pick_eevdf_bf(s) : pick_cfs(s, mode == M_CFS_NAIVE), mode);
    for (s->now = TICK_NS; s->now <= sim_ns; s->now += TICK_NS) {
        pent_t *c = &s->e[s->curr];
        c->exec_ns += TICK_NS;
        c->v += vdelta(TICK_NS, c->w);
        bool resched;
        if (mode == M_EEVDF) {
            resched = false;
            if (!before_signed(c->v, c->d)) {                       /* update_deadline (fair.c:1127) */
                c->d = c->v + vdelta((uint64_t)c->slice_us * 1000u, c->w);
                resched = true;
            }
            if (!before_signed(c->v, c->vprot)) resched = true;      /* !protect_slice */
        } else {
            uint64_t ideal = 6000000ull * c->w / Wsum;
            uint64_t ran = c->exec_ns - c->pick_exec_ns;
            resched = ran > ideal;
            if (!resched && ran >= 750000ull) {
                int l = -1;
                for (int i = 0; i < n; i++)
                    if (i != s->curr && (l < 0 || before_signed(s->e[i].v, s->e[l].v))) l = i;
                resched = l >= 0 && (int64_t)(c->v - s->e[l].v) > (int64_t)ideal;
            }
        }
        if (resched)
            set_next(s, mode == M_EEVDF ? pick_eevdf_bf(s) : pick_cfs(s, mode == M_CFS_NAIVE), mode);
    }
}
static double pshare(const psim_t *s, int i, uint64_t sim_ns) { return (double)s->e[i].exec_ns / (double)sim_ns; }

/* ======================= 연습 2: 그룹 스케줄링 ======================= */
typedef struct { int ng; uint32_t gw[4]; int gn[4]; uint64_t gv[4]; uint64_t tv[4][8]; uint64_t texec[4][8]; } grp_t;

static void run_groups(grp_t *g, uint64_t sim_ns)
{
    for (uint64_t t = 0; t < sim_ns; t += TICK_NS) {
        int bg = 0;                                         /* ① 그룹 엔티티 중 최소 vruntime */
        for (int k = 1; k < g->ng; k++) if (before_signed(g->gv[k], g->gv[bg])) bg = k;
        int bt = 0;                                         /* ② 그 그룹의 cfs_rq 에서 최소 vruntime */
        for (int k = 1; k < g->gn[bg]; k++) if (before_signed(g->tv[bg][k], g->tv[bg][bt])) bt = k;
        g->texec[bg][bt] += TICK_NS;
        g->tv[bg][bt] += vdelta(TICK_NS, 1024);             /* 태스크는 모두 nice 0 */
        g->gv[bg] += TICK_NS * 1024u / g->gw[bg];           /* 그룹 엔티티는 그룹 가중치로 */
    }
}

/* ======================= 연습 4: avg_vruntime 증분 추적 ======================= */
typedef struct { uint64_t zero; int64_t sum_w; int64_t sum_weight; } avgv_t;

static void av_add(avgv_t *a, uint64_t v, uint32_t w) { a->sum_w += (int64_t)(v - a->zero) * w; a->sum_weight += w; }
static void av_sub(avgv_t *a, uint64_t v, uint32_t w) { a->sum_w -= (int64_t)(v - a->zero) * w; a->sum_weight -= w; }
static uint64_t av_avg(avgv_t *a)                      /* fair.c:715 avg_vruntime (curr 별도 처리 생략) */
{
    int64_t delta = 0;
    if (a->sum_weight) {
        int64_t rt = a->sum_w;
        if (rt < 0) rt -= (a->sum_weight - 1);         /* 음수에서도 floor (왼쪽 치우침) */
        delta = rt / a->sum_weight;
    }
    a->sum_w -= a->sum_weight * delta;                 /* update_zero_vruntime (fair.c:694) */
    a->zero += (uint64_t)delta;
    return a->zero;
}
static bool av_eligible(const avgv_t *a, uint64_t v)  /* fair.c:802 vruntime_eligible */
{
    return a->sum_w >= (int64_t)(v - a->zero) * a->sum_weight;
}

/* ======================= 연습 5: lag 보존 배치 ======================= */
/* 큐(가중치 합 Wq, 가중 평균 V) 에 lag vl 을 가진 엔티티(w)를 넣는다. inflate 면 fair.c:5313 의 식. */
static int64_t place_lag(int64_t vl, uint64_t Wq, uint32_t w, bool inflate)
{
    if (!inflate) return vl;
    return (int64_t)((__int128)vl * (int64_t)(Wq + w) / (int64_t)Wq);
}
static int64_t clamp_lag(int64_t vlag, uint64_t max_slice_ns, uint32_t w)   /* fair.c:767 entity_lag */
{
    int64_t lim = (int64_t)vdelta(max_slice_ns + TICK_NS, w);
    return vlag < -lim ? -lim : vlag > lim ? lim : vlag;
}

int main(void)
{
    static psim_t P;

    /* ---- 연습 1: 래핑 ---- */
    {
        pspec_t sp[2] = { { 0, 0 }, { 0, 0 } };
        uint64_t vr0 = UINT64_MAX - 2000000ull;            /* 2 ms 뒤에 u64 가 넘친다 */
        run_psim(&P, sp, 2, M_CFS_NAIVE, vr0, 2000000000ull);
        double naive0 = pshare(&P, 0, 2000000000ull);
        run_psim(&P, sp, 2, M_CFS, vr0, 2000000000ull);
        double ok0 = pshare(&P, 0, 2000000000ull);
        printf("  ex1 wrap: naive share0 %.3f  signed share0 %.3f\n", naive0, ok0);
        CHECK(naive0 > 0.99);                               /* 넘친 쪽이 '가장 작은 값' 이 되어 독점 */
        CHECK(absd(ok0 - 0.5) < 0.005);
        CHECK(before_signed(UINT64_MAX - 5, 5) && !(UINT64_MAX - 5 < 5));   /* 부호 차이: 앞 / 순진: 뒤 */
    }

    /* ---- 연습 2: 그룹 ---- */
    {
        grp_t g; memset(&g, 0, sizeof g);
        g.ng = 2; g.gw[0] = g.gw[1] = 1024; g.gn[0] = 1; g.gn[1] = 4;
        run_groups(&g, 4000000000ull);
        double a = g.texec[0][0] / 4e9, b = g.texec[1][0] / 4e9, b3 = g.texec[1][3] / 4e9;
        printf("  ex2 groups 1:1  -> G1 task %.3f, G2 tasks %.3f .. %.3f\n", a, b, b3);
        CHECK(absd(a - 0.5) < 0.002 && absd(b - 0.125) < 0.002 && absd(b3 - 0.125) < 0.002);
        memset(&g, 0, sizeof g);
        g.ng = 2; g.gw[0] = 2048; g.gw[1] = 1024; g.gn[0] = 1; g.gn[1] = 4;
        run_groups(&g, 3000000000ull);
        a = g.texec[0][0] / 3e9;
        printf("  ex2 groups 2:1  -> G1 task %.3f\n", a);
        CHECK(absd(a - 2.0 / 3) < 0.002);
        pspec_t flat[5] = { {0,0},{0,0},{0,0},{0,0},{0,0} };     /* 그룹 없이 평평하면 1/5 씩 */
        run_psim(&P, flat, 5, M_CFS, 0, 3000000000ull);
        CHECK(absd(pshare(&P, 0, 3000000000ull) - 0.2) < 0.005);
    }

    /* ---- 연습 3: 미니 EEVDF vs CFS ---- */
    {
        const uint64_t T = 3000000000ull;
        pspec_t sp[3] = { { 0, 0 }, { 0, 0 }, { 0, 500 } };     /* 태스크 2 = slice 0.5 ms 요청 */
        run_psim(&P, sp, 3, M_CFS, 0, T);
        uint64_t cfs_wait = P.e[2].max_wait_ns; double cfs_sh = pshare(&P, 2, T); uint32_t cfs_sw = P.switches;
        run_psim(&P, sp, 3, M_EEVDF, 0, T);
        uint64_t ee_wait = P.e[2].max_wait_ns; double ee_sh = pshare(&P, 2, T);
        uint64_t ee_wait_hog = P.e[0].max_wait_ns; uint32_t ee_sw = P.switches;
        pspec_t sp2[3] = { { 0, 0 }, { 0, 0 }, { 0, 0 } };      /* 같은 태스크가 기본 slice 라면 */
        run_psim(&P, sp2, 3, M_EEVDF, 0, T);
        uint64_t ee_wait_def = P.e[2].max_wait_ns; uint32_t def_sw = P.switches;
        printf("  ex3 task2 max wait: CFS %.1f ms (switches %u) | EEVDF slice 0.5 ms %.1f ms (hog %.1f ms, switches %u)"
               " | EEVDF default %.1f ms (switches %u); share CFS %.3f EEVDF %.3f\n",
               cfs_wait / 1e6, cfs_sw, ee_wait / 1e6, ee_wait_hog / 1e6, ee_sw, ee_wait_def / 1e6, def_sw, cfs_sh, ee_sh);
        CHECK(absd(cfs_sh - 1.0 / 3) < 0.01 && absd(ee_sh - 1.0 / 3) < 0.01);   /* 몫은 같다 */
        CHECK(ee_wait < cfs_wait && ee_wait <= 2 * TICK_NS);                   /* 짧은 slice = 짧은 대기 */
        CHECK(ee_wait_def > ee_wait && ee_sw > def_sw);                       /* 대가: 문맥 전환 증가 */
        /* 손 계산: v = {0, 3, 1} ms(가중치 같음) → V = 4/3 ms. eligible = {0, 2}. 마감 {5, 4, 1.5} → 2 */
        memset(&P, 0, sizeof P); P.n = 3;
        uint64_t vv[3] = { 0, 3000000, 1000000 }, dd[3] = { 5000000, 4000000, 1500000 };
        for (int i = 0; i < 3; i++) { P.e[i].w = 1024; P.e[i].v = vv[i]; P.e[i].d = dd[i]; }
        CHECK(eligible_bf(&P, 0) && !eligible_bf(&P, 1) && eligible_bf(&P, 2) && pick_eevdf_bf(&P) == 2);
        P.e[2].d = 6000000;                                  /* 이제 0 이 eligible 중 가장 이른 마감 */
        CHECK(pick_eevdf_bf(&P) == 0);
    }

    /* ---- 연습 4: avg_vruntime 증분 ---- */
    {
        avgv_t a = { .zero = 1000000000ull, .sum_w = 0, .sum_weight = 0 };
        uint64_t v[16]; uint32_t w[16]; bool in[16] = { false };
        int bad_avg = 0, bad_elig = 0, bad_self = 0, checks = 0;
        for (int step = 0; step < 20000; step++) {
            int i = (int)(xorshift32() % 16);
            uint32_t r = xorshift32();
            if (!in[i]) {
                v[i] = a.zero + (uint64_t)(int64_t)((int32_t)(r % 20000000u) - 10000000);   /* ±10 ms */
                w[i] = weight_of((int)(xorshift32() % 40) - 20);
                av_add(&a, v[i], w[i]); in[i] = true;
            } else if (r & 1) {
                av_sub(&a, v[i], w[i]); in[i] = false;
            } else {                                          /* 실행: v 증가 (빼고 → 바꾸고 → 넣기) */
                av_sub(&a, v[i], w[i]);
                v[i] += vdelta(xorshift32() % 3000000u, w[i]);
                av_add(&a, v[i], w[i]);
            }
            if (a.sum_weight == 0) continue;
            uint64_t got = av_avg(&a);
            __int128 num = 0, den = 0;                        /* 기준점 0 으로 전수 계산 */
            for (int j = 0; j < 16; j++) if (in[j]) { num += (__int128)w[j] * (__int128)v[j]; den += w[j]; }
            __int128 fl = num / den; if (num % den != 0 && num < 0) fl--;
            if ((__int128)got != fl) bad_avg++;
            for (int j = 0; j < 16; j++) if (in[j]) {
                bool exact = (__int128)v[j] * den <= num;      /* v_j ≤ Σwv / Σw */
                if (exact != av_eligible(&a, v[j])) bad_elig++;
            }
            if (!av_eligible(&a, got)) bad_self++;            /* avg_vruntime() 자체는 항상 eligible */
            checks++;
        }
        printf("  ex4 avg_vruntime: %d states, avg mismatch %d, eligible mismatch %d\n", checks, bad_avg, bad_elig);
        CHECK(checks > 10000 && bad_avg == 0);
        CHECK(bad_elig == 0 && bad_self == 0);
    }

    /* ---- 연습 5: lag 보존 배치 ---- */
    {
        int bad_infl = 0, shrink_ok = 0, cases = 0;
        for (int k = 0; k < 2000; k++) {
            uint32_t n = 1 + xorshift32() % 6, wi = weight_of((int)(xorshift32() % 40) - 20);
            uint64_t vq[8], Wq = 0; __int128 sum = 0;
            for (uint32_t j = 0; j < n; j++) {
                vq[j] = 50000000ull + xorshift32() % 4000000u;
                uint32_t wj = weight_of((int)(xorshift32() % 40) - 20);
                Wq += wj; sum += (__int128)wj * vq[j];
            }
            int64_t vl = (int64_t)(xorshift32() % 6000000u) - 3000000;   /* 저장된 lag ±3 ms */
            if (vl == 0) continue;
            int64_t V = (int64_t)(sum / Wq);
            for (int inf = 0; inf < 2; inf++) {
                int64_t vi = V - place_lag(vl, Wq, wi, inf);          /* v_i = V − vl' */
                __int128 sum2 = sum + (__int128)wi * vi;
                int64_t V2 = (int64_t)(sum2 / (Wq + wi));
                int64_t eff = V2 - vi;                                /* 넣은 뒤 실제 lag */
                if (inf) { if (eff - vl > 2 || vl - eff > 2) bad_infl++; }
                else if ((vl > 0 && eff < vl) || (vl < 0 && eff > vl)) shrink_ok++;
            }
            cases++;
        }
        printf("  ex5 place: %d cases, inflate off-by>2ns %d, no-inflate shrank %d\n", cases, bad_infl, shrink_ok);
        CHECK(cases > 1900 && bad_infl == 0);
        CHECK(shrink_ok == cases);                            /* 부풀리지 않으면 매번 줄어든다 */
        CHECK(clamp_lag(-50000000, 2800000, 1024) == -3800000 && clamp_lag(100, 2800000, 1024) == 100);
        CHECK(clamp_lag(50000000, 2800000, 335) == (int64_t)(3800000ull * 1024 / 335));
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
