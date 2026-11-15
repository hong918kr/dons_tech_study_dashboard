/* OS-B03 시스템 콜 비용 측정: 사용자↔커널 경계
 *
 *   Part A  측정 통계 도구: tick → ns 변환(오버플로 없이), median, nearest-rank 백분위수,
 *           루프 오버헤드 빼기, run 여러 개 요약(min / median / p90 / max)
 *   Part B  "정말 커널에 들어갔나?" — task_info(TASK_EVENTS_INFO) 의 syscalls_unix / syscalls_mach
 *           카운터로 호출 n 번당 커널 진입 수를 센다 (macOS 판 strace -c). 그 비율로 호출을 분류.
 *   Part C  예산 계산: 타이머 눈금에 맞는 반복 횟수, 센서 읽기 배치 크기 (CPU 예산 vs 지연 한도)
 *
 * 체크는 매번 같은 결과가 나오는 것만 본다 (고정 입력의 계산, 커널 진입 "횟수").
 * 시간(ns) 실측은 demo_bench.c / demo_pollute.c 에 있다 — 시간은 CHECK 하지 않는다.
 * macOS 전용 (mach/task_info). 리눅스에서는 strace -c 가 Part B 의 역할을 한다.
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so
 */
#define _DARWIN_C_SOURCE
#include <fcntl.h>
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* ======================= Part A — 측정 통계 도구 ======================= */

/* mach_absolute_time 의 tick → ns. ns = ticks × numer / denom (Apple Silicon: 125/3, Intel Mac: 1/1).
 * ticks × numer 를 그대로 곱하면 2^64 를 넘을 수 있다 (125 배 → 2^64/125 ≈ 1.5e17 tick 부터).
 * 몫과 나머지로 나눠 계산해 정확히 floor(ticks × numer / denom) 을 돌려준다. denom == 0 이면 0. */
static uint64_t ticks_to_ns(uint64_t ticks, uint32_t numer, uint32_t denom) {
    if (denom == 0) return 0;
    uint64_t q = ticks / denom, r = ticks % denom;     /* ticks = q·denom + r,  r < denom */
    return q * numer + r * numer / denom;              /* r·numer < 2^32 · 2^32 → 넘치지 않음 */
}

/* a[0..n) 을 제자리 정렬한 뒤 중앙값. n 이 짝수면 가운데 두 값의 평균. n == 0 이면 NAN. */
static double median(double *a, size_t n) {
    if (n == 0) return NAN;
    for (size_t i = 1; i < n; i++) {                   /* 삽입 정렬: run 수는 많아야 수십 개 */
        double x = a[i];
        size_t j = i;
        while (j > 0 && a[j - 1] > x) { a[j] = a[j - 1]; j--; }
        a[j] = x;
    }
    return (n % 2) ? a[n / 2] : (a[n / 2 - 1] + a[n / 2]) / 2.0;
}

/* 정렬된 s[0..n) 의 nearest-rank 백분위수: rank = ceil(p/100 × n) 을 [1, n] 로 자른 뒤 s[rank-1].
 * p ∈ [0, 100]. n == 0 이면 NAN. (p = 50 이고 n 이 짝수면 median() 과 다르다 — 아래쪽 값) */
static double percentile_nr(const double *s, size_t n, double p) {
    if (n == 0) return NAN;
    double r = ceil(p * (double)n / 100.0);            /* p·n 먼저: 90·10/100 = 9 정확히 */
    size_t rank = r < 1.0 ? 1 : (r > (double)n ? n : (size_t)r);
    return s[rank - 1];
}

/* 배치 하나의 결과 → 호출 1 회의 순수 비용.
 * batch_ns / n 에서 빈 루프 1 회 비용 loop_ns 를 뺀다. 음수(잡음)는 0 으로. n == 0 이면 0. */
static double net_ns_per_call(double batch_ns, uint64_t n, double loop_ns) {
    if (n == 0) return 0.0;
    double v = batch_ns / (double)n - loop_ns;
    return v > 0.0 ? v : 0.0;
}

typedef struct {
    double min, median, p90, max;
} stats_t;

/* run 별 배치 시간 batch_ns[0..runs) (각 배치는 n 회 호출) → 호출당 순수 ns 로 바꾼 뒤 요약.
 * 순서: 각 run 을 net_ns_per_call 로 변환 → 정렬 → min, median, p90(nearest-rank), max.
 * batch_ns 는 바꾸지 않는다. runs == 0 이거나 runs > 64 면 전부 NAN. */
static stats_t summarize(const double *batch_ns, size_t runs, uint64_t n, double loop_ns) {
    stats_t s = {NAN, NAN, NAN, NAN};
    if (runs == 0 || runs > 64) return s;
    double v[64];
    for (size_t i = 0; i < runs; i++) v[i] = net_ns_per_call(batch_ns[i], n, loop_ns);
    s.median = median(v, runs);                        /* v 를 정렬까지 해 준다 */
    s.min = v[0];
    s.max = v[runs - 1];
    s.p90 = percentile_nr(v, runs, 90.0);
    return s;
}

/* ======================= Part B — 커널 진입 횟수 세기 ======================= */

typedef struct {
    int64_t unix_calls;    /* BSD 시스템 콜 (svc #0x80, x16 = 양수 번호): read, getppid, ... */
    int64_t mach_calls;    /* Mach 트랩 (x16 = 음수 번호): mach_msg, mach_port_deallocate, ... */
} sc_count_t;

/* 이 태스크(프로세스)의 누적 카운터를 읽는다 — 하네스 제공.
 * task_info 자체도 Mach 트랩 1 회(mach_msg)라서 두 번 읽은 차이에 mach +1 이 섞인다. */
static int sc_read(sc_count_t *out) {
    task_events_info_data_t e;
    mach_msg_type_number_t cnt = TASK_EVENTS_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_EVENTS_INFO, (task_info_t)&e, &cnt) != KERN_SUCCESS) return -1;
    out->unix_calls = e.syscalls_unix;
    out->mach_calls = e.syscalls_mach;
    return 0;
}

typedef void (*call_fn)(void);

/* fn 을 n 번 부르는 동안 늘어난 unix / mach 진입 수를 *d 에 (sc_read 두 번의 차이).
 * 성공 0, sc_read 실패나 n <= 0 이면 -1. */
static int count_syscalls(call_fn fn, int n, sc_count_t *d) {
    if (n <= 0 || !d) return -1;
    sc_count_t a, b;
    if (sc_read(&a) != 0) return -1;
    for (int i = 0; i < n; i++) fn();
    if (sc_read(&b) != 0) return -1;
    d->unix_calls = b.unix_calls - a.unix_calls;
    d->mach_calls = b.mach_calls - a.mach_calls;
    return 0;
}

typedef enum { KIND_UNKNOWN, KIND_USER_ONLY, KIND_UNIX_TRAP, KIND_MACH_TRAP, KIND_PARTIAL } call_kind_t;

/* 호출 n 번 동안의 진입 수 d 로 분류한다.
 *   unix ≥ 0.95 n            → KIND_UNIX_TRAP   (매번 BSD 시스템 콜)
 *   그 외 mach ≥ 0.95 n      → KIND_MACH_TRAP   (매번 Mach 트랩)
 *   unix + mach ≤ n / 100 + 2 → KIND_USER_ONLY  (커널에 안 들어감. +2 는 sc_read 자체의 mach 트랩 등)
 *   나머지                    → KIND_PARTIAL     (가끔만 들어감: 캐시 미스 때만 등)
 * n <= 0 이면 KIND_UNKNOWN. */
static call_kind_t classify(const sc_count_t *d, int n) {
    if (n <= 0 || !d) return KIND_UNKNOWN;
    if (d->unix_calls * 100 >= (int64_t)n * 95) return KIND_UNIX_TRAP;   /* 정수로: ≥ 0.95 n */
    if (d->mach_calls * 100 >= (int64_t)n * 95) return KIND_MACH_TRAP;
    if (d->unix_calls + d->mach_calls <= n / 100 + 2) return KIND_USER_ONLY;
    return KIND_PARTIAL;
}

/* ======================= Part C — 예산 계산 ======================= */

/* 눈금 tick_ps(피코초)인 타이머로 호출당 est_ps 짜리 대상을 잴 때, 배치 하나의 양자화 오차
 * (최대 1 tick)가 배치 시간의 err_ppm(백만분율) 이하가 되는 최소 반복 수.
 *   n ≥ tick_ps × 1e6 / (est_ps × err_ppm)  →  정수 올림 나눗셈. 입력 중 0 이 있으면 0. */
static uint64_t iters_for_resolution(uint64_t tick_ps, uint64_t est_ps, uint32_t err_ppm) {
    if (tick_ps == 0 || est_ps == 0 || err_ppm == 0) return 0;
    uint64_t num = tick_ps * 1000000u, den = est_ps * err_ppm;
    return (num + den - 1) / den;                      /* 올림 나눗셈 */
}

/* 센서를 sample_hz 로 받는데, 샘플 batch 개를 한 번에 읽을 때마다 시스템 콜 syscalls_per_read 개
 * (예: poll + read), 시스템 콜 하나에 ns_per_syscall ns. 1 초 동안 시스템 콜에 쓰는 CPU 시간(ns).
 *   = sample_hz × syscalls_per_read × ns_per_syscall / batch   (정수 올림). batch == 0 이면 UINT64_MAX. */
static uint64_t syscall_ns_per_sec(uint32_t sample_hz, uint32_t batch, uint32_t syscalls_per_read,
                                   uint32_t ns_per_syscall) {
    if (batch == 0) return UINT64_MAX;
    uint64_t num = (uint64_t)sample_hz * syscalls_per_read * ns_per_syscall;   /* 32×32×32 → 64 비트로 먼저 */
    return (num + batch - 1) / batch;
}

/* 위 비용이 budget_ns_per_sec 이하가 되는 최소 batch (≥ 1). budget == 0 이면 0 (불가능). */
static uint32_t min_batch_for_budget(uint32_t sample_hz, uint32_t syscalls_per_read,
                                     uint32_t ns_per_syscall, uint64_t budget_ns_per_sec) {
    if (budget_ns_per_sec == 0) return 0;
    uint64_t num = (uint64_t)sample_hz * syscalls_per_read * ns_per_syscall;
    uint64_t b = (num + budget_ns_per_sec - 1) / budget_ns_per_sec;   /* num / b ≤ budget 인 최소 b */
    if (b == 0) b = 1;
    return b > UINT32_MAX ? 0 : (uint32_t)b;
}

/* batch 개를 모으는 동안 첫 샘플이 기다리는 시간 ≈ batch / sample_hz. 이것이 max_latency_us 이하인
 * 최대 batch = floor(sample_hz × max_latency_us / 1e6). (0 일 수 있다 = 지연 한도가 샘플 간격보다 짧음) */
static uint32_t max_batch_for_latency(uint32_t sample_hz, uint32_t max_latency_us) {
    return (uint32_t)((uint64_t)sample_hz * max_latency_us / 1000000u);
}

/* 예산과 지연을 둘 다 만족하는 가장 작은 batch. 없으면 0. (지연은 작을수록 좋으니 최소값) */
static uint32_t plan_batch(uint32_t sample_hz, uint32_t syscalls_per_read, uint32_t ns_per_syscall,
                           uint64_t budget_ns_per_sec, uint32_t max_latency_us) {
    uint32_t lo = min_batch_for_budget(sample_hz, syscalls_per_read, ns_per_syscall, budget_ns_per_sec);
    uint32_t hi = max_batch_for_latency(sample_hz, max_latency_us);
    return (lo != 0 && lo <= hi) ? lo : 0;
}

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static bool near(double a, double b) { return fabs(a - b) <= 1e-9 * (fabs(b) > 1 ? fabs(b) : 1); }

/* Part B 에서 n 번 부를 대상들 (반환값은 sink 에 더해 지워지지 않게) */
static int g_fd = -1;
static volatile uint64_t g_sink;
static void c_noop(void)    { __asm__ volatile("" ::: "memory"); }
static void c_getpid(void)  { g_sink += (uint64_t)getpid(); }
static void c_getppid(void) { g_sink += (uint64_t)getppid(); }
static void c_read0(void)   { char b; g_sink += (uint64_t)read(g_fd, &b, 0); }
static void c_write1(void)  { g_sink += (uint64_t)write(g_fd, "x", 1); }
static void c_cg_mono(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); g_sink += (uint64_t)ts.tv_nsec; }
static void c_cg_real(void) { struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts); g_sink += (uint64_t)ts.tv_nsec; }
static void c_cg_thr(void)  { struct timespec ts; clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts); g_sink += (uint64_t)ts.tv_nsec; }
static void c_mach_abs(void){ g_sink += mach_absolute_time(); }
static void c_mach_port(void) {                    /* Mach 트랩 2 개: 포트 이름 얻기 + 참조 반납 */
    mach_port_t p = mach_thread_self();
    mach_port_deallocate(mach_task_self(), p);
}

static call_kind_t kind_of(call_fn fn, int n) {
    sc_count_t d = {0, 0};
    if (count_syscalls(fn, n, &d) != 0) return KIND_UNKNOWN;
    return classify(&d, n);
}

int main(void) {
    /* ---------- Part A ---------- */
    CHECK(ticks_to_ns(24000000u, 125, 3) == 1000000000u);          /* 24 MHz 카운터 1 초 */
    CHECK(ticks_to_ns(1, 125, 3) == 41u && ticks_to_ns(3, 125, 3) == 125u);
    CHECK(ticks_to_ns(123456789u, 1, 1) == 123456789u);             /* Intel Mac: 1 tick = 1 ns */
    CHECK(ticks_to_ns(1ull << 58, 125, 3) == 12009599006321322666ull); /* 2^58 × 125 는 64 비트를 넘는다 */
    CHECK(ticks_to_ns(UINT64_MAX / 125, 125, 3) == 6148914691236517166ull);
    CHECK(ticks_to_ns(5, 125, 0) == 0u);

    double odd[] = {5, 1, 4, 2, 3};
    CHECK(near(median(odd, 5), 3.0) && odd[0] == 1 && odd[4] == 5);  /* 제자리 정렬 */
    double even[] = {10, 40, 20, 30};
    CHECK(near(median(even, 4), 25.0));
    double one[] = {7.5};
    CHECK(near(median(one, 1), 7.5) && isnan(median(one, 0)));

    double s10[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    CHECK(near(percentile_nr(s10, 10, 90), 9.0) && near(percentile_nr(s10, 10, 91), 10.0));
    CHECK(near(percentile_nr(s10, 10, 0), 1.0) && near(percentile_nr(s10, 10, 100), 10.0));
    CHECK(near(percentile_nr(s10, 4, 50), 2.0));                     /* median() 이면 2.5 */
    CHECK(isnan(percentile_nr(s10, 0, 50)));
    static double s1000[1000];
    for (int i = 0; i < 1000; i++) s1000[i] = i + 1;
    CHECK(near(percentile_nr(s1000, 1000, 99), 990.0) && near(percentile_nr(s1000, 1000, 99.9), 999.0));

    CHECK(near(net_ns_per_call(1050e6, 10000000u, 5.0), 100.0));     /* 105 ns − 루프 5 ns */
    CHECK(net_ns_per_call(2.0e6, 10000000u, 0.25) == 0.0);           /* 0.2 − 0.25 < 0 → 0 */
    CHECK(net_ns_per_call(1e6, 0, 0.0) == 0.0);

    /* run 7 개, 각 배치 1e6 회. 하나는 인터럽트에 얻어맞은 run (230 ns) */
    double runs[] = {105e6, 101e6, 230e6, 103e6, 102e6, 104e6, 101e6};
    stats_t st = summarize(runs, 7, 1000000u, 1.0);
    CHECK(near(st.min, 100.0) && near(st.median, 102.0) && near(st.max, 229.0));
    CHECK(near(st.p90, 229.0));                                       /* ceil(0.9 × 7) = 7 번째 */
    CHECK(runs[2] == 230e6);                                          /* 입력은 그대로 */
    stats_t bad = summarize(runs, 0, 1000000u, 1.0);
    CHECK(isnan(bad.min) && isnan(bad.median) && isnan(bad.p90) && isnan(bad.max));

    /* ---------- Part B (macOS: 호출 n 번당 커널 진입 수) ---------- */
    g_fd = open("/dev/null", O_RDWR);
    CHECK(g_fd >= 0);
    const int N = 20000;
    sc_count_t d = {-1, -1};
    CHECK(count_syscalls(c_getppid, N, &d) == 0 && d.unix_calls == N);   /* 정확히 n 번 */
    CHECK(count_syscalls(c_noop, N, &d) == 0 && d.unix_calls == 0 && d.mach_calls <= 2);
    CHECK(count_syscalls(c_noop, 0, &d) == -1);

    CHECK(kind_of(c_noop, N) == KIND_USER_ONLY);
    CHECK(kind_of(c_getppid, N) == KIND_UNIX_TRAP);
    CHECK(kind_of(c_read0, N) == KIND_UNIX_TRAP);        /* 0 바이트여도 커널까지 간다 */
    CHECK(kind_of(c_write1, N) == KIND_UNIX_TRAP);
    CHECK(kind_of(c_getpid, N) == KIND_USER_ONLY);       /* macOS libc 가 pid 를 캐시한다! */
    CHECK(kind_of(c_cg_mono, N) == KIND_USER_ONLY);      /* commpage 에서 읽는다 */
    CHECK(kind_of(c_cg_real, N) == KIND_USER_ONLY);
    CHECK(kind_of(c_mach_abs, N) == KIND_USER_ONLY);
    CHECK(kind_of(c_cg_thr, N) == KIND_UNIX_TRAP);       /* 스레드 CPU 시간은 커널만 안다 */
    CHECK(kind_of(c_mach_port, N) == KIND_MACH_TRAP);

    sc_count_t fake = {37, 0};                           /* 1000 번 중 37 번만 진입 */
    CHECK(classify(&fake, 1000) == KIND_PARTIAL);
    fake = (sc_count_t){0, 960};
    CHECK(classify(&fake, 1000) == KIND_MACH_TRAP);
    fake = (sc_count_t){0, 12};                          /* 10 + 2 이하 = 사용자 공간 */
    CHECK(classify(&fake, 1000) == KIND_USER_ONLY && classify(&fake, 0) == KIND_UNKNOWN);
    close(g_fd);

    /* ---------- Part C ---------- */
    CHECK(iters_for_resolution(41667, 250, 100) == 1666680u);   /* 41.667 ns 눈금, 0.25 ns 대상, 100 ppm */
    CHECK(iters_for_resolution(41667, 100000, 100) == 4167u);   /* 100 ns 대상: 4166.7 → 올림 */
    CHECK(iters_for_resolution(1000, 1000, 1000000) == 1u && iters_for_resolution(0, 1, 1) == 0u);

    /* 8 kHz IMU, 읽기마다 poll + read = 2 콜, 콜당 1500 ns (예시 값) */
    CHECK(syscall_ns_per_sec(8000, 1, 2, 1500) == 24000000u);   /* 2.4 % CPU */
    CHECK(syscall_ns_per_sec(8000, 7, 2, 1500) == 3428572u);    /* 3428571.4 → 올림 */
    CHECK(syscall_ns_per_sec(8000, 0, 2, 1500) == UINT64_MAX);
    CHECK(min_batch_for_budget(8000, 2, 1500, 5000000u) == 5u); /* 0.5 % 예산 */
    CHECK(min_batch_for_budget(8000, 2, 1500, 24000000u) == 1u);
    CHECK(min_batch_for_budget(8000, 2, 1500, 0) == 0u);
    CHECK(max_batch_for_latency(8000, 1000) == 8u && max_batch_for_latency(8000, 100) == 0u);
    CHECK(plan_batch(8000, 2, 1500, 5000000u, 1000) == 5u);
    CHECK(plan_batch(8000, 2, 1500, 1000000u, 1000) == 0u);     /* 0.1 % 는 24 필요 > 8 → 불가 */
    CHECK(plan_batch(4000000u, 2, 1500, 10000000u, 1000) == 1200u);  /* 큰 값도 오버플로 없이 */

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
