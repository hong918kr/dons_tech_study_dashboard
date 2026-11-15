/* ARCH-B10 평균 메모리 접근 시간(AMAT) 계산기
 *
 *   Part A  AMAT 재귀식 (LOCAL 히트율), 도달 비율 reach, 전역 미스율, 계층별 기여, CPI 영향
 *   Part B  입력 검사 (NaN 포함), 전역 → 로컬 변환, 민감도 (+1%p 로컬 / +1%p 전역), 병목 계층 찾기
 *   Part C  클럭 스케일링 (캐시는 cycle 고정, DRAM 은 ns 고정), I/D 분리 CPI, 실행 시간, 최악 지연
 *
 * 지연 숫자는 "교과서적/예시" 파라미터다 (이 맥에서 실측한 값이 아님 — 실측은 MEM-B10 포인터 체이싱).
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so
 */
#include <math.h>
#include <stdio.h>

#define AMAT_MAX_LEVELS 8

typedef struct {
    const char *name;
    double hit_time;  /* 이 계층에서 히트로 끝날 때 드는 시간 (cycles) */
    double hit_rate;  /* LOCAL 히트율: 이 계층까지 "내려온" 접근 중 히트 비율. 마지막(DRAM) = 1.0 */
} level_t;

/* ======================= Part A — AMAT 핵심 ======================= */

/* AMAT = t0 + m0 * (t1 + m1 * (t2 + ... (t_{n-1}))) , m_k = 1 - h_k (local miss).
 * n >= 1, 입력은 이미 검사되었다고 가정 (Part B amat_validate). */
static double amat(const level_t *L, int n) {
    (void)L; (void)n;
    return 0.0; /* TODO: 맨 아래부터 위로 (Horner) 접어 올린다 */
}

/* 전체 접근 중 계층 k 까지 내려오는 비율 = prod_{j<k} (1 - h_j). reach(0) = 1. */
static double reach_frac(const level_t *L, int n, int k) {
    (void)L; (void)n; (void)k;
    return 0.0; /* TODO */
}

/* 계층 k 의 GLOBAL 미스율 = 전체 접근 중 계층 k 까지 미스한 비율 = prod_{j<=k} (1 - h_j). */
static double global_miss(const level_t *L, int n, int k) {
    (void)L; (void)n; (void)k;
    return 0.0; /* TODO */
}

/* base CPI 는 "모든 접근이 L1 히트" 라고 보고 잰 값 (L1 히트 지연은 파이프라인에 숨음).
 * 메모리 스톨/명령 = refs_per_instr * (AMAT - L1 히트 시간). */
static double cpi_with_memory(double cpi_base, double refs_per_instr, double amat_cycles, double l1_hit) {
    (void)refs_per_instr; (void)amat_cycles; (void)l1_hit;
    return cpi_base; /* TODO */
}

/* ======================= Part B — 함정과 민감도 ======================= */
typedef enum { AMAT_OK = 0, AMAT_E_EMPTY, AMAT_E_RATE, AMAT_E_TIME, AMAT_E_LAST } amat_err_t;

/* n 범위(1..AMAT_MAX_LEVELS) → 각 계층 시간(>= 0, 유한) → 히트율([0,1], NaN 거부) → 마지막 = 1.0 */
static amat_err_t amat_validate(const level_t *L, int n) {
    (void)L; (void)n;
    return AMAT_OK; /* TODO: NaN 은 어떤 비교도 거짓 → 비교를 "통과 조건" 쪽으로 쓴다 */
}

/* 측정 도구가 준 GLOBAL 미스율 gmiss[0..nc-1] (캐시 계층만) → LOCAL 히트율 local_hit[0..nc-1].
 * local miss_k = gmiss[k] / gmiss[k-1] (gmiss[-1] = 1). 범위 밖이거나 증가하면 -1.
 * 위 계층 전역 미스가 0 이면 그 계층엔 아무도 안 온다 → local_hit = 1.0 으로 둔다. */
static int local_from_global(const double *gmiss, int nc, double *local_hit) {
    (void)gmiss; (void)nc; (void)local_hit;
    return -1; /* TODO */
}

/* 계층 k 의 LOCAL 히트율을 dp 만큼 올렸을 때 ΔAMAT (음수 = 좋아짐). 1.0 에서 잘린다.
 * k 가 마지막 계층이거나 범위 밖이면 0. */
static double sens_local(const level_t *L, int n, int k, double dp) {
    (void)L; (void)n; (void)k; (void)dp;
    return 0.0; /* TODO: 복사본의 h_k 만 바꿔서 amat 을 다시 계산 → 차이 */
}

/* "전체 접근의 dp" 가 계층 k 에서 새로 히트하게 될 때 ΔAMAT.
 * 로컬로 환산하면 dp / reach(k). reach(k) == 0 이면 0. */
static double sens_global(const level_t *L, int n, int k, double dp) {
    (void)L; (void)n; (void)k; (void)dp;
    return 0.0; /* TODO */
}

/* 0..n-2 중 +1%p 개선의 효과가 가장 큰(ΔAMAT 가 가장 음수인) 계층. 동점이면 위쪽(작은 k). n < 2 → -1. */
static int bottleneck(const level_t *L, int n, int use_global) {
    (void)L; (void)n; (void)use_global;
    return -1; /* TODO */
}

/* ======================= Part C — 클럭 · CPI · 최악 지연 ======================= */
typedef struct {
    const char *name;
    double cycles;   /* 코어 클럭에 묶인 부분 (on-chip 캐시: 클럭 올려도 cycle 수 그대로) */
    double ns;       /* 절대 시간에 묶인 부분 (DRAM tRC/tCAS 등: 클럭 올려도 ns 그대로) */
    double hit_rate; /* LOCAL */
} level_spec_t;

typedef struct {
    double cpi_base;          /* 모든 접근이 L1 히트일 때의 CPI */
    double ifetch_per_instr;  /* 명령어 fetch 수 / 명령 (보통 1.0) */
    double data_per_instr;    /* load/store 수 / 명령 (예: 0.35) */
} workload_t;

/* f_ghz 에서 계층 지연을 cycle 로: hit_time = cycles + ns * f_ghz (1 GHz = 1 cycle/ns). */
static void scale_to_freq(const level_spec_t *spec, int n, double f_ghz, level_t *out) {
    for (int k = 0; k < n; k++) {
        out[k].name = spec[k].name;
        out[k].hit_time = spec[k].cycles; /* TODO: ns 부분도 cycle 로 바꿔 더한다 */
        out[k].hit_rate = spec[k].hit_rate;
    }
    (void)f_ghz;
}

/* I-side 와 D-side 를 따로: CPI = base + ifetch*(AMAT_I - tI1) + data*(AMAT_D - tD1) */
static double cpi_split(const workload_t *w, double amat_i, double t_i1, double amat_d, double t_d1) {
    (void)amat_i; (void)t_i1; (void)amat_d; (void)t_d1;
    return w->cpi_base; /* TODO */
}

/* 실행 시간 (us) = 명령 수 * CPI / f.  f_ghz GHz = f_ghz * 1000 cycles/us. */
static double exec_time_us(double instr, double cpi, double f_ghz) {
    (void)instr; (void)cpi; (void)f_ghz;
    return 0.0; /* TODO */
}

/* 직렬 조회 모델의 최악 지연: 모든 캐시가 미스하고 DRAM 까지 = sum t_k.
 * (실제 최악은 dirty victim write-back, 버스 경합, 리프레시까지 더해진다 — 하한) */
static double worst_case_latency(const level_t *L, int n) {
    (void)L; (void)n;
    return 0.0; /* TODO */
}

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; printf("PASS  %s\n", #expr); } \
                         else printf("FAIL  %s  (line %d)\n", #expr, __LINE__); } while (0)
#define NEAR(a, b) (fabs((a) - (b)) < 1e-9)

/* 계층 구조 A: "보통" 워크로드 (교과서적 지연, cycles @ 2 GHz 가정) */
static const level_t HA[] = {
    {"L1", 4, 0.90}, {"L2", 12, 0.70}, {"L3", 40, 0.60}, {"DRAM", 200, 1.0},
};
/* 계층 구조 B: 워킹셋이 L1 보다 크고 L2 에는 들어감 → L1 히트 50%, L2 로컬 95% */
static const level_t HB[] = {
    {"L1", 4, 0.50}, {"L2", 12, 0.95}, {"L3", 40, 0.50}, {"DRAM", 200, 1.0},
};

int main(void) {
    const int n = 4;

    /* ---------- Part A ---------- */
    CHECK(NEAR(amat(HA, n), 8.8));                                   /* 4 + .1*(12 + .3*(40 + .4*200)) */
    const level_t two[] = {{"L1", 1, 0.95}, {"MEM", 100, 1.0}};
    CHECK(NEAR(amat(two, 2), 6.0));
    const level_t only[] = {{"TCM", 1, 1.0}};
    CHECK(NEAR(amat(only, 1), 1.0));
    CHECK(NEAR(reach_frac(HA, n, 0), 1.0));
    CHECK(NEAR(reach_frac(HA, n, 1), 0.10));
    CHECK(NEAR(reach_frac(HA, n, 2), 0.03));
    CHECK(NEAR(reach_frac(HA, n, 3), 0.012));
    CHECK(NEAR(global_miss(HA, n, 0), 0.10));
    CHECK(NEAR(global_miss(HA, n, 2), 0.012));
    CHECK(NEAR(global_miss(HA, n, 3), 0.0));                         /* DRAM 은 미스가 없다 */
    {   /* 합 형태: AMAT = sum reach_k * t_k  (계층별 기여 4 + 1.2 + 1.2 + 2.4) */
        double s = 0.0;
        for (int k = 0; k < n; k++) s += reach_frac(HA, n, k) * HA[k].hit_time;
        CHECK(NEAR(s, amat(HA, n)));
        CHECK(NEAR(reach_frac(HA, n, 3) * HA[3].hit_time, 2.4));     /* 1.2% 만 가는 DRAM 이 최대 기여 */
    }
    CHECK(NEAR(cpi_with_memory(1.0, 0.35, amat(HA, n), 4.0), 2.68));
    CHECK(NEAR(cpi_with_memory(1.0, 0.35, 4.0, 4.0), 1.0));          /* 전부 L1 히트면 base 그대로 */

    /* ---------- Part B ---------- */
    CHECK(amat_validate(HA, n) == AMAT_OK);
    CHECK(amat_validate(HA, 0) == AMAT_E_EMPTY);
    {
        level_t bad[4];
        for (int k = 0; k < 4; k++) bad[k] = HA[k];
        bad[1].hit_rate = 1.2;  CHECK(amat_validate(bad, 4) == AMAT_E_RATE);
        bad[1].hit_rate = NAN;  CHECK(amat_validate(bad, 4) == AMAT_E_RATE);   /* NaN 비교는 항상 거짓 */
        bad[1].hit_rate = 0.7;  bad[2].hit_time = -1;  CHECK(amat_validate(bad, 4) == AMAT_E_TIME);
        bad[2].hit_time = 40;   bad[3].hit_rate = 0.99; CHECK(amat_validate(bad, 4) == AMAT_E_LAST);
    }
    {   /* 프로파일러가 준 전역 미스율 → 로컬 */
        const double g[3] = {0.10, 0.03, 0.012};
        double h[3] = {0};
        CHECK(local_from_global(g, 3, h) == 0);
        CHECK(NEAR(h[0], 0.90) && NEAR(h[1], 0.70) && NEAR(h[2], 0.60));
        level_t conv[4] = {{"L1", 4, h[0]}, {"L2", 12, h[1]}, {"L3", 40, h[2]}, {"DRAM", 200, 1.0}};
        CHECK(NEAR(amat(conv, 4), 8.8));
        /* 흔한 실수: 전역 미스율을 재귀식에 그대로 넣는다 → 4 + .1*(12 + .03*(40 + .012*200)) */
        level_t naive[4] = {{"L1", 4, 1 - g[0]}, {"L2", 12, 1 - g[1]}, {"L3", 40, 1 - g[2]}, {"DRAM", 200, 1.0}};
        CHECK(NEAR(amat(naive, 4), 5.3272));                         /* 실제 8.8 보다 40% 낙관적 */
        const double up[2] = {0.03, 0.10};                           /* 전역 미스율이 증가 → 불가능 */
        CHECK(local_from_global(up, 2, h) == -1);
        const double z[2] = {0.0, 0.0};                              /* L1 이 전부 히트 */
        CHECK(local_from_global(z, 2, h) == 0 && h[0] == 1.0 && h[1] == 1.0);
    }
    /* 민감도: 로컬 +1%p */
    CHECK(NEAR(sens_local(HA, n, 0, 0.01), -0.48));                  /* -.01 * 1    * 48  */
    CHECK(NEAR(sens_local(HA, n, 1, 0.01), -0.12));                  /* -.01 * .1   * 120 */
    CHECK(NEAR(sens_local(HA, n, 2, 0.01), -0.06));                  /* -.01 * .03  * 200 */
    CHECK(sens_local(HA, n, 3, 0.01) == 0.0);                        /* DRAM 은 대상 아님 */
    for (int k = 0; k < n - 1; k++) {                                /* 해석식과 일치: 각 h_k 에 대해 선형 */
        double analytic = -0.01 * reach_frac(HA, n, k) * amat(HA + k + 1, n - k - 1);
        CHECK(NEAR(sens_local(HA, n, k, 0.01), analytic));
    }
    {   /* 1.0 에서 잘림: .995 + .01 → 1.0 (실제로는 +0.5%p 만) */
        level_t c[2] = {{"L1", 1, 0.995}, {"MEM", 100, 1.0}};
        CHECK(NEAR(sens_local(c, 2, 0, 0.01), -0.5));
    }
    /* 민감도: 전역 +1%p (전체 접근의 1% 가 그 계층에서 새로 히트) */
    CHECK(NEAR(sens_global(HA, n, 0, 0.01), -0.48));
    CHECK(NEAR(sens_global(HA, n, 1, 0.01), -1.2));
    CHECK(NEAR(sens_global(HA, n, 2, 0.01), -2.0));
    {
        level_t perfect[3] = {{"L1", 4, 1.0}, {"L2", 12, 0.5}, {"MEM", 100, 1.0}};
        CHECK(sens_global(perfect, 3, 1, 0.01) == 0.0);               /* 아무도 L2 에 안 온다 */
    }
    CHECK(bottleneck(HA, n, 0) == 0);    /* 로컬 정의: L1 */
    CHECK(bottleneck(HA, n, 1) == 2);    /* 전역 정의: L3 — "1%" 의 정의가 답을 뒤집는다 */
    CHECK(bottleneck(HB, n, 0) == 1);    /* 워크로드 B: 로컬로도 L2 가 병목 */
    CHECK(NEAR(amat(HB, n), 13.5));
    CHECK(bottleneck(only, 1, 0) == -1);

    /* ---------- Part C ---------- */
    const level_spec_t SA[] = {
        {"L1", 4, 0, 0.90}, {"L2", 12, 0, 0.70}, {"L3", 40, 0, 0.60}, {"DRAM", 0, 100, 1.0},
    };
    const level_spec_t SI[] = {   /* I-side: L1I 만 다르고 L2 부터는 공유 */
        {"L1I", 4, 0, 0.99}, {"L2", 12, 0, 0.70}, {"L3", 40, 0, 0.60}, {"DRAM", 0, 100, 1.0},
    };
    const workload_t w = {1.0, 1.0, 0.35};
    level_t d1[4], d2[4], i1[4], i2[4];
    scale_to_freq(SA, n, 1.0, d1);  scale_to_freq(SA, n, 2.0, d2);
    scale_to_freq(SI, n, 1.0, i1);  scale_to_freq(SI, n, 2.0, i2);
    CHECK(NEAR(d2[3].hit_time, 200.0) && NEAR(d1[3].hit_time, 100.0) && NEAR(d2[1].hit_time, 12.0));
    CHECK(NEAR(amat(d2, n), 8.8));
    CHECK(NEAR(amat(d1, n), 7.6));
    double cpi1 = cpi_split(&w, amat(i1, n), 4, amat(d1, n), 4);
    double cpi2 = cpi_split(&w, amat(i2, n), 4, amat(d2, n), 4);
    CHECK(NEAR(cpi1, 2.62));
    CHECK(NEAR(cpi2, 3.16));
    double t1 = exec_time_us(1e6, cpi1, 1.0), t2 = exec_time_us(1e6, cpi2, 2.0);
    CHECK(NEAR(t1, 2620.0) && NEAR(t2, 1580.0));
    CHECK(t1 / t2 > 1.6 && t1 / t2 < 1.7);                           /* 클럭 2배 → 1.66배 */
    CHECK(NEAR(worst_case_latency(HA, n), 256.0));
    CHECK(worst_case_latency(HA, n) / amat(HA, n) > 29.0);           /* 평균 8.8 vs 최악 256 */
    {   /* Cortex-M7 급 예시: D-cache 1 cycle 95% + 외부 SDRAM 75 ns @ 400 MHz, vs DTCM */
        const level_spec_t m7[] = {{"L1D", 1, 0, 0.95}, {"SDRAM", 0, 75, 1.0}};
        level_t m[2];
        scale_to_freq(m7, 2, 0.4, m);
        CHECK(NEAR(m[1].hit_time, 30.0));
        CHECK(NEAR(amat(m, 2), 2.5) && NEAR(worst_case_latency(m, 2), 31.0));
        const level_t dtcm[] = {{"DTCM", 1, 1.0}};
        CHECK(NEAR(amat(dtcm, 1), worst_case_latency(dtcm, 1)));      /* TCM: 평균 = 최악 */
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
