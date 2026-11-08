/* C-A05 정렬 보장 할당: aligned_malloc과 DMA 버퍼
 *
 * malloc 은 "어떤 기본 타입에도 맞는" 정렬(이 맥 16 B, Cortex-M newlib 보통 8 B)만 보장한다.
 * DMA 엔진·캐시 유지보수·MPU 는 더 큰 정렬(32 B 캐시 라인, 4 KB 페이지)과
 * "버퍼가 4 KB 경계를 넘지 말 것" 같은 조건을 요구한다.
 *   Part A — aligned_malloc/aligned_free: 과할당 + 정렬 포인터 바로 앞에 헤더(원래 포인터) 저장
 *   Part B — 잘못된 align, size 오버플로, malloc 실패, aligned_realloc
 *   Part C — crosses_boundary, 힙 dma_buf_alloc(캐시 라인 격리 + 경계), 정적 아레나 arena_alloc
 * 시스템 할당기는 sys_malloc/sys_free 로 계측된다: 호출 수·요청 크기·살아 있는 블록·잘못된 free.
 */
#include <stdalign.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CACHE_LINE 32u            /* Cortex-M7 L1 D-cache 라인 (Cortex-A 보통 64, Apple M 계열 128) */
#define GIVEN __attribute__((unused))   /* 주어진 도우미: starter 에서 아직 안 쓰여도 경고 없게 */

/* ---------------- 계측되는 시스템 할당기 (주어진 코드 — 고치지 않는다) ---------------- */
#define LIVE_MAX 256
static struct { void *p; size_t n; } g_live[LIVE_MAX];   /* 살아 있는 raw 블록과 그 크기 */
static int g_nlive, g_malloc_calls, g_free_calls, g_bad_free;
static int g_fail_next;                                   /* 1 이면 다음 sys_malloc 이 NULL */
static size_t g_last_req;                                 /* 마지막 sys_malloc 요청 크기 */

GIVEN static void *sys_malloc(size_t n) {
    g_malloc_calls++;
    g_last_req = n;
    if (g_fail_next) { g_fail_next = 0; return NULL; }
    if (n == 0 || n > ((size_t)1 << 30) || g_nlive == LIVE_MAX) return NULL;   /* "메모리 부족" 모델 */
    void *p = malloc(n);
    if (p) { g_live[g_nlive].p = p; g_live[g_nlive].n = n; g_nlive++; }
    return p;
}
GIVEN static void sys_free(void *p) {
    g_free_calls++;
    for (int i = 0; i < g_nlive; i++)
        if (g_live[i].p == p) { free(p); g_live[i] = g_live[--g_nlive]; return; }
    g_bad_free++;                 /* malloc 이 준 적 없는 포인터 — 진짜 free 였다면 abort 또는 힙 손상 */
}

/* ---------------- 도우미 (주어진 코드) ---------------- */
GIVEN static bool is_pow2(size_t x) { return x != 0 && (x & (x - 1)) == 0; }
static uintptr_t align_up(uintptr_t v, size_t a) { return (v + (a - 1)) & ~(uintptr_t)(a - 1); }  /* a = 2^k */
static size_t max_sz(size_t a, size_t b) { return a > b ? a : b; }

/* 정렬 포인터 바로 앞 HDR 바이트에 놓는 헤더 */
typedef struct { void *raw; size_t size; size_t align; } ahdr_t;
#define HDR sizeof(ahdr_t)

/* raw 블록의 off 위치를 사용자 포인터로 만들고, 그 바로 앞에 헤더를 쓴다 */
GIVEN static void *place(void *raw, size_t off, size_t size, size_t align) {
    unsigned char *p = (unsigned char *)raw + off;          /* 정수→포인터 변환 없이 raw 에서 전진 */
    ahdr_t h = { raw, size, align };
    memcpy(p - HDR, &h, HDR);
    return p;
}
static ahdr_t hdr_of(const void *p) {
    ahdr_t h;
    memcpy(&h, (const unsigned char *)p - HDR, HDR);
    return h;
}

/* ======================= Part A: aligned_malloc / aligned_free ======================= */

/* size 바이트, 주소가 align(2^k) 의 배수. align < alignof(ahdr_t) 이면 alignof(ahdr_t) 로 올린다.
 * size == 0 또는 align 이 2의 거듭제곱이 아니면 NULL. sys_malloc 요청은 정확히 size + HDR + (a - 1). */
static void *aligned_malloc(size_t size, size_t align) {
    if (size == 0 || !is_pow2(align)) return NULL;
    size_t a = max_sz(align, alignof(ahdr_t));               /* 헤더 자리도 정렬되게 */
    if (size > SIZE_MAX - HDR - (a - 1)) return NULL;        /* 요청 크기 계산이 넘친다 */
    void *raw = sys_malloc(size + HDR + (a - 1));
    if (raw == NULL) return NULL;
    uintptr_t r = (uintptr_t)raw;
    return place(raw, (size_t)(align_up(r + HDR, a) - r), size, a);
}

/* aligned_malloc/dma_buf_alloc 이 준 포인터 해제. NULL 은 아무 일도 안 한다. */
static void aligned_free(void *p) {
    if (p == NULL) return;
    sys_free(hdr_of(p).raw);
}

/* ======================= Part B: aligned_realloc ======================= */

/* p == NULL → aligned_malloc(new_size, align). new_size == 0 → 해제 후 NULL.
 * 그 외: align 으로 새로 할당 + 앞부분 복사 + 옛 블록 해제. 실패하면 NULL, p 는 그대로 유효. */
static void *aligned_realloc(void *p, size_t new_size, size_t align) {
    if (p == NULL) return aligned_malloc(new_size, align);
    if (new_size == 0) { aligned_free(p); return NULL; }
    void *q = aligned_malloc(new_size, align);
    if (q == NULL) return NULL;                              /* 옛 블록은 건드리지 않는다 */
    size_t old = hdr_of(p).size;
    memcpy(q, p, old < new_size ? old : new_size);
    aligned_free(p);
    return q;
}

/* ======================= Part C: DMA 버퍼 ======================= */

/* [addr, addr+len) 이 boundary(2^k) 의 배수를 "가로지르나"? len == 0 은 false. 주소 끝에서 감싸면 true. */
static bool crosses_boundary(uintptr_t addr, size_t len, size_t boundary) {
    if (len == 0) return false;
    if (len > boundary) return true;
    return ((addr ^ (addr + (len - 1))) & ~(uintptr_t)(boundary - 1)) != 0;
}

/* 힙 DMA 버퍼: 시작은 max(align, CACHE_LINE) 정렬, 끝은 CACHE_LINE 까지 raw 블록 안(라인 격리),
 * [p, p+size) 는 boundary 를 넘지 않는다. aligned_free 로 해제. sys_malloc 은 HDR + (a-1) + 2n. */
static void *dma_buf_alloc(size_t size, size_t align, size_t boundary) {
    if (size == 0 || !is_pow2(align) || !is_pow2(boundary) || size > boundary) return NULL;
    size_t a = max_sz(max_sz(align, CACHE_LINE), alignof(ahdr_t));
    size_t n = (size + (CACHE_LINE - 1)) & ~(size_t)(CACHE_LINE - 1);   /* 끝도 라인 경계까지 */
    if (a > boundary || n > boundary) return NULL;
    if (n > (SIZE_MAX - HDR - (a - 1)) / 2) return NULL;                /* 2n 계산이 넘친다 */
    void *raw = sys_malloc(HDR + (a - 1) + 2 * n);
    if (raw == NULL) return NULL;
    uintptr_t r = (uintptr_t)raw, s = align_up(r + HDR, a);
    if (crosses_boundary(s, n, boundary)) s = align_up(s + 1, boundary);  /* 다음 경계로 옮긴다 */
    return place(raw, (size_t)(s - r), size, a);
}

/* 정적 DMA 아레나 (펌웨어: malloc 없음, 초기화 때 한 번 잘라 쓰고 해제 없음) */
typedef struct { uint8_t *base; size_t cap; size_t used; } arena_t;

/* dma_buf_alloc 과 같은 규칙(정렬 max(align, CACHE_LINE), 크기는 라인 단위로 올림, 경계 안 넘김).
 * 공간이 없으면 NULL 이고 used 는 바뀌지 않는다. */
static void *arena_alloc(arena_t *ar, size_t size, size_t align, size_t boundary) {
    if (size == 0 || !is_pow2(align) || !is_pow2(boundary) || size > boundary) return NULL;
    size_t a = max_sz(align, CACHE_LINE);
    size_t n = (size + (CACHE_LINE - 1)) & ~(size_t)(CACHE_LINE - 1);
    if (a > boundary || n > boundary) return NULL;
    uintptr_t b = (uintptr_t)ar->base, s = align_up(b + ar->used, a);
    if (crosses_boundary(s, n, boundary)) s = align_up(s + 1, boundary);
    size_t off = (size_t)(s - b);
    if (off > ar->cap || n > ar->cap - off) return NULL;     /* 넘치면 used 그대로 */
    ar->used = off + n;
    return ar->base + off;
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t rng_state = 0x2545F491u;                  /* xorshift32 — 고정 시드, 재현 가능 */
static uint32_t xorshift32(void) {
    uint32_t x = rng_state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return rng_state = x;
}
static bool aligned_to(const void *p, size_t a) { return ((uintptr_t)p & (a - 1)) == 0; }
/* [lo, hi) 가 살아 있는 raw 블록 하나 안에 완전히 들어 있나 */
static bool inside_raw(uintptr_t lo, uintptr_t hi) {
    for (int i = 0; i < g_nlive; i++) {
        uintptr_t r = (uintptr_t)g_live[i].p;
        if (lo >= r && hi <= r + g_live[i].n) return true;
    }
    return false;
}
static bool is_seq(const unsigned char *p, size_t n) {
    for (size_t i = 0; i < n; i++) if (p[i] != (unsigned char)i) return false;
    return true;
}
static void reset_counters(void) { g_malloc_calls = g_free_calls = g_bad_free = 0; g_fail_next = 0; }

/* 모의 DMA 엔진 (주어진 코드): 하드웨어 규칙 = 주소 32 B 정렬, 길이 1..4096, 4 KB 페이지 하나 안 */
static bool dma_engine_accepts(const void *buf, size_t len) {
    uintptr_t a = (uintptr_t)buf;
    return len != 0 && len <= 4096 && a % CACHE_LINE == 0 && a / 4096 == (a + len - 1) / 4096;
}

_Alignas(4096) static uint8_t g_dma_mem[4 * 4096];       /* 펌웨어라면 .bss.dma (MPU non-cacheable) */

int main(void) {
    /* ---------- Part A: 기본 ---------- */
    reset_counters();
    unsigned char *p = aligned_malloc(100, 64);
    CHECK(p != NULL && aligned_to(p, 64));
    CHECK(g_malloc_calls == 1 && g_nlive == 1 && g_last_req == 100 + HDR + 63);
    CHECK(p != NULL && inside_raw((uintptr_t)p - HDR, (uintptr_t)p + 100));   /* 헤더+본문이 raw 안 */
    if (p) memset(p, 0xAB, 100);
    aligned_free(p);
    CHECK(g_free_calls == 1 && g_nlive == 0 && g_bad_free == 0);   /* raw 포인터를 정확히 돌려줬다 */

    bool ok = true;                                    /* 작은 align(1,2,4)도 동작 */
    for (size_t al = 1; al <= 4; al <<= 1) {
        void *q = aligned_malloc(10, al);
        ok = ok && q != NULL && aligned_to(q, al) && aligned_to(q, alignof(ahdr_t));
        aligned_free(q);
    }
    CHECK(ok && g_nlive == 0);
    void *big = aligned_malloc(1, (size_t)1 << 20);    /* 1 MB 정렬도 같은 원리 */
    CHECK(big != NULL && aligned_to(big, (size_t)1 << 20));
    aligned_free(big);
    CHECK(g_nlive == 0 && g_bad_free == 0);

    /* ---------- Part A: 랜덤 — 정렬 2^3..2^12, 크기 1..5000, 동시에 최대 32개, 내용 침범 없음 ---------- */
    enum { SLOTS = 32, ITER = 3000 };
    struct { unsigned char *p; size_t n; unsigned char tag; } slot[SLOTS];
    memset(slot, 0, sizeof slot);
    int null_ret = 0, bad_align = 0, bad_bounds = 0, bad_data = 0;
    unsigned seen_k = 0;
    for (int it = 0; it < ITER; it++) {
        int k = (int)(xorshift32() % SLOTS);
        if (slot[k].p) {                               /* 옛 블록의 무늬가 그대로인지 보고 해제 */
            for (size_t i = 0; i < slot[k].n; i++)
                if (slot[k].p[i] != slot[k].tag) { bad_data++; break; }
            aligned_free(slot[k].p);
            slot[k].p = NULL;
        }
        unsigned e = 3u + xorshift32() % 10u;
        size_t al = (size_t)1 << e, n = 1 + xorshift32() % 5000u;
        seen_k |= 1u << e;
        unsigned char *q = aligned_malloc(n, al);
        if (q == NULL) { null_ret++; continue; }
        if (!aligned_to(q, al)) bad_align++;
        if (!inside_raw((uintptr_t)q, (uintptr_t)q + n)) bad_bounds++;
        slot[k].p = q; slot[k].n = n; slot[k].tag = (unsigned char)(it * 7 + 1);
        memset(q, slot[k].tag, n);
    }
    for (int k = 0; k < SLOTS; k++) aligned_free(slot[k].p);
    CHECK(seen_k == 0x1FF8u);                          /* 2^3..2^12 모두 나왔다 */
    CHECK(null_ret == 0 && bad_align == 0);
    CHECK(bad_bounds == 0 && bad_data == 0);
    CHECK(g_nlive == 0 && g_bad_free == 0 && g_malloc_calls == g_free_calls);

    /* ---------- Part B: 잘못된 인자 · 오버플로 · 실패 ---------- */
    reset_counters();
    CHECK(aligned_malloc(64, 0) == NULL && aligned_malloc(64, 3) == NULL);
    CHECK(aligned_malloc(64, 24) == NULL && aligned_malloc(64, 4097) == NULL);
    CHECK(aligned_malloc(0, 64) == NULL);
    CHECK(g_malloc_calls == 0);                        /* 잘못된 인자는 malloc 까지 가지도 않는다 */
    size_t maxsz = SIZE_MAX - HDR - 63;                /* align 64 에서 요청이 정확히 SIZE_MAX 가 되는 크기 */
    CHECK(aligned_malloc(maxsz + 1, 64) == NULL && g_malloc_calls == 0);           /* 넘침: 호출 안 함 */
    CHECK(aligned_malloc(maxsz, 64) == NULL && g_malloc_calls == 1 && g_last_req == SIZE_MAX);
    reset_counters();
    g_fail_next = 1;
    CHECK(aligned_malloc(100, 64) == NULL && g_malloc_calls == 1 && g_nlive == 0); /* 실패 전파 */
    reset_counters();
    aligned_free(NULL);
    CHECK(g_free_calls == 0 && g_bad_free == 0);

    /* aligned_realloc */
    unsigned char *r = aligned_malloc(100, 256);
    if (r) for (size_t i = 0; i < 100; i++) r[i] = (unsigned char)i;
    unsigned char *r2 = aligned_realloc(r, 5000, 256);           /* 키우기 */
    CHECK(r2 != NULL && aligned_to(r2, 256) && is_seq(r2, 100) && g_nlive == 1);
    if (r2) r2[4999] = 0x5A;                                     /* 새 크기 전체를 쓸 수 있다 */
    unsigned char *r3 = aligned_realloc(r2, 10, 4096);           /* 줄이면서 정렬도 바꾼다 */
    CHECK(r3 != NULL && aligned_to(r3, 4096) && is_seq(r3, 10) && g_nlive == 1);
    g_fail_next = 1;
    CHECK(aligned_realloc(r3, 20000, 64) == NULL);               /* 새 할당 실패 → */
    CHECK(r3 != NULL && is_seq(r3, 10) && g_nlive == 1);         /* 옛 블록은 살아 있다 */
    g_fail_next = 0;
    CHECK(aligned_realloc(r3, 0, 64) == NULL && g_nlive == 0);   /* 크기 0 = 해제 */
    void *r4 = aligned_realloc(NULL, 33, 128);                   /* NULL = aligned_malloc */
    CHECK(r4 != NULL && aligned_to(r4, 128) && g_nlive == 1);
    aligned_free(r4);
    CHECK(g_nlive == 0 && g_bad_free == 0);

    /* ---------- Part C: crosses_boundary ---------- */
    CHECK(!crosses_boundary(0x0FF0, 16, 4096) && crosses_boundary(0x0FF0, 17, 4096));
    CHECK(!crosses_boundary(0x1000, 4096, 4096) && crosses_boundary(0x1000, 4097, 4096));
    CHECK(!crosses_boundary(0x1FFF, 1, 4096) && crosses_boundary(0x1FFF, 2, 4096));
    CHECK(!crosses_boundary(0x1234, 0, 4096));
    CHECK(!crosses_boundary(UINTPTR_MAX - 15, 16, 4096) && crosses_boundary(UINTPTR_MAX - 15, 17, 4096));
    CHECK(crosses_boundary(0x1FFF, SIZE_MAX, 4096));             /* 경계보다 길면 무조건 */
    CHECK(!crosses_boundary(0x10000, 0x10000, 0x10000) && crosses_boundary(0xFFFF, 2, 0x10000));

    /* ---------- Part C: 힙 DMA 버퍼 ---------- */
    reset_counters();
    CHECK(dma_buf_alloc(0, 32, 4096) == NULL && dma_buf_alloc(4097, 32, 4096) == NULL);
    CHECK(dma_buf_alloc(64, 48, 4096) == NULL && dma_buf_alloc(64, 32, 3000) == NULL);
    CHECK(dma_buf_alloc(16, 8, 16) == NULL && dma_buf_alloc(64, 8192, 4096) == NULL);
    CHECK(dma_buf_alloc(SIZE_MAX / 2, 32, SIZE_MAX / 2 + 1) == NULL);   /* 2n 넘침 */
    CHECK(g_malloc_calls == 0);
    void *d = dma_buf_alloc(4096, 32, 4096);           /* 한 페이지를 꽉 채우려면 페이지 정렬뿐 */
    CHECK(d != NULL && aligned_to(d, 4096) && g_last_req == HDR + 31 + 2 * 4096);
    aligned_free(d);
    CHECK(g_nlive == 0 && g_bad_free == 0);            /* aligned_free 로 해제된다 */

    int dnull = 0, dalign = 0, drej = 0, diso = 0, reloc = 0;
    for (int it = 0; it < 2000; it++) {
        size_t n = 1 + xorshift32() % 4096u, al = (size_t)1 << (3u + xorshift32() % 10u);
        unsigned char *b = dma_buf_alloc(n, al, 4096);
        if (b == NULL) { dnull++; continue; }
        size_t eff = max_sz(al, CACHE_LINE);
        if (!aligned_to(b, eff)) dalign++;
        if (!dma_engine_accepts(b, n)) drej++;         /* 독립 오라클: 페이지 번호 비교 */
        uintptr_t lo = (uintptr_t)b & ~(uintptr_t)(CACHE_LINE - 1);
        uintptr_t hi = align_up((uintptr_t)b + n, CACHE_LINE);
        if (!inside_raw(lo, hi)) diso++;               /* 버퍼가 걸친 캐시 라인이 전부 내 raw 블록 안 */
        if ((uintptr_t)b != align_up((uintptr_t)hdr_of(b).raw + HDR, eff)) reloc++;
        memset(b, 0xCD, n);
        aligned_free(b);
    }
    CHECK(dnull == 0 && dalign == 0);
    CHECK(drej == 0 && diso == 0);
    CHECK(reloc > 0);                                  /* "다음 경계로 옮기기" 경로가 실제로 쓰였다 */
    CHECK(g_nlive == 0 && g_bad_free == 0);

    /* ---------- Part C: 정적 아레나 — ETH RX 링 버퍼 8 × 1536 B ---------- */
    arena_t ar = { g_dma_mem, sizeof g_dma_mem, 0 };
    size_t off[9];
    for (int i = 0; i < 9; i++) {
        uint8_t *b = arena_alloc(&ar, 1536, 32, 4096);
        off[i] = b ? (size_t)(b - g_dma_mem) : (size_t)-1;
    }
    CHECK(off[0] == 0 && off[1] == 1536 && off[2] == 4096 && off[3] == 5632);   /* 3번째는 경계 넘어 이동 */
    CHECK(off[4] == 8192 && off[5] == 9728 && off[6] == 12288 && off[7] == 13824);
    CHECK(off[8] == (size_t)-1 && ar.used == 15360);   /* 9번째는 실패, used 그대로 */
    ok = true;
    for (int i = 0; i < 8; i++) ok = ok && off[i] != (size_t)-1 && dma_engine_accepts(g_dma_mem + off[i], 1536);
    CHECK(ok);                                         /* 모든 RX 버퍼를 DMA 엔진이 받아들인다 */
    uint8_t *desc = arena_alloc(&ar, 16 * 16, 64, 4096);           /* 디스크립터 16개 — 남은 틈에 */
    CHECK(desc == g_dma_mem + 15360 && ar.used == 15616);
    CHECK(arena_alloc(&ar, 20, 8, 4096) == g_dma_mem + 15616 && ar.used == 15648);   /* 20 B → 라인 32 B */
    CHECK(arena_alloc(&ar, 64, 32, 3000) == NULL && arena_alloc(&ar, 0, 32, 4096) == NULL);
    CHECK(arena_alloc(&ar, 800, 32, 4096) == NULL && ar.used == 15648);   /* 남은 736 B 로는 부족 */
    unsigned char *m = malloc(64);                     /* 참고: 일반 malloc 버퍼 +4 는 엔진이 거부 */
    CHECK(m != NULL && !dma_engine_accepts(m + 4, 32));
    free(m);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
