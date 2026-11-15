/* ALGO-M10 스케줄링 문제: 쿨다운 태스크·마감시간 작업 선택
 *
 *   Part A  쿨다운 태스크 스케줄러 (LeetCode 621): 같은 종류 사이에 cool 칸 이상 비워야 한다.
 *           sched_min_time  = 공식 max(n, (f-1)(cool+1) + m)
 *           sched_build     = 최대 힙 라운드 시뮬레이션 → 실제 실행 순서 (IDLE 포함)
 *           sched_validate  = 순서 검증기 (개수·쿨다운·id·끝 IDLE)
 *   Part B  마감시간·이익 작업 선택 (job sequencing, 단위 시간 작업, 슬롯 1..n)
 *           jobseq_dsu      = 이익 내림차순(최대 힙) + "d 이하 가장 늦은 빈 슬롯" (DSU)
 *           jobseq_heap     = 마감시간 오름차순(계수 정렬) + 이익 최소 힙 (크기 > d 면 최솟값 버림) + EDF 배치
 *   Part C  펌웨어: I2C EEPROM 페이지 쓰기 배치 계획(쓰기 사이클 tWR = 쿨다운), 텔레메트리 다운링크 프레임 선택
 *
 * 테스트: 작은 입력은 전수/무작위(xorshift32 고정 시드)로 브루트포스(서로 다른 순열 전부 / 부분집합 전부)와
 *         대조, 큰 무작위 입력은 공식·검증기·두 방법끼리 대조. 동적 할당 없음.
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o st && ./st   (solution.c 도 같은 명령)
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ======================= Part A: 쿨다운 태스크 스케줄러 ======================= */
#define MAX_TYPES 26          /* 종류 id 0..25 ('A'..'Z') */
#define IDLE      0xFFu       /* 실행 순서에서 "쉬는 칸" */

enum { SV_OK = 0, SV_BAD_ID, SV_COUNT, SV_COOLDOWN, SV_TRAILING_IDLE };

/* 종류 힙 (DS-B12에서 자세히): 남은 개수 cnt 가 큰 것이 위, 같으면 id 가 작은 것이 위 */
int type_above(const uint32_t *cnt, uint8_t a, uint8_t b) {
    return cnt[a] > cnt[b] || (cnt[a] == cnt[b] && a < b);
}
void th_push(uint8_t *h, size_t *hn, const uint32_t *cnt, uint8_t x) {
    size_t i = (*hn)++;
    while (i > 0) { size_t p = (i - 1) / 2; if (!type_above(cnt, x, h[p])) break; h[i] = h[p]; i = p; }
    h[i] = x;
}
uint8_t th_pop(uint8_t *h, size_t *hn, const uint32_t *cnt) {
    uint8_t top = h[0], x = h[--*hn];
    size_t i = 0, n = *hn;
    for (;;) {
        size_t c = 2 * i + 1;
        if (c >= n) break;
        if (c + 1 < n && type_above(cnt, h[c + 1], h[c])) c++;
        if (!type_above(cnt, h[c], x)) break;
        h[i] = h[c]; i = c;
    }
    if (n) h[i] = x;
    return top;
}

/* 최소 총 시간(칸 수). 잘못된 id 가 있으면 0. n 은 2^32 미만이라고 가정 (cnt 가 uint32_t). */
uint64_t sched_min_time(const uint8_t *task, size_t n, uint32_t cool) {
    (void)task; (void)n; (void)cool;
    return 0; /* TODO: 종류별 개수 → 최대 개수 f, 그 개수를 가진 종류 수 m → max(n, (f-1)(cool+1)+m) (64비트로!) */
}

/* 라운드 시뮬레이션: 한 라운드 = cool+1 칸. 힙에서 서로 다른 종류를 최대 cool+1 개 꺼내 실행하고,
 * 개수를 1 줄여 다시 넣는다. 아직 남은 작업이 있으면 라운드를 IDLE 로 채운다.
 * 반환 = 전체 길이. out 에는 앞에서부터 최대 cap 칸만 쓴다 (snprintf 처럼). 잘못된 id → 0. */
uint64_t sched_build(const uint8_t *task, size_t n, uint32_t cool, uint8_t *out, size_t cap) {
    (void)task; (void)n; (void)cool; (void)out; (void)cap;
    return 0; /* TODO: 라운드마다 힙에서 서로 다른 종류 최대 cool+1 개 → 실행 → 개수-1 → 다시 넣기 → 남았으면 IDLE 채움 */
}

/* seq[0..len) 가 task 의 올바른 실행 순서인가. SV_OK 또는 첫 번째로 발견한 오류 코드. */
int sched_validate(const uint8_t *task, size_t n, uint32_t cool, const uint8_t *seq, uint64_t len) {
    (void)task; (void)n; (void)cool; (void)seq; (void)len;
    return SV_OK; /* TODO: 끝 IDLE → 종류별 마지막 실행 시각으로 간격 ≥ cool+1 → 개수 정확히 일치 */
}

/* ======================= Part B: 마감시간·이익 작업 선택 ======================= */
#define MAX_JOBS 4096
#define NO_JOB   (-1)

typedef struct { uint32_t deadline; uint32_t profit; } job_t;   /* 작업 id = 배열 인덱스, 단위 시간, 슬롯 t 는 1..n */

/* 작업 인덱스 힙 (DS-B12/DS-M11에서 자세히). above(J, a, b) == 1 이면 a 가 b 보다 먼저 나온다 */
typedef int (*job_above_fn)(const job_t *J, uint32_t a, uint32_t b);
int by_profit_desc(const job_t *J, uint32_t a, uint32_t b) {   /* 최대 힙: 이익 큰 것, 같으면 id 작은 것 */
    return J[a].profit > J[b].profit || (J[a].profit == J[b].profit && a < b);
}
int by_profit_asc(const job_t *J, uint32_t a, uint32_t b) {    /* 최소 힙: 이익 작은 것, 같으면 id 큰 것을 먼저 버림 */
    return J[a].profit < J[b].profit || (J[a].profit == J[b].profit && a > b);
}
void ih_sift_down(uint32_t *h, size_t n, size_t i, const job_t *J, job_above_fn above) {
    uint32_t x = h[i];
    for (;;) {
        size_t c = 2 * i + 1;
        if (c >= n) break;
        if (c + 1 < n && above(J, h[c + 1], h[c])) c++;
        if (!above(J, h[c], x)) break;
        h[i] = h[c]; i = c;
    }
    h[i] = x;
}
void ih_heapify(uint32_t *h, size_t n, const job_t *J, job_above_fn above) {
    for (size_t i = n / 2; i-- > 0;) ih_sift_down(h, n, i, J, above);
}
void ih_push(uint32_t *h, size_t *n, uint32_t x, const job_t *J, job_above_fn above) {
    size_t i = (*n)++;
    while (i > 0) { size_t p = (i - 1) / 2; if (!above(J, x, h[p])) break; h[i] = h[p]; i = p; }
    h[i] = x;
}
uint32_t ih_pop(uint32_t *h, size_t *n, const job_t *J, job_above_fn above) {
    uint32_t top = h[0];
    if (--*n) { h[0] = h[*n]; ih_sift_down(h, *n, 0, J, above); }
    return top;
}

uint32_t g_par[MAX_JOBS + 1];     /* 슬롯 DSU: g_par[s] == s 면 s 는 빈 슬롯. 0 = "빈 슬롯 없음" 보초 */
uint32_t g_heap[MAX_JOBS];
uint32_t g_order[MAX_JOBS];
uint32_t g_bucket[MAX_JOBS + 2];
uint8_t  g_chosen[MAX_JOBS];

/* s 이하에서 가장 늦은 빈 슬롯 (없으면 0). 경로 반감(path halving). */
uint32_t slot_find(uint32_t s) {
    (void)s;
    return 0; /* TODO: g_par 를 따라 루트(= s 이하 가장 늦은 빈 슬롯)로. 경로 반감 */
}

/* 방법 1: 이익 내림차순으로 보며, 마감 이하의 가장 늦은 빈 슬롯에 넣는다.
 * slot_job[0..n] 을 채운다 (slot_job[t] = 슬롯 t 의 작업 id 또는 NO_JOB, [0] 은 항상 NO_JOB). 반환 = 총 이익. */
uint64_t jobseq_dsu(const job_t *J, size_t n, int32_t *slot_job) {
    (void)J; (void)n;
    if (slot_job) slot_job[0] = NO_JOB;
    return 0; /* TODO: g_par[s]=s 초기화 → 이익 최대 힙(ih_heapify) → pop 마다 s = slot_find(min(d, n)) → s>0 이면 배치, g_par[s]=s-1 */
}

/* 방법 2: 마감 오름차순으로 보며 이익 최소 힙에 넣고, 힙 크기가 마감 d 를 넘으면 가장 작은 이익을 버린다.
 * 끝에 힙에 남은 작업들을 마감 순서(EDF)로 슬롯 1, 2, ... 에 놓는다. 출력 형식은 jobseq_dsu 와 같다. */
uint64_t jobseq_heap(const job_t *J, size_t n, int32_t *slot_job) {
    (void)J; (void)n;
    if (slot_job) slot_job[0] = NO_JOB;
    return 0; /* TODO: 마감 계수 정렬 → 이익 최소 힙에 push, 크기 > d 면 pop → 남은 것들을 마감 순서로 슬롯 1,2,... */
}

/* ======================= Part C: 펌웨어 시나리오 ======================= */
/* 바쁜 시간 busy_us (전송 + 쓰기 사이클) 를 slot_us 슬롯 단위 쿨다운으로: 다음 같은 칩 쓰기는
 * ceil(busy/slot) 칸 뒤부터 → cool = ceil(busy/slot) - 1 (busy == 0 이면 0). slot_us == 0 이면 UINT32_MAX. */
uint32_t cooldown_from_busy(uint32_t busy_us, uint32_t slot_us) {
    (void)busy_us; (void)slot_us;
    return 0; /* TODO: ceil(busy/slot) - 1, 넘침 없이. busy == 0 → 0, slot == 0 → UINT32_MAX */
}

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t g_rng = 0x2545F491u;
static uint32_t xorshift32(void) {
    uint32_t x = g_rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return g_rng = x;
}

/* ---- Part A 기준 구현 (테스트 쪽) ---- */
/* 정해진 순서 p 를 지키며 각 작업을 가능한 가장 이른 칸에 놓을 때의 길이 */
static uint64_t ref_len_for_order(const uint8_t *p, size_t n, uint32_t cool) {
    int64_t last[MAX_TYPES]; int64_t time = -1;
    for (int t = 0; t < MAX_TYPES; t++) last[t] = INT64_MIN;
    for (size_t i = 0; i < n; i++) {
        int64_t t0 = time + 1;
        if (last[p[i]] != INT64_MIN && last[p[i]] + (int64_t)cool + 1 > t0) t0 = last[p[i]] + (int64_t)cool + 1;
        time = last[p[i]] = t0;
    }
    return (uint64_t)(time + 1);
}
static bool next_perm(uint8_t *a, size_t n) {
    if (n < 2) return false;
    size_t i = n - 1;
    while (i > 0 && a[i - 1] >= a[i]) i--;
    if (i == 0) return false;
    size_t j = n - 1;
    while (a[j] <= a[i - 1]) j--;
    uint8_t t = a[i - 1]; a[i - 1] = a[j]; a[j] = t;
    for (size_t l = i, r = n - 1; l < r; l++, r--) { t = a[l]; a[l] = a[r]; a[r] = t; }
    return true;
}
/* 브루트포스 최적: 서로 다른 순열 전부 × 가장 이른 배치 (n ≤ 9 용) */
static uint64_t brute_sched(const uint8_t *task, size_t n, uint32_t cool) {
    uint8_t p[16];
    memcpy(p, task, n);
    for (size_t i = 1; i < n; i++) for (size_t j = i; j > 0 && p[j - 1] > p[j]; j--) { uint8_t t = p[j]; p[j] = p[j - 1]; p[j - 1] = t; }
    uint64_t best = UINT64_MAX;
    do { uint64_t L = ref_len_for_order(p, n, cool); if (L < best) best = L; } while (next_perm(p, n));
    return n ? best : 0;
}
/* 테스트 쪽 검증기 (학생 sched_validate 와 독립) */
static bool ref_valid(const uint8_t *task, size_t n, uint32_t cool, const uint8_t *seq, uint64_t len) {
    long need[MAX_TYPES] = {0}; int64_t last[MAX_TYPES];
    for (int t = 0; t < MAX_TYPES; t++) last[t] = -1 - (int64_t)cool - 1;
    for (size_t i = 0; i < n; i++) { if (task[i] >= MAX_TYPES) return false; need[task[i]]++; }
    if (n == 0) return len == 0;
    if (len == 0 || seq[len - 1] == IDLE) return false;
    for (uint64_t t = 0; t < len; t++) {
        if (seq[t] == IDLE) continue;
        if (seq[t] >= MAX_TYPES) return false;
        if ((int64_t)t - last[seq[t]] < (int64_t)cool + 1) return false;
        last[seq[t]] = (int64_t)t; need[seq[t]]--;
    }
    for (int t = 0; t < MAX_TYPES; t++) if (need[t]) return false;
    return true;
}
static void print_seq(const char *label, const uint8_t *seq, uint64_t len) {
    printf("%s", label);
    for (uint64_t i = 0; i < len; i++) putchar(seq[i] == IDLE ? '_' : (seq[i] < MAX_TYPES ? 'A' + seq[i] : '?'));
    printf("  (len %llu)\n", (unsigned long long)len);
}

/* ---- Part B 기준 구현 ---- */
static bool feasible_set(const job_t *J, size_t n, uint32_t mask) {      /* ∀t: |{d ≤ t}| ≤ t */
    uint32_t c[16] = {0};
    for (size_t i = 0; i < n; i++) if (mask >> i & 1) {
        uint32_t d = J[i].deadline < n ? J[i].deadline : (uint32_t)n;
        if (d == 0) return false;
        c[d]++;
    }
    uint32_t run = 0;
    for (size_t t = 1; t <= n; t++) { run += c[t]; if (run > t) return false; }
    return true;
}
static uint64_t brute_jobs(const job_t *J, size_t n) {
    uint64_t best = 0;
    for (uint32_t mask = 0; mask < (1u << n); mask++) {
        if (!feasible_set(J, n, mask)) continue;
        uint64_t s = 0;
        for (size_t i = 0; i < n; i++) if (mask >> i & 1) s += J[i].profit;
        if (s > best) best = s;
    }
    return best;
}
/* 슬롯 배치 검증: 각 작업 한 번, 슬롯 ≤ 마감, 이익 합 일치 */
static bool ref_jobs_valid(const job_t *J, size_t n, const int32_t *slot_job, uint64_t total) {
    static uint8_t used[MAX_JOBS];
    memset(used, 0, n);
    if (slot_job[0] != NO_JOB) return false;
    uint64_t s = 0;
    for (size_t t = 1; t <= n; t++) {
        int32_t j = slot_job[t];
        if (j == NO_JOB) continue;
        if (j < 0 || (size_t)j >= n || used[j] || t > J[j].deadline) return false;
        used[j] = 1; s += J[j].profit;
    }
    return s == total;
}

static uint8_t g_seq[1 << 16];
static int32_t g_slots[MAX_JOBS + 1], g_slots2[MAX_JOBS + 1];
static job_t   g_jobs[MAX_JOBS];

int main(void) {
    /* ---------- Part A: 이름 붙은 사례 ---------- */
    const uint8_t ex1[] = {0, 0, 0, 1, 1, 1};                       /* LeetCode 621 예시 1: AAABBB, n=2 → 8 */
    uint64_t L = sched_build(ex1, 6, 2, g_seq, sizeof g_seq);
    print_seq("A: AAABBB cool=2 -> ", g_seq, L);
    CHECK(sched_min_time(ex1, 6, 2) == 8 && L == 8 && memcmp(g_seq, "\x00\x01\xff\x00\x01\xff\x00\x01", 8) == 0);
    CHECK(sched_min_time(ex1, 6, 0) == 6 && sched_min_time(ex1, 6, 3) == 10);
    const uint8_t ex2[] = {0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 5, 6};     /* AAAAAABCDEFG, cool=2 → 16 */
    L = sched_build(ex2, 12, 2, g_seq, sizeof g_seq);
    print_seq("A: AAAAAABCDEFG cool=2 -> ", g_seq, L);
    CHECK(sched_min_time(ex2, 12, 2) == 16 && L == 16 && ref_valid(ex2, 12, 2, g_seq, L));
    const uint8_t tr[] = {0, 0, 0, 1, 1, 1, 2};                     /* 풀이의 추적 예: AAABBBC, cool=2 → 8 */
    L = sched_build(tr, 7, 2, g_seq, sizeof g_seq);
    print_seq("A: AAABBBC cool=2 -> ", g_seq, L);
    CHECK(L == 8 && memcmp(g_seq, "\x00\x01\x02\x00\x01\xff\x00\x01", 8) == 0);
    CHECK(sched_min_time(NULL, 0, 5) == 0 && sched_build(NULL, 0, 5, g_seq, sizeof g_seq) == 0);
    const uint8_t aa[] = {7, 7};                                     /* cool = UINT32_MAX: 64비트 곱/길이 */
    CHECK(sched_min_time(aa, 2, UINT32_MAX) == 4294967297ull);
    CHECK(sched_build(aa, 2, UINT32_MAX, g_seq, 4) == 4294967297ull && g_seq[0] == 7 && g_seq[1] == IDLE && g_seq[3] == IDLE);
    const uint8_t badid[] = {0, 26};
    CHECK(sched_min_time(badid, 2, 1) == 0 && sched_build(badid, 2, 1, g_seq, sizeof g_seq) == 0);

    /* 검증기: 손으로 만든 좋은/나쁜 순서 */
    const uint8_t good[] = {0, 1, IDLE, 0, 1, IDLE, 0, 1};
    const uint8_t cd[]   = {0, 1, 0, IDLE, 1, IDLE, 0, 1};           /* A 두 번 사이 간격 2 < 3 */
    const uint8_t miss[] = {0, 1, IDLE, 0, 1, IDLE, 0};              /* B 하나 빠짐 */
    const uint8_t extra[]= {0, 1, IDLE, 0, 1, IDLE, 0, 1, IDLE, IDLE, 0};
    const uint8_t trail[]= {0, 1, IDLE, 0, 1, IDLE, 0, 1, IDLE};
    const uint8_t bid[]  = {0, 1, IDLE, 0, 1, 30, 0, 1};
    CHECK(sched_validate(ex1, 6, 2, good, 8) == SV_OK);
    CHECK(sched_validate(ex1, 6, 2, cd, 8) == SV_COOLDOWN);
    CHECK(sched_validate(ex1, 6, 2, miss, 7) == SV_COUNT);
    CHECK(sched_validate(ex1, 6, 2, extra, 11) == SV_COUNT);
    CHECK(sched_validate(ex1, 6, 2, trail, 9) == SV_TRAILING_IDLE);
    CHECK(sched_validate(ex1, 6, 2, bid, 8) == SV_BAD_ID);
    CHECK(sched_validate(ex1, 6, 1, cd, 8) == SV_OK);               /* cool=1 이면 간격 2 는 합법 */

    /* 전수: 종류 ≤ 3, 개수 0..3, cool 0..3 → 공식 == 시뮬 길이 == 브루트포스, 순서 검증 */
    {
        long cases = 0, bad_len = 0, bad_valid = 0, bad_brute = 0;
        uint8_t task[16];
        for (uint32_t cool = 0; cool <= 3; cool++)
        for (int c0 = 0; c0 <= 3; c0++) for (int c1 = 0; c1 <= 3; c1++) for (int c2 = 0; c2 <= 3; c2++) {
            size_t n = 0;
            for (int i = 0; i < c0; i++) task[n++] = 0;
            for (int i = 0; i < c1; i++) task[n++] = 1;
            for (int i = 0; i < c2; i++) task[n++] = 2;
            uint64_t Lb = sched_build(task, n, cool, g_seq, sizeof g_seq);
            cases++;
            if (Lb != sched_min_time(task, n, cool)) bad_len++;
            if (!ref_valid(task, n, cool, g_seq, Lb) || sched_validate(task, n, cool, g_seq, Lb) != SV_OK) bad_valid++;
            if (Lb != brute_sched(task, n, cool)) bad_brute++;
        }
        printf("Part A exhaustive: %ld cases, len!=formula %ld, invalid %ld, !=brute %ld\n", cases, bad_len, bad_valid, bad_brute);
        CHECK(bad_len == 0 && bad_valid == 0 && bad_brute == 0);
    }
    /* 무작위 작은 입력: 종류 ≤ 5, n ≤ 9 → 브루트포스 */
    {
        long bad = 0; uint8_t task[16];
        for (int it = 0; it < 600; it++) {
            size_t n = xorshift32() % 10; uint32_t k = 1 + xorshift32() % 5, cool = xorshift32() % 5;
            for (size_t i = 0; i < n; i++) task[i] = (uint8_t)(xorshift32() % k);
            uint64_t Lb = sched_build(task, n, cool, g_seq, sizeof g_seq);
            if (Lb != brute_sched(task, n, cool) || Lb != sched_min_time(task, n, cool) || !ref_valid(task, n, cool, g_seq, Lb)) bad++;
        }
        printf("Part A random small vs brute: 600 cases, bad %ld\n", bad);
        CHECK(bad == 0);
    }
    /* 무작위 큰 입력: n ≤ 3000, 26종, 치우친 분포, cool ≤ 40 → 공식 + 검증기 */
    {
        static uint8_t task[3000];
        long bad = 0, idles = 0;
        for (int it = 0; it < 300; it++) {
            size_t n = 1 + xorshift32() % 3000; uint32_t cool = xorshift32() % 41, hot = xorshift32() % 26;
            for (size_t i = 0; i < n; i++) task[i] = (uint8_t)((xorshift32() % 4 == 0) ? hot : xorshift32() % 26);
            uint64_t Lb = sched_build(task, n, cool, g_seq, sizeof g_seq);
            if (Lb != sched_min_time(task, n, cool) || !ref_valid(task, n, cool, g_seq, Lb)) bad++;
            if (Lb >= n) idles += (long)(Lb - n);
        }
        printf("Part A random large: 300 cases, bad %ld, total idle slots %ld\n", bad, idles);
        CHECK(bad == 0);
    }
    /* 검증기 대 기준 검증기: 올바른 순서를 한 칸 망가뜨려서 판정이 같은지 */
    {
        uint8_t task[40], s2[1 << 10];
        long agree = 0, total = 0, caught = 0;
        for (int it = 0; it < 3000; it++) {
            size_t n = 1 + xorshift32() % 40; uint32_t cool = xorshift32() % 4, k = 1 + xorshift32() % 6;
            for (size_t i = 0; i < n; i++) task[i] = (uint8_t)(xorshift32() % k);
            uint64_t Lb = sched_build(task, n, cool, s2, sizeof s2);
            size_t a = Lb ? xorshift32() % Lb : 0, b = Lb ? xorshift32() % Lb : 0;   /* 스텁이 0 을 줘도 안전하게 */
            if (Lb) switch (xorshift32() % 4) {
            case 0: { uint8_t t = s2[a]; s2[a] = s2[b]; s2[b] = t; } break;   /* 두 칸 교환 */
            case 1: s2[a] = IDLE; break;                                      /* 작업 지움 */
            case 2: s2[a] = (uint8_t)(xorshift32() % k); break;               /* 다른 종류로 */
            default: if (Lb > 1) Lb--; break;                                 /* 끝 자르기 */
            }
            bool r = ref_valid(task, n, cool, s2, Lb), s = sched_validate(task, n, cool, s2, Lb) == SV_OK;
            total++; agree += (r == s); caught += !r;
        }
        printf("Part A validator mutations: %ld cases, agree %ld, ref says invalid %ld\n", total, agree, caught);
        CHECK(agree == total);
    }

    /* ---------- Part B: 이름 붙은 사례 ---------- */
    const job_t gfg[] = {{2, 100}, {1, 19}, {2, 27}, {1, 25}, {3, 15}};    /* 고전 예: a..e → 142 (c, a, e) */
    uint64_t P1 = jobseq_dsu(gfg, 5, g_slots), P2 = jobseq_heap(gfg, 5, g_slots2);
    printf("B: classic 5 jobs: dsu=%llu slots[1..3]=%d,%d,%d  heap=%llu slots[1..3]=%d,%d,%d\n",
           (unsigned long long)P1, g_slots[1], g_slots[2], g_slots[3], (unsigned long long)P2, g_slots2[1], g_slots2[2], g_slots2[3]);
    CHECK(P1 == 142 && g_slots[1] == 2 && g_slots[2] == 0 && g_slots[3] == 4 && ref_jobs_valid(gfg, 5, g_slots, P1));
    CHECK(P2 == 142 && ref_jobs_valid(gfg, 5, g_slots2, P2));
    const job_t g2[] = {{4, 20}, {1, 10}, {1, 40}, {1, 30}};              /* → 60 (40 + 20) */
    CHECK(jobseq_dsu(g2, 4, g_slots) == 60 && jobseq_heap(g2, 4, g_slots2) == 60);
    const job_t late[] = {{2, 100}, {1, 50}};                              /* 가장 이른 슬롯에 넣으면 100 */
    CHECK(jobseq_dsu(late, 2, g_slots) == 150 && g_slots[1] == 1 && g_slots[2] == 0);
    const job_t edge[] = {{0, 999}, {UINT32_MAX, 5}, {1, 5}, {1, 5}};      /* 마감 0 은 불가, 거대 마감은 n 으로 */
    CHECK(jobseq_dsu(edge, 4, g_slots) == 10 && jobseq_heap(edge, 4, g_slots2) == 10 && ref_jobs_valid(edge, 4, g_slots, 10));
    CHECK(jobseq_dsu(NULL, 0, g_slots) == 0 && g_slots[0] == NO_JOB && jobseq_heap(NULL, 0, g_slots2) == 0);

    /* 무작위 작은 입력 vs 브루트포스 (n ≤ 12, 이익 동점 많음) */
    {
        long bad = 0;
        for (int it = 0; it < 2000; it++) {
            size_t n = xorshift32() % 13;
            for (size_t i = 0; i < n; i++) { g_jobs[i].deadline = xorshift32() % (uint32_t)(n + 3); g_jobs[i].profit = 1 + xorshift32() % 20; }
            uint64_t b = brute_jobs(g_jobs, n);
            P1 = jobseq_dsu(g_jobs, n, g_slots); P2 = jobseq_heap(g_jobs, n, g_slots2);
            if (P1 != b || P2 != b || !ref_jobs_valid(g_jobs, n, g_slots, P1) || !ref_jobs_valid(g_jobs, n, g_slots2, P2)) bad++;
        }
        printf("Part B random small vs brute: 2000 cases, bad %ld\n", bad);
        CHECK(bad == 0);
    }
    /* 무작위 큰 입력: 두 방법 이익 일치 + 배치 유효 */
    {
        long bad = 0; uint64_t sum = 0;
        for (int it = 0; it < 50; it++) {
            size_t n = 1 + xorshift32() % MAX_JOBS;
            for (size_t i = 0; i < n; i++) { g_jobs[i].deadline = 1 + xorshift32() % (uint32_t)(n / 4 + 1); g_jobs[i].profit = xorshift32() % 100000; }
            P1 = jobseq_dsu(g_jobs, n, g_slots); P2 = jobseq_heap(g_jobs, n, g_slots2);
            if (P1 != P2 || !ref_jobs_valid(g_jobs, n, g_slots, P1) || !ref_jobs_valid(g_jobs, n, g_slots2, P2)) bad++;
            sum += P1;
        }
        printf("Part B random large: 50 cases, bad %ld (profit sum %llu)\n", bad, (unsigned long long)sum);
        CHECK(bad == 0);
    }

    /* ---------- Part C ---------- */
    CHECK(cooldown_from_busy(5790, 1000) == 5 && cooldown_from_busy(5000, 1000) == 4);
    CHECK(cooldown_from_busy(0, 1000) == 0 && cooldown_from_busy(1000, 1000) == 0 && cooldown_from_busy(1001, 1000) == 1);
    CHECK(cooldown_from_busy(UINT32_MAX, 1) == UINT32_MAX - 1 && cooldown_from_busy(UINT32_MAX, 2) == 2147483647u);
    CHECK(cooldown_from_busy(10, 0) == UINT32_MAX);
    {   /* C-1: 24LC256 류 EEPROM 4개, 1 ms 버스 슬롯, 바쁨 = 전송 ~0.79 ms + tWR 5 ms */
        const char *chip[] = {"E0", "E1", "E2", "E3"};
        uint8_t task[32]; size_t n = 0;
        const int pages_a[4] = {6, 3, 3, 1}, pages_b[4] = {4, 3, 3, 3};   /* 같은 13페이지, 배치만 다름 */
        uint32_t cool = cooldown_from_busy(790 + 5000, 1000);
        for (int c = 0; c < 4; c++) for (int p = 0; p < pages_a[c]; p++) task[n++] = (uint8_t)c;
        uint64_t La = sched_build(task, n, cool, g_seq, sizeof g_seq);
        printf("C-1 EEPROM pages {6,3,3,1}, cool=%u: %llu slots, idle %llu\n  ", cool, (unsigned long long)La, (unsigned long long)(La >= n ? La - n : 0));
        for (uint64_t t = 0; t < La; t++) printf("%s ", g_seq[t] == IDLE ? "--" : chip[g_seq[t]]);
        printf("\n");
        CHECK(La == 31 && sched_validate(task, n, cool, g_seq, La) == SV_OK);
        n = 0;
        for (int c = 0; c < 4; c++) for (int p = 0; p < pages_b[c]; p++) task[n++] = (uint8_t)c;
        uint64_t Lb = sched_build(task, n, cool, g_seq, sizeof g_seq);
        printf("C-1 EEPROM pages {4,3,3,3}, cool=%u: %llu slots, idle %llu\n", cool, (unsigned long long)Lb, (unsigned long long)(Lb >= n ? Lb - n : 0));
        CHECK(Lb == 19 && sched_validate(task, n, cool, g_seq, Lb) == SV_OK);
    }
    {   /* C-2: 다운링크 프레임 1개 = 패킷 1개. 마감(프레임) 안에 보내야 가치가 있다 */
        const char *name[] = {"BATT_ALARM", "GPS_FIX", "IMU_LOG", "HEARTBEAT", "CAM_THUMB", "PARAM_ACK", "TEMP", "MOTOR_DIAG"};
        const job_t pk[] = {{1, 90}, {2, 60}, {4, 20}, {2, 30}, {4, 40}, {2, 70}, {4, 10}, {3, 50}};
        size_t n = 8;
        P1 = jobseq_dsu(pk, n, g_slots); P2 = jobseq_heap(pk, n, g_slots2);
        printf("C-2 downlink: value dsu=%llu heap=%llu brute=%llu\n", (unsigned long long)P1, (unsigned long long)P2,
               (unsigned long long)brute_jobs(pk, n));
        for (size_t t = 1; t <= 4; t++)
            printf("  frame %zu: %-10s | %-10s\n", t, g_slots[t] == NO_JOB ? "-" : name[g_slots[t]], g_slots2[t] == NO_JOB ? "-" : name[g_slots2[t]]);
        printf("  dropped:");
        for (size_t j = 0; j < n; j++) { bool in = false; for (size_t t = 1; t <= n; t++) in |= g_slots[t] == (int32_t)j; if (!in) printf(" %s", name[j]); }
        printf("\n");
        CHECK(P1 == 250 && P2 == 250 && brute_jobs(pk, n) == 250 && ref_jobs_valid(pk, n, g_slots, P1) && ref_jobs_valid(pk, n, g_slots2, P2));
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
