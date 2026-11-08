/* C-A05 추가 연습 — 한 번에 검증
 * 연습 1: 1바이트 오프셋 헤더 — 오버헤드를 align 바이트로 줄인 aligned_malloc 변형
 * 연습 2: Linux dma_pool 스타일 고정 크기 풀 — 어떤 블록도 경계를 넘지 않게 잘라 free list 로 O(1)
 * 연습 3: 두 버퍼가 캐시 라인을 공유하나 — RX invalidate 가 이웃을 지우는 버그의 판정
 * 연습 4: ARMv7-M MPU 영역 계산 — 버퍼를 덮는 가장 작은 (2의 거듭제곱 크기, 크기 정렬) 영역
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t rng_state = 0x9E3779B9u;
static uint32_t xorshift32(void) {
    uint32_t x = rng_state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return rng_state = x;
}
static bool is_pow2(size_t x) { return x != 0 && (x & (x - 1)) == 0; }
static uintptr_t align_up(uintptr_t v, size_t a) { return (v + (a - 1)) & ~(uintptr_t)(a - 1); }
static bool aligned_to(const void *p, size_t a) { return ((uintptr_t)p & (a - 1)) == 0; }
static bool crosses(uintptr_t addr, size_t len, size_t b) {
    if (len == 0) return false;
    if (len > b) return true;
    return ((addr ^ (addr + (len - 1))) & ~(uintptr_t)(b - 1)) != 0;
}

/* ---------- 연습 1: 1바이트 오프셋 헤더 ---------- */
static size_t g_req1;
static void *small_aligned_malloc(size_t size, size_t align) {
    if (size == 0 || !is_pow2(align) || align > 128 || size > SIZE_MAX - align) return NULL;
    g_req1 = size + align;                         /* 앞쪽에 최소 1바이트, 최대 align 바이트 */
    unsigned char *raw = malloc(g_req1);
    if (raw == NULL) return NULL;
    uintptr_t r = (uintptr_t)raw;
    size_t off = (size_t)(align_up(r + 1, align) - r); /* 1..align (≤ 128 → uint8_t 에 들어간다) */
    unsigned char *p = raw + off;
    p[-1] = (unsigned char)off;
    return p;
}
static void small_aligned_free(void *p) {
    if (p == NULL) return;
    unsigned char *q = p;
    free(q - q[-1]);
}

/* ---------- 연습 2: dma_pool ---------- */
typedef struct { void *free_head; size_t nblk, nfree; } pool_t;
/* mem[0..cap) 을 stride = blk 를 align 배수로 올린 크기로 자른다. 어떤 블록도 boundary 를 넘지 않는다.
 * 각 빈 블록의 첫 sizeof(void*) 바이트에 다음 빈 블록 포인터를 둔다 (침입형 free list).
 * mem 은 uint8_t 배열(선언된 타입이 있는 객체)이라 void* 로 직접 읽고 쓰면 6.5p7 위반 → memcpy. */
static size_t pool_init(pool_t *pl, uint8_t *mem, size_t cap, size_t blk, size_t align, size_t boundary) {
    memset(pl, 0, sizeof *pl);
    if (!is_pow2(align) || !is_pow2(boundary) || blk == 0) return 0;
    size_t stride = (blk + align - 1) & ~(align - 1);
    if (stride < sizeof(void *) || stride > boundary) return 0;
    uintptr_t base = (uintptr_t)mem, end = base + cap, s = align_up(base, align);
    void *prev = NULL;
    for (;;) {
        if (crosses(s, stride, boundary)) s = align_up(s + 1, boundary);
        if (s > end || stride > end - s) break;
        void *b = mem + (s - base);
        if (prev) memcpy(prev, &b, sizeof b); else pl->free_head = b;   /* 주소 순서대로 연결 */
        prev = b;
        pl->nblk++;
        s += stride;
    }
    if (prev) { void *none = NULL; memcpy(prev, &none, sizeof none); }
    pl->nfree = pl->nblk;
    return pl->nblk;
}
static void *pool_alloc(pool_t *pl) {
    void *b = pl->free_head;
    if (b) { memcpy(&pl->free_head, b, sizeof b); pl->nfree--; }
    return b;
}
static void pool_free(pool_t *pl, void *b) {
    memcpy(b, &pl->free_head, sizeof pl->free_head);
    pl->free_head = b;
    pl->nfree++;
}

/* ---------- 연습 3: 캐시 라인 공유 ---------- */
static bool shares_cache_line(uintptr_t a, size_t alen, uintptr_t b, size_t blen, size_t line) {
    if (alen == 0 || blen == 0) return false;
    uintptr_t a0 = a / line, a1 = (a + alen - 1) / line;
    uintptr_t b0 = b / line, b1 = (b + blen - 1) / line;
    return a0 <= b1 && b0 <= a1;                   /* 라인 번호 구간이 겹치나 */
}

/* ---------- 연습 4: ARMv7-M MPU 영역 ---------- */
/* 크기 2^k (32 B ~ 4 GB), 시작 주소는 크기의 배수. [addr, addr+len) 을 덮는 가장 작은 영역. */
static bool mpu_region_for(uint32_t addr, uint32_t len, uint32_t *base, uint64_t *size) {
    uint64_t end = (uint64_t)addr + len;           /* 끝(포함 안 함), 64비트라 넘치지 않는다 */
    if (len == 0 || end > ((uint64_t)1 << 32)) return false;
    for (uint64_t sz = 32; sz <= ((uint64_t)1 << 32); sz <<= 1) {
        uint64_t b = (uint64_t)addr & ~(sz - 1);
        if (b + sz >= end) { *base = (uint32_t)b; *size = sz; return true; }
    }
    return false;
}

_Alignas(4096) static uint8_t g_pool_mem[4 * 4096];

int main(void) {
    /* 연습 1 */
    unsigned char *p = small_aligned_malloc(100, 64);
    CHECK(p != NULL && aligned_to(p, 64) && g_req1 == 164);       /* Part A 판은 100 + 24 + 63 = 187 */
    if (p) memset(p, 0x11, 100);
    small_aligned_free(p);
    CHECK(small_aligned_malloc(10, 256) == NULL && small_aligned_malloc(10, 48) == NULL);
    int bad = 0;
    for (int i = 0; i < 2000; i++) {
        size_t al = (size_t)1 << (xorshift32() % 8u), n = 1 + xorshift32() % 3000u;   /* 1..128 */
        unsigned char *q = small_aligned_malloc(n, al);
        if (q == NULL || !aligned_to(q, al)) { bad++; small_aligned_free(q); continue; }
        memset(q, 0x22, n);
        small_aligned_free(q);
    }
    CHECK(bad == 0);

    /* 연습 2 */
    pool_t pl;
    CHECK(pool_init(&pl, g_pool_mem, sizeof g_pool_mem, 1536, 32, 4096) == 8);
    CHECK(pool_init(&pl, g_pool_mem, sizeof g_pool_mem, 96, 32, 4096) == 168);    /* 페이지당 42개 */
    CHECK(pool_init(&pl, g_pool_mem, sizeof g_pool_mem, 100, 32, 4096) == 128);   /* stride 128 */
    pool_init(&pl, g_pool_mem, sizeof g_pool_mem, 96, 32, 4096);
    void *blk[168];
    int nul = 0, crossing = 0, misal = 0, dup = 0;
    for (int i = 0; i < 168; i++) {
        blk[i] = pool_alloc(&pl);
        if (blk[i] == NULL) { nul++; continue; }
        if (crosses((uintptr_t)blk[i], 96, 4096)) crossing++;
        if (!aligned_to(blk[i], 32)) misal++;
        for (int j = 0; j < i; j++) if (blk[j] == blk[i]) dup++;
    }
    CHECK(nul == 0 && crossing == 0 && misal == 0 && dup == 0);
    CHECK(pool_alloc(&pl) == NULL && pl.nfree == 0);
    CHECK(blk[41] == (void *)(g_pool_mem + 41 * 96) && blk[42] == (void *)(g_pool_mem + 4096));
    pool_free(&pl, blk[7]);
    pool_free(&pl, blk[3]);
    CHECK(pool_alloc(&pl) == blk[3] && pool_alloc(&pl) == blk[7] && pl.nfree == 0);   /* LIFO */
    CHECK(pool_init(&pl, g_pool_mem, sizeof g_pool_mem, 8192, 32, 4096) == 0);       /* 블록 > 경계 */

    /* 연습 3 */
    CHECK(!shares_cache_line(0, 32, 32, 32, 32) && shares_cache_line(0, 32, 31, 1, 32));
    CHECK(shares_cache_line(0, 1500, 1500, 1500, 32));     /* 4바이트 정렬로 붙여 놓은 두 RX 버퍼 */
    CHECK(!shares_cache_line(0, 1500, 1504, 1500, 32));    /* 라인 단위로 올려 배치 (arena_alloc) */
    CHECK(!shares_cache_line(0, 0, 0, 10, 32));

    /* 연습 4 */
    uint32_t b; uint64_t sz;
    CHECK(mpu_region_for(0x20000000u, 16384, &b, &sz) && b == 0x20000000u && sz == 16384);
    CHECK(mpu_region_for(0x20001000u, 0x1800, &b, &sz) && b == 0x20000000u && sz == 0x4000);
    CHECK(mpu_region_for(0x20000FF0u, 0x20, &b, &sz) && b == 0x20000000u && sz == 0x2000);   /* 32 B 인데 8 KB */
    CHECK(mpu_region_for(0x20000040u, 1, &b, &sz) && b == 0x20000040u && sz == 32);
    CHECK(mpu_region_for(0x80000000u, 0x80000000u, &b, &sz) && b == 0x80000000u && sz == 0x80000000u);
    CHECK(!mpu_region_for(0x20000000u, 0, &b, &sz) && !mpu_region_for(0xFFFFFFF0u, 0x20, &b, &sz));

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
