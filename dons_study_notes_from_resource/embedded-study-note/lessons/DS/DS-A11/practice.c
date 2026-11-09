/* DS-A11 추가 연습 — 네 가지를 한 번에 검증한다.
 *   1. 필드 순서: 정렬 내림차순 배치가 전수 탐색 최솟값과 같은가
 *   2. Hot/Cold 분리: 합·라인 수를 AoS/SoA 와 비교
 *   3. SoA 의 flags 를 비트맵으로: 메모리 1/8, 합산 결과 동일
 *   4. DMA 수신 버퍼: 캐시 라인 경계에 맞지 않으면 어떤 구간이 위험한가 (Cortex-M7 32B 라인)
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
 */
#include <stdalign.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(e) do { g_total++; if (e) { g_pass++; printf("PASS %s\n", #e); } \
                      else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #e); } while (0)

static uint32_t rng_s = 0xBEEFCAFEu;
static uint32_t rnd(void) { uint32_t x = rng_s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return rng_s = x; }

/* ========== 연습 1 — 필드 순서와 sizeof ========== */
typedef struct { uint32_t size, align; } Field;

static uint32_t layout_size(const Field *f, uint32_t n) {
    uint32_t off = 0, maxa = 1;
    for (uint32_t i = 0; i < n; i++) {
        off = (off + f[i].align - 1u) / f[i].align * f[i].align;
        off += f[i].size;
        if (f[i].align > maxa) maxa = f[i].align;
    }
    return (off + maxa - 1u) / maxa * maxa;
}

/* 삽입 정렬로 align 내림차순 */
static void sort_by_align_desc(Field *f, uint32_t n) {
    for (uint32_t i = 1; i < n; i++) {
        Field t = f[i];
        uint32_t j = i;
        while (j > 0 && f[j - 1].align < t.align) { f[j] = f[j - 1]; j--; }
        f[j] = t;
    }
}

static uint32_t brute_min(Field *f, uint32_t n, uint32_t k) {
    if (k == n) return layout_size(f, n);
    uint32_t best = UINT32_MAX;
    for (uint32_t i = k; i < n; i++) {
        Field t = f[k]; f[k] = f[i]; f[i] = t;
        uint32_t s = brute_min(f, n, k + 1);
        if (s < best) best = s;
        t = f[k]; f[k] = f[i]; f[i] = t;
    }
    return best;
}

static void ex1(void) {
    printf("--- 연습 1: 정렬 내림차순 = 최소 크기\n");
    static const Field kinds[] = {{1, 1}, {2, 2}, {4, 4}, {8, 8}, {3, 1}, {6, 2}, {12, 4}, {16, 8}};
    bool ok = true;
    for (uint32_t t = 0; t < 300u && ok; t++) {
        Field f[7];
        uint32_t n = 3u + rnd() % 5u;
        for (uint32_t i = 0; i < n; i++) f[i] = kinds[rnd() % 8u];
        Field g[7];
        memcpy(g, f, sizeof f);
        uint32_t bmin = brute_min(f, n, 0);
        sort_by_align_desc(g, n);
        if (layout_size(g, n) != bmin) ok = false;
    }
    CHECK(ok);
    Field bad[] = {{1, 1}, {8, 8}, {1, 1}, {4, 4}, {2, 2}};
    CHECK(layout_size(bad, 5) == 32u);
    sort_by_align_desc(bad, 5);
    CHECK(layout_size(bad, 5) == 16u);
}

/* ========== 연습 2 — Hot/Cold 분리 ========== */
#define CAP 1024u
#define LINE 128u
typedef struct { uint32_t x, y, z, vx, vy, vz; uint16_t temp; uint8_t flags, health; } Hot;   /* 28B */
typedef struct { uint64_t t_us; uint32_t seq; uint32_t cal[6]; } Cold;                      /* 40B */
_Static_assert(sizeof(Hot) == 28 && sizeof(Cold) == 40, "sizes");

typedef struct { alignas(LINE) Hot hot[CAP]; alignas(LINE) Cold cold[CAP]; uint32_t n; } HC;
typedef struct { uint64_t t_us; uint32_t x, y, z, vx, vy, vz, seq; uint16_t temp; uint8_t flags, health; uint32_t cal[6]; } Rec;
_Static_assert(sizeof(Rec) == 64, "rec");

static HC g_hc;
static alignas(LINE) Rec g_rec[CAP];
static alignas(LINE) uint32_t g_x[CAP];

static uint32_t hc_sum_x(const HC *h) { uint32_t s = 0; for (uint32_t i = 0; i < h->n; i++) s += h->hot[i].x; return s; }

static uint32_t lines_for_stride(const void *base, size_t stride, uint32_t n, size_t elem, size_t line) {
    uint32_t c = 0;
    uintptr_t prev = UINTPTR_MAX;
    for (uint32_t i = 0; i < n; i++) {
        uintptr_t a = (uintptr_t)base + stride * i;
        for (uintptr_t l = a / line; l <= (a + elem - 1) / line; l++)
            if (l != prev) { c++; prev = l; }
    }
    return c;
}

static void ex2(void) {
    printf("--- 연습 2: Hot/Cold\n");
    g_hc.n = CAP;
    uint32_t sum = 0;
    for (uint32_t i = 0; i < CAP; i++) {
        uint32_t v = rnd();
        g_hc.hot[i].x = v; g_rec[i].x = v; g_x[i] = v;
        sum += v;
    }
    CHECK(hc_sum_x(&g_hc) == sum);
    uint32_t aos = lines_for_stride(&g_rec[0].x, sizeof(Rec), CAP, 4, LINE);
    uint32_t hc = lines_for_stride(&g_hc.hot[0].x, sizeof(Hot), CAP, 4, LINE);
    uint32_t soa = lines_for_stride(&g_x[0], 4, CAP, 4, LINE);
    printf("  x 스캔 라인 수 (128B): AoS %u, Hot/Cold %u, SoA %u\n", aos, hc, soa);
    CHECK(aos == 512u && hc == 224u && soa == 32u);
    /* 교훈: Hot 28B → AoS 대비 라인 2.3 배 절감. 더 작게 쪼갤수록 SoA 쪽으로 간다. */
}

/* ========== 연습 3 — 비트맵 flags ========== */
#define BM_WORDS ((CAP + 31u) / 32u)
static void bm_set(uint32_t *w, uint32_t i, bool v) {
    uint32_t m = 1u << (i & 31u);
    if (v) w[i >> 5] |= m; else w[i >> 5] &= ~m;
}
static bool bm_get(const uint32_t *w, uint32_t i) { return (w[i >> 5] >> (i & 31u)) & 1u; }

/* 비트 하나당 x 하나: 비트가 켜진 인덱스만 ctz 로 건너뛰며 합산. */
static uint32_t bm_sum_masked(const uint32_t *x, const uint32_t *w, uint32_t n) {
    uint32_t s = 0;
    for (uint32_t k = 0; k < (n + 31u) / 32u; k++) {
        uint32_t bits = w[k];
        if (k == n / 32u && (n & 31u)) bits &= (1u << (n & 31u)) - 1u;   /* 범위 밖 비트 무시 */
        while (bits) {
            s += x[(k << 5) + (uint32_t)__builtin_ctz(bits)];
            bits &= bits - 1u;
        }
    }
    return s;
}

static void ex3(void) {
    printf("--- 연습 3: 비트맵 flags\n");
    static uint32_t x[CAP], w[BM_WORDS];
    static uint8_t flags[CAP];
    memset(w, 0, sizeof w);
    uint32_t sum = 0, n = 1000u;
    for (uint32_t i = 0; i < CAP; i++) {
        x[i] = rnd();
        flags[i] = (uint8_t)(rnd() & 1u);
        bm_set(w, i, flags[i]);
        if (i < n && flags[i]) sum += x[i];
    }
    bool same = true;
    for (uint32_t i = 0; i < CAP; i++) same = same && (bm_get(w, i) == (bool)flags[i]);
    CHECK(same);
    CHECK(bm_sum_masked(x, w, n) == sum);
    CHECK(bm_sum_masked(x, w, 0) == 0u);
    CHECK(sizeof w == CAP / 8u && sizeof w * 8u == sizeof flags);        /* 8 배 절감 */
    bm_set(w, 3, true); CHECK(bm_get(w, 3));
    bm_set(w, 3, false); CHECK(!bm_get(w, 3));
}

/* ========== 연습 4 — DMA 수신 버퍼와 캐시 라인 ========== */
typedef struct { uintptr_t start, end; } Range;    /* [start, end) */

/* RX 버퍼 [addr, addr+len) 에서 "무효화해도 안전한" 안쪽 구간: 양 끝을 라인에 맞춰 안으로 민다. */
static Range rx_safe_inner(uintptr_t addr, size_t len, size_t line) {
    Range r;
    r.start = (addr + line - 1u) & ~(uintptr_t)(line - 1u);
    r.end = (addr + len) & ~(uintptr_t)(line - 1u);
    if (r.end < r.start) r.end = r.start;
    return r;
}

static bool rx_buffer_ok(uintptr_t addr, size_t len, size_t line) {
    return (addr & (line - 1u)) == 0u && (len & (line - 1u)) == 0u;
}

static void ex4(void) {
    printf("--- 연습 4: DMA RX 버퍼 (32B 라인)\n");
    alignas(32) static uint8_t buf[64];
    _Static_assert(sizeof buf % 32 == 0, "DMA buffer size must be a whole number of lines");
    CHECK(((uintptr_t)buf & 31u) == 0u);
    CHECK(rx_buffer_ok((uintptr_t)buf, sizeof buf, 32));
    CHECK(!rx_buffer_ok(0x20000010u, 64, 32));
    CHECK(!rx_buffer_ok(0x20000000u, 60, 32));
    Range r = rx_safe_inner(0x20000010u, 64, 32);
    CHECK(r.start == 0x20000020u && r.end == 0x20000040u);
    r = rx_safe_inner(0x20000000u, 64, 32);
    CHECK(r.start == 0x20000000u && r.end == 0x20000040u);
    r = rx_safe_inner(0x20000005u, 10, 32);
    CHECK(r.start == r.end);        /* 라인 하나 안에 다 들어감 → 안전 구간 없음 */
}

int main(void) {
    ex1();
    ex2();
    ex3();
    ex4();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
