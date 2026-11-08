/* ENV: linux */
/* LPERF-B10 추가 연습 — 한 번에 검증
 *   연습 1  /proc/<pid>/task/<tid>/sched 의 "key : 1234.567890"(ms) → ns 정수 (부동소수 없이), 키는 정확히 일치
 *   연습 2  stat 의 utime+stime(USER_HZ 틱) 변화량 ↔ schedstat run 변화량: 몇 틱 안에서 일치하나
 *   연습 3  런큐 대기 / 슬라이스 = 깨어날 때마다 평균 몇 ms 기다렸나, 선점 비율, slices ≈ vcsw + nvcsw
 *   연습 4  누적 샘플 시계열에서 "대기 비율이 가장 나빴던 구간" 찾기 (평균이 숨기는 스파이크)
 *
 * 숫자 픽스처는 이 레슨의 실측(5. 풀이의 실행 2 표)에서 가져왔다.
 * build (Lima VM elinux): gcc -std=c11 -Wall -Wextra -Werror -O2 -pthread practice.c -o pr && ./pr
 */
#define _GNU_SOURCE
#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; printf("PASS  %s\n", #expr); } \
                         else printf("FAIL  %s  (line %d)\n", #expr, __LINE__); } while (0)

static int next_u64(const char **pp, uint64_t *out) {
    const char *p = *pp;
    while (*p == ' ' || *p == '\t') p++;
    if (!isdigit((unsigned char)*p)) return -1;
    errno = 0;
    char *end;
    unsigned long long v = strtoull(p, &end, 10);
    if (errno == ERANGE) return -1;
    *out = (uint64_t)v;
    *pp = end;
    return 0;
}

/* ---------- 연습 1: /proc/.../sched 한 줄 ---------- */
/* 줄 맨 앞이 key 이고, 바로 뒤가 공백 또는 ':' 이어야 일치 ("wait" 가 "wait_start" 에 걸리면 안 된다). */
static int parse_sched_ns(const char *buf, const char *key, uint64_t *ns) {
    size_t k = strlen(key);
    for (const char *line = buf; line && *line; ) {
        if (strncmp(line, key, k) == 0 && (line[k] == ' ' || line[k] == ':')) {
            const char *p = line + k;
            while (*p == ' ') p++;
            if (*p++ != ':') return -1;
            uint64_t ip, fp = 0;
            if (next_u64(&p, &ip) != 0) return -1;
            if (*p == '.') {                         /* 소수 6자리 = ns. 짧으면 0 을 채운다 */
                p++;
                int d = 0;
                for (; d < 6 && isdigit((unsigned char)*p); d++, p++) fp = fp * 10 + (uint64_t)(*p - '0');
                for (; d < 6; d++) fp *= 10;
            }
            *ns = ip * 1000000u + fp;
            return 0;
        }
        line = strchr(line, '\n');
        if (line) line++;
    }
    return -1;
}

/* ---------- 연습 2: 틱 기반 CPU 시간과 비교 ---------- */
static uint64_t ticks_to_ns(uint64_t ticks, long hz) { return ticks * (1000000000u / (uint64_t)hz); }
/* |run - ticks| ≤ slack 틱 이면 1. 앞·뒤 샘플이 각각 1틱 미만 잘리므로 차이 변화량은 2틱 미만 + 읽기 시차. */
static int cpu_ticks_agree(uint64_t run_ns, uint64_t ticks, long hz, unsigned slack) {
    uint64_t t = ticks_to_ns(ticks, hz), lim = ticks_to_ns(slack, hz);
    uint64_t diff = run_ns > t ? run_ns - t : t - run_ns;
    return diff <= lim;
}

/* ---------- 연습 3: 슬라이스당 대기, 선점 비율 ---------- */
typedef struct { uint64_t wall_ns, run_ns, wait_ns, off_ns, slices, vcsw, nvcsw; } bd_t;
static uint64_t wait_per_slice_ns(const bd_t *b) { return b->slices ? b->wait_ns / b->slices : 0; }
static unsigned preempt_permille(const bd_t *b) {
    uint64_t sw = b->vcsw + b->nvcsw;
    return sw ? (unsigned)(b->nvcsw * 1000u / sw) : 0;
}
/* slices 는 CPU 에 올라간 횟수, 스위치는 CPU 에서 내려온 횟수 → 창 양 끝에서 달리는 중이면 ±1 차이. */
static int slices_consistent(const bd_t *b) {
    uint64_t sw = b->vcsw + b->nvcsw;
    return (b->slices > sw ? b->slices - sw : sw - b->slices) <= 1;
}

/* ---------- 연습 4: 시계열에서 최악 구간 ---------- */
typedef struct { uint64_t t_ns, run_ns, wait_ns; } cum_t;   /* 누적값 샘플 */
/* 연속한 두 샘플 사이 구간 중 wait/wall 이 가장 큰 구간의 시작 인덱스, 그 비율(permille). n < 2 면 -1. */
static int worst_wait_interval(const cum_t *s, int n, unsigned *permille) {
    int best = -1;
    uint64_t bw = 0, bwall = 1;
    for (int i = 0; i + 1 < n; i++) {
        uint64_t wall = s[i + 1].t_ns - s[i].t_ns, w = s[i + 1].wait_ns - s[i].wait_ns;
        if (wall == 0) continue;
        if (best < 0 || w * bwall > bw * wall) { best = i; bw = w; bwall = wall; }   /* 나눗셈 없이 비교 */
    }
    if (best >= 0 && permille) *permille = (unsigned)(bw * 1000u / bwall);
    return best;
}

int main(void) {
    /* 연습 1 — /proc/<pid>/sched 형식 (줄 모양은 실측 그대로, 숫자는 실행 2 표 근처 값으로 만든 픽스처) */
    const char *sched_on =
        "sleep (11092, #threads: 1)\n"
        "-------------------------------------------------------------------\n"
        "se.sum_exec_runtime                          :             0.885583\n"
        "sum_sleep_runtime                            :          2963.800123\n"
        "sum_block_runtime                            :             0.000000\n"
        "wait_start                                   :             0.000000\n"
        "wait_sum                                     :           760.3\n"
        "nr_wakeups                                   :                  407\n"
        "nr_wakeups_sync                              :                   12\n";
    uint64_t v = 1;
    CHECK(parse_sched_ns(sched_on, "se.sum_exec_runtime", &v) == 0 && v == 885583u);
    CHECK(parse_sched_ns(sched_on, "sum_sleep_runtime", &v) == 0 && v == 2963800123u);
    CHECK(parse_sched_ns(sched_on, "sum_block_runtime", &v) == 0 && v == 0u);
    CHECK(parse_sched_ns(sched_on, "wait_sum", &v) == 0 && v == 760300000u);       /* 소수 1자리여도 ns 로 */
    CHECK(parse_sched_ns(sched_on, "nr_wakeups", &v) == 0 && v == 407000000u);     /* 정수 줄 (단위는 호출자 몫) */
    CHECK(parse_sched_ns(sched_on, "wait", &v) == -1);                             /* wait_start 에 걸리면 안 됨 */
    CHECK(parse_sched_ns(sched_on, "sum_exec_runtime", &v) == -1);                 /* 줄 중간 일치 아님 */
    const char *sched_off = "se.sum_exec_runtime                          :             0.000000\n"
                            "nr_switches                                  :                    0\n";
    CHECK(parse_sched_ns(sched_off, "sum_sleep_runtime", &v) == -1);               /* schedstats off → 줄 없음 */

    /* 연습 2 — 실행 2 실측: cpu-1 tick 2290 ms vs run 2289.2 ms, lock-1 600 vs 609.6, io-0 140 vs 146.5 */
    CHECK(ticks_to_ns(229, 100) == 2290000000u);
    CHECK(cpu_ticks_agree(2289200000u, 229, 100, 2));
    CHECK(cpu_ticks_agree(609600000u, 60, 100, 2));
    CHECK(cpu_ticks_agree(146500000u, 14, 100, 2));
    CHECK(!cpu_ticks_agree(146500000u, 10, 100, 2));                               /* 46.5 ms(4틱 넘게) 차이는 이상 */
    CHECK(ticks_to_ns(3, 250) == 12000000u);                                        /* 틱 길이는 sysconf(_SC_CLK_TCK) 에 따라 다르다 */

    /* 연습 3 — 실행 2 실측: tick-0 / tick-1 / cpu-0 */
    bd_t tick0 = { 4066600000u, 271700000u, 760300000u, 3034700000u, 467, 407, 60 };
    bd_t tick1 = { 4066600000u, 281900000u, 606000000u, 3178700000u, 833, 407, 426 };
    bd_t cpu0 = { 4014500000u, 1749300000u, 2265800000u, 0, 592, 0, 593 };
    CHECK(wait_per_slice_ns(&tick0) == 1628051u);                                  /* 깨어날 때마다 평균 1.63 ms */
    CHECK(wait_per_slice_ns(&tick1) == 727490u);                                   /* 0.73 ms: 더 자주 선점당해 잘게 */
    CHECK(preempt_permille(&tick0) == 128u && preempt_permille(&tick1) == 511u);
    CHECK(preempt_permille(&cpu0) == 1000u);                                       /* 순수 CPU 바운드 = 전부 비자발 */
    CHECK(slices_consistent(&tick0) && slices_consistent(&cpu0));
    bd_t odd = tick1;
    odd.slices = 900;
    CHECK(!slices_consistent(&odd));
    bd_t zero = { 0, 0, 0, 0, 0, 0, 0 };
    CHECK(wait_per_slice_ns(&zero) == 0u && preempt_permille(&zero) == 0u);

    /* 연습 4 — 1 초 간격 누적 샘플, 4 초 평균 대기 18.75 % 지만 2→3 구간만 60 % */
    const cum_t ser[] = {
        { 0u, 0u, 0u },
        { 1000000000u, 400000000u, 50000000u },
        { 2000000000u, 800000000u, 100000000u },
        { 3000000000u, 1100000000u, 700000000u },
        { 4000000000u, 1500000000u, 750000000u },
    };
    unsigned pm = 0;
    CHECK(worst_wait_interval(ser, 5, &pm) == 2 && pm == 600u);
    CHECK(worst_wait_interval(ser, 1, &pm) == -1);
    const cum_t uneven[] = { { 0u, 0u, 0u }, { 100u, 0u, 50u }, { 1100u, 0u, 500u } };   /* 50 % vs 45 % */
    CHECK(worst_wait_interval(uneven, 3, &pm) == 0 && pm == 500u);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
