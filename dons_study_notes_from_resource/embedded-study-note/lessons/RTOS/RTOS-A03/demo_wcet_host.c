/* RTOS-A03 demo — 진짜 시간으로 재 보기 (macOS, mach_absolute_time). 결과는 매번 다르다 → CHECK 없음.
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 demo_wcet_host.c -o demo_wcet_host && ./demo_wcet_host
 * 측정 대상은 solution.c 의 ctrl_step 과 같은 구조, 크기만 키움 (LUT 256 구간, 재보정 표 4096 개 = 16 KB):
 * 호스트 시계 한 칸이 41.67 ns 라 원래 크기로는 거의 0~1 칸이라서.
 * 입력 클래스 6개 × warm(연속 호출) / cold(매 호출 전에 64 MB 를 읽어 캐시를 밀어냄).
 * 주의: 호스트 = OS 인터럽트·다른 프로세스·DVFS·P/E 코어 이동이 다 섞인다. 원리 확인용이지 MCU 의 WCET 가 아니다.
 */
#define _DARWIN_C_SOURCE
#include <mach/mach_time.h>
#include <pthread.h>
#include <pthread/qos.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ADC_MAX 4095
#define SPIKE_TH 1500
#define DEADBAND 8
#define OUT_LIM 1000
#define LUT_N 256
#define CAL_N 4096
#define FAULT_N 3

static int32_t g_bp[LUT_N], g_ff[LUT_N], g_cal[CAL_N];
typedef struct { int32_t kp_q8, ki_q8, integ, out, prev, cal_off, hist[5]; uint32_t hpos, bad_run; } ctrl_t;
static int32_t abs_i32(int32_t v) { return v < 0 ? -v : v; }

static void ctrl_init(ctrl_t *c) {
    memset(c, 0, sizeof *c);
    c->kp_q8 = 384; c->ki_q8 = 16; c->prev = 2048;
    for (int i = 0; i < 5; i++) c->hist[i] = 2048;
}
__attribute__((noinline)) static int32_t ctrl_step(ctrl_t *c, int32_t sp, int32_t meas) {
    int32_t m;
    if (meas < 0 || meas > ADC_MAX) {
        m = c->prev;
        if (++c->bad_run >= FAULT_N) {
            int64_t acc = 0;
            for (int i = 0; i < CAL_N; i++) acc += g_cal[i];
            c->cal_off = (int32_t)(acc / CAL_N); c->integ = 0; c->bad_run = 0;
            for (int i = 0; i < 5; i++) c->hist[i] = m;
        }
    } else { c->bad_run = 0; m = meas; }
    c->hist[c->hpos] = m; c->hpos = (c->hpos + 1) % 5;
    if (abs_i32(m - c->prev) > SPIKE_TH) {
        int32_t s[5]; memcpy(s, c->hist, sizeof s);
        for (int i = 1; i < 5; i++) { int32_t v = s[i]; int j = i - 1; while (j >= 0 && s[j] > v) { s[j + 1] = s[j]; j--; } s[j + 1] = v; }
        m = s[2];
    }
    c->prev = m;
    int32_t err = sp - (m - c->cal_off);
    if (abs_i32(err) <= DEADBAND) return c->out;
    int32_t spc = sp < 0 ? 0 : sp > ADC_MAX ? ADC_MAX : sp;
    int i = 0;
    while (i < LUT_N - 2 && spc >= g_bp[i + 1]) i++;
    int32_t b0 = g_bp[i], f0 = g_ff[i], ff = f0;
    if (spc != b0) ff = f0 + (g_ff[i + 1] - f0) * (spc - b0) / (g_bp[i + 1] - b0);
    int32_t u = (c->kp_q8 * err + c->integ) / 256 + ff;
    if (u > OUT_LIM || u < -OUT_LIM) u = u > 0 ? OUT_LIM : -OUT_LIM;
    else { c->integ += c->ki_q8 * err; if (c->integ > OUT_LIM * 256) c->integ = OUT_LIM * 256; if (c->integ < -OUT_LIM * 256) c->integ = -OUT_LIM * 256; }
    c->out = u;
    return u;
}

/* ---- 입력 클래스: 측정할 호출 직전까지 상태를 만들고 (sp, meas) 를 돌려준다 ---- */
enum { C_HOLD, C_NOMINAL, C_HIGHSP, C_SPIKE, C_RECAL, C_RANDOM, C_N };
static const char *cname[C_N] = {"hold", "nominal", "high_sp", "spike", "recal", "random"};
static uint32_t g_seed = 12345;
static uint32_t xs32(void) { uint32_t x = g_seed; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return g_seed = x; }
static volatile int32_t g_sink;
static void prepare(ctrl_t *c, int cls, int32_t *sp, int32_t *meas) {
    switch (cls) {
    case C_HOLD:    *sp = 2048; *meas = 2048 + (int32_t)(xs32() % 9) - 4 + c->cal_off; break;
    case C_NOMINAL: *sp = 1000 + (int32_t)(xs32() % 200); *meas = *sp - 300; break;
    case C_HIGHSP:  *sp = 4000 + (int32_t)(xs32() % 90); *meas = 2048; break;
    case C_SPIKE:   g_sink = ctrl_step(c, 1000, 1000); *sp = 1000; *meas = (xs32() & 1) ? 3900 : 50; break;
    case C_RECAL:   for (int k = 0; k < FAULT_N - 1; k++) g_sink = ctrl_step(c, 4000, 5000);
                    *sp = 4000; *meas = 5000; break;
    default:        *sp = (int32_t)(xs32() % 4096); *meas = (int32_t)(xs32() % 4096); break;
    }
}

/* ---- 캐시 밀어내기: 64 MB 를 128 B 줄마다 한 번씩 읽는다 ---- */
#define SWEEP_B (64u << 20)
static uint8_t *g_sweep;
static void evict(void) {
    uint32_t acc = 0;
    for (uint32_t i = 0; i < SWEEP_B; i += 128) acc += g_sweep[i];
    g_sink = (int32_t)acc;
}

static int cmp_u64(const void *a, const void *b) { uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b; return (x > y) - (x < y); }
static uint64_t nr(const uint64_t *s, uint32_t n, uint32_t q10k) { uint64_t r = ((uint64_t)q10k * n + 9999) / 10000; if (r < 1) r = 1; return s[r - 1]; }
static double g_ns_per_tick;

static void report(const char *tag, uint64_t *t, uint32_t n, int hist) {
    qsort(t, n, sizeof *t, cmp_u64);
    double sum = 0; for (uint32_t i = 0; i < n; i++) sum += (double)t[i];
    printf("%-8s n=%6u  min=%4llu p50=%4llu p99=%4llu p99.9=%5llu max=%6llu ticks | p50=%6.0f p99.9=%7.0f max=%8.0f ns  mean=%.2f\n",
           tag, n, (unsigned long long)t[0], (unsigned long long)nr(t, n, 5000),
           (unsigned long long)nr(t, n, 9900), (unsigned long long)nr(t, n, 9990), (unsigned long long)t[n - 1],
           (double)nr(t, n, 5000) * g_ns_per_tick, (double)nr(t, n, 9990) * g_ns_per_tick,
           (double)t[n - 1] * g_ns_per_tick, sum / n);
    if (hist > 0) {                              /* 고정 bin 히스토그램 (bin = hist ticks), 마지막 bin 은 그 이상 */
        uint32_t b[12] = {0}; uint32_t mx = 1;
        for (uint32_t i = 0; i < n; i++) { uint64_t k = t[i] / (uint64_t)hist; if (k > 11) k = 11; b[k]++; }
        for (int k = 0; k < 12; k++) if (b[k] > mx) mx = b[k];
        for (int k = 0; k < 12; k++) {
            if (!b[k]) continue;
            if (k == 11) printf("    [%3d, inf) %6u |", k * hist, b[k]);
            else         printf("    [%3d,%4d) %6u |", k * hist, (k + 1) * hist, b[k]);
            for (uint32_t j = 0; j < (b[k] * 40 + mx - 1) / mx; j++) putchar('#');
            putchar('\n');
        }
    }
}

#define NWARM 50000u
#define NCOLD 400u
static uint64_t g_t[NWARM];

static uint32_t measure(int cls, int cold, uint32_t n) {
    ctrl_t c; ctrl_init(&c);
    for (uint32_t k = 0; k < n; k++) {
        int32_t sp, meas;
        prepare(&c, cls, &sp, &meas);
        if (cold) evict();
        uint64_t t0 = mach_absolute_time();
        int32_t r = ctrl_step(&c, sp, meas);
        uint64_t t1 = mach_absolute_time();
        g_sink = r;
        g_t[k] = t1 - t0;
    }
    return n;
}

int main(int argc, char **argv) {
    (void)argv;
    mach_timebase_info_data_t tb; mach_timebase_info(&tb);
    g_ns_per_tick = (double)tb.numer / tb.denom;
    int qos = argc > 1 ? 0 : 1;                  /* 인자를 주면 QoS 설정 안 함 (비교용) */
    if (qos) pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
    double la[3] = {0}; getloadavg(la, 3);
    printf("timebase %u/%u -> %.2f ns/tick, QoS=%s, loadavg %.2f %.2f %.2f\n", tb.numer, tb.denom, g_ns_per_tick,
           qos ? "USER_INTERACTIVE" : "default", la[0], la[1], la[2]);
    for (int i = 0; i < LUT_N; i++) { g_bp[i] = i * ADC_MAX / (LUT_N - 1); g_ff[i] = i * 675 / (LUT_N - 1); }
    for (int i = 0; i < CAL_N; i++) g_cal[i] = (i * 7) % 11 - 5;
    g_sweep = malloc(SWEEP_B);
    if (!g_sweep) return 1;
    for (uint32_t i = 0; i < SWEEP_B; i += 64) g_sweep[i] = (uint8_t)i;

    /* 측정 오버헤드: 빈 구간 */
    for (uint32_t k = 0; k < NWARM; k++) { uint64_t t0 = mach_absolute_time(); uint64_t t1 = mach_absolute_time(); g_t[k] = t1 - t0; }
    report("empty", g_t, NWARM, -1);
    printf("--- warm (연속 호출, 캐시 데워짐) ---\n");
    for (int cls = 0; cls < C_N; cls++) report(cname[cls], g_t, measure(cls, 0, NWARM), cls == C_RANDOM ? 1 : -1);
    printf("--- cold (매 호출 전 64 MB 읽기) ---\n");
    for (int cls = 0; cls < C_N; cls++) report(cname[cls], g_t, measure(cls, 1, NCOLD), cls == C_RECAL ? 10 : -1);
    printf("--- batch: 같은 클래스 64회 연속을 한 구간으로 (평균 해상도 0.65 ns, 상태 없는 클래스만) ---\n");
    int stateless[4] = {C_HOLD, C_NOMINAL, C_HIGHSP, C_RANDOM};
    for (int q = 0; q < 4; q++) {
        ctrl_t c; ctrl_init(&c); int32_t sp[64], me[64];
        uint64_t best = UINT64_MAX;
        for (int rep = 0; rep < 200; rep++) {
            for (int k = 0; k < 64; k++) prepare(&c, stateless[q], &sp[k], &me[k]);
            uint64_t t0 = mach_absolute_time();
            for (int k = 0; k < 64; k++) g_sink = ctrl_step(&c, sp[k], me[k]);
            uint64_t t1 = mach_absolute_time();
            if (t1 - t0 < best) best = t1 - t0;
        }
        printf("%-8s best of 200 batches: %.1f ns/call\n", cname[stateless[q]], (double)best * g_ns_per_tick / 64);
    }
    free(g_sweep);
    return 0;
}
