/* ALGO-B09 투 포인터: 쌍 합·중복 제거·병합
 *
 *   Part A: pair_sum_sorted, dedup_sorted, merge_sorted
 *   Part B: dedup_keep_k (LeetCode 80 일반화), merge_into_a (뒤에서 채우기), three_sum_zero
 *   Part C: merge_events (CAN1/CAN2 타임스탬프 스트림 병합), scan_union / scan_intersect (I2C 스캔)
 *
 * 값은 int32_t, 합은 반드시 int64_t 로 계산한다 — int32 + int32 는 int 범위를 넘을 수 있고
 * 부호 있는 정수 overflow 는 UB (C11 6.5p5).
 * 모든 함수는 브루트포스 참조 구현과 고정 시드 xorshift32 랜덤 대조 + 이름 붙은 경계 사례로 검사한다.
 *
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------- 주어진 도우미 ---------------- */
static int cmp_i32(const void *p, const void *q) {
    int32_t x = *(const int32_t *)p, y = *(const int32_t *)q;
    return (x > y) - (x < y);                 /* x - y 는 overflow 할 수 있다 */
}
static void sort_i32(int32_t *a, size_t n) { if (n > 1) qsort(a, n, sizeof a[0], cmp_i32); }

/* ================================ Part A ================================ */

/* 정렬된 a[0..n) 에서 a[i] + a[j] == target (i < j) 인 쌍을 찾는다.
 * 여러 쌍이면: i 가 가장 작은 쌍, 그중 j 가 가장 큰 쌍. 찾으면 1 (*pi, *pj 기록), 없으면 0. */
static int pair_sum_sorted(const int32_t *a, size_t n, int32_t target, size_t *pi, size_t *pj) {
    (void)a; (void)n; (void)target; (void)pi; (void)pj;
    return 0; /* TODO: lo = 0, hi = n-1 에서 시작해 합(int64!)을 target 과 비교하며 좁힌다 */
}

/* 정렬된 a 의 중복을 제자리 제거. 새 길이 k 를 돌려주고 a[0..k) = 서로 다른 값 (오름차순). */
static size_t dedup_sorted(int32_t *a, size_t n) {
    (void)a;
    return n; /* TODO: 쓰기 포인터 w, 읽기 포인터 r */
}

/* 정렬된 a, b 를 out[0..na+nb) 로 병합. 같은 값이면 a 쪽이 먼저 (안정). out 은 a, b 와 겹치지 않는다. */
static void merge_sorted(const int32_t *a, size_t na, const int32_t *b, size_t nb, int32_t *out) {
    (void)a; (void)na; (void)b; (void)nb; (void)out;
    /* TODO: 앞에서부터 작은 쪽을 꺼낸다. 같으면 a 먼저 */
}

/* ================================ Part B ================================ */

/* 정렬된 a 에서 각 값을 최대 k 개까지만 남긴다 (k >= 1). k == 2 가 LeetCode 80. 반환 = 새 길이. */
static size_t dedup_keep_k(int32_t *a, size_t n, size_t k) {
    (void)a; (void)k;
    return n; /* TODO: 이미 쓴 영역의 a[w-k] 와 비교 */
}

/* a 의 앞 na 칸이 정렬, 배열 전체 길이는 na + nb (뒤 nb 칸은 쓰레기). b[0..nb) 정렬.
 * 결과: a[0..na+nb) 정렬. 같은 값이면 원래 a 의 원소가 앞 (안정). 뒤에서부터 채운다. */
static void merge_into_a(int32_t *a, size_t na, const int32_t *b, size_t nb) {
    (void)a; (void)na; (void)b; (void)nb;
    /* TODO: 맨 뒤 칸부터 큰 쪽을 채운다 (size_t 가 0 아래로 내려가지 않게) */
}

/* a 를 제자리 정렬한 뒤, 합이 0 인 서로 다른 값-세쌍 (x <= y <= z) 을 사전순으로 out 에 쓴다.
 * 반환 = 세쌍의 전체 개수 (max_out 를 넘는 것은 세기만 하고 쓰지 않는다 — snprintf 식). */
static size_t three_sum_zero(int32_t *a, size_t n, int32_t (*out)[3], size_t max_out) {
    (void)a; (void)n; (void)out; (void)max_out;
    return 0; /* TODO: sort_i32 → x 고정 + 나머지 구간 투 포인터, 중복 건너뛰기 */
}

/* ================================ Part C ================================ */

typedef struct {
    uint32_t ts_us;    /* 수신 타임스탬프 (µs). 이 레슨에선 랩어라운드 없음 (Q&A 참고) */
    uint16_t can_id;   /* 11비트 표준 ID */
    uint16_t seq;      /* 버스 안 수신 순번 — 테스트가 안정성 확인에 쓴다 */
    uint8_t  bus;      /* 0 = CAN1, 1 = CAN2 */
} can_ev_t;

/* ts 오름차순으로 병합. 같은 ts 면 a (CAN1) 먼저, 각 버스 안 순서는 유지 (안정).
 * out 은 cap 칸: 가장 이른 cap 개만 쓴다. 반환 = 쓴 개수. */
static size_t merge_events(const can_ev_t *a, size_t na, const can_ev_t *b, size_t nb,
                           can_ev_t *out, size_t cap) {
    (void)a; (void)na; (void)b; (void)nb; (void)out; (void)cap;
    return 0; /* TODO: ts 로 병합, 같은 ts 면 a 먼저, cap 개에서 멈춤 */
}

/* 정렬된 (중복 있을 수 있는) 7비트 주소 목록 두 개의 합집합 / 교집합을 중복 없이 오름차순으로.
 * out 은 na + nb 칸 이상. 반환 = 쓴 개수. */
static size_t scan_union(const uint8_t *a, size_t na, const uint8_t *b, size_t nb, uint8_t *out) {
    (void)a; (void)na; (void)b; (void)nb; (void)out;
    return 0; /* TODO: 병합하면서 마지막에 쓴 값과 같으면 건너뛴다 */
}
static size_t scan_intersect(const uint8_t *a, size_t na, const uint8_t *b, size_t nb, uint8_t *out) {
    (void)a; (void)na; (void)b; (void)nb; (void)out;
    return 0; /* TODO: 작은 쪽을 전진, 같으면 기록 후 둘 다 전진 */
}

/* ======================= 테스트 하네스 (수정 불필요) ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t g_rng = 0x2545F491u;                 /* xorshift32, 고정 시드 */
static uint32_t rnd(void) { uint32_t x = g_rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return g_rng = x; }
static int32_t rnd_in(int32_t lo, int32_t hi) {      /* [lo, hi], 범위 폭 < 2^31 */
    return (int32_t)(lo + (int64_t)(rnd() % (uint32_t)((int64_t)hi - lo + 1)));
}
static int32_t rnd_full(void) { return (int32_t)((int64_t)rnd() - 2147483648LL); }  /* 구현 정의 변환 회피 */
static void rnd_sorted(int32_t *a, size_t n, int32_t lo, int32_t hi) {
    for (size_t i = 0; i < n; i++) a[i] = rnd_in(lo, hi);
    sort_i32(a, n);
}

/* --- 참조 구현 (브루트포스) --- */
static int ref_pair(const int32_t *a, size_t n, int32_t t, size_t *pi, size_t *pj) {
    for (size_t i = 0; i < n; i++)                    /* 가장 작은 i, 그중 가장 큰 j */
        for (size_t j = n; j-- > i + 1;)
            if ((int64_t)a[i] + a[j] == t) { *pi = i; *pj = j; return 1; }
    return 0;
}
static size_t ref_keep_k(const int32_t *a, size_t n, size_t k, int32_t *out) {
    size_t m = 0;
    for (size_t r = 0; r < n;) {                      /* 같은 값 묶음(run) 단위로 min(len, k) 개 */
        size_t e = r;
        while (e < n && a[e] == a[r]) e++;
        for (size_t c = 0; c < e - r && c < k; c++) out[m++] = a[r];
        r = e;
    }
    return m;
}
static int cmp_tri(const void *p, const void *q) {
    const int32_t *x = p, *y = q;
    for (int t = 0; t < 3; t++) if (x[t] != y[t]) return (x[t] > y[t]) - (x[t] < y[t]);
    return 0;
}
static size_t ref_three_sum(const int32_t *a, size_t n, int32_t (*out)[3]) {   /* O(n^3) + 정규화 */
    size_t m = 0;
    for (size_t i = 0; i < n; i++)
        for (size_t j = i + 1; j < n; j++)
            for (size_t k = j + 1; k < n; k++)
                if ((int64_t)a[i] + a[j] + a[k] == 0) {
                    int32_t t[3] = { a[i], a[j], a[k] };
                    sort_i32(t, 3);
                    memcpy(out[m++], t, sizeof t);
                }
    if (m > 1) qsort(out, m, sizeof out[0], cmp_tri);
    size_t u = 0;                                     /* 같은 세쌍 제거 */
    for (size_t r = 0; r < m; r++)
        if (u == 0 || cmp_tri(out[u - 1], out[r]) != 0) memcpy(out[u++], out[r], sizeof out[0]);
    return u;
}
static int cmp_ev(const void *p, const void *q) {    /* 안정 병합 결과 = (ts, bus, seq) 정렬 */
    const can_ev_t *x = p, *y = q;
    if (x->ts_us != y->ts_us) return x->ts_us < y->ts_us ? -1 : 1;
    if (x->bus != y->bus) return x->bus < y->bus ? -1 : 1;
    return (x->seq > y->seq) - (x->seq < y->seq);
}
static size_t ref_scan(const uint8_t *a, size_t na, const uint8_t *b, size_t nb, bool inter, uint8_t *out) {
    bool ina[256] = { false }, inb[256] = { false };
    for (size_t i = 0; i < na; i++) ina[a[i]] = true;
    for (size_t j = 0; j < nb; j++) inb[b[j]] = true;
    size_t k = 0;
    for (int v = 0; v < 256; v++)
        if (inter ? (ina[v] && inb[v]) : (ina[v] || inb[v])) out[k++] = (uint8_t)v;
    return k;
}
static void gen_stream(can_ev_t *s, size_t n, uint8_t bus, uint32_t t0) {
    uint32_t t = t0;
    for (size_t i = 0; i < n; i++) {
        t += rnd() % 4;                               /* 같은 ts 도 나오게 (0 증가) */
        s[i] = (can_ev_t){ .ts_us = t, .can_id = (uint16_t)(rnd() & 0x7FF), .seq = (uint16_t)i, .bus = bus };
    }
}
static void gen_scan(uint8_t *s, size_t n) {
    for (size_t i = 0; i < n; i++) s[i] = (uint8_t)(0x08 + rnd() % 24);   /* 0x08..0x1F, 중복 많이 */
    for (size_t i = 1; i < n; i++)                    /* 삽입 정렬 (작다) */
        for (size_t j = i; j > 0 && s[j - 1] > s[j]; j--) { uint8_t t = s[j]; s[j] = s[j - 1]; s[j - 1] = t; }
}

#define NMAX 64
int main(void) {
    int32_t a[2 * NMAX], b[NMAX], out[2 * NMAX], ref[2 * NMAX];
    size_t i, j, ri, rj;

    /* ---------- Part A: pair_sum_sorted ---------- */
    { int32_t x[] = { 2, 3, 4, 5, 8, 11, 18 };
      CHECK(pair_sum_sorted(x, 7, 8, &i, &j) == 1 && i == 1 && j == 3); }
    { int32_t x[] = { 1, 1, 5, 5 };                    /* 쌍 4개 → (0,3) */
      CHECK(pair_sum_sorted(x, 4, 6, &i, &j) == 1 && i == 0 && j == 3); }
    { int32_t x[] = { 3, 3 };
      CHECK(pair_sum_sorted(x, 2, 6, &i, &j) == 1 && i == 0 && j == 1);
      CHECK(pair_sum_sorted(x, 1, 6, &i, &j) == 0);  /* 원소 하나를 두 번 쓰면 안 됨 */
      CHECK(pair_sum_sorted(x, 0, 6, &i, &j) == 0); }
    { int32_t x[] = { INT32_MIN, -1, 0, INT32_MAX };   /* INT32_MIN + INT32_MAX = -1 */
      CHECK(pair_sum_sorted(x, 4, -1, &i, &j) == 1 && i == 0 && j == 3); }
    { int32_t x[] = { INT32_MAX - 1, INT32_MAX };      /* 진짜 합 4294967293, int32 로 wrap 하면 -3 */
      CHECK(pair_sum_sorted(x, 2, -3, &i, &j) == 0); }
    { int32_t x[] = { 1, 2, INT32_MAX };               /* 2 + MAX 가 wrap 하면 MIN+1 이 "보인다" */
      CHECK(pair_sum_sorted(x, 3, INT32_MIN + 1, &i, &j) == 0);
      CHECK(pair_sum_sorted(x, 3, 3, &i, &j) == 1 && i == 0 && j == 1); }
    {   int bad = 0;
        for (int c = 0; c < 20000; c++) {              /* 작은 값: 중복·다중 해 많음 */
            size_t n = rnd() % 41; rnd_sorted(a, n, -20, 20);
            int32_t t = rnd_in(-42, 42);
            int r1 = pair_sum_sorted(a, n, t, &i, &j), r2 = ref_pair(a, n, t, &ri, &rj);
            if (r1 != r2 || (r1 && (i != ri || j != rj))) bad++;
        }
        for (int c = 0; c < 5000; c++) {               /* 전 범위 값: overflow 경계 */
            size_t n = 2 + rnd() % 30;
            for (size_t k = 0; k < n; k++) a[k] = rnd_full();
            sort_i32(a, n);
            int64_t s = (int64_t)a[rnd() % n] + a[rnd() % n];
            int32_t t = (s >= INT32_MIN && s <= INT32_MAX && (rnd() & 1)) ? (int32_t)s : rnd_full();
            int r1 = pair_sum_sorted(a, n, t, &i, &j), r2 = ref_pair(a, n, t, &ri, &rj);
            if (r1 != r2 || (r1 && (i != ri || j != rj))) bad++;
        }
        printf("pair_sum   : 25000 random cases vs O(n^2), mismatches=%d\n", bad);
        CHECK(bad == 0);
    }

    /* ---------- Part A: dedup_sorted ---------- */
    { int32_t x[] = { 0, 0, 1, 1, 1, 2, 2, 3, 3, 4 };
      size_t k = dedup_sorted(x, 10);
      CHECK(k == 5 && x[0] == 0 && x[1] == 1 && x[2] == 2 && x[3] == 3 && x[4] == 4); }
    { int32_t x[] = { 7, 7, 7, 7 };
      CHECK(dedup_sorted(x, 4) == 1 && x[0] == 7);
      CHECK(dedup_sorted(x, 0) == 0);
      CHECK(dedup_sorted(x, 1) == 1); }
    {   int bad = 0;
        for (int c = 0; c < 5000; c++) {
            size_t n = rnd() % 51; rnd_sorted(a, n, -8, 8);
            size_t m = ref_keep_k(a, n, 1, ref);
            size_t k = dedup_sorted(a, n);
            if (k != m || memcmp(a, ref, m * sizeof a[0]) != 0) bad++;
        }
        printf("dedup      : 5000 random cases vs reference, mismatches=%d\n", bad);
        CHECK(bad == 0);
    }

    /* ---------- Part A: merge_sorted ---------- */
    { int32_t x[] = { 1, 3, 5 }, y[] = { 2, 3, 4, 9 }, o[7] = { 0 };
      merge_sorted(x, 3, y, 4, o);
      int32_t e[] = { 1, 2, 3, 3, 4, 5, 9 };
      CHECK(memcmp(o, e, sizeof e) == 0); }
    { int32_t x[] = { INT32_MIN, INT32_MAX }, o[2] = { 0, 0 };
      merge_sorted(x, 2, NULL, 0, o);
      CHECK(o[0] == INT32_MIN && o[1] == INT32_MAX);
      merge_sorted(NULL, 0, x, 2, o);
      CHECK(o[0] == INT32_MIN && o[1] == INT32_MAX); }
    {   int bad = 0;
        for (int c = 0; c < 5000; c++) {
            size_t na = rnd() % 33, nb = rnd() % 33;
            rnd_sorted(a, na, -30, 30); rnd_sorted(b, nb, -30, 30);
            memcpy(ref, a, na * sizeof a[0]); memcpy(ref + na, b, nb * sizeof b[0]);
            sort_i32(ref, na + nb);
            memset(out, 0x7F, sizeof out);
            merge_sorted(a, na, b, nb, out);
            if (memcmp(out, ref, (na + nb) * sizeof out[0]) != 0) bad++;
        }
        printf("merge      : 5000 random cases vs sort(concat), mismatches=%d\n", bad);
        CHECK(bad == 0);
    }

    /* ---------- Part B: dedup_keep_k ---------- */
    { int32_t x[] = { 1, 1, 1, 2, 2, 3 };              /* LeetCode 80 예 → 1 1 2 2 3 */
      size_t k = dedup_keep_k(x, 6, 2);
      CHECK(k == 5 && x[0] == 1 && x[1] == 1 && x[2] == 2 && x[3] == 2 && x[4] == 3); }
    { int32_t x[] = { 0, 0, 1, 1, 1, 1, 2, 3, 3 };    /* → 0 0 1 1 2 3 3 */
      size_t k = dedup_keep_k(x, 9, 2);
      int32_t e[] = { 0, 0, 1, 1, 2, 3, 3 };
      CHECK(k == 7 && memcmp(x, e, sizeof e) == 0); }
    { int32_t x[] = { 5, 5, 5, 5, 5 };
      CHECK(dedup_keep_k(x, 5, 2) == 2);
      CHECK(dedup_keep_k(x, 1, 2) == 1);
      CHECK(dedup_keep_k(x, 0, 2) == 0); }
    {   int bad = 0;
        for (int c = 0; c < 6000; c++) {
            size_t n = rnd() % 51, k = 1 + rnd() % 3; rnd_sorted(a, n, -5, 5);
            size_t m = ref_keep_k(a, n, k, ref);
            size_t w = dedup_keep_k(a, n, k);
            if (w != m || memcmp(a, ref, m * sizeof a[0]) != 0) bad++;
        }
        printf("keep_k     : 6000 random cases (k=1..3) vs run-length reference, mismatches=%d\n", bad);
        CHECK(bad == 0);
    }

    /* ---------- Part B: merge_into_a (뒤에서 채우기) ---------- */
    { int32_t x[6] = { 1, 2, 3, -99, -99, -99 }, y[] = { 2, 5, 6 };   /* LeetCode 88 예 */
      merge_into_a(x, 3, y, 3);
      int32_t e[] = { 1, 2, 2, 3, 5, 6 };
      CHECK(memcmp(x, e, sizeof e) == 0); }
    { int32_t x[3] = { -99, -99, -99 }, y[] = { -4, 0, 4 };          /* A 가 비었다 */
      merge_into_a(x, 0, y, 3);
      CHECK(x[0] == -4 && x[1] == 0 && x[2] == 4); }
    { int32_t x[2] = { 8, 9 };                                       /* B 가 비었다 */
      merge_into_a(x, 2, NULL, 0);
      CHECK(x[0] == 8 && x[1] == 9); }
    { int32_t x[4] = { 7, 8, -99, -99 }, y[] = { 1, 2 };             /* B 전부가 A 보다 작다 */
      merge_into_a(x, 2, y, 2);
      CHECK(x[0] == 1 && x[1] == 2 && x[2] == 7 && x[3] == 8); }
    {   int bad = 0;
        for (int c = 0; c < 5000; c++) {
            size_t na = rnd() % 33, nb = rnd() % 33;
            rnd_sorted(a, na, -30, 30); rnd_sorted(b, nb, -30, 30);
            for (size_t k = na; k < na + nb; k++) a[k] = 0x7F7F7F7F;   /* 쓰레기 */
            memcpy(ref, a, na * sizeof a[0]); memcpy(ref + na, b, nb * sizeof b[0]);
            sort_i32(ref, na + nb);
            merge_into_a(a, na, b, nb);
            if (memcmp(a, ref, (na + nb) * sizeof a[0]) != 0) bad++;
        }
        printf("merge_into : 5000 random cases vs sort(concat), mismatches=%d\n", bad);
        CHECK(bad == 0);
    }

    /* ---------- Part B: three_sum_zero ---------- */
    static int32_t tri[4096][3], rtri[4096][3];
    { int32_t x[] = { -1, 0, 1, 2, -1, -4 };          /* LeetCode 15 예 → [-1,-1,2] [-1,0,1] */
      size_t m = three_sum_zero(x, 6, tri, 4096);
      CHECK(m == 2 && tri[0][0] == -1 && tri[0][1] == -1 && tri[0][2] == 2
                   && tri[1][0] == -1 && tri[1][1] == 0 && tri[1][2] == 1); }
    { int32_t x[] = { 0, 0, 0, 0, 0 };
      size_t m = three_sum_zero(x, 5, tri, 4096);
      CHECK(m == 1 && tri[0][0] == 0 && tri[0][1] == 0 && tri[0][2] == 0); }
    { int32_t x[] = { 0, 1, 1 };
      CHECK(three_sum_zero(x, 3, tri, 4096) == 0);
      CHECK(three_sum_zero(x, 2, tri, 4096) == 0); }
    { int32_t x[] = { INT32_MAX, INT32_MIN, 1, INT32_MAX, -1, 0 };  /* MAX+MIN+1 = 0, MAX+MAX 는 int32 밖 */
      size_t m = three_sum_zero(x, 6, tri, 4096);
      CHECK(m == 2 && tri[0][0] == INT32_MIN && tri[0][1] == 1 && tri[0][2] == INT32_MAX
                   && tri[1][0] == -1 && tri[1][1] == 0 && tri[1][2] == 1); }
    { int32_t x[] = { -2, -1, -1, 0, 1, 2, 3 };        /* 4개: [-2,-1,3] [-2,0,2] [-1,-1,2] [-1,0,1] */
      size_t m = three_sum_zero(x, 7, tri, 2);         /* 2개만 담을 공간 → 개수는 4, 앞 2개만 */
      CHECK(m == 4 && tri[0][2] == 3 && tri[1][1] == 0 && tri[1][2] == 2); }
    {   int bad = 0;
        for (int c = 0; c < 2500; c++) {
            size_t n = rnd() % 29;
            for (size_t k = 0; k < n; k++) a[k] = rnd_in(-12, 12);
            size_t m = ref_three_sum(a, n, rtri);     /* 정렬 전 원본으로 */
            size_t w = three_sum_zero(a, n, tri, 4096);
            if (w != m || memcmp(tri, rtri, m * sizeof tri[0]) != 0) bad++;
        }
        static const int32_t ext[] = { INT32_MIN, INT32_MIN + 1, -1073741824, -2, -1, 0, 1, 2,
                                       1073741824, 1073741823, INT32_MAX - 1, INT32_MAX };
        for (int c = 0; c < 2000; c++) {               /* 극단값: int32 합이면 wrap 하는 조합 */
            size_t n = rnd() % 13;
            for (size_t k = 0; k < n; k++) a[k] = ext[rnd() % 12];
            size_t m = ref_three_sum(a, n, rtri);
            size_t w = three_sum_zero(a, n, tri, 4096);
            if (w != m || memcmp(tri, rtri, m * sizeof tri[0]) != 0) bad++;
        }
        printf("three_sum  : 4500 random cases vs O(n^3) canonical set, mismatches=%d\n", bad);
        CHECK(bad == 0);
    }

    /* ---------- Part C: merge_events (CAN1 + CAN2 → 하나의 타임라인) ---------- */
    static can_ev_t ea[NMAX], eb[NMAX], eo[2 * NMAX], er[2 * NMAX];
    {   /* 장면: CAN1 = IMU 0x101 (1 ms 주기), CAN2 = 휠 엔코더 0x201/0x202 (2.5 ms 주기, 2000 µs 에 동시 도착) */
        can_ev_t c1[] = { { 1000, 0x101, 0, 0 }, { 2000, 0x101, 1, 0 }, { 3000, 0x101, 2, 0 }, { 4000, 0x101, 3, 0 } };
        can_ev_t c2[] = { { 0, 0x201, 0, 1 }, { 2000, 0x202, 1, 1 }, { 2500, 0x201, 2, 1 }, { 5000, 0x202, 3, 1 } };
        size_t k = merge_events(c1, 4, c2, 4, eo, 8);
        printf("timeline   :");
        for (size_t t = 0; t < k; t++) printf(" %u/CAN%u:%03X", (unsigned)eo[t].ts_us, eo[t].bus + 1u, eo[t].can_id);
        printf("\n");
        CHECK(k == 8 && eo[0].ts_us == 0 && eo[2].ts_us == 2000 && eo[2].bus == 0 && eo[3].bus == 1
                     && eo[7].ts_us == 5000);
        k = merge_events(c1, 4, c2, 4, eo, 3);        /* 버퍼 3칸 → 가장 이른 3개 */
        CHECK(k == 3 && eo[0].ts_us == 0 && eo[1].ts_us == 1000 && eo[2].ts_us == 2000 && eo[2].bus == 0);
        CHECK(merge_events(c1, 0, c2, 0, eo, 8) == 0);
        CHECK(merge_events(c1, 4, c2, 4, eo, 0) == 0);
    }
    {   int bad = 0;
        for (int c = 0; c < 4000; c++) {
            size_t na = rnd() % (NMAX + 1), nb = rnd() % (NMAX + 1), cap = rnd() % (na + nb + 2);
            gen_stream(ea, na, 0, rnd() % 50); gen_stream(eb, nb, 1, rnd() % 50);
            memcpy(er, ea, na * sizeof ea[0]); memcpy(er + na, eb, nb * sizeof eb[0]);
            if (na + nb > 1) qsort(er, na + nb, sizeof er[0], cmp_ev);
            size_t want = cap < na + nb ? cap : na + nb;
            size_t k = merge_events(ea, na, eb, nb, eo, cap);
            if (k != want) { bad++; continue; }
            for (size_t t = 0; t < k; t++)
                if (eo[t].ts_us != er[t].ts_us || eo[t].bus != er[t].bus || eo[t].seq != er[t].seq) { bad++; break; }
        }
        printf("merge_ev   : 4000 random stream pairs vs sort by (ts, bus, seq), mismatches=%d\n", bad);
        CHECK(bad == 0);
    }

    /* ---------- Part C: I2C 스캔 두 번 → 안정 장치 / 불안정 장치 ---------- */
    {   uint8_t p1[] = { 0x1E, 0x1E, 0x48, 0x50, 0x68 };          /* 1회차: 0x1E 가 재시도로 두 번 기록 */
        uint8_t p2[] = { 0x1E, 0x48, 0x68, 0x68, 0x76 };          /* 2회차: 0x50 사라짐, 0x76 새로 */
        uint8_t u[16], s[16];
        size_t nu = scan_union(p1, 5, p2, 5, u), ns = scan_intersect(p1, 5, p2, 5, s);
        printf("i2c scan   : seen=");
        for (size_t t = 0; t < nu; t++) printf("%s0x%02X", t ? "," : "", u[t]);
        printf("  stable=");
        for (size_t t = 0; t < ns; t++) printf("%s0x%02X", t ? "," : "", s[t]);
        printf("  flaky=");
        for (size_t t = 0, q = 0, first = 1; t < nu; t++) {       /* seen - stable (두 포인터 차집합) */
            while (q < ns && s[q] < u[t]) q++;
            if (q < ns && s[q] == u[t]) continue;
            printf("%s0x%02X", first ? "" : ",", u[t]); first = 0;
        }
        printf("\n");
        uint8_t eu[] = { 0x1E, 0x48, 0x50, 0x68, 0x76 }, es[] = { 0x1E, 0x48, 0x68 };
        CHECK(nu == 5 && memcmp(u, eu, 5) == 0);
        CHECK(ns == 3 && memcmp(s, es, 3) == 0);
        CHECK(scan_union(p1, 0, p2, 0, u) == 0 && scan_intersect(p1, 5, p2, 0, s) == 0);
    }
    {   int bad = 0;
        uint8_t sa[NMAX], sb[NMAX], so[2 * NMAX], sr[256];
        for (int c = 0; c < 5000; c++) {
            size_t na = rnd() % 41, nb = rnd() % 41;
            gen_scan(sa, na); gen_scan(sb, nb);
            for (int inter = 0; inter < 2; inter++) {
                size_t m = ref_scan(sa, na, sb, nb, inter, sr);
                size_t k = inter ? scan_intersect(sa, na, sb, nb, so) : scan_union(sa, na, sb, nb, so);
                if (k != m || memcmp(so, sr, m) != 0) bad++;
            }
        }
        printf("scan       : 5000 random list pairs x {union, intersect} vs bitmap, mismatches=%d\n", bad);
        CHECK(bad == 0);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
