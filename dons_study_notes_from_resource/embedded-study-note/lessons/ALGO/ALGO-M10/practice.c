/* ALGO-M10 추가 연습 — 한 번에 검증
 *   연습 1  이벤트 루프판 쿨다운 스케줄러: 칸마다 "준비된 종류 중 남은 개수 최대" (최대 힙 + 쿨다운 FIFO)
 *   연습 2  Reorganize String (LeetCode 767): 같은 글자 인접 금지 (= cool 1, IDLE 금지)
 *   연습 3  Task Scheduler II (LeetCode 2365): 순서 고정, 같은 종류 사이 space 일
 *   연습 4  Rearrange String k Distance Apart (LeetCode 358): 같은 글자 간격 ≥ k, IDLE 금지
 *   연습 5  Course Schedule III (LeetCode 630) = Moore–Hodgson: 길이·마감 있는 작업 최대 개수
 * 모두 브루트포스 / 공식과 대조. 동적 할당 없음.
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr && ./pr
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MAX_TYPES 26
#define IDLE 0xFFu

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
static uint32_t g_rng = 0x2545F491u;
static uint32_t xorshift32(void) { uint32_t x = g_rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return g_rng = x; }

/* ---- 공용: 공식, 검증기, 종류 힙 ---- */
static uint64_t formula(const uint8_t *task, size_t n, uint32_t cool) {
    uint32_t cnt[MAX_TYPES] = {0}, f = 0, m = 0;
    for (size_t i = 0; i < n; i++) cnt[task[i]]++;
    if (!n) return 0;
    for (int t = 0; t < MAX_TYPES; t++) { if (cnt[t] > f) { f = cnt[t]; m = 1; } else if (cnt[t] == f) m++; }
    uint64_t a = (uint64_t)(f - 1) * ((uint64_t)cool + 1) + m;
    return a > n ? a : n;
}
static bool valid(const uint8_t *task, size_t n, uint32_t cool, const uint8_t *seq, uint64_t len) {
    long need[MAX_TYPES] = {0}; int64_t last[MAX_TYPES];
    for (int t = 0; t < MAX_TYPES; t++) last[t] = INT64_MIN / 2;
    for (size_t i = 0; i < n; i++) need[task[i]]++;
    if (n == 0) return len == 0;
    if (len == 0 || seq[len - 1] == IDLE) return false;
    for (uint64_t t = 0; t < len; t++) {
        if (seq[t] == IDLE) continue;
        if (seq[t] >= MAX_TYPES || (int64_t)t - last[seq[t]] <= (int64_t)cool) return false;
        last[seq[t]] = (int64_t)t; need[seq[t]]--;
    }
    for (int t = 0; t < MAX_TYPES; t++) if (need[t]) return false;
    return true;
}
static int above(const uint32_t *cnt, uint8_t a, uint8_t b) { return cnt[a] > cnt[b] || (cnt[a] == cnt[b] && a < b); }
static void hpush(uint8_t *h, size_t *hn, const uint32_t *cnt, uint8_t x) {
    size_t i = (*hn)++;
    while (i > 0) { size_t p = (i - 1) / 2; if (!above(cnt, x, h[p])) break; h[i] = h[p]; i = p; }
    h[i] = x;
}
static uint8_t hpop(uint8_t *h, size_t *hn, const uint32_t *cnt) {
    uint8_t top = h[0], x = h[--*hn]; size_t i = 0, n = *hn;
    for (;;) { size_t c = 2 * i + 1; if (c >= n) break; if (c + 1 < n && above(cnt, h[c + 1], h[c])) c++; if (!above(cnt, h[c], x)) break; h[i] = h[c]; i = c; }
    if (n) h[i] = x;
    return top;
}

/* ======================= 연습 1: 이벤트 루프판 ======================= */
/* 준비된 종류 = 최대 힙, 쿨다운 중인 종류 = FIFO (모두 같은 cool 이라 준비 시각 순서 = 넣은 순서).
 * 준비된 것이 없으면 FIFO 맨 앞이 준비될 때까지 IDLE 을 한 번에 건너뛴다. */
static uint64_t sched_event_loop(const uint8_t *task, size_t n, uint32_t cool, uint8_t *out, size_t cap) {
    uint32_t cnt[MAX_TYPES] = {0};
    uint8_t h[MAX_TYPES], fq_type[MAX_TYPES]; uint64_t fq_ready[MAX_TYPES];
    size_t hn = 0, fh = 0, fn = 0;                          /* FIFO: 원형 버퍼, 최대 26개 */
    uint64_t t = 0;                                         /* 지금 칸 = 지금까지의 길이 */
    for (size_t i = 0; i < n; i++) cnt[task[i]]++;
    for (uint8_t k = 0; k < MAX_TYPES; k++) if (cnt[k]) hpush(h, &hn, cnt, k);
    while (hn || fn) {
        while (fn && fq_ready[fh] <= t) { hpush(h, &hn, cnt, fq_type[fh]); fh = (fh + 1) % MAX_TYPES; fn--; }
        if (!hn) {                                         /* 전부 쿨다운 중 → IDLE 로 건너뛰기 */
            uint64_t until = fq_ready[fh];
            for (uint64_t s = t; s < until && s < cap; s++) out[s] = IDLE;
            t = until;
            continue;
        }
        uint8_t k = hpop(h, &hn, cnt);
        if (t < cap) out[t] = k;
        t++;
        if (--cnt[k]) { size_t tail = (fh + fn) % MAX_TYPES; fq_type[tail] = k; fq_ready[tail] = t + cool; fn++; }
    }
    return t;                                               /* 마지막은 항상 작업 칸 (IDLE 로 끝나지 않음) */
}

/* ======================= 연습 2: Reorganize String (LC 767) ======================= */
/* 가능하면 out 에 쓰고 true. 짝수 칸 0,2,4,... 를 최빈 글자부터 채우고 넘치면 1,3,5,... */
static bool reorganize(const char *s, char *out) {
    size_t n = strlen(s), cnt[26] = {0}, best = 0;
    for (size_t i = 0; i < n; i++) cnt[s[i] - 'a']++;
    for (int c = 1; c < 26; c++) if (cnt[c] > cnt[best]) best = (size_t)c;
    if (cnt[best] > (n + 1) / 2) { out[0] = 0; return false; }
    size_t p = 0;
    for (int k = -1; k < 26; k++) {
        int c = k < 0 ? (int)best : k;
        if (k >= 0 && (size_t)k == best) continue;
        for (size_t r = 0; r < cnt[c]; r++) { out[p] = (char)('a' + c); p += 2; if (p >= n) p = 1; }
    }
    out[n] = 0;
    return true;
}

/* ======================= 연습 3: Task Scheduler II (LC 2365) ======================= */
/* 순서 고정, 하루에 하나 또는 쉼. 같은 종류 사이 space 일 이상. 마지막 작업을 끝낸 날(1부터) */
static uint64_t task_scheduler_ii(const uint8_t *task, size_t n, uint32_t space) {
    uint64_t last[256], day = 0; bool seen[256] = {false};
    memset(last, 0, sizeof last);
    for (size_t i = 0; i < n; i++) {
        day++;
        if (seen[task[i]] && day - last[task[i]] <= space) day = last[task[i]] + space + 1;
        last[task[i]] = day; seen[task[i]] = true;
    }
    return day;
}

/* ======================= 연습 4: Rearrange String k Distance Apart (LC 358) ======================= */
/* 같은 글자 간격 ≥ k (k ≤ 1 이면 제약 없음). 라운드 = k 칸, IDLE 이 필요해지면 불가능 → false */
static bool rearrange_k(const uint8_t *task, size_t n, uint32_t k, uint8_t *out) {
    uint32_t cnt[MAX_TYPES] = {0}; uint8_t h[MAX_TYPES], round[MAX_TYPES]; size_t hn = 0, len = 0;
    uint32_t width = k ? k : 1;
    for (size_t i = 0; i < n; i++) cnt[task[i]]++;
    for (uint8_t t = 0; t < MAX_TYPES; t++) if (cnt[t]) hpush(h, &hn, cnt, t);
    while (hn) {
        size_t r = 0;
        while (hn && r < width) round[r++] = hpop(h, &hn, cnt);
        for (size_t j = 0; j < r; j++) out[len++] = round[j];
        for (size_t j = 0; j < r; j++) if (--cnt[round[j]]) hpush(h, &hn, cnt, round[j]);
        if (hn && r < width) return false;                 /* 다음 라운드 전에 쉬어야 한다 = 불가능 */
    }
    return true;
}

/* ======================= 연습 5: Course Schedule III (LC 630) ======================= */
typedef struct { uint32_t dur, last; } course_t;            /* 길이, 이 날까지 끝내야 함 (1일 = 1 단위) */
#define MAX_C 64
static size_t max_courses(const course_t *c, size_t n) {
    uint32_t idx[MAX_C], h[MAX_C]; size_t hn = 0; uint64_t total = 0;
    for (size_t i = 0; i < n; i++) idx[i] = (uint32_t)i;
    for (size_t i = 1; i < n; i++)                            /* 마감 오름차순 (삽입 정렬, n 작음) */
        for (size_t j = i; j > 0 && c[idx[j]].last < c[idx[j - 1]].last; j--) { uint32_t t = idx[j]; idx[j] = idx[j - 1]; idx[j - 1] = t; }
    for (size_t k = 0; k < n; k++) {
        uint32_t d = c[idx[k]].dur;
        size_t i = hn++;                                      /* 길이 최대 힙에 push */
        while (i > 0 && h[(i - 1) / 2] < d) { h[i] = h[(i - 1) / 2]; i = (i - 1) / 2; }
        h[i] = d; total += d;
        if (total > c[idx[k]].last) {                         /* 넘치면 가장 긴 것을 뺀다 */
            total -= h[0];
            uint32_t x = h[--hn]; size_t p = 0;
            for (;;) { size_t ch = 2 * p + 1; if (ch >= hn) break; if (ch + 1 < hn && h[ch + 1] > h[ch]) ch++; if (h[ch] <= x) break; h[p] = h[ch]; p = ch; }
            if (hn) h[p] = x;
        }
    }
    return hn;
}
static size_t brute_courses(const course_t *c, size_t n) {
    size_t best = 0;
    for (uint32_t mask = 0; mask < (1u << n); mask++) {
        uint32_t idx[MAX_C]; size_t k = 0;
        for (size_t i = 0; i < n; i++) if (mask >> i & 1) idx[k++] = (uint32_t)i;
        for (size_t i = 1; i < k; i++) for (size_t j = i; j > 0 && c[idx[j]].last < c[idx[j - 1]].last; j--) { uint32_t t = idx[j]; idx[j] = idx[j - 1]; idx[j - 1] = t; }
        uint64_t time = 0; bool ok = true;                    /* EDF 순서로 해 보면 충분 (교환 논증) */
        for (size_t i = 0; i < k && ok; i++) { time += c[idx[i]].dur; if (time > c[idx[i]].last) ok = false; }
        if (ok && k > best) best = k;
    }
    return best;
}

static uint8_t g_seq[1 << 16];

int main(void) {
    /* ---- 연습 1 ---- */
    {
        const uint8_t ex[] = {0, 0, 0, 1, 1, 1};
        uint64_t L = sched_event_loop(ex, 6, 2, g_seq, sizeof g_seq);
        CHECK(L == 8 && valid(ex, 6, 2, g_seq, L));
        long bad = 0, cases = 0; uint8_t task[64];
        for (uint32_t cool = 0; cool <= 4; cool++)                        /* 전수: 종류 4, 개수 0..4 */
        for (uint32_t code = 0; code < 625; code++) {
            size_t n = 0; uint32_t x = code;
            for (int t = 0; t < 4; t++) { for (uint32_t r = 0; r < x % 5; r++) task[n++] = (uint8_t)t; x /= 5; }
            L = sched_event_loop(task, n, cool, g_seq, sizeof g_seq);
            cases++; if (L != formula(task, n, cool) || !valid(task, n, cool, g_seq, L)) bad++;
        }
        for (int it = 0; it < 2000; it++) {                                /* 무작위: 26종, n ≤ 64, cool ≤ 30 */
            size_t n = xorshift32() % 65; uint32_t cool = xorshift32() % 31, k = 1 + xorshift32() % 26;
            for (size_t i = 0; i < n; i++) task[i] = (uint8_t)(xorshift32() % 3 == 0 ? 0 : xorshift32() % k);
            L = sched_event_loop(task, n, cool, g_seq, sizeof g_seq);
            cases++; if (L != formula(task, n, cool) || !valid(task, n, cool, g_seq, L)) bad++;
        }
        printf("연습 1 event-loop: %ld cases, bad %ld\n", cases, bad);
        CHECK(bad == 0);
    }
    /* ---- 연습 2 ---- */
    {
        char out[16];
        CHECK(reorganize("aab", out) && strcmp(out, "aba") == 0);
        CHECK(!reorganize("aaab", out) && out[0] == 0);
        long bad = 0, possible = 0, cases = 0; char s[16]; uint8_t task[16];
        for (size_t n = 0; n <= 9; n++) {                                 /* 전수: 길이 0..9, 글자 {a,b,c} */
            uint32_t tot = 1; for (size_t i = 0; i < n; i++) tot *= 3;
            for (uint32_t code = 0; code < tot; code++) {
                uint32_t x = code;
                for (size_t i = 0; i < n; i++) { s[i] = (char)('a' + x % 3); task[i] = (uint8_t)(x % 3); x /= 3; }
                s[n] = 0;
                bool ok = reorganize(s, out), expect = formula(task, n, 1) == n;   /* IDLE 없이 cool 1 */
                bool good = ok == expect;
                if (ok) { int c[3] = {0, 0, 0}; for (size_t i = 0; i < n; i++) { c[out[i] - 'a']++; c[s[i] - 'a']--; if (i && out[i] == out[i - 1]) good = false; }
                          if (c[0] || c[1] || c[2] || strlen(out) != n) good = false; possible++; }
                cases++; bad += !good;
            }
        }
        printf("연습 2 reorganize: %ld strings, possible %ld, bad %ld\n", cases, possible, bad);
        CHECK(bad == 0);
    }
    /* ---- 연습 3 ---- */
    {
        const uint8_t t1[] = {1, 2, 1, 2, 3, 1};                           /* LC 2365 예시: space 3 → 9 */
        const uint8_t t2[] = {5, 8, 8, 5};                                 /* space 2 → 6 */
        CHECK(task_scheduler_ii(t1, 6, 3) == 9 && task_scheduler_ii(t2, 4, 2) == 6);
        long bad = 0; uint8_t task[40];
        for (int it = 0; it < 3000; it++) {                                /* 기준: 하루씩 시뮬레이션 */
            size_t n = xorshift32() % 40; uint32_t space = xorshift32() % 6, k = 1 + xorshift32() % 5;
            for (size_t i = 0; i < n; i++) task[i] = (uint8_t)(xorshift32() % k);
            int64_t last[256]; for (int i = 0; i < 256; i++) last[i] = -1000;
            uint64_t day = 0; size_t i = 0;
            while (i < n) { day++; if ((int64_t)day - last[task[i]] > (int64_t)space) { last[task[i]] = (int64_t)day; i++; } }
            if (task_scheduler_ii(task, n, space) != day) bad++;
        }
        printf("연습 3 task scheduler II: 3000 cases, bad %ld\n", bad);
        CHECK(bad == 0);
    }
    /* ---- 연습 4 ---- */
    {
        const uint8_t a1[] = {0, 0, 1, 1, 2, 2};                           /* aabbcc, k=3 → 가능 (abcabc) */
        const uint8_t a2[] = {0, 0, 0, 1, 1, 2};                           /* aaabbc, k=3 → 불가능 */
        uint8_t out[64];
        CHECK(rearrange_k(a1, 6, 3, out) && valid(a1, 6, 2, out, 6));
        CHECK(!rearrange_k(a2, 6, 3, out));
        long bad = 0, possible = 0, cases = 0; uint8_t task[64];
        for (uint32_t k = 0; k <= 5; k++)
        for (uint32_t code = 0; code < 625; code++) {                      /* 전수: 종류 4, 개수 0..4 */
            size_t n = 0; uint32_t x = code;
            for (int t = 0; t < 4; t++) { for (uint32_t r = 0; r < x % 5; r++) task[n++] = (uint8_t)t; x /= 5; }
            uint32_t cool = k ? k - 1 : 0;
            bool ok = rearrange_k(task, n, k, out), expect = formula(task, n, cool) == n;
            bool good = ok == expect && (!ok || valid(task, n, cool, out, n));
            cases++; bad += !good; possible += ok;
        }
        printf("연습 4 rearrange k-apart: %ld cases, possible %ld, bad %ld\n", cases, possible, bad);
        CHECK(bad == 0);
    }
    /* ---- 연습 5 ---- */
    {
        const course_t c1[] = {{100, 200}, {200, 1300}, {1000, 1250}, {2000, 3200}};   /* LC 630 예시 → 3 */
        const course_t c2[] = {{1, 2}};
        const course_t c3[] = {{3, 2}, {4, 3}};
        CHECK(max_courses(c1, 4) == 3 && max_courses(c2, 1) == 1 && max_courses(c3, 2) == 0);
        long bad = 0; course_t c[MAX_C];
        for (int it = 0; it < 3000; it++) {
            size_t n = xorshift32() % 13;
            for (size_t i = 0; i < n; i++) { c[i].dur = 1 + xorshift32() % 10; c[i].last = xorshift32() % 40; }
            if (max_courses(c, n) != brute_courses(c, n)) bad++;
        }
        printf("연습 5 course schedule III: 3000 cases vs brute, bad %ld\n", bad);
        CHECK(bad == 0);
    }
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
