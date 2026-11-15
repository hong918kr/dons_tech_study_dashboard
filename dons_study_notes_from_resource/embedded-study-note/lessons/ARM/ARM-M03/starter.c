/* ARM-M03 시스템 카운터 기반 마이크로벤치 하네스 — 하네스 수학 + 배관 + 테스트 (시작 코드: TODO 채우기)
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 {starter|solution}.c {starter|solution}.S -o /tmp/ARM-M03_v
 *
 * 구조: asm(.S) = 카운터 읽기 + 측정 커널.  C(이 파일) = 반복 측정, 오버헤드 보정, 통계, 단위 변환.
 * 테스트는 결정적이다: 시간을 재서 판정하지 않는다. 시계는 함수 포인터로 주입(가짜 시계로 배관 검증),
 * 커널은 반환값을 C 참조 모델과 비교한다. 실제 측정 표는 demo_bench.c 가 만든다.
 */
#include <inttypes.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- asm (solution.S) ---------- */
uint64_t cnt_read(void);                 /* isb; mrs cntvct_el0 */
uint64_t cnt_freq(void);                 /* mrs cntfrq_el0 */
typedef uint64_t (*ikern_fn)(uint64_t iters, uint64_t x, uint64_t y);
uint64_t k_empty(uint64_t, uint64_t, uint64_t);
uint64_t lat_add(uint64_t, uint64_t, uint64_t),  tput_add(uint64_t, uint64_t, uint64_t);
uint64_t lat_mul(uint64_t, uint64_t, uint64_t),  tput_mul(uint64_t, uint64_t, uint64_t);
uint64_t lat_madd(uint64_t, uint64_t, uint64_t), lat_madd_acc(uint64_t, uint64_t, uint64_t);
uint64_t lat_sdiv(uint64_t, uint64_t, uint64_t), tput_sdiv(uint64_t, uint64_t, uint64_t);
uint64_t lat_udiv(uint64_t, uint64_t, uint64_t);
uint64_t lat_ldr(uint64_t, uint64_t, uint64_t),  tput_ldr(uint64_t, uint64_t, uint64_t);
double lat_fadd(uint64_t, double, double),  tput_fadd(uint64_t, double, double);
double lat_fmul(uint64_t, double, double),  tput_fmul(uint64_t, double, double);
double lat_fmadd(uint64_t, double, double), tput_fmadd(uint64_t, double, double);
#define OPS_PER_ITER 16u                 /* 모든 커널: 한 반복 = 측정 명령 16개 */

/* ---------- 하네스 공개 인터페이스 ---------- */
typedef uint64_t (*clock_fn)(void);
struct bench_stats { uint64_t min, med, max; size_t outliers; };
volatile uint64_t g_sink;                /* 결과 싱크: 커널 반환값을 여기 XOR → 호출/결과가 "쓰인다" */

/* ================= Part B: 하네스 수학 ================= */

/* a - b, 음수가 되면 0 (오버헤드 보정 후 언더플로 방지) */
uint64_t u64_sub_clamp(uint64_t a, uint64_t b) {
    (void)b;
    return a;                                                /* TODO: a > b 일 때만 a - b, 아니면 0 */
}

static void u64_isort(uint64_t *a, size_t n) {               /* 삽입 정렬: reps 는 수십 개 */
    (void)a; (void)n;                                        /* TODO: 오름차순 정렬 */
}

/* 중앙값 (a 를 제자리 정렬). n==0 → 0. 짝수 n → 가운데 둘의 평균(내림), 오버플로 없이 */
uint64_t u64_median(uint64_t *a, size_t n) {
    if (n == 0) return 0;
    u64_isort(a, n);
    return a[0];                                             /* TODO: 가운데 값. 짝수 n 은 두 값의 평균(넘침 없이) */
}

/* floor(ticks * 1e9 / freq) 를 정확히. 결과가 2^64 를 넘으면 UINT64_MAX. freq==0 → 0. 전제: freq < 2^34 */
uint64_t ticks_to_ns(uint64_t ticks, uint64_t freq_hz) {
    (void)freq_hz;
    return ticks;                                            /* TODO: ticks*1e9/freq 를 넘침 없이 (몫/나머지로 쪼개기) */
}

/* floor(ns * 1000 / nops) = 명령 1개당 피코초. nops==0 → 0. 넘치면 UINT64_MAX. 전제: nops < 2^54 */
uint64_t per_op_ps(uint64_t ns, uint64_t nops) {
    (void)nops;
    return ns;                                               /* TODO: ns*1000/nops, 넘침 없이 + 포화 */
}

/* round(1000 * ps_op / ps_cycle) = 밀리사이클 (1 사이클 = 1000). ps_cycle==0 → 0. 전제: 입력 < 2^50 */
uint64_t mcycles(uint64_t ps_op, uint64_t ps_cycle) {
    (void)ps_cycle;
    return ps_op;                                            /* TODO: 반올림 나눗셈 */
}

/* ================= Part B: 배관 ================= */

/* reps 번: t0=clk(); r=k(iters,x,y); t1=clk(); out[i]=t1-t0 (wrap 안전한 unsigned 뺄셈); g_sink ^= r */
void bench_measure(clock_fn clk, ikern_fn k, uint64_t iters, uint64_t x, uint64_t y,
                   uint64_t *out, size_t reps) {
    (void)clk; (void)k; (void)iters; (void)x; (void)y; (void)out; (void)reps;
    /* TODO: reps 번 { t0 = clk(); r = k(...); t1 = clk(); out[i] = t1 - t0; g_sink ^= r; } */
}

/* raw[i] - overhead (0 에서 멈춤) 를 scratch 에 담아 min/중앙값/max, 그리고
 * outliers = 중앙값의 2배를 넘는 샘플 수 (인터럽트·문맥 전환에 맞은 샘플). n==0 → 전부 0 */
void bench_summarize(const uint64_t *raw, size_t n, uint64_t overhead, uint64_t *scratch,
                     struct bench_stats *st) {
    memset(st, 0, sizeof *st);
    (void)raw; (void)n; (void)overhead; (void)scratch;
    /* TODO: scratch[i] = raw[i] - overhead (클램프) → 중앙값/min/max, 2*med 초과 개수 */
}

/* ================= Part C: 해석 ================= */

/* 지연 L, 처리량 역수 T 인 명령으로 유닛을 꽉 채우는 데 필요한 독립 사슬 수 = ceil(L / T) (Little 의 법칙) */
uint64_t chains_needed(uint64_t lat_mcyc, uint64_t rtp_mcyc) {
    (void)rtp_mcyc;
    return lat_mcyc;                                         /* TODO: ceil(L / T), T==0 → 0 */
}

/* ---------- 이 아래는 starter 와 같다 (테스트) ---------- */
#ifndef BENCH_NO_MAIN
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; printf("PASS  %s\n", #expr); } \
    else printf("FAIL  %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

typedef unsigned __int128 u128;
static uint64_t g_rng = 0x9E3779B97F4A7C15ull;
static uint64_t rnd(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 7; g_rng ^= g_rng << 17; return g_rng; }
static uint64_t rnd_mag(void) { uint64_t r = rnd(); return r >> (rnd() & 63); }
static double   bits2d(uint64_t b) { double d; memcpy(&d, &b, sizeof d); return d; }
static int      same_d(double a, double b) { return memcmp(&a, &b, sizeof a) == 0; }   /* 비트 단위 */

/* ----- 참조 모델 (u128 로 넘침 없이) ----- */
static uint64_t sat64(u128 v) { return v > UINT64_MAX ? UINT64_MAX : (uint64_t)v; }
static uint64_t ref_ticks_to_ns(uint64_t t, uint64_t f) { return f ? sat64((u128)t * 1000000000u / f) : 0; }
static uint64_t ref_per_op_ps(uint64_t ns, uint64_t n) { return n ? sat64((u128)ns * 1000u / n) : 0; }
static int cmp_u64(const void *a, const void *b) {
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b; return (x > y) - (x < y); }
static uint64_t ref_median(const uint64_t *a, size_t n) {
    if (!n) return 0;
    uint64_t *c = malloc(n * sizeof *c); memcpy(c, a, n * sizeof *c); qsort(c, n, sizeof *c, cmp_u64);
    uint64_t m = (uint64_t)(((u128)c[(n - 1) / 2] + c[n / 2]) / 2); free(c); return m;
}

/* ----- 심어 둔 버그 3개 (테스트가 잡아야 한다) ----- */
static uint64_t median2_naive(uint64_t a, uint64_t b) { return (a + b) / 2; }          /* 합이 넘친다 */
static uint64_t ticks_to_ns_naive(uint64_t t, uint64_t f) { return t * 1000000000u / f; } /* 곱이 넘친다 */
static uint64_t sub_naive(uint64_t a, uint64_t b) { return a - b; }                      /* 음수 → 2^64 근처 */

/* ----- 가짜 시계 + 가짜 커널: 배관을 결정적으로 검사 ----- */
static const uint64_t *g_fake_seq; static size_t g_fake_pos;
static uint64_t fake_clock(void) { return g_fake_seq[g_fake_pos++]; }
static size_t g_kcalls; static size_t g_kcall_clockpos[8]; static uint64_t g_karg[3];
static uint64_t fake_kernel(uint64_t it, uint64_t x, uint64_t y) {
    if (g_kcalls < 8) g_kcall_clockpos[g_kcalls] = g_fake_pos;    /* 커널 호출 시점의 시계 읽기 횟수 */
    g_kcalls++; g_karg[0] = it; g_karg[1] = x; g_karg[2] = y;
    return 0x100u << g_kcalls;
}

static uint64_t g_slot[3];                                         /* 포인터 추적용 */

int main(void) {
    int bad;
    /* ===== Part A: 카운터와 커널 ===== */
    uint64_t f = cnt_freq();
    printf("INFO  cntfrq_el0 = %" PRIu64 " Hz\n", f);
    CHECK(f >= 1000000u);                                          /* 최소 1 MHz (Arm 권장 1–50 MHz, v8.6+ 1 GHz) */
    {
        uint64_t prev = cnt_read(); bad = 0;
        for (int i = 0; i < 10000; i++) { uint64_t t = cnt_read(); if (t < prev) bad++; prev = t; }
        CHECK(bad == 0);                                           /* 단조 증가 (같은 값 반복은 허용) */
        uint64_t t0 = cnt_read(); g_sink ^= lat_add(100000, 0, 0); uint64_t t1 = cnt_read();
        CHECK(t1 > t0);                                            /* 160만 add 동안 카운터가 움직였다 */
    }
    static const uint64_t ITS[] = {0, 1, 2, 7, 100};
    for (size_t k = 0; k < sizeof ITS / sizeof ITS[0]; k++) {
        uint64_t it = ITS[k], x = rnd(), y = rnd() | 1, n = it * OPS_PER_ITER;
        CHECK(lat_add(it, x, y) == x + n);
        CHECK(tput_add(it, x, y) == 16u * (x + it));
        uint64_t m = x, a = x, acc = x, mt = 0;
        for (uint64_t i = 0; i < n; i++) { m *= y; a = a * y + y; acc += y * y; }
        for (uint64_t c = 0; c < 16; c++) { uint64_t v = x + c; for (uint64_t i = 0; i < it; i++) v *= y; mt ^= v; }
        CHECK(lat_mul(it, x, y) == m && tput_mul(it, x, y) == mt);
        CHECK(lat_madd(it, x, y) == a && lat_madd_acc(it, x, y) == acc);
        int64_t sx = (int64_t)(x >> 1), sd = (int64_t)(rnd_mag() >> 2) + 1, q = sx;
        uint64_t qs = 0;  /* sd ≥ 1: UB 없음 */
        uint64_t uq = x, ud = rnd_mag() | 1;
        for (uint64_t i = 0; i < n; i++) { q /= sd; uq /= ud; }
        for (uint64_t c = 0; c < 16; c++) { int64_t v = sx; for (uint64_t i = 0; i < it; i++) v /= sd; qs += (uint64_t)v; }
        CHECK(lat_sdiv(it, (uint64_t)sx, (uint64_t)sd) == (uint64_t)q && lat_udiv(it, x, ud) == uq);
        CHECK(tput_sdiv(it, (uint64_t)sx, (uint64_t)sd) == qs);
        /* 포인터 추적: 3칸 고리 a→b→c→a. 16*it 번 따라가면 (16*it mod 3) 칸 앞 */
        g_slot[0] = (uint64_t)&g_slot[1]; g_slot[1] = (uint64_t)&g_slot[2]; g_slot[2] = (uint64_t)&g_slot[0];
        CHECK(lat_ldr(it, (uint64_t)&g_slot[0], 0) == (uint64_t)&g_slot[n % 3]);
        uint64_t arr[16], sum = 0;
        for (int i = 0; i < 16; i++) { arr[i] = rnd(); sum += arr[i]; }
        CHECK(tput_ldr(it, (uint64_t)arr, 0) == (it ? sum : 0));
        double fx = 1.0 + (double)(x >> 12) * 0x1p-52, fy = 0.5 + (double)(y >> 12) * 0x1p-53;
        double ra = fx, rm = fx, rf = fx, c1 = fx, c2 = fx, c3 = fx;
        for (uint64_t i = 0; i < n; i++) { ra += fy; rm *= fy; rf = fma(rf, fy, fy); }
        for (uint64_t i = 0; i < it; i++) { c1 += fy; c2 *= fy; c3 = fma(c3, fy, fy); }
        double s1 = c1, s2 = c2, s3 = c3;                          /* 16사슬이 같은 값 → 합도 같은 순서로 */
        for (int i = 1; i < 16; i++) { s1 += c1; s2 += c2; s3 += c3; }
        CHECK(same_d(lat_fadd(it, fx, fy), ra) && same_d(tput_fadd(it, fx, fy), s1));
        CHECK(same_d(lat_fmul(it, fx, fy), rm) && same_d(tput_fmul(it, fx, fy), s2));
        CHECK(same_d(lat_fmadd(it, fx, fy), rf) && same_d(tput_fmadd(it, fx, fy), s3));
    }
    CHECK(k_empty(5, 42, 0) == 42 && k_empty(0, 7, 0) == 7);
    { uint64_t self = 0; self = (uint64_t)&self; CHECK(lat_ldr(1000, self, 0) == self); }  /* L1 hit 사슬 */
    CHECK(same_d(lat_fadd(3, bits2d(0x7FF0000000000000ull), -1.0), bits2d(0x7FF0000000000000ull)));  /* inf */

    /* ===== Part B: 수학 ===== */
    CHECK(u64_sub_clamp(5, 3) == 2 && u64_sub_clamp(3, 5) == 0 && u64_sub_clamp(0, UINT64_MAX) == 0);
    CHECK(sub_naive(3, 5) == UINT64_MAX - 1);                      /* 버그 1: 클램프 없으면 2^64-2 */
    {
        uint64_t e0[1] = {0}, e1[] = {5}, e3[] = {3, 1, 2}, e4[] = {4, 1, 3, 2};
        CHECK(u64_median(e0, 0) == 0 && u64_median(e1, 1) == 5);
        CHECK(u64_median(e3, 3) == 2 && e3[0] == 1 && e3[2] == 3);  /* 제자리 정렬됨 */
        CHECK(u64_median(e4, 4) == 2);                             /* (2+3)/2 내림 */
        uint64_t big[] = {UINT64_MAX, UINT64_MAX - 2};
        CHECK(u64_median(big, 2) == UINT64_MAX - 1);
        CHECK(median2_naive(UINT64_MAX, UINT64_MAX - 2) != UINT64_MAX - 1);   /* 버그 2: 합 넘침 */
        uint64_t buf[257]; bad = 0;
        for (int t = 0; t < 2000; t++) {
            size_t n = (size_t)(rnd() % 257);
            for (size_t i = 0; i < n; i++) buf[i] = (t & 1) ? rnd() : rnd() % 50;   /* 중복값도 */
            uint64_t want = ref_median(buf, n);
            if (u64_median(buf, n) != want) bad++;
            for (size_t i = 1; i < n; i++) if (buf[i - 1] > buf[i]) { bad++; break; }
        }
        CHECK(bad == 0);
    }
    CHECK(ticks_to_ns(24000000u, 24000000u) == 1000000000u);
    CHECK(ticks_to_ns(1, 24000000u) == 41);                        /* 41.67 ns 내림 */
    CHECK(ticks_to_ns(1ull << 40, 24000000u) == 45812984490666u);  /* 2^40 tick ≈ 12.7 시간 */
    CHECK(ticks_to_ns(UINT64_MAX, 1000000000u) == UINT64_MAX);     /* 1 GHz: tick == ns */
    CHECK(ticks_to_ns(UINT64_MAX, 1) == UINT64_MAX);               /* 포화 */
    CHECK(ticks_to_ns(123, 0) == 0);
    CHECK(ticks_to_ns_naive(1ull << 40, 24000000u) != 45812984490666u);  /* 버그 3: t*1e9 넘침 */
    {
        static const uint64_t FR[] = {1, 32768, 19200000, 24000000, 1000000000, 4294967295u, (1ull << 34) - 1};
        bad = 0;
        for (int t = 0; t < 200000; t++) {
            uint64_t tk = rnd_mag(), fr = FR[rnd() % (sizeof FR / sizeof FR[0])];
            if (ticks_to_ns(tk, fr) != ref_ticks_to_ns(tk, fr)) bad++;
            uint64_t ns = rnd_mag(), no = (rnd_mag() >> 10) + (rnd() & 1);
            if (per_op_ps(ns, no) != ref_per_op_ps(ns, no)) bad++;
        }
        CHECK(bad == 0);
    }
    CHECK(per_op_ps(1000, 16) == 62500 && per_op_ps(5, 0) == 0 && per_op_ps(UINT64_MAX, 1) == UINT64_MAX);
    CHECK(per_op_ps(1, 3) == 333);
    CHECK(mcycles(250, 250) == 1000 && mcycles(750, 250) == 3000 && mcycles(1, 0) == 0);
    CHECK(mcycles(1, 3) == 333 && mcycles(2, 3) == 667);          /* 반올림 */
    {
        bad = 0;
        for (int t = 0; t < 100000; t++) {
            uint64_t a = rnd() >> 14, c = (rnd() >> 14) + 1;
            if (mcycles(a, c) != (uint64_t)(((u128)a * 1000 + c / 2) / c)) bad++;
        }
        CHECK(bad == 0);
    }

    /* ===== Part B: 배관 (가짜 시계) ===== */
    {
        static const uint64_t seq[] = {100, 130, 200, 229, UINT64_MAX - 4, 10, 500, 500};
        uint64_t out[4] = {0}; g_fake_seq = seq; g_fake_pos = 0; g_kcalls = 0; g_sink = 0;
        bench_measure(fake_clock, fake_kernel, 7, 8, 9, out, 4);
        CHECK(out[0] == 30 && out[1] == 29);
        CHECK(out[2] == 15);                                       /* 카운터 wrap 도 unsigned 뺄셈이면 OK */
        CHECK(out[3] == 0);
        CHECK(g_fake_pos == 8 && g_kcalls == 4);                   /* 시계는 반복당 정확히 2번 */
        CHECK(g_kcall_clockpos[0] == 1 && g_kcall_clockpos[3] == 7);  /* 커널은 두 읽기 사이 */
        CHECK(g_karg[0] == 7 && g_karg[1] == 8 && g_karg[2] == 9);
        CHECK(g_sink == (0x200u ^ 0x400u ^ 0x800u ^ 0x1000u));    /* 결과가 싱크에 모였다 */
        g_fake_pos = 0; g_kcalls = 0;
        bench_measure(fake_clock, fake_kernel, 1, 2, 3, out, 0);
        CHECK(g_fake_pos == 0 && g_kcalls == 0);                   /* reps=0 → 아무것도 안 함 */
    }
    {
        uint64_t raw[] = {130, 100, 105, 400, 101}, sc[5]; struct bench_stats st;
        bench_summarize(raw, 5, 100, sc, &st);
        CHECK(st.min == 0 && st.med == 5 && st.max == 300 && st.outliers == 2);
        CHECK(raw[0] == 130);                                      /* 원본은 안 건드린다 */
        bench_summarize(raw, 5, 1000, sc, &st);
        CHECK(st.min == 0 && st.med == 0 && st.max == 0 && st.outliers == 0);
        st.min = 9; bench_summarize(raw, 0, 0, sc, &st);
        CHECK(st.min == 0 && st.med == 0 && st.max == 0 && st.outliers == 0);
        uint64_t hu[] = {UINT64_MAX, UINT64_MAX - 1, 3};
        bench_summarize(hu, 3, 0, sc, &st);
        CHECK(st.med == UINT64_MAX - 1 && st.outliers == 0);      /* 2*med 를 계산했다면 넘침 */
    }
    /* 실제 시계로 배관 한 바퀴: 값은 판정하지 않고 "움직였다"만 */
    {
        uint64_t out[5] = {0}, sc[5]; struct bench_stats st; g_sink = 0;
        bench_measure(cnt_read, lat_add, 20000, 1, 0, out, 5);
        bench_summarize(out, 5, 0, sc, &st);
        CHECK(st.min > 0 && g_sink == (1u + 20000u * 16u));       /* 5번 XOR → 홀수 번이라 한 번 값 */
    }

    /* ===== Part C: 해석 ===== */
    CHECK(chains_needed(3000, 250) == 12);                        /* FADD 지연 3, 클럭당 4개 → 12사슬 */
    CHECK(chains_needed(1000, 125) == 8 && chains_needed(4000, 1000) == 4);
    CHECK(chains_needed(3100, 1000) == 4 && chains_needed(5, 0) == 0);
    {   /* 합성 측정 → 표 한 줄: 1 GHz 카운터, 16만 add = 40000 tick → 250 ps/add = 1 사이클 기준 */
        uint64_t nops = 10000u * OPS_PER_ITER;
        uint64_t ps_add = per_op_ps(ticks_to_ns(40000, 1000000000u), nops);
        uint64_t ps_mul = per_op_ps(ticks_to_ns(120000, 1000000000u), nops);
        CHECK(ps_add == 250 && ps_mul == 750 && mcycles(ps_mul, ps_add) == 3000);
        uint64_t ps24 = per_op_ps(ticks_to_ns(960, 24000000u), nops);  /* 24 MHz 카운터로 같은 40 us */
        CHECK(ps24 == 250);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
#endif /* BENCH_NO_MAIN */
