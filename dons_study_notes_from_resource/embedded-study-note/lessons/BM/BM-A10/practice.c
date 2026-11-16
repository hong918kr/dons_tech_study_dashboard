/* BM-A10 추가 연습 — 호스트 C (맥). 펌웨어가 아니다.
 *   연습 1  로직 애널라이저(LA) 샘플링으로 본 간격 → 실제 사이클 구간 (양자화), 전수 대조
 *   연습 2  CYCCNT 감김 주기 + 64 비트 확장
 *   연습 3  최고/중간 우선순위 IRQ 의 최악 응답 시간 (RTA + 진입/복귀 오버헤드 + 마스킹 구간) 대 시뮬레이션
 *   연습 4  "표로 정리": WS × ART × FP 진입 지연 표 (본문 모델의 닫힌 식 축약판)
 *   연습 5  차분 측정: 모르는 트리거 오프셋이 소거되는지 + tail-chaining 절감 식
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o /tmp/BM-A10_pr && /tmp/BM-A10_pr
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(cond, msg) do { ++g_total; if (cond) { ++g_pass; } \
    else printf("FAIL  %s  (line %d)\n", msg, __LINE__); } while (0)
static uint32_t g_rng = 0xA10u;
static uint32_t xs32(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }

/* ---------- 연습 1: LA 양자화 ----------
 * LA 는 T 사이클마다 한 번 샘플(T = core_hz / la_hz, 정수라고 가정). 에지는 "그 시각 이후 첫 샘플" 에 보인다.
 * 두 에지가 k 샘플 떨어져 보였다면 실제 간격 Δ(사이클)의 범위는? */
static void la_interval(uint32_t k, uint32_t T, uint32_t *lo, uint32_t *hi)
{
    *lo = (k == 0) ? 0 : (k - 1) * T + 1;
    *hi = (k + 1) * T - 1;
}
static uint32_t ceil_div(uint32_t a, uint32_t b) { return (a + b - 1) / b; }

static void ex1(void)
{
    int bad = 0;
    for (uint32_t T = 1; T <= 12; ++T) {
        uint32_t mn[64], mx[64];
        for (int k = 0; k < 64; ++k) { mn[k] = 0xFFFFFFFFu; mx[k] = 0; }
        for (uint32_t delta = 0; delta <= 300; ++delta)
            for (uint32_t ph = 0; ph < T; ++ph) {             /* 첫 에지의 위상 */
                uint32_t k = ceil_div(ph + delta, T) - ceil_div(ph, T);
                if (k < 64) { if (delta < mn[k]) mn[k] = delta; if (delta > mx[k]) mx[k] = delta; }
            }
        for (uint32_t k = 0; k < 64 && (k + 1) * T <= 301; ++k) {     /* 범위 안에서 양 끝이 다 나오는 k 만 */
            uint32_t lo, hi;
            la_interval(k, T, &lo, &hi);
            if (lo != mn[k] || hi != mx[k]) ++bad;
        }
    }
    CHECK(bad == 0, "ex1: la_interval is tight vs brute force (T=1..12, all phases)");
    /* 168 MHz 코어, 24 MHz LA → T=7. 12 사이클 진입은 k=1 또는 2 로 보인다 */
    uint32_t lo, hi;
    la_interval(2, 7, &lo, &hi);
    printf("  ex1: T=7 (168 MHz / 24 MHz LA), k=2 -> delta in [%u, %u] cycles\n", (unsigned)lo, (unsigned)hi);
    CHECK(lo == 8 && hi == 20, "ex1: k=2 at T=7 -> [8, 20]: 12 and 20 cycles look the same");
    /* 12 와 22 (ART hit/miss) 를 한 번의 측정으로 구별하려면 구간 폭 2T−2 < 10 → T ≤ 5 */
    uint32_t Tmax = 0;
    for (uint32_t T = 1; T <= 12; ++T) if (2 * T - 2 < 22 - 12) Tmax = T;
    printf("  ex1: to separate 12 vs 22 in one shot need T <= %u -> LA >= %u MHz at 168 MHz\n",
           (unsigned)Tmax, (unsigned)ceil_div(168, Tmax));
    CHECK(Tmax == 5, "ex1: max T = 5");
}

/* ---------- 연습 2: CYCCNT 감김 + 64 비트 확장 ---------- */
static uint32_t cyccnt_wrap_ms(uint32_t core_hz) { return (uint32_t)((((uint64_t)1 << 32) * 1000u) / core_hz); }
typedef struct { uint32_t last; uint64_t ext; } cyc64_t;
/* 감김 주기보다 자주 불리면 정확: 부호 없는 차이를 누적 */
static uint64_t cyc64_update(cyc64_t *s, uint32_t now) { s->ext += (uint32_t)(now - s->last); s->last = now; return s->ext; }

static void ex2(void)
{
    uint32_t a = cyccnt_wrap_ms(16000000u), b = cyccnt_wrap_ms(168000000u), c = cyccnt_wrap_ms(480000000u);
    printf("  ex2: CYCCNT wraps every %u ms @16 MHz, %u ms @168 MHz, %u ms @480 MHz\n", (unsigned)a, (unsigned)b, (unsigned)c);
    CHECK(a == 268435u && b == 25565u && c == 8947u, "ex2: wrap periods");
    int bad = 0;
    for (int r = 0; r < 200; ++r) {
        uint64_t truth = (uint64_t)xs32() << 8;
        cyc64_t s = { (uint32_t)truth, truth };
        for (int i = 0; i < 100; ++i) {
            truth += xs32();                                   /* 한 번에 < 2^32 진행 */
            if (cyc64_update(&s, (uint32_t)truth) != truth) ++bad;
        }
    }
    CHECK(bad == 0, "ex2: 64-bit extension exact for 20000 random steps < 2^32");
    /* 2^32 이상 건너뛰면(호출을 한 주기 넘게 놓치면) 틀린다 */
    cyc64_t s = { 0, 0 };
    uint64_t got = cyc64_update(&s, 5u);                      /* 실제로는 2^32 + 5 가 지났다 */
    CHECK(got == 5u && got != ((uint64_t)1 << 32) + 5u, "ex2: missing a whole wrap is undetectable (the limit)");
}

/* ---------- 연습 3: 최악 응답 시간 ----------
 * 우선순위 i 의 IRQ: 본문 C_i, 최소 간격 T_i. 오버헤드 O = 진입 + 복귀 (모델 12 + 12) 를 매 활성화에 더한다.
 * B = 가장 긴 마스킹 구간(PRIMASK/BASEPRI ≥ i, 또는 더 낮은 우선순위의 비선점 구간).
 * R = B + (C_i + O) + Σ_{j 더 급함} ceil(R / T_j)·(C_j + O)   (고정점 반복, 한도 넘으면 실패) */
typedef struct { uint32_t c, t; } task_t;
static int rta(const task_t *hp, int nhp, uint32_t c, uint32_t b, uint32_t o, uint32_t limit, uint32_t *r_out)
{
    uint32_t r = b + c + o;
    for (int it = 0; it < 1000; ++it) {
        uint64_t nr = (uint64_t)b + c + o;
        for (int j = 0; j < nhp; ++j) nr += (uint64_t)ceil_div(r, hp[j].t) * (hp[j].c + o);
        if (nr > limit) return -1;
        if (nr == r) { *r_out = r; return 0; }
        r = (uint32_t)nr;
    }
    return -1;
}
/* 대조: 임계 순간(모두 t=0 에 해제, 그 직전 낮은 쪽이 B 동안 마스킹)을 사이클 단위로 시뮬레이션 */
static uint32_t sim_response(const task_t *hp, int nhp, uint32_t c, uint32_t b, uint32_t o, uint32_t limit)
{
    uint32_t rem[8] = {0}, next[8] = {0}, me = c + o, t;
    for (t = 0; t < limit; ++t) {
        for (int j = 0; j < nhp; ++j) if (t == next[j]) { rem[j] += hp[j].c + o; next[j] += hp[j].t; }
        if (t < b) continue;                                   /* 마스킹: 아무도 못 들어옴 */
        int run = -1;
        for (int j = 0; j < nhp; ++j) if (rem[j]) { run = j; break; }   /* hp 는 급한 순 */
        if (run >= 0) --rem[run];
        else if (--me == 0) return t + 1;
    }
    return 0xFFFFFFFFu;
}
static void ex3(void)
{
    task_t hp[2] = { { 40, 1000 }, { 100, 3000 } };
    uint32_t r;
    int rc = rta(hp, 2, 200, 150, 24, 100000, &r);
    printf("  ex3: C=200, B=150, O=24, hp={40/1000, 100/3000} -> R = %u cycles (%u ns @168 MHz)\n",
           (unsigned)r, (unsigned)((uint64_t)r * 1000u / 168u));
    CHECK(rc == 0 && r == sim_response(hp, 2, 200, 150, 24, 100000), "ex3: hand case RTA == simulation");
    int bad = 0, cases = 0;
    g_rng = 0x3A3Au;
    for (int k = 0; k < 2000; ++k) {
        task_t h[3];
        int n = (int)(xs32() % 4u);
        uint32_t util = 0;
        for (int j = 0; j < n; ++j) { h[j].t = 200 + xs32() % 2000u; h[j].c = 1 + xs32() % (h[j].t / 6u); util += 1000u * (h[j].c + 24) / h[j].t; }
        if (util > 800) continue;
        uint32_t c = 1 + xs32() % 300u, b = xs32() % 200u, rr;
        ++cases;
        if (rta(h, n, c, b, 24, 200000, &rr) != 0 || rr != sim_response(h, n, c, b, 24, 200000)) ++bad;
    }
    printf("  ex3: %d random task sets (util <= 80%%), RTA vs simulation mismatches = %d\n", cases, bad);
    CHECK(cases > 1000 && bad == 0, "ex3: RTA fixed point equals critical-instant simulation");
}

/* ---------- 연습 4: 진입 지연 표 (BM-A10 모델 닫힌 식 축약판) ---------- */
static uint32_t entry_closed(uint32_t ws, int hit, int vram, int fpca, int lspen)
{
    uint32_t pen = hit ? 0 : ((vram ? 0 : ws) + ws);
    return 12 + pen + ((fpca && !lspen) ? 17u : 0u);
}
static void ex4(void)
{
    printf("  ex4: entry latency table (MODEL cycles, not measured)\n");
    printf("       WS | ART hit | miss | miss+VTOR RAM | miss+FP full | miss+FP lazy\n");
    uint32_t t[6][5];
    for (uint32_t ws = 0; ws <= 5; ++ws) {
        t[ws][0] = entry_closed(ws, 1, 0, 0, 1); t[ws][1] = entry_closed(ws, 0, 0, 0, 1);
        t[ws][2] = entry_closed(ws, 0, 1, 0, 1); t[ws][3] = entry_closed(ws, 0, 0, 1, 0);
        t[ws][4] = entry_closed(ws, 0, 0, 1, 1);
        printf("       %2u | %7u | %4u | %13u | %12u | %12u\n", (unsigned)ws, (unsigned)t[ws][0], (unsigned)t[ws][1],
               (unsigned)t[ws][2], (unsigned)t[ws][3], (unsigned)t[ws][4]);
    }
    CHECK(t[0][0] == 12 && t[0][1] == 12 && t[0][3] == 29, "ex4: WS0 row: 12 / 12 / 29");
    CHECK(t[5][1] == 22 && t[5][2] == 17 && t[5][3] == 39 && t[5][4] == 22, "ex4: WS5 row: 22 / 17 / 39 / 22");
    int mono = 1;
    for (uint32_t ws = 1; ws <= 5; ++ws) for (int c = 0; c < 5; ++c) if (t[ws][c] < t[ws - 1][c]) mono = 0;
    CHECK(mono, "ex4: every column non-decreasing in WS");
    /* ns 로: 168 MHz 에서 WS5 miss + FP full */
    printf("  ex4: worst cell %u cycles = %u ns @168 MHz\n", (unsigned)t[5][3], (unsigned)((uint64_t)t[5][3] * 1000u / 168u));
    CHECK(t[5][3] * 1000u / 168u == 232u, "ex4: 39 cycles = 232 ns");
}

/* ---------- 연습 5: 차분 측정 + tail-chaining 절감 ---------- */
static int cmp_u32(const void *a, const void *b) { uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b; return (x > y) - (x < y); }
static uint32_t median(uint32_t *v, uint32_t n) { qsort(v, n, sizeof *v, cmp_u32); return v[(n + 1) / 2 - 1]; }
/* 두 시나리오 A, B 는 같은 미지 오프셋(트리거 경로 + 읽기)을 공유 → 중앙값 차이는 오프셋과 무관 */
static uint32_t diff_median(uint32_t *a, uint32_t na, uint32_t *b, uint32_t nb) { return median(b, nb) - median(a, na); }
static uint32_t overhead_tail(uint32_t n) { return n ? 12 + (n - 1) * 6 + 12 : 0; }
static uint32_t overhead_no_tail(uint32_t n) { return n * (12 + 12); }

static void ex5(void)
{
    static uint32_t a[501], b[501];
    int bad = 0;
    g_rng = 0xD1FFu;
    for (int r = 0; r < 300; ++r) {
        uint32_t off = xs32() % 40u, pen = xs32() % 20u;      /* 미지 오프셋, 찾을 차이 */
        for (int i = 0; i < 501; ++i) {
            a[i] = off + 12 + ((xs32() % 10u) == 0);           /* 10 % +1 지터 */
            b[i] = off + 12 + pen + ((xs32() % 10u) == 0);
        }
        if (diff_median(a, 501, b, 501) != pen) ++bad;
    }
    CHECK(bad == 0, "ex5: median difference recovers the WS penalty for any unknown offset (300 trials)");
    printf("  ex5: n pending, overhead tail-chained vs not (MODEL cycles)\n");
    for (uint32_t n = 1; n <= 6; ++n)
        printf("       n=%u  %3u vs %3u  (saved %u)\n", (unsigned)n, (unsigned)overhead_tail(n),
               (unsigned)overhead_no_tail(n), (unsigned)(overhead_no_tail(n) - overhead_tail(n)));
    int okf = 1;
    for (uint32_t n = 1; n <= 64; ++n) if (overhead_no_tail(n) - overhead_tail(n) != (n - 1) * 18u) okf = 0;
    CHECK(okf, "ex5: saving = (n-1)*(entry+exit-tail) = 18(n-1)");
    CHECK(overhead_tail(4) == 42 && overhead_no_tail(4) == 96, "ex5: n=4 -> 42 vs 96");
}

int main(void)
{
    ex1(); ex2(); ex3(); ex4(); ex5();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
