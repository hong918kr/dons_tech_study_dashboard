/* ALGO-B07 머지소트: Top-down·Bottom-up·연결 리스트
 *
 *   Part A  merge_into(): 정렬된 두 구간을 안정(stable)하게 병합 + 배열 top-down 재귀 머지소트
 *           (호출자가 준 scratch 버퍼 하나만 사용 — 호출마다 malloc 하지 않는다)
 *   Part B  반복 bottom-up 머지소트: 폭 1,2,4,… / n 이 2의 거듭제곱이 아닐 때 꼬리 run 처리 / ping-pong 버퍼
 *   Part C  단일 연결 리스트 bottom-up 머지소트 — 추가 메모리 O(1), 재귀 없음, 노드 재연결만
 *           펌웨어: 정적 풀에 있는 대기 중 플래시 요청(ERASE/PROGRAM) 리스트를 블록 번호로 묶기
 *
 * 검증: qsort 결과와 "키 순서"만 대조 (qsort 는 안정 정렬을 보장하지 않는다),
 *       안정성은 (key, seq) 레코드로 따로 검사, n ≤ 64 는 안정 삽입 정렬(기준 구현)과 레코드 단위 일치.
 *       입력: 랜덤 / 정렬 / 역순 / 전부 같음 / 키 4종류(중복 많음), n = 0..64 전수 + 1000·4097·100000.
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o st && ./st   (solution.c 도 같은 명령)
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { int32_t key; uint32_t seq; } rec_t;   /* seq = 입력에서의 원래 위치 (안정성 검사용) */

static unsigned long g_cmp;                             /* 키 비교 횟수 (측정용) */
#define KEY_LE(x, y) (g_cmp++, (x).key <= (y).key)      /* 배열 병합은 이 매크로로만 비교한다 */

/* ======================= Part A: 병합 + top-down ======================= */
/* src[lo,mid) 와 src[mid,hi) (각각 정렬됨) 를 dst[lo,hi) 로 병합한다. 키가 같으면 왼쪽 먼저 → 안정.
 * dst 와 src 는 겹치지 않는다. mid == lo 또는 mid == hi (한쪽이 빔) 이면 그냥 복사가 된다. */
static void merge_into(rec_t *dst, const rec_t *src, size_t lo, size_t mid, size_t hi) {
    size_t i = lo, j = mid, k = lo;
    while (i < mid && j < hi)
        dst[k++] = KEY_LE(src[i], src[j]) ? src[i++] : src[j++];   /* <= : 같으면 왼쪽(먼저 온 것) */
    while (i < mid) dst[k++] = src[i++];
    while (j < hi)  dst[k++] = src[j++];
}

/* a[lo,hi) 정렬. tmp 는 a 와 같은 인덱스를 쓰는 scratch (tmp[lo,hi) 만 건드린다). */
static void td_rec(rec_t *a, rec_t *tmp, size_t lo, size_t hi) {
    if (hi - lo < 2) return;
    size_t mid = lo + (hi - lo) / 2;                          /* (lo+hi)/2 는 overflow 가능 */
    td_rec(a, tmp, lo, mid);
    td_rec(a, tmp, mid, hi);
    memcpy(tmp + lo, a + lo, (hi - lo) * sizeof *a);          /* 두 run 을 scratch 로 */
    merge_into(a, tmp, lo, mid, hi);                          /* scratch → 제자리 병합 */
}

/* a[0,n) 를 안정 정렬. tmp = 호출자가 준 n 칸 scratch (n < 2 이면 NULL 이어도 된다). 재귀 깊이 ⌈log2 n⌉+1. */
static void msort_topdown(rec_t *a, size_t n, rec_t *tmp) {
    if (n < 2) return;
    td_rec(a, tmp, 0, n);
}

/* ======================= Part B: bottom-up (반복) ======================= */
/* 폭 w = 1,2,4,… 인 인접 run 쌍을 src → dst 로 병합하고 src/dst 를 바꾼다 (ping-pong: 패스마다 복사 1번).
 * 꼬리: 남은 원소가 w 이하면 왼쪽 run 만 있고 오른쪽은 빈 run (merge_into 가 복사), w 초과 2w 미만이면 오른쪽이 짧다. */
static void msort_bottomup(rec_t *a, size_t n, rec_t *tmp) {
    if (n < 2) return;
    rec_t *src = a, *dst = tmp;
    for (size_t w = 1; w < n; ) {
        for (size_t lo = 0; lo < n; ) {
            size_t mid = (n - lo > w) ? lo + w : n;           /* lo + w 를 직접 더하지 않는다 (overflow 안전) */
            size_t hi  = (n - mid > w) ? mid + w : n;
            merge_into(dst, src, lo, mid, hi);
            lo = hi;
        }
        rec_t *t = src; src = dst; dst = t;                   /* 이번 패스 결과가 다음 패스의 입력 */
        if (w > n / 2) break;                                 /* 2w > n: 방금이 마지막 패스, 2w overflow 도 방지 */
        w *= 2;
    }
    if (src != a) memcpy(a, src, n * sizeof *a);              /* 패스 수가 홀수면 결과가 tmp 에 있다 */
}

/* ======================= Part C: 연결 리스트 (bottom-up, O(1) 추가 메모리) ======================= */
enum { OP_ERASE = 0, OP_PROGRAM = 1 };
typedef struct fl_req {
    struct fl_req *next;
    uint32_t block;            /* 정렬 키: 물리 블록 번호 */
    uint32_t seq;              /* 도착 순서 */
    uint16_t page;             /* PROGRAM 할 페이지 (블록 안에서는 오름차순으로만 프로그램 가능) */
    uint8_t  op;               /* OP_ERASE / OP_PROGRAM */
} fl_req_t;

#define REQ_LE(x, y) (g_cmp++, (x)->block <= (y)->block)

/* head 부터 최대 k 개(k ≥ 1)를 앞 리스트로 남기고 끊는다. 반환 = 나머지 리스트 head (없으면 NULL). */
static fl_req_t *list_split(fl_req_t *head, size_t k) {
    while (head && --k) head = head->next;                    /* k-1 칸 이동 → 앞 리스트의 마지막 노드 */
    if (!head) return NULL;
    fl_req_t *rest = head->next;
    head->next = NULL;
    return rest;
}

/* 정렬된 두 리스트 a(원래 앞쪽), b 를 안정 병합. 반환 = 새 head, *tail_out = 마지막 노드 (빈 결과면 NULL).
 * 노드는 새로 만들지 않고 next 만 바꾼다. pp = "다음 노드를 써 넣을 자리" (dummy 노드 대신 포인터의 포인터). */
static fl_req_t *list_merge(fl_req_t *a, fl_req_t *b, fl_req_t **tail_out) {
    fl_req_t *head = NULL, **pp = &head, *last = NULL;
    while (a && b) {
        if (REQ_LE(a, b)) { *pp = a; a = a->next; }           /* <= : 같은 블록이면 a(먼저 도착) 먼저 */
        else              { *pp = b; b = b->next; }
        last = *pp;
        pp = &last->next;
    }
    *pp = a ? a : b;                                          /* 남은 쪽을 통째로 붙인다 (O(1)) */
    for (fl_req_t *p = *pp; p; p = p->next) last = p;         /* tail = 남은 쪽의 끝 (빈 결과면 NULL) */
    *tail_out = last;
    return head;
}

/* 리스트를 block 오름차순으로 안정 정렬. 재귀 없음, 추가 메모리 = 포인터 몇 개 (O(1)). 반환 = 새 head. */
static fl_req_t *list_sort(fl_req_t *head) {
    size_t n = 0;
    for (fl_req_t *p = head; p; p = p->next) n++;
    for (size_t w = 1; w < n; ) {
        fl_req_t *rest = head, *out = NULL, **pp = &out;      /* pp = 결과 리스트 끝에 붙일 자리 */
        while (rest) {
            fl_req_t *a = rest;
            fl_req_t *b = list_split(a, w);                   /* a = 앞 w 개 */
            rest = list_split(b, w);                          /* b = 다음 w 개 (NULL 일 수도) */
            fl_req_t *tail;
            *pp = list_merge(a, b, &tail);
            pp = &tail->next;
        }
        head = out;
        if (w > n / 2) break;
        w *= 2;
    }
    return head;
}

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t g_rng = 0x2545F491u;
static uint32_t rnd(void) { uint32_t x = g_rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return g_rng = x; }

enum { PAT_RANDOM, PAT_SORTED, PAT_REVERSED, PAT_EQUAL, PAT_FEWKEYS, NPAT };

static void gen(rec_t *a, size_t n, int pat) {
    for (size_t i = 0; i < n; i++) {
        int32_t k = 0;
        switch (pat) {
        case PAT_RANDOM:   k = (int32_t)(rnd() >> 1); break;            /* 0 .. 2^31-1 */
        case PAT_SORTED:   k = (int32_t)i; break;
        case PAT_REVERSED: k = (int32_t)(n - i); break;
        case PAT_EQUAL:    k = 7; break;
        default:           k = (int32_t)(rnd() % 4u); break;
        }
        a[i].key = k; a[i].seq = (uint32_t)i;
    }
}

static int cmp_i32(const void *p, const void *q) {          /* x - y 는 overflow → 비교 결과로 */
    int32_t x = *(const int32_t *)p, y = *(const int32_t *)q;
    return (x > y) - (x < y);
}

static void ref_stable(rec_t *a, size_t n) {                 /* 기준 구현: 안정 삽입 정렬 (ALGO-B05) */
    for (size_t i = 1; i < n; i++) {
        rec_t x = a[i]; size_t j = i;
        while (j > 0 && a[j - 1].key > x.key) { a[j] = a[j - 1]; j--; }
        a[j] = x;
    }
}

static unsigned ceil_lg(size_t n) { unsigned c = 0; while (((size_t)1 << c) < n) c++; return c; }

#define MAXN 100000
static rec_t   g_orig[MAXN], g_a[MAXN + 1], g_tmp[MAXN + 1], g_ref[MAXN];
static int32_t g_keys[MAXN];
static uint8_t g_seen[MAXN];
static fl_req_t g_pool[MAXN];
static const rec_t GUARD = { 0x5A5A5A5A, 0xDEADBEEFu };

typedef void (*sort_fn)(rec_t *, size_t, rec_t *);
typedef struct { int keys, perm, stable, ref, guard, cmp; } fails_t;

/* 공통 판정: 키 순서 = qsort, 순열(레코드 보존), 안정성, (작은 n) 기준 구현과 완전 일치 */
static void judge(const rec_t *out, size_t n, fails_t *F) {
    for (size_t i = 0; i < n; i++) g_keys[i] = g_orig[i].key;
    qsort(g_keys, n, sizeof g_keys[0], cmp_i32);
    size_t i;
    for (i = 0; i < n && out[i].key == g_keys[i]; i++) {}
    if (i < n) F->keys++;
    memset(g_seen, 0, n);
    bool perm = true, stable = true;
    for (i = 0; i < n; i++) {
        uint32_t s = out[i].seq;
        if (s >= n || g_seen[s] || g_orig[s].key != out[i].key) { perm = false; break; }
        g_seen[s] = 1;
        if (i > 0 && out[i - 1].key == out[i].key && out[i - 1].seq > out[i].seq) stable = false;
    }
    if (!perm) F->perm++;
    if (!stable) F->stable++;
    if (n <= 64) {
        memcpy(g_ref, g_orig, n * sizeof *g_ref);
        ref_stable(g_ref, n);
        for (i = 0; i < n && g_ref[i].key == out[i].key && g_ref[i].seq == out[i].seq; i++) {}
        if (i < n) F->ref++;
    }
}

static void run_array(sort_fn f, size_t n, int pat, fails_t *F) {
    gen(g_orig, n, pat);
    memcpy(g_a, g_orig, n * sizeof *g_a);
    g_a[n] = GUARD; g_tmp[n] = GUARD;                         /* 끝 바로 뒤에 쓰면 들킨다 */
    g_cmp = 0;
    f(g_a, n, g_tmp);
    if (g_a[n].key != GUARD.key || g_a[n].seq != GUARD.seq ||
        g_tmp[n].key != GUARD.key || g_tmp[n].seq != GUARD.seq) F->guard++;
    if (g_cmp > (unsigned long)n * ceil_lg(n)) F->cmp++;     /* 비교 ≤ n⌈lg n⌉ */
    judge(g_a, n, F);
}

static fails_t suite_array(sort_fn f, const char *name) {
    fails_t F = { 0 };
    int runs = 0;
    for (int pat = 0; pat < NPAT; pat++)
        for (size_t n = 0; n <= 64; n++) { run_array(f, n, pat, &F); runs++; }   /* 0..64 전수: 2^k±1 포함 */
    static const size_t big[] = { 1000, 4097, MAXN };
    for (size_t b = 0; b < sizeof big / sizeof big[0]; b++)
        for (int pat = 0; pat < NPAT; pat++) { run_array(f, big[b], pat, &F); runs++; }
    printf("  %-9s %d runs: bad keys=%d perm=%d stable=%d ref=%d guard=%d cmp=%d\n",
           name, runs, F.keys, F.perm, F.stable, F.ref, F.guard, F.cmp);
    return F;
}

static bool rec_eq(const rec_t *a, const int32_t *k, const uint32_t *s, size_t n) {
    for (size_t i = 0; i < n; i++) if (a[i].key != k[i] || a[i].seq != s[i]) return false;
    return true;
}

/* ---- 리스트 도우미: g_orig[0..n) 를 풀에 놓고 연결. scatter = 노드 위치를 풀 안에서 섞는다 ---- */
static fl_req_t *build_list(size_t n, bool scatter) {
    static uint32_t slot[MAXN];
    for (size_t i = 0; i < n; i++) slot[i] = (uint32_t)i;
    if (scatter) for (size_t i = n; i > 1; i--) { size_t j = rnd() % i; uint32_t t = slot[i - 1]; slot[i - 1] = slot[j]; slot[j] = t; }
    fl_req_t *head = NULL, **pp = &head;
    for (size_t i = 0; i < n; i++) {
        fl_req_t *r = &g_pool[slot[i]];
        r->block = (uint32_t)g_orig[i].key; r->seq = g_orig[i].seq; r->page = 0; r->op = OP_PROGRAM;
        *pp = r; pp = &r->next;
    }
    *pp = NULL;
    return head;
}

typedef struct { fails_t f; int shape; } lfails_t;          /* shape = 개수·종료·풀 밖 노드 */

static void run_list(size_t n, int pat, bool scatter, lfails_t *L) {
    gen(g_orig, n, pat);
    fl_req_t *h = build_list(n, scatter);
    g_cmp = 0;
    h = list_sort(h);
    if (g_cmp > (unsigned long)n * ceil_lg(n)) L->f.cmp++;
    size_t cnt = 0;
    bool ok = true;
    for (fl_req_t *p = h; p; p = p->next) {
        if (cnt == n || p < g_pool || p >= g_pool + n) { ok = false; break; }   /* 사이클·유실·외부 노드 */
        g_a[cnt].key = (int32_t)p->block; g_a[cnt].seq = p->seq; cnt++;
    }
    if (!ok || cnt != n) { L->shape++; return; }
    judge(g_a, n, &L->f);
}

/* 펌웨어 큐: 블록 nb 개, 블록마다 ERASE 1개 → PROGRAM page 0,1,2,… 순으로 도착. 블록끼리는 무작위로 섞여 도착. */
static fl_req_t *build_ftl_queue(size_t nb, size_t *count) {
    uint32_t blk[16]; size_t left[16], next_op[16], total = 0;
    for (size_t b = 0; b < nb; b++) {
        blk[b] = (uint32_t)((b * 389u + 101u) % 4096u);        /* 서로 다른 PBA */
        left[b] = 1 + 4 + rnd() % 13u;                         /* ERASE + PROGRAM 4..16 개 */
        next_op[b] = 0; total += left[b];
    }
    fl_req_t *head = NULL, **pp = &head;
    for (uint32_t s = 0; s < total; s++) {
        size_t b;
        do b = rnd() % nb; while (left[b] == 0);
        fl_req_t *r = &g_pool[s];
        r->block = blk[b]; r->seq = s;
        r->op = next_op[b] == 0 ? OP_ERASE : OP_PROGRAM;
        r->page = (uint16_t)(next_op[b] == 0 ? 0 : next_op[b] - 1);
        next_op[b]++; left[b]--;
        *pp = r; pp = &r->next;
    }
    *pp = NULL;
    *count = total;
    return head;
}

static size_t block_switches(const fl_req_t *h) {
    size_t s = 0;
    for (; h && h->next; h = h->next) s += h->block != h->next->block;
    return s;
}

int main(void) {
    /* ---------- Part A ---------- */
    {   /* merge_into: 같은 키 1 이 양쪽에 → 왼쪽(seq 0) 먼저 */
        rec_t src[6] = { {1,0},{3,1},{5,2}, {1,3},{2,4},{6,5} }, dst[6] = { {0,0} };
        merge_into(dst, src, 0, 3, 6);
        const int32_t k[] = { 1,1,2,3,5,6 }; const uint32_t s[] = { 0,3,4,1,2,5 };
        CHECK(rec_eq(dst, k, s, 6));
        rec_t d2[3] = { {9,9},{9,9},{9,9} };
        merge_into(d2, src, 0, 3, 3);                           /* 오른쪽이 빈 run → 복사 */
        CHECK(d2[0].seq == 0 && d2[1].seq == 1 && d2[2].seq == 2);
        rec_t d3[6] = { {9,9},{9,9},{9,9},{9,9},{9,9},{9,9} };
        merge_into(d3, src, 3, 3, 6);                           /* 왼쪽이 빈 run, lo ≠ 0 */
        CHECK(d3[3].seq == 3 && d3[4].seq == 4 && d3[5].seq == 5 && d3[0].seq == 9);
    }
    {   /* 손 계산 예: 2 가 세 번 */
        rec_t a[6] = { {5,0},{2,1},{4,2},{2,3},{1,4},{2,5} }, t[6];
        msort_topdown(a, 6, t);
        const int32_t k[] = { 1,2,2,2,4,5 }; const uint32_t s[] = { 4,1,3,5,2,0 };
        CHECK(rec_eq(a, k, s, 6));
    }
    {   /* 극값: 비교에 뺄셈을 쓰면 여기서 틀린다 */
        rec_t a[6] = { {INT32_MAX,0},{INT32_MIN,1},{0,2},{-1,3},{INT32_MIN,4},{INT32_MAX,5} }, t[6];
        msort_topdown(a, 6, t);
        const int32_t k[] = { INT32_MIN, INT32_MIN, -1, 0, INT32_MAX, INT32_MAX }; const uint32_t s[] = { 1,4,3,2,0,5 };
        CHECK(rec_eq(a, k, s, 6));
    }
    msort_topdown(NULL, 0, NULL);                               /* n=0, 포인터 NULL: 아무것도 안 건드려야 */
    CHECK(true);
    printf("Part A/B suites (n=0..64 x 5 patterns + n=1000,4097,100000 x 5):\n");
    fails_t A = suite_array(msort_topdown, "top-down");
    CHECK(A.keys == 0); CHECK(A.perm == 0); CHECK(A.stable == 0);
    CHECK(A.ref == 0);  CHECK(A.guard == 0); CHECK(A.cmp == 0);

    /* ---------- Part B ---------- */
    fails_t B = suite_array(msort_bottomup, "bottom-up");
    CHECK(B.keys == 0); CHECK(B.perm == 0); CHECK(B.stable == 0);
    CHECK(B.ref == 0);  CHECK(B.guard == 0); CHECK(B.cmp == 0);
    {   /* 안정 정렬의 결과는 유일하다 → top-down 과 bottom-up 결과가 레코드 단위로 같아야 */
        static rec_t x[MAXN], y[MAXN];
        gen(g_orig, MAXN, PAT_FEWKEYS);
        memcpy(x, g_orig, sizeof x); memcpy(y, g_orig, sizeof y);
        unsigned long ct, cb;
        g_cmp = 0; msort_topdown(x, MAXN, g_tmp);  ct = g_cmp;
        g_cmp = 0; msort_bottomup(y, MAXN, g_tmp); cb = g_cmp;
        CHECK(memcmp(x, y, sizeof x) == 0);
        gen(g_orig, 1000, PAT_RANDOM);
        memcpy(x, g_orig, 1000 * sizeof *x); memcpy(y, g_orig, 1000 * sizeof *y);
        unsigned long rt, rb;
        g_cmp = 0; msort_topdown(x, 1000, g_tmp);  rt = g_cmp;
        g_cmp = 0; msort_bottomup(y, 1000, g_tmp); rb = g_cmp;
        printf("  comparisons: n=100000 4-keys td=%lu bu=%lu | n=1000 random td=%lu bu=%lu (bound n*ceil(lg n)=%u)\n",
               ct, cb, rt, rb, 1000u * ceil_lg(1000));
    }
    {   /* n=5 (2의 거듭제곱 아님): 꼬리 run 1개가 패스마다 그대로 따라오다가 마지막에 병합 */
        rec_t a[5] = { {3,0},{1,1},{3,2},{0,3},{1,4} }, t[5];
        msort_bottomup(a, 5, t);
        const int32_t k[] = { 0,1,1,3,3 }; const uint32_t s[] = { 3,1,4,0,2 };
        CHECK(rec_eq(a, k, s, 5));
    }

    /* ---------- Part C ---------- */
    {   /* list_split / list_merge 단위 테스트 */
        gen(g_orig, 5, PAT_SORTED);
        fl_req_t *h = build_list(5, false);
        fl_req_t *rest = list_split(h, 2);
        CHECK(h->next && h->next->next == NULL && rest && rest->seq == 2);
        CHECK(list_split(rest, 3) == NULL && list_split(rest, 99) == NULL && list_split(NULL, 1) == NULL);

        rec_t two[6] = { {1,0},{3,1},{5,2},{1,3},{2,4},{6,5} };
        memcpy(g_orig, two, sizeof two);
        h = build_list(6, false);
        fl_req_t *b = list_split(h, 3), *tail = NULL;
        fl_req_t *m = list_merge(h, b, &tail);
        const uint32_t s[] = { 0,3,4,1,2,5 };
        bool ok = true; size_t i = 0;
        for (fl_req_t *p = m; p; p = p->next, i++) if (i >= 6 || p->seq != s[i]) { ok = false; break; }
        CHECK(ok && i == 6 && tail == &g_pool[5] && tail->next == NULL);
        tail = &g_pool[0];
        CHECK(list_merge(NULL, NULL, &tail) == NULL && tail == NULL);
    }
    {
        lfails_t L = { { 0 }, 0 };
        int runs = 0;
        for (int pat = 0; pat < NPAT; pat++)
            for (size_t n = 0; n <= 64; n++) { run_list(n, pat, (n & 1) != 0, &L); runs++; }
        for (int pat = 0; pat < NPAT; pat++) { run_list(1000, pat, true, &L); run_list(MAXN, pat, true, &L); runs += 2; }
        printf("  %-9s %d runs: bad keys=%d perm=%d stable=%d ref=%d shape=%d cmp=%d\n",
               "list", runs, L.f.keys, L.f.perm, L.f.stable, L.f.ref, L.shape, L.f.cmp);
        CHECK(L.f.keys == 0); CHECK(L.f.perm == 0); CHECK(L.f.stable == 0);
        CHECK(L.f.ref == 0);  CHECK(L.shape == 0);  CHECK(L.f.cmp == 0);
    }
    {   /* 펌웨어: 블록별로 묶되 블록 안 순서(ERASE → page 0,1,2,…)는 도착 순서 그대로 */
        size_t cnt;
        fl_req_t *q = build_ftl_queue(8, &cnt);
        size_t before = block_switches(q);
        q = list_sort(q);
        size_t after = block_switches(q);
        bool grouped = true, order_ok = true; size_t seen = 0;
        for (fl_req_t *p = q; p; ) {
            fl_req_t *g = p; uint16_t want = 0;
            if (g->op != OP_ERASE) order_ok = false;
            for (p = g->next; p && p->block == g->block; p = p->next) {
                if (p->op != OP_PROGRAM || p->page != want) order_ok = false;
                want++;
            }
            if (p && p->block < g->block) grouped = false;
            seen += 1u + want;
        }
        printf("  FTL queue: 8 blocks, %zu requests, block switches %zu -> %zu\n", cnt, before, after);
        CHECK(grouped && seen == cnt && after == 7);
        CHECK(order_ok);                                        /* 불안정 정렬이면 PROGRAM 이 ERASE 앞으로 */
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
