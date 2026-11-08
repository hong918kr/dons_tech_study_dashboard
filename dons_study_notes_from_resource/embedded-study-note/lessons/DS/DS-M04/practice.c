/* DS-M04 추가 연습 — 한 번에 검증
 * Linux 부팅 초기 버디처럼 "unit 번호(pfn)"만 관리하는 메타데이터 전용 버디:
 *   - free list 링크는 블록 안이 아니라 옆 배열 next[]/prev[] 에 (Linux struct page 의 lru 처럼)
 *   - 연습 1: 버디 쌍마다 1비트 (XOR 비트맵) — 비트맵이 2047 → 1023 비트로 절반
 *   - 연습 2: 2의 거듭제곱이 아닌 관리 범위 (1000 unit)
 *   - 연습 3: 예약 구멍이 있는 부팅 초기화 (free 범위 목록으로 init)
 *   - 연습 4: 할당 블록을 제자리에서 줄이기 (shrink)
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define P_MAX  10u
#define P_NU   (1u << P_MAX)                  /* 최대 1024 unit */
#define NIL    0xFFFFu

typedef struct {
    uint16_t next[P_NU], prev[P_NU];          /* 옆 배열 링크 (free 블록의 첫 unit 칸만 사용) */
    uint16_t head[P_MAX + 1];
    uint32_t nfree[P_MAX + 1];
    uint32_t pairmap[P_NU / 32];              /* 쌍 비트: order k 쌍의 비트 = 부모 노드 번호 (1..1023) */
    uint32_t nunits;                          /* 실제 관리 범위 [0, nunits) */
} pb_t;

typedef struct { uint32_t start, end; } range_t;   /* [start, end) */

/* order k, unit u 블록이 속한 쌍의 비트 번호 = 부모 노드 = (P_NU >> (k+1)) + (u >> (k+1)) */
static uint32_t pair_bit(uint32_t k, uint32_t u) { return (P_NU >> (k + 1u)) + (u >> (k + 1u)); }
static bool pair_test(const pb_t *pb, uint32_t k, uint32_t u) {
    uint32_t i = pair_bit(k, u);
    return (pb->pairmap[i >> 5] >> (i & 31u)) & 1u;
}
static void pair_flip(pb_t *pb, uint32_t k, uint32_t u) {
    if (k == P_MAX) return;                   /* 최상위 order 에는 짝이 없다 */
    uint32_t i = pair_bit(k, u);
    pb->pairmap[i >> 5] ^= 1u << (i & 31u);
}

/* 불변식: pair 비트 = isfree_k(A) XOR isfree_k(B). list 에 넣고 뺄 때마다 토글하면 저절로 유지 */
static void pb_push(pb_t *pb, uint32_t k, uint32_t u) {
    pb->next[u] = pb->head[k]; pb->prev[u] = NIL;
    if (pb->head[k] != NIL) pb->prev[pb->head[k]] = (uint16_t)u;
    pb->head[k] = (uint16_t)u;
    pb->nfree[k]++;
    pair_flip(pb, k, u);
}
static void pb_remove(pb_t *pb, uint32_t k, uint32_t u) {
    if (pb->prev[u] != NIL) pb->next[pb->prev[u]] = pb->next[u]; else pb->head[k] = pb->next[u];
    if (pb->next[u] != NIL) pb->prev[pb->next[u]] = pb->prev[u];
    pb->nfree[k]--;
    pair_flip(pb, k, u);
}

/* 연습 1: 해제 + 병합. u 자신은 아직 list 에 없으므로 쌍 비트 == "버디가 free" */
static void pb_free(pb_t *pb, uint32_t u, uint32_t k) {
    while (k < P_MAX && pair_test(pb, k, u)) {
        pb_remove(pb, k, u ^ (1u << k));
        u &= ~(1u << k);
        k++;
    }
    pb_push(pb, k, u);
}

/* 할당: 성공하면 unit 번호, 실패하면 -1 */
static int32_t pb_alloc(pb_t *pb, uint32_t order) {
    uint32_t k = order;
    while (k <= P_MAX && pb->nfree[k] == 0) k++;
    if (k > P_MAX) return -1;
    uint32_t u = pb->head[k];
    pb_remove(pb, k, u);
    while (k > order) { k--; pb_push(pb, k, u + (1u << k)); }
    return (int32_t)u;
}

/* 연습 2·3: free 범위 목록으로 초기화. 각 범위를 "정렬되고 범위 안에 들어가는 가장 큰 블록"들로
 * 잘라 pb_free 로 넣는다 → 붙어 있는 조각은 병합이 알아서 합친다 (Linux memblock_free_all 과 같은 발상) */
static void pb_init(pb_t *pb, uint32_t nunits, const range_t *r, uint32_t nr) {
    memset(pb, 0, sizeof *pb);
    for (uint32_t k = 0; k <= P_MAX; k++) pb->head[k] = NIL;
    pb->nunits = nunits;
    for (uint32_t i = 0; i < nr; i++) {
        uint32_t u = r[i].start, e = r[i].end < nunits ? r[i].end : nunits;
        while (u < e) {
            uint32_t k = P_MAX;
            while ((u & ((1u << k) - 1u)) || u + (1u << k) > e) k--;   /* k = 0 이면 항상 만족 */
            pb_free(pb, u, k);
            u += 1u << k;
        }
    }
}

/* 연습 4: order 블록을 new_order 로 줄인다 — 위쪽 절반들을 큰 것부터 돌려준다 */
static void pb_shrink(pb_t *pb, uint32_t u, uint32_t order, uint32_t new_order) {
    while (order > new_order) {
        order--;
        pb_free(pb, u + (1u << order), order);   /* 버디(아래 절반)는 사용 중 → 병합 없이 push */
    }
}

/* ---------------- 검사 도구 ---------------- */
static uint8_t g_isfree[P_MAX + 1][P_NU];      /* [k][u] = order k 블록 u 가 list 에 있음 (검사용) */
static bool pb_check(const pb_t *pb, const uint8_t *own) {   /* own[u]: 0 free, 1 사용, 2 관리 밖/예약 */
    memset(g_isfree, 0, sizeof g_isfree);
    static uint8_t cover[P_NU];
    memset(cover, 0, sizeof cover);
    for (uint32_t k = 0; k <= P_MAX; k++) {
        uint32_t cnt = 0, prev = NIL;
        for (uint32_t u = pb->head[k]; u != NIL; prev = u, u = pb->next[u]) {
            if (u >= pb->nunits || pb->prev[u] != prev || ++cnt > P_NU) return false;
            if ((u & ((1u << k) - 1u)) || u + (1u << k) > pb->nunits) return false;
            g_isfree[k][u] = 1;
            for (uint32_t x = u; x < u + (1u << k); x++) if (cover[x]++ || own[x] != 0) return false;
        }
        if (cnt != pb->nfree[k]) return false;
    }
    for (uint32_t x = 0; x < P_NU; x++) if ((cover[x] == 1) != (own[x] == 0)) return false;
    for (uint32_t k = 0; k < P_MAX; k++)                           /* 쌍 비트 == XOR, 그리고 free 쌍 없음 */
        for (uint32_t u = 0; u < P_NU; u += 2u << k) {
            uint8_t a = g_isfree[k][u], c = g_isfree[k][u + (1u << k)];
            if (pair_test(pb, k, u) != (a ^ c) || (a && c)) return false;
        }
    return true;
}
static bool ref_can_alloc(const uint8_t *own, uint32_t k) {
    for (uint32_t s = 0; s < P_NU; s += 1u << k) {
        uint32_t x = s;
        while (x < s + (1u << k) && own[x] == 0) x++;
        if (x == s + (1u << k)) return true;
    }
    return false;
}
static void snapshot(const pb_t *pb, uint32_t *cnt) { memcpy(cnt, pb->nfree, sizeof pb->nfree); }
static void print_info(const char *tag, const pb_t *pb) {
    printf("%s", tag);
    for (uint32_t k = 0; k <= P_MAX; k++) printf(" %u", pb->nfree[k]);
    printf("\n");
}

static uint32_t rng = 1u;
static uint32_t xorshift32(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

typedef struct { uint32_t u, k; } blk_t;
static blk_t g_live[P_NU];
static pb_t g_pb;
static uint8_t g_own[P_NU];

/* 랜덤 alloc/free → 참조 모델 대조 → 전부 해제 → 초기 free 블록 집합과 같아야 함 */
static bool stress(pb_t *pb, uint32_t seed, int ops, uint32_t *allocs_out) {
    static uint8_t init_free[P_MAX + 1][P_NU];
    uint32_t init_cnt[P_MAX + 1], nlive = 0, allocs = 0;
    bool ok = pb_check(pb, g_own);
    memcpy(init_free, g_isfree, sizeof init_free);
    snapshot(pb, init_cnt);
    rng = seed;
    for (int op = 0; op < ops && ok; op++) {
        uint32_t r = xorshift32();
        if (nlive == 0 || r % 100u < 55u) {
            uint32_t k = (r >> 8) % 100u < 70u ? (r >> 16) % 4u : 4u + (r >> 16) % 7u;
            bool can = ref_can_alloc(g_own, k);
            int32_t u = pb_alloc(pb, k);
            ok = (u >= 0) == can;
            if (u >= 0) {
                for (uint32_t x = (uint32_t)u; x < (uint32_t)u + (1u << k); x++) { ok = ok && g_own[x] == 0; g_own[x] = 1; }
                g_live[nlive++] = (blk_t){ (uint32_t)u, k };
                allocs++;
            }
        } else {
            uint32_t i = (r >> 8) % nlive;
            pb_free(pb, g_live[i].u, g_live[i].k);
            for (uint32_t x = g_live[i].u; x < g_live[i].u + (1u << g_live[i].k); x++) g_own[x] = 0;
            g_live[i] = g_live[--nlive];
        }
        ok = ok && pb_check(pb, g_own);
    }
    while (ok && nlive) {
        uint32_t i = xorshift32() % nlive;
        pb_free(pb, g_live[i].u, g_live[i].k);
        for (uint32_t x = g_live[i].u; x < g_live[i].u + (1u << g_live[i].k); x++) g_own[x] = 0;
        g_live[i] = g_live[--nlive];
        ok = pb_check(pb, g_own);
    }
    uint32_t end_cnt[P_MAX + 1];
    snapshot(pb, end_cnt);
    *allocs_out = allocs;
    return ok && memcmp(init_cnt, end_cnt, sizeof end_cnt) == 0 && memcmp(init_free, g_isfree, sizeof init_free) == 0;
}

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main(void) {
    pb_t *pb = &g_pb;
    uint32_t allocs;

    /* 연습 1: 쌍 XOR 비트맵 — 2의 거듭제곱 아레나 1024 unit, 랜덤 대조 + 완전 복원 */
    {
        static const range_t all = { 0, P_NU };
        memset(g_own, 0, sizeof g_own);
        pb_init(pb, P_NU, &all, 1);
        CHECK(pb->nfree[P_MAX] == 1 && pb_check(pb, g_own));
        bool ok = stress(pb, 0x5EEDu, 20000, &allocs);
        printf("[1] pair-XOR bitmap (%u bits used): random 20000 ops %s, allocs %u\n",
               P_NU - 1u, ok ? "ok" : "BROKEN", allocs);
        CHECK(ok && pb->nfree[P_MAX] == 1);
    }
    /* 연습 2: 1000 unit — 2의 거듭제곱이 아닌 관리 범위 */
    {
        static const range_t r = { 0, 1000 };
        memset(g_own, 0, sizeof g_own);
        memset(g_own + 1000, 2, P_NU - 1000);
        pb_init(pb, 1000, &r, 1);
        print_info("[2] 1000 units init       nfree[0..10] =", pb);
        CHECK(pb->nfree[9] == 1 && pb->nfree[8] == 1 && pb->nfree[7] == 1 && pb->nfree[6] == 1
              && pb->nfree[5] == 1 && pb->nfree[3] == 1 && pb->nfree[P_MAX] == 0 && pb_check(pb, g_own));
        CHECK(pb_alloc(pb, P_MAX) < 0 && pb_check(pb, g_own));       /* 1024 unit 블록은 없다 */
        bool ok = stress(pb, 1000u, 20000, &allocs);
        print_info("[2] after stress+free all nfree[0..10] =", pb);
        CHECK(ok);
    }
    /* 연습 3: 예약 구멍 [100,130) (부트로더·DMA 디스크립터 영역) + 관리 범위 1000 */
    {
        static const range_t r[2] = { { 0, 100 }, { 130, 1000 } };
        memset(g_own, 0, sizeof g_own);
        memset(g_own + 100, 2, 30);
        memset(g_own + 1000, 2, P_NU - 1000);
        pb_init(pb, 1000, r, 2);
        print_info("[3] hole [100,130) init   nfree[0..10] =", pb);
        CHECK(pb_check(pb, g_own));
        bool ok = stress(pb, 130u, 20000, &allocs);
        CHECK(ok);
    }
    /* 연습 4: 1 KB (order 4) 를 128 B (order 1) 로 제자리 축소 → order 3,2,1 블록 반납 */
    {
        static const range_t all = { 0, P_NU };
        memset(g_own, 0, sizeof g_own);
        pb_init(pb, P_NU, &all, 1);
        int32_t u = pb_alloc(pb, 4);
        for (uint32_t x = 0; x < 16; x++) g_own[x] = 1;
        uint32_t before[P_MAX + 1];
        snapshot(pb, before);
        pb_shrink(pb, (uint32_t)u, 4, 1);
        for (uint32_t x = 2; x < 16; x++) g_own[x] = 0;
        print_info("[4] shrink order 4 -> 1   nfree[0..10] =", pb);
        CHECK(u == 0 && pb->nfree[3] == before[3] + 1u && pb->nfree[2] == before[2] + 1u
              && pb->nfree[1] == before[1] + 1u && pb_check(pb, g_own));
        pb_free(pb, 0, 1);
        memset(g_own, 0, 2);
        CHECK(pb->nfree[P_MAX] == 1 && pb_check(pb, g_own));
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
