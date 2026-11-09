/* TEST-A05 추가 연습 — 한 번에 검증
 *
 * 연습 1: check/commit 2단계 인터리빙 전수 열거 — 전체 6개 중 double 4개, 우선순위 모델 도달 3개
 * 연습 2: 32비트 시계 wrap — 가상 시계를 wrap 직전에서 시작해 마감 판정 검증
 * 연습 3: prefix hash + 이진 탐색으로 두 trace가 처음 갈라진 지점 찾기
 * 연습 4: PCT 스케줄러 vs 균등 랜덤 스케줄러 — 깊이 2 버그 검출률
 * 연습 5: ddmin — 실패를 일으키는 입력을 최소 부분집합으로 줄이기
 * (본문 solution.c의 시뮬레이터 없이, 각 연습에 필요한 만큼만 축약했다.)
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; \
    if (expr) { g_pass++; printf("PASS %s\n", #expr); } \
    else { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } } while (0)

static uint64_t rng_step(uint64_t *s)
{
    uint64_t x = *s;
    x ^= x << 13; x ^= x >> 7; x ^= x << 17;
    return *s = x;
}

/* ---------- 연습 1: 인터리빙 전수 열거 ---------- */
/* 순서를 R,r,T,t 문자열로 본다. R<r, T<t 조건을 만족하는 순열은 C(4,2)=6개 */
static int enumerate_orders(char out[6][5])
{
    static const char *const all[] = { "RrTt", "RTrt", "RTtr", "TRrt", "TRtr", "TtRr" };
    for (int i = 0; i < 6; i++) strcpy(out[i], all[i]);
    return 6;
}

/* 모델: active = 1 에서 시작. check는 active를 읽고, commit은 check가 1을 봤을 때만 일어나 active=0 */
static bool is_double(const char *order)
{
    int active = 1;
    bool saw[2] = { false, false };
    int commits = 0;
    for (const char *p = order; *p; p++) {
        switch (*p) {
        case 'R': saw[0] = active == 1; break;
        case 'T': saw[1] = active == 1; break;
        case 'r': if (saw[0]) { active = 0; commits++; } break;
        case 't': if (saw[1]) { active = 0; commits++; } break;
        default: break;
        }
    }
    return commits == 2;
}

/* TMR 우선순위 > RX: 타이머 ISR은 한 번 시작하면 끝까지 달린다 → T와 t 사이에 끼어들 수 없다 */
static bool reachable_tmr_high(const char *order)
{
    const char *t = strchr(order, 'T');
    return t && t[1] == 't';
}

static void practice_1(void)
{
    char orders[6][5];
    int n = enumerate_orders(orders), doubles = 0, reach = 0, reach_double = 0;
    for (int i = 0; i < n; i++) {
        bool d = is_double(orders[i]), r = reachable_tmr_high(orders[i]);
        doubles += d; reach += r; reach_double += d && r;
    }
    printf("[1] orders=%d double=%d reachable(TMR>RX)=%d reachable&double=%d\n", n, doubles, reach, reach_double);
    CHECK(n == 6 && doubles == 4);
    CHECK(reach == 3 && reach_double == 1);
}

/* ---------- 연습 2: 32비트 시계 wrap ---------- */
static bool naive_passed(uint32_t now, uint32_t deadline) { return now >= deadline; }

/* 부호 있는 차이로 비교: 마감까지 2^31 us(약 35.8분) 이내면 wrap을 넘어가도 맞다 */
static bool deadline_passed(uint32_t now, uint32_t deadline)
{
    return (int32_t)(now - deadline) >= 0;
}

static void practice_2(void)
{
    uint32_t start = 0xFFFFFF00u;           /* 가상 시계를 wrap 256us 전에서 시작 */
    uint32_t deadline = start + 1000u;      /* wrap 뒤의 마감 */
    CHECK(deadline < start);
    CHECK(naive_passed(start, deadline));   /* 순진한 비교는 시작하자마자 "지났다"고 말한다 */
    CHECK(!deadline_passed(start, deadline));
    CHECK(!deadline_passed(start + 999u, deadline));
    CHECK(deadline_passed(start + 1000u, deadline));
    CHECK(deadline_passed(start + 1500u, deadline));
}

/* ---------- 연습 3: 처음 갈라진 지점 ---------- */
#define FNV_BASIS 0xcbf29ce484222325ull
#define FNV_PRIME 0x100000001b3ull

static void prefix_hashes(const uint32_t *rec, int n, uint64_t *out)
{
    uint64_t h = FNV_BASIS;
    for (int i = 0; i < n; i++) {
        h = (h ^ rec[i]) * FNV_PRIME;
        out[i] = h;
    }
}

/* prefix hash는 한번 달라지면 (충돌이 없는 한) 계속 달라서 이진 탐색이 된다. 같으면 -1 */
static int first_divergence(const uint64_t *a, const uint64_t *b, int n)
{
    if (n == 0 || a[n - 1] == b[n - 1]) return -1;
    int lo = 0, hi = n - 1;
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (a[mid] == b[mid]) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

static void practice_3(void)
{
    enum { N = 512 };
    static uint32_t x[N], y[N];
    static uint64_t hx[N], hy[N];
    uint64_t s = 77;
    for (int i = 0; i < N; i++) x[i] = y[i] = (uint32_t)rng_step(&s);
    prefix_hashes(x, N, hx);
    prefix_hashes(y, N, hy);
    CHECK(first_divergence(hx, hy, N) == -1);
    y[300] ^= 1u;
    prefix_hashes(y, N, hy);
    CHECK(first_divergence(hx, hy, N) == 300);
    y[300] ^= 1u; y[0] ^= 4u;
    prefix_hashes(y, N, hy);
    CHECK(first_divergence(hx, hy, N) == 0);
}

/* ---------- 연습 4: PCT vs 균등 랜덤 ---------- */
/* 스레드 A = 2스텝, B = K스텝. 버그는 "a0 다음, a1 전에 B가 전부 실행될 때"만 난다 (깊이 2) */
#define KB 10
#define TOTAL (2 + KB)

static bool bug_hit(const int *sched)
{
    int a_seen = 0, b_seen = 0, b_before_a0 = 0, b_after_a1 = 0;
    for (int i = 0; i < TOTAL; i++) {
        if (sched[i] == 0) a_seen++;
        else {
            b_seen++;
            if (a_seen == 0) b_before_a0++;
            if (a_seen == 2) b_after_a1++;
        }
    }
    return b_before_a0 == 0 && b_after_a1 == 0 && a_seen == 2 && b_seen == KB;
}

static void schedule_uniform(uint64_t *s, int *sched)
{
    int left[2] = { 2, KB };
    for (int i = 0; i < TOTAL; i++) {
        int who = left[0] == 0 ? 1 : left[1] == 0 ? 0 : (int)(rng_step(s) % 2);
        sched[i] = who;
        left[who]--;
    }
}

/* PCT: 스레드마다 무작위 우선순위, 매 스텝 가장 높은 runnable을 실행.
 * change point(스텝 번호) 한 곳에서 지금 달린 스레드의 우선순위를 최하로 내린다.
 * 깊이 d 버그 검출 확률 하한: 1 / (n * K^(d-1)) */
static void schedule_pct(uint64_t *s, int *sched)
{
    int left[2] = { 2, KB };
    int prio[2];
    prio[0] = (int)(rng_step(s) % 2) ? 10 : 20;
    prio[1] = prio[0] == 10 ? 20 : 10;
    int change = (int)(rng_step(s) % TOTAL);        /* 0..TOTAL-1 스텝 뒤 변경 */
    for (int i = 0; i < TOTAL; i++) {
        int who;
        if (left[0] == 0) who = 1;
        else if (left[1] == 0) who = 0;
        else who = prio[0] > prio[1] ? 0 : 1;
        sched[i] = who;
        left[who]--;
        if (i == change) prio[who] = 0;
    }
}

static void practice_4(void)
{
    enum { TRIALS = 20000 };
    int sched[TOTAL], pct_hits = 0, uni_hits = 0, pct_hits2 = 0;
    uint64_t s = 0x1234567ull;
    for (int i = 0; i < TRIALS; i++) { schedule_uniform(&s, sched); uni_hits += bug_hit(sched); }
    s = 0x7654321ull;
    for (int i = 0; i < TRIALS; i++) { schedule_pct(&s, sched); pct_hits += bug_hit(sched); }
    s = 0x7654321ull;
    for (int i = 0; i < TRIALS; i++) { schedule_pct(&s, sched); pct_hits2 += bug_hit(sched); }
    double pct_rate = (double)pct_hits / TRIALS, bound = 1.0 / (2.0 * TOTAL);
    printf("[4] trials=%d uniform=%d (%.3f%%) pct=%d (%.3f%%) bound=%.3f%%\n", TRIALS, uni_hits,
           100.0 * uni_hits / TRIALS, pct_hits, 100.0 * pct_rate, 100.0 * bound);
    CHECK(pct_hits == pct_hits2);              /* 결정론: 같은 seed면 같은 결과 */
    CHECK(pct_rate >= bound);                  /* 깊이 2 버그의 이론 하한 */
    CHECK(pct_hits > uni_hits * 6);
}

/* ---------- 연습 5: ddmin ---------- */
/* 실패 조건: 입력에 3과 11이 둘 다 있어야 한다 */
static int g_tests;
static bool fails(const int *v, int n)
{
    bool a = false, b = false;
    g_tests++;
    for (int i = 0; i < n; i++) { a |= v[i] == 3; b |= v[i] == 11; }
    return a && b;
}

/* v[0..n)을 granularity 조각으로 나눠 조각 제거(complement)를 시도한다 */
static int ddmin(int *v, int n)
{
    int gran = 2;
    while (n >= 2) {
        int chunk = n / gran;
        if (chunk == 0) chunk = 1;
        bool reduced = false;
        for (int start = 0; start < n; start += chunk) {
            int tmp[32], m = 0;
            for (int i = 0; i < n; i++)
                if (i < start || i >= start + chunk) tmp[m++] = v[i];
            if (m > 0 && fails(tmp, m)) {
                memcpy(v, tmp, (size_t)m * sizeof *v);
                n = m;
                gran = gran > 2 ? gran - 1 : 2;
                reduced = true;
                break;
            }
        }
        if (!reduced) {
            if (chunk == 1) break;
            gran = gran * 2 < n ? gran * 2 : n;
        }
    }
    return n;
}

static void practice_5(void)
{
    int v[16];
    for (int i = 0; i < 16; i++) v[i] = i;
    g_tests = 0;
    int n = ddmin(v, 16);
    printf("[5] ddmin: 16 -> %d items, %d tests\n", n, g_tests);
    CHECK(n == 2 && fails(v, n));
    CHECK((v[0] == 3 && v[1] == 11) || (v[0] == 11 && v[1] == 3));
    CHECK(g_tests < 16 * 16);       /* 전수(2^16)보다 훨씬 적다 */
}

int main(void)
{
    practice_1();
    practice_2();
    practice_3();
    practice_4();
    practice_5();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
