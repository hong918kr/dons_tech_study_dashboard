/* ALGO-B01 복잡도 분석: Big-O를 실측으로 확인
 *
 * 같은 질문 "배열에 같은 값이 두 번 나오나?" 를 세 가지 복잡도로 푼다.
 *   Part A: O(n^2) 쌍 비교 / O(n log n) 복사본 qsort + 이웃 비교 / O(n) 오픈 어드레싱 해시 집합
 *   Part B: 경계 버그 — qsort 비교 함수 오버플로, 값 0(빈 칸 표시) 처리, 테이블 가득 참,
 *           "처음 반복이 보이는 위치" (스트리밍 변형)
 *   Part C: RAM 예산으로 방법 고르기 (비트맵 / 해시 / 정렬 / 쌍 비교) — CAN UID, FTL LBA 로그
 * 모든 함수는 malloc 을 쓰지 않는다: 작업 메모리는 호출자가 준다.
 * 시간 측정은 demo_bench.c 에 있다 (이 파일은 정확성만 검사, < 1 s).
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------- Part A: 세 가지 풀이 ---------------- */

/* A1. O(n^2): 모든 쌍 (i < j) 비교. 추가 메모리 O(1). */
static bool has_dup_pairwise(const uint32_t *a, size_t n) {
    for (size_t i = 0; i + 1 < n; i++)
        for (size_t j = i + 1; j < n; j++)
            if (a[i] == a[j]) return true;
    return false;
}

/* qsort 비교 함수: 음수/0/양수. uint32_t 를 빼서 int 로 돌려주면 틀린다 (Part B). */
static int cmp_u32(const void *pa, const void *pb) {
    uint32_t x = *(const uint32_t *)pa, y = *(const uint32_t *)pb;
    return (x > y) - (x < y);
}

/* A2. O(n log n): 입력을 scratch 로 복사 → qsort → 이웃끼리 비교. 추가 메모리 4n 바이트.
 * 입력은 바꾸지 않는다 (호출자의 센서 버퍼일 수 있다). */
static bool has_dup_sort(const uint32_t *a, size_t n, uint32_t *scratch) {
    if (n < 2) return false;
    memcpy(scratch, a, n * sizeof *a);
    qsort(scratch, n, sizeof *scratch, cmp_u32);
    for (size_t i = 1; i < n; i++)
        if (scratch[i - 1] == scratch[i]) return true;
    return false;
}

/* A3. O(n) 평균: 오픈 어드레싱(선형 탐사) 해시 집합. 칸 수 cap = 2^k, 칸 값 0 = 빈 칸.
 * 키 0 은 칸에 넣지 않고 has_zero 플래그로 따로 기억한다 (빈 칸 표시와 겹치니까). */
typedef struct {
    uint32_t *slot;     /* cap 개, 호출자가 준 버퍼 */
    uint32_t  mask;     /* cap - 1 */
    unsigned  shift;    /* 32 - k */
    uint32_t  count;    /* 칸에 들어간 (0 이 아닌) 키 수 */
    bool      has_zero;
} hset_t;

/* 1 <= k <= 30. 버퍼는 (1u << k) 개의 uint32_t. */
static void hs_init(hset_t *h, uint32_t *buf, unsigned k) {
    h->slot = buf;
    h->mask = (1u << k) - 1u;
    h->shift = 32u - k;
    h->count = 0;
    h->has_zero = false;
    memset(buf, 0, ((size_t)1 << k) * sizeof *buf);
}

/* 피보나치(곱셈) 해싱: 2^32 / φ 를 곱하고 상위 k 비트. 나눗셈·나머지 연산 없음. */
static inline uint32_t hs_hash(const hset_t *h, uint32_t x) {
    return (x * 2654435769u) >> h->shift;
}

/* 반환: 0 = 새로 넣음, 1 = 이미 있음, -1 = 가득 참 (빈 칸을 최소 1개 남긴다 → 탐사가 반드시 끝난다) */
static int hs_insert(hset_t *h, uint32_t x) {
    if (x == 0) {
        if (h->has_zero) return 1;
        h->has_zero = true;
        return 0;
    }
    uint32_t i = hs_hash(h, x);
    while (h->slot[i] != 0) {
        if (h->slot[i] == x) return 1;
        i = (i + 1) & h->mask;
    }
    if (h->count >= h->mask) return -1;       /* 넣으면 빈 칸이 0개 → 거절 (최대 cap-1 개) */
    h->slot[i] = x;
    h->count++;
    return 0;
}

/* 반환: 1 = 중복 있음, 0 = 없음, -1 = 테이블이 작다 (cap = 2^k 는 2n 이상으로 잡아라) */
static int has_dup_hash(const uint32_t *a, size_t n, uint32_t *buf, unsigned k) {
    hset_t h;
    hs_init(&h, buf, k);
    for (size_t i = 0; i < n; i++) {
        int r = hs_insert(&h, a[i]);
        if (r != 0) return r;
    }
    return 0;
}

/* ---------------- Part B: 스트리밍 변형 — 처음으로 반복이 보이는 위치 ---------------- */

/* 가장 작은 j 를 돌려준다: a[j] == a[i] 인 i < j 가 있는 것. 없으면 -1.
 * (UART 로 값이 하나씩 들어올 때 "몇 번째 샘플에서 처음 중복을 알아채나") */
static ptrdiff_t first_repeat_pairwise(const uint32_t *a, size_t n) {
    for (size_t j = 1; j < n; j++)
        for (size_t i = 0; i < j; i++)
            if (a[i] == a[j]) return (ptrdiff_t)j;
    return -1;
}

/* 같은 답을 해시로. 테이블이 작으면 -2. */
static ptrdiff_t first_repeat_hash(const uint32_t *a, size_t n, uint32_t *buf, unsigned k) {
    hset_t h;
    hs_init(&h, buf, k);
    for (size_t j = 0; j < n; j++) {
        int r = hs_insert(&h, a[j]);
        if (r == 1) return (ptrdiff_t)j;
        if (r < 0) return -2;
    }
    return -1;
}

/* ---------------- Part C: RAM 예산으로 방법 고르기 ---------------- */

enum { M_BITMAP = 0, M_HASH = 1, M_SORT = 2, M_PAIRWISE = 3 };
static const char *const method_name[] = { "bitmap", "hash", "sort", "pairwise" };

/* 값의 범위가 [0, limit) 로 알려져 있을 때: limit 비트짜리 비트맵. O(n + limit/8).
 * bits 는 (limit + 31) / 32 개의 uint32_t. 반환 1/0, 범위 밖 값이면 -1. */
static int has_dup_bitmap(const uint32_t *a, size_t n, uint32_t limit, uint32_t *bits) {
    memset(bits, 0, ((size_t)limit + 31) / 32 * sizeof *bits);
    for (size_t i = 0; i < n; i++) {
        uint32_t v = a[i];
        if (v >= limit) return -1;
        uint32_t m = 1u << (v & 31u);
        if (bits[v >> 5] & m) return 1;
        bits[v >> 5] |= m;
    }
    return 0;
}

/* 2n 이상인 가장 작은 2의 거듭제곱의 지수 k (최소 1). */
static unsigned log2_cap_for(size_t n) {
    unsigned k = 1;
    while (((uint64_t)1 << k) < 2 * (uint64_t)n) k++;
    return k;
}

/* 각 방법이 쓰는 작업 메모리(바이트). 표로도 쓴다. */
static uint64_t bytes_bitmap(uint32_t limit) { return ((uint64_t)limit + 31) / 32 * 4; }
static uint64_t bytes_hash(size_t n)         { return (uint64_t)4 << log2_cap_for(n); }
static uint64_t bytes_sort(size_t n)         { return (uint64_t)4 * n; }

/* ram(ram_bytes 바이트, uint32_t 정렬) 안에서 가장 빠른 방법을 골라 실행한다.
 * 우선순위: 비트맵(limit != 0 이고 들어가면) → 해시 → 정렬 → 쌍 비교.
 * 반환 1/0/-1 (bitmap 범위 밖), *method 에 고른 방법. */
static int dup_check_budget(const uint32_t *a, size_t n, uint32_t limit,
                            uint32_t *ram, size_t ram_bytes, int *method) {
    if (limit != 0 && bytes_bitmap(limit) <= ram_bytes) {
        *method = M_BITMAP;
        return has_dup_bitmap(a, n, limit, ram);
    }
    if (n <= ((size_t)1 << 29) && bytes_hash(n) <= ram_bytes) {   /* k <= 30 보장 */
        *method = M_HASH;
        return has_dup_hash(a, n, ram, log2_cap_for(n));
    }
    if (bytes_sort(n) <= ram_bytes) {
        *method = M_SORT;
        return has_dup_sort(a, n, ram);
    }
    *method = M_PAIRWISE;
    return has_dup_pairwise(a, n);
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t g_rng = 0x2545F491u;
static uint32_t xorshift32(void) {
    uint32_t x = g_rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return g_rng = x;
}
/* 서로 다른 값 만들기: i*홀수 는 2^32 위에서 전단사, murmur3 fmix32 도 전단사 → 겹치지 않는다 */
static uint32_t fmix32(uint32_t h) {
    h ^= h >> 16; h *= 0x85EBCA6Bu; h ^= h >> 13; h *= 0xC2B2AE35u; h ^= h >> 16;
    return h;
}
static void fill_distinct(uint32_t *a, size_t n, uint32_t seed) {
    for (size_t i = 0; i < n; i++) a[i] = fmix32((uint32_t)i * 0x9E3779B1u + seed);
}
/* 기준 답: 정의 그대로 (테스트 쪽) */
static ptrdiff_t ref_first_repeat(const uint32_t *a, size_t n) {
    for (size_t j = 0; j < n; j++)
        for (size_t i = 0; i < j; i++)
            if (a[i] == a[j]) return (ptrdiff_t)j;
    return -1;
}

#define MAXN 20000
static uint32_t A[MAXN], B[MAXN], S[MAXN];
static uint32_t HT[1u << 16];
static uint32_t RAM[4096];          /* Part C: 최대 16 KB 예산 */

/* 세 방법 모두 같은 답(want)을 내는가 */
static bool all3(const uint32_t *a, size_t n, bool want) {
    uint32_t keep[64];
    size_t m = n < 64 ? n : 64;
    memcpy(keep, a, m * sizeof *a);
    bool p = has_dup_pairwise(a, n);
    bool s = has_dup_sort(a, n, S);
    int  h = has_dup_hash(a, n, HT, log2_cap_for(n));
    return p == want && s == want && h == (int)want && memcmp(keep, a, m * sizeof *a) == 0;
}

int main(void) {
    /* ===== Part A: 이름 붙은 경계 사례 ===== */
    static const uint32_t e2[] = { 5, 5 };
    static const uint32_t e3[] = { 0u, 0xFFFFFFFFu };
    static const uint32_t e4[] = { 0xFFFFFFFFu, 0u, 0xFFFFFFFFu };
    static const uint32_t e5[] = { 0, 1, 0 };
    static const uint32_t e6[] = { 9, 1, 2, 3, 9 };
    static const uint32_t e7[] = { 3, 1, 4, 1, 5 };
    static const uint32_t e8[] = { 0x80000000u, 0x7FFFFFFFu, 1u, 0x80000001u };
    CHECK(all3(e2, 0, false));                       /* 빈 배열 */
    CHECK(all3(e2, 1, false));                       /* 원소 1개 */
    CHECK(all3(e2, 2, true));
    CHECK(all3(e3, 2, false));                       /* 최솟값·최댓값 */
    CHECK(all3(e4, 3, true));
    CHECK(all3(e5, 3, true));                        /* 0 이 두 번 (해시 빈 칸 표시와 같은 값) */
    CHECK(all3(e6, 5, true));                        /* 처음과 끝 */
    CHECK(all3(e7, 5, true));
    CHECK(all3(e8, 4, false));                       /* 부호 경계 근처, 중복 없음 */
    for (size_t i = 0; i < 100; i++) A[i] = 0xDEADBEEFu;
    CHECK(all3(A, 100, true));                       /* 전부 같음 */

    /* 구성으로 답을 아는 큰 입력: 서로 다른 값 → 없음, 한 칸 복사 → 있음 */
    fill_distinct(A, MAXN, 12345u);
    CHECK(all3(A, MAXN, false));
    A[MAXN - 1] = A[0];
    CHECK(all3(A, MAXN, true));
    fill_distinct(A, MAXN, 777u);
    A[5000] = A[4999];
    CHECK(all3(A, MAXN, true));

    /* 무작위 대조: 값 범위를 작게 해 중복이 자주 생기게. 기준 = 정의 그대로 */
    {
        int bad = 0, yes = 0, no = 0;
        for (int t = 0; t < 3000; t++) {
            size_t n = xorshift32() % 120;
            uint32_t range = 1u + xorshift32() % 400u;
            bool wide = (t % 5 == 0);                /* 가끔은 32비트 전 범위 */
            for (size_t i = 0; i < n; i++) A[i] = wide ? xorshift32() : xorshift32() % range;
            bool want = ref_first_repeat(A, n) >= 0;
            if (want) yes++; else no++;
            if (!all3(A, n, want)) bad++;
        }
        printf("random: 3000 arrays, %d with dup, %d without, mismatches=%d\n", yes, no, bad);
        CHECK(bad == 0);
        CHECK(yes > 300 && no > 300);
    }

    /* ===== Part B: 경계 버그 ===== */
    {   /* 비교 함수: 빼기로 만들면 이 세 줄이 틀린다 */
        uint32_t x = 0, y = 0xFFFFFFFFu, p = 1u, q = 0x80000001u, r = 7u;
        CHECK(cmp_u32(&x, &y) < 0);
        CHECK(cmp_u32(&y, &x) > 0);
        CHECK(cmp_u32(&p, &q) < 0);
        CHECK(cmp_u32(&r, &r) == 0);
        uint32_t v[] = { 0xFFFFFFFFu, 0u, 0x80000000u, 1u, 0x7FFFFFFFu };
        qsort(v, 5, sizeof v[0], cmp_u32);
        CHECK(v[0] == 0u && v[1] == 1u && v[2] == 0x7FFFFFFFu && v[3] == 0x80000000u && v[4] == 0xFFFFFFFFu);
        /* 빼기 비교 함수 + 이 맥의 qsort 에서 두 0x80000001 이 이웃이 되지 않던 입력 */
        static const uint32_t e9[] = { 1u, 0x80000001u, 0xFFFFFFFFu, 0x80000000u, 0x40000000u, 0x80000001u };
        CHECK(all3(e9, 6, true));
    }
    {   /* 해시 집합: 0 처리, 가득 참 */
        hset_t h;
        uint32_t buf[8];
        hs_init(&h, buf, 3);                          /* cap 8 → 0 아닌 키 최대 7개 */
        CHECK(hs_insert(&h, 0) == 0 && hs_insert(&h, 0) == 1 && h.count == 0);
        CHECK(hs_insert(&h, 42) == 0 && hs_insert(&h, 42) == 1 && h.count == 1);
        int ok = 1;
        for (uint32_t v = 100; v < 106; v++) ok &= (hs_insert(&h, v) == 0);
        CHECK(ok && h.count == 7);
        CHECK(hs_insert(&h, 999) == -1);              /* 8번째 → 거절, 무한 루프 없음 */
        CHECK(hs_insert(&h, 103) == 1 && hs_insert(&h, 0) == 1);   /* 가득 차도 조회는 된다 */
        fill_distinct(A, 10, 5u);
        CHECK(has_dup_hash(A, 10, HT, 3) == -1);      /* cap 8 < 2n: 작은 테이블은 오류로 */
        CHECK(has_dup_hash(A, 10, HT, log2_cap_for(10)) == 0);
    }
    {   /* 처음 반복 위치 */
        static const uint32_t f1[] = { 1, 2, 3, 2, 1 };
        static const uint32_t f2[] = { 1, 2, 1, 2 };
        static const uint32_t f3[] = { 0, 7, 0 };
        CHECK(first_repeat_pairwise(f1, 0) == -1 && first_repeat_hash(f1, 0, HT, 1) == -1);
        CHECK(first_repeat_pairwise(f1, 1) == -1 && first_repeat_hash(f1, 1, HT, 1) == -1);
        CHECK(first_repeat_pairwise(f1, 5) == 3 && first_repeat_hash(f1, 5, HT, 4) == 3);
        CHECK(first_repeat_pairwise(f2, 4) == 2 && first_repeat_hash(f2, 4, HT, 4) == 2);
        CHECK(first_repeat_pairwise(f3, 3) == 2 && first_repeat_hash(f3, 3, HT, 3) == 2);
        CHECK(first_repeat_pairwise(e2, 2) == 1 && first_repeat_hash(e2, 2, HT, 2) == 1);
        fill_distinct(A, 2000, 99u);
        CHECK(first_repeat_pairwise(A, 2000) == -1 && first_repeat_hash(A, 2000, HT, 12) == -1);
        A[1999] = A[1234];
        CHECK(first_repeat_pairwise(A, 2000) == 1999 && first_repeat_hash(A, 2000, HT, 12) == 1999);
        fill_distinct(A, 100, 3u);
        CHECK(first_repeat_hash(A, 100, HT, 5) == -2);   /* cap 32 에 100개 → 오류 */
        int bad = 0;
        for (int t = 0; t < 2000; t++) {
            size_t n = xorshift32() % 80;
            uint32_t range = 1u + xorshift32() % 200u;
            for (size_t i = 0; i < n; i++) A[i] = xorshift32() % range;
            ptrdiff_t want = ref_first_repeat(A, n);
            if (first_repeat_pairwise(A, n) != want) bad++;
            if (first_repeat_hash(A, n, HT, log2_cap_for(n)) != want) bad++;
        }
        CHECK(bad == 0);
    }

    /* ===== Part C: RAM 예산으로 고르기 ===== */
    {
        int m = -1, r;
        /* 비트맵 단독: LBA 범위 65536 */
        for (size_t i = 0; i < 20000; i++) B[i] = (uint32_t)(i * 3u);       /* 0..59997, 겹침 없음 */
        CHECK(has_dup_bitmap(B, 20000, 65536u, RAM) == 0);
        B[19999] = 30000u;                                                 /* 30000 = 10000*3 과 겹침 */
        CHECK(has_dup_bitmap(B, 20000, 65536u, RAM) == 1);
        B[19999] = 70000u;
        CHECK(has_dup_bitmap(B, 20000, 65536u, RAM) == -1);               /* 범위 밖 = 손상된 로그 */
        static const uint32_t edge[] = { 31, 32, 0, 63, 64, 65535 };
        CHECK(has_dup_bitmap(edge, 6, 65536u, RAM) == 0);                  /* 워드 경계 */

        /* 시나리오 1: CAN 관절 보드 200개의 32비트 UID, 예산 4 KB → 해시 (cap 512 = 2 KB) */
        fill_distinct(A, 200, 0xCA11u);
        r = dup_check_budget(A, 200, 0, RAM, 4096, &m);
        CHECK(r == 0 && m == M_HASH);
        A[150] = A[17];
        r = dup_check_budget(A, 200, 0, RAM, 4096, &m);
        CHECK(r == 1 && m == M_HASH);

        /* 시나리오 2: UID 1000개, 예산 4 KB → 해시는 8 KB 라 안 됨, 정렬 복사본 4000 B → 정렬 */
        fill_distinct(A, 1000, 0xB00Du);
        r = dup_check_budget(A, 1000, 0, RAM, 4096, &m);
        CHECK(r == 0 && m == M_SORT);
        A[999] = A[0];
        r = dup_check_budget(A, 1000, 0, RAM, 4096, &m);
        CHECK(r == 1 && m == M_SORT);

        /* 시나리오 3: UID 3000개, 예산 4 KB → 해시 32 KB, 정렬 12 KB 둘 다 안 됨 → 쌍 비교 */
        fill_distinct(A, 3000, 0x5EEDu);
        r = dup_check_budget(A, 3000, 0, RAM, 4096, &m);
        CHECK(r == 0 && m == M_PAIRWISE);
        A[2000] = A[1000];
        r = dup_check_budget(A, 3000, 0, RAM, 4096, &m);
        CHECK(r == 1 && m == M_PAIRWISE);

        /* 시나리오 4: 전원 손실 후 FTL 복구 — OOB 에서 읽은 LBA 20000개, LBA < 65536, 예산 8 KB → 비트맵 */
        for (size_t i = 0; i < 20000; i++) B[i] = (uint32_t)((i * 7919u) & 0xFFFFu); /* 7919 홀수 → 겹침 없음 */
        r = dup_check_budget(B, 20000, 65536u, RAM, 8192, &m);
        CHECK(r == 0 && m == M_BITMAP);
        B[12345] = B[54];
        r = dup_check_budget(B, 20000, 65536u, RAM, 8192, &m);
        CHECK(r == 1 && m == M_BITMAP);
        /* 같은 로그, 예산 4 KB → 비트맵(8 KB) 불가, 해시(128 KB) 불가, 정렬(80 KB) 불가 → 쌍 비교 */
        r = dup_check_budget(B, 2000, 65536u, RAM, 4096, &m);
        CHECK(r == (ref_first_repeat(B, 2000) >= 0) && m == M_PAIRWISE);

        /* 예산 0 → 항상 쌍 비교, 그래도 답은 맞다 */
        r = dup_check_budget(e7, 5, 0, RAM, 0, &m);
        CHECK(r == 1 && m == M_PAIRWISE);

        /* 무작위 예산 대조 */
        int bad = 0;
        for (int t = 0; t < 500; t++) {
            size_t n = xorshift32() % 300;
            uint32_t limit = (t & 1) ? 1u + xorshift32() % 4000u : 0u;
            uint32_t range = limit ? limit : 1u + xorshift32() % 5000u;
            for (size_t i = 0; i < n; i++) A[i] = xorshift32() % range;
            size_t budget = xorshift32() % sizeof RAM;
            int want = ref_first_repeat(A, n) >= 0;
            r = dup_check_budget(A, n, limit, RAM, budget, &m);
            if (r != want || m < M_BITMAP || m > M_PAIRWISE) bad++;
        }
        CHECK(bad == 0);
        printf("work RAM n=1000: bitmap(limit 65536) %llu B, hash %llu B, sort %llu B, pairwise 0 B -> 4 KB budget picks %s\n",
               (unsigned long long)bytes_bitmap(65536u), (unsigned long long)bytes_hash(1000),
               (unsigned long long)bytes_sort(1000), method_name[M_SORT]);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
