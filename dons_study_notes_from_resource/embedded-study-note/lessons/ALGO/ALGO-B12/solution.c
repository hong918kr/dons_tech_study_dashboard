/* ALGO-B12 비트 트릭 1: popcount·최하위 비트·2의 거듭제곱
 *
 *   Part A  popcount: Kernighan 루프(1의 개수만큼 반복) · SWAR 32/64비트(분기 없음, 연산 수 고정)
 *   Part B  최하위 1비트 x & -x, 최하위 1비트 지우기 x & (x-1), 2의 거듭제곱 판정,
 *           ctz/clz (0 이면 32 로 정의), 다음 2의 거듭제곱 올림 (0 → 1, 2^31 초과 → 0 = 답 없음)
 *   Part C  ALIGN_UP: 32비트 오버플로 검사판, 64비트 주소 + 32비트 정렬값, DMA 캐시 라인 구간,
 *           빈 슬롯 비트맵 할당기, 이벤트 그룹 비트 처리
 *
 * 규칙: TODO 안에서는 __builtin_* 를 쓰지 않는다. 테스트 쪽에서만 __builtin_popcount/ctz/clz 와 대조한다
 *       (__builtin_ctz/clz 는 0 에서 정의되지 않으므로 0 은 따로 검사).
 * 범위: 16비트 전 범위(아래·위 반쪽) + 구조적 값(한 비트, 두 비트, 2^k-1, 2^k±1) + xorshift32 랜덤 2^20 개.
 *       2^32 전 범위 대조는 demo_exhaustive.c (여기서 하면 ASan 빌드가 2초를 넘는다).
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o st && ./st   (solution.c 도 같은 명령)
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ======================= Part A: popcount ======================= */
/* 1인 비트를 하나씩 지운다 → 반복 횟수 = popcount(x) (최악 32번) */
static unsigned pc_kernighan(uint32_t x) {
    unsigned c = 0;
    while (x) { x &= x - 1u; c++; }
    return c;
}

/* SWAR (SIMD Within A Register): 칸 크기를 2→4→8 비트로 키우며 이웃 칸을 더한다 */
static unsigned pc_swar32(uint32_t x) {
    x = x - ((x >> 1) & 0x55555555u);                  /* 2비트 칸마다 0..2  */
    x = (x & 0x33333333u) + ((x >> 2) & 0x33333333u);  /* 4비트 칸마다 0..4  */
    x = (x + (x >> 4)) & 0x0F0F0F0Fu;                  /* 8비트 칸마다 0..8  */
    return (x * 0x01010101u) >> 24;                    /* 네 바이트의 합 → 최상위 바이트 */
}

static unsigned pc_swar64(uint64_t x) {
    x = x - ((x >> 1) & 0x5555555555555555ull);
    x = (x & 0x3333333333333333ull) + ((x >> 2) & 0x3333333333333333ull);
    x = (x + (x >> 4)) & 0x0F0F0F0F0F0F0F0Full;
    return (unsigned)((x * 0x0101010101010101ull) >> 56);  /* 여덟 바이트의 합 ≤ 64 */
}

/* ======================= Part B: 최하위 비트 · 2의 거듭제곱 ======================= */
/* 가장 낮은 1비트만 남긴다. 0 → 0. (unsigned 라서 0u - x 는 2^32 - x, UB 없음) */
static uint32_t lowbit(uint32_t x) {
    return x & (0u - x);
}

/* 가장 낮은 1비트를 지운다. 0 → 0. */
static uint32_t clear_lowbit(uint32_t x) {
    return x & (x - 1u);
}

/* 1인 비트가 정확히 하나인가. 0 은 거짓. */
static bool is_pow2(uint32_t x) {
    return x != 0 && (x & (x - 1u)) == 0;
}

/* 뒤쪽(최하위) 0 의 개수. ctz32(0) = 32 로 정의 (builtin 과 달리). */
static unsigned ctz32(uint32_t x) {
    return pc_swar32(lowbit(x) - 1u);       /* 0b1000 - 1 = 0b0111 → 3.  x=0: 0 - 1 = 0xFFFFFFFF → 32 */
}

/* 앞쪽(최상위) 0 의 개수. clz32(0) = 32 로 정의. */
static unsigned clz32(uint32_t x) {
    x |= x >> 1; x |= x >> 2; x |= x >> 4; x |= x >> 8; x |= x >> 16;   /* 최상위 1 아래를 모두 1로 번지게 */
    return 32u - pc_swar32(x);
}

/* x 이상인 가장 작은 2의 거듭제곱. 정의: next_pow2(0) = 1, x > 2^31 → 0 (32비트에 답이 없음 = 오류 표시) */
static uint32_t next_pow2(uint32_t x) {
    if (x == 0) return 1u;
    if (x > 0x80000000u) return 0u;
    x--;                                    /* 이미 2의 거듭제곱이면 그대로 나오게 */
    x |= x >> 1; x |= x >> 2; x |= x >> 4; x |= x >> 8; x |= x >> 16;
    return x + 1u;
}

/* ======================= Part C: 정렬 올림 · 펌웨어 사용처 ======================= */
/* a 는 2의 거듭제곱(1..2^31). x 이상인 a 의 배수를 *out 에. 결과가 2^32 이상(=x + a-1 이 넘침)이면 false. */
static bool align_up32(uint32_t x, uint32_t a, uint32_t *out) {
    uint32_t m = a - 1u;
    if (x > UINT32_MAX - m) return false;
    *out = (x + m) & ~m;
    return true;
}

/* 64비트 주소, 32비트 정렬값. 마스크를 64비트로 넓힌 뒤 ~ 를 해야 상위 32비트가 살아남는다.
 * 전제: addr + a - 1 이 2^64 를 넘지 않음 (주소 공간 끝 a 바이트 안의 주소는 받지 않는다). */
static uint64_t align_up64(uint64_t addr, uint32_t a) {
    uint64_t m = (uint64_t)a - 1u;
    return (addr + m) & ~m;
}

/* DMA 버퍼 [addr, addr+len) 이 캐시 라인 경계에 딱 맞나 (시작·길이 모두 line 의 배수). line 은 2의 거듭제곱. */
static bool dma_buf_ok(uint32_t addr, uint32_t len, uint32_t line) {
    return ((addr | len) & (line - 1u)) == 0;
}

/* [addr, addr+len) 을 덮는 캐시 라인들: *start = 첫 라인 주소, 반환 = 라인 개수. len == 0 → 0개.
 * 전제: addr + len ≤ 2^32 (버퍼가 주소 공간 안에 있음). 끝 주소 올림은 2^32 가 될 수 있으니 64비트로 계산. */
static uint32_t cache_span(uint32_t addr, uint32_t len, uint32_t line, uint32_t *start) {
    uint32_t m = line - 1u;
    *start = addr & ~m;
    if (len == 0) return 0;
    uint64_t end = ((uint64_t)addr + len + m) & ~(uint64_t)m;
    return (uint32_t)((end - *start) >> ctz32(line));          /* line 이 2^k 면 ÷line = >>k */
}

/* 비트맵 할당기: 비트 1 = 사용 중. 가장 낮은 번호의 빈 슬롯을 1로 바꾸고 번호를 돌려준다. 없으면 -1.
 * nslots 가 32의 배수가 아니면 마지막 워드의 남는 비트는 슬롯이 아니다. */
static int bm_alloc(uint32_t *map, unsigned nwords, unsigned nslots) {
    for (unsigned w = 0; w < nwords; w++) {
        uint32_t freeb = ~map[w];                     /* 빈 슬롯 = 1 */
        if (freeb == 0) continue;                     /* 이 워드는 꽉 참 */
        unsigned slot = w * 32u + ctz32(freeb);
        if (slot >= nslots) return -1;                /* 남은 빈 비트는 패딩뿐 */
        map[w] |= lowbit(freeb);
        return (int)slot;
    }
    return -1;
}

/* 이벤트 그룹: 대기 조건. all = 마스크의 비트가 전부 켜져야, 아니면 하나라도. mask == 0 이면 false (잘못된 호출). */
static bool ev_wait_ok(uint32_t bits, uint32_t mask, bool all) {
    if (mask == 0) return false;
    return all ? (bits & mask) == mask : (bits & mask) != 0;
}

/* pending 의 1 비트를 낮은 번호부터 처리: order[] 에 비트 번호를 적고 개수를 돌려준다.
 * 반복 횟수 = popcount(pending) — 32칸을 다 훑지 않는다. */
static unsigned ev_drain(uint32_t pending, uint8_t order[32]) {
    unsigned n = 0;
    while (pending) {
        order[n++] = (uint8_t)ctz32(pending);
        pending = clear_lowbit(pending);
    }
    return n;
}

/* ---------------- 주어진 도우미 (TODO 아님) ---------------- */
static void bm_free(uint32_t *map, unsigned slot) { map[slot / 32u] &= ~(1u << (slot % 32u)); }
static unsigned bm_used(const uint32_t *map, unsigned nwords) {
    unsigned s = 0;
    for (unsigned w = 0; w < nwords; w++) s += pc_swar32(map[w]);
    return s;
}

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t rng = 0x2545F491u;
static uint32_t xorshift32(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static uint64_t xorshift64(void) { uint64_t hi = xorshift32(); return (hi << 32) | xorshift32(); }

/* 기준(reference): builtin 은 0 에서 정의되지 않으므로 0 을 먼저 거른다 */
static uint32_t ref_lowbit(uint32_t x) { return x ? 1u << __builtin_ctz(x) : 0u; }
static unsigned ref_ctz(uint32_t x)    { return x ? (unsigned)__builtin_ctz(x) : 32u; }
static unsigned ref_clz(uint32_t x)    { return x ? (unsigned)__builtin_clz(x) : 32u; }
static uint32_t ref_next_pow2(uint32_t x) {           /* 64비트로 하나씩 키우는 무식한 방법 */
    uint64_t p = 1;
    while (p < x) p <<= 1;
    return p > 0x80000000u ? 0u : (uint32_t)p;
}
static bool ref_align_up32(uint32_t x, uint32_t a, uint32_t *out) {   /* 나눗셈으로, 64비트에서 */
    uint64_t r = ((uint64_t)x + a - 1u) / a * a;
    if (r > UINT32_MAX) return false;
    *out = (uint32_t)r;
    return true;
}

/* 한 값 x 에 대해 Part A·B 함수 전부를 기준과 대조. 함수별 틀린 횟수를 bad[] 에 누적. */
static void bad_ab(uint32_t x, unsigned bad[8]) {
    unsigned ref = (unsigned)__builtin_popcount(x);
    if (pc_kernighan(x) != ref) bad[0]++;
    if (pc_swar32(x) != ref) bad[1]++;
    if (lowbit(x) != ref_lowbit(x)) bad[2]++;
    if (clear_lowbit(x) != (x ^ ref_lowbit(x))) bad[3]++;
    if (is_pow2(x) != (ref == 1)) bad[4]++;
    if (ctz32(x) != ref_ctz(x)) bad[5]++;
    if (clz32(x) != ref_clz(x)) bad[6]++;
    if (next_pow2(x) != ref_next_pow2(x)) bad[7]++;
}

int main(void) {
    /* ---------- Part A: 손 계산 값 ---------- */
    CHECK(pc_kernighan(0x12345678u) == 13 && pc_swar32(0x12345678u) == 13);
    CHECK(pc_kernighan(0xFFFFFFFFu) == 32 && pc_swar32(0xFFFFFFFFu) == 32);
    CHECK(pc_kernighan(0x80000000u) == 1 && pc_swar32(0x80000000u) == 1);
    CHECK(pc_kernighan(0xAAAAAAAAu) == 16 && pc_swar32(0x0F0F0F0Fu) == 16);
    CHECK(pc_kernighan(0) == 0 && pc_swar32(0) == 0);
    CHECK(pc_swar64(0xFFFFFFFFFFFFFFFFull) == 64 && pc_swar64(1ull << 63) == 1);
    CHECK(pc_swar64(0x00000001FFFFFFFFull) == 33);       /* 32비트 경계를 넘는 값 */

    /* ---------- Part A·B: 16비트 전 범위 (아래 반쪽 x, 위 반쪽 x<<16, 섞어서 x*0x10001) ---------- */
    unsigned bad[8] = {0};
    for (uint32_t x = 0; x <= 0xFFFFu; x++) {
        bad_ab(x, bad);
        bad_ab(x << 16, bad);
        bad_ab(x * 0x10001u, bad);
    }
    /* 구조적 값: 한 비트, 두 비트, 2^k-1, 2^k+1, 그 보수 */
    for (unsigned i = 0; i < 32; i++) {
        uint32_t b = 1u << i, lo = b - 1u;
        bad_ab(b, bad); bad_ab(~b, bad); bad_ab(lo, bad); bad_ab(~lo, bad); bad_ab(b + 1u, bad);
        for (unsigned j = i + 1; j < 32; j++) bad_ab(b | (1u << j), bad);
    }
    /* 랜덤 2^20 개 */
    for (int k = 0; k < (1 << 20); k++) bad_ab(xorshift32(), bad);
    printf("A/B sweep (16-bit x3 + structured + 2^20 random): bad kern=%u swar=%u lowbit=%u clear=%u "
           "pow2=%u ctz=%u clz=%u next=%u\n", bad[0], bad[1], bad[2], bad[3], bad[4], bad[5], bad[6], bad[7]);
    CHECK(bad[0] == 0);
    CHECK(bad[1] == 0);
    CHECK(bad[2] == 0);
    CHECK(bad[3] == 0);
    CHECK(bad[4] == 0);
    CHECK(bad[5] == 0);
    CHECK(bad[6] == 0);
    CHECK(bad[7] == 0);

    unsigned bad64 = 0;
    for (int k = 0; k < (1 << 20); k++) {
        uint64_t v = xorshift64();
        if (pc_swar64(v) != (unsigned)__builtin_popcountll(v)) bad64++;
        if (pc_swar64(v & (v >> 7)) != (unsigned)__builtin_popcountll(v & (v >> 7))) bad64++;   /* 성긴 값 */
    }
    printf("pc_swar64: 2^21 random 64-bit values, bad=%u\n", bad64);
    CHECK(bad64 == 0);

    /* ---------- Part B: 손 계산 값과 경계 ---------- */
    CHECK(lowbit(0x58u) == 0x08u && clear_lowbit(0x58u) == 0x50u);
    CHECK(lowbit(0) == 0 && clear_lowbit(0) == 0);
    CHECK(lowbit(0x80000000u) == 0x80000000u && clear_lowbit(0x80000000u) == 0);
    CHECK(!is_pow2(0) && is_pow2(1) && is_pow2(0x80000000u) && !is_pow2(0xFFFFFFFFu) && !is_pow2(6));
    CHECK(ctz32(0) == 32 && ctz32(1) == 0 && ctz32(0x80000000u) == 31 && ctz32(0x58u) == 3);
    CHECK(clz32(0) == 32 && clz32(1) == 31 && clz32(0x80000000u) == 0 && clz32(0x00010000u) == 15);
    CHECK(next_pow2(0) == 1 && next_pow2(1) == 1 && next_pow2(2) == 2 && next_pow2(3) == 4);
    CHECK(next_pow2(1000) == 1024 && next_pow2(1024) == 1024 && next_pow2(1025) == 2048);
    CHECK(next_pow2(0x80000000u) == 0x80000000u && next_pow2(0x80000001u) == 0 && next_pow2(0xFFFFFFFFu) == 0);

    /* ---------- Part C: align_up32 ---------- */
    uint32_t r = 0;
    CHECK(align_up32(0x1001u, 0x1000u, &r) && r == 0x2000u);
    CHECK(align_up32(0x1000u, 0x1000u, &r) && r == 0x1000u);
    CHECK(align_up32(0, 64, &r) && r == 0);
    CHECK(align_up32(0xFFFFFFC0u, 64, &r) && r == 0xFFFFFFC0u);       /* 마지막 정렬 블록: 넘치지 않음 */
    r = 12345;
    CHECK(!align_up32(0xFFFFFFC1u, 64, &r) && r == 12345);           /* 2^32 로 올라감 → false, *out 안 건드림 */
    CHECK(align_up32(0xFFFFFFFFu, 1, &r) && r == 0xFFFFFFFFu);
    CHECK(align_up32(1, 0x80000000u, &r) && r == 0x80000000u);
    CHECK(align_up32(0x80000000u, 0x80000000u, &r) && r == 0x80000000u && !align_up32(0x80000001u, 0x80000000u, &r));
    unsigned bad_al = 0, n_al = 0;
    for (unsigned k = 0; k < 32; k++) {
        uint32_t a = 1u << k;
        uint32_t probes[] = {0, 1, a - 1u, a, a + 1u, 0x7FFFFFFFu, 0x80000000u, 0x80000001u,
                             UINT32_MAX - a, UINT32_MAX - a + 1u, UINT32_MAX - a + 2u, UINT32_MAX - 1u, UINT32_MAX};
        for (unsigned p = 0; p < sizeof probes / sizeof probes[0] + 2000u; p++) {
            uint32_t x = p < sizeof probes / sizeof probes[0] ? probes[p] : xorshift32();
            uint32_t got = 0xDEADBEEFu, want = 0xDEADBEEFu;
            bool ok1 = align_up32(x, a, &got), ok2 = ref_align_up32(x, a, &want);
            n_al++;
            if (ok1 != ok2 || (ok1 && got != want)) bad_al++;
        }
    }
    printf("align_up32: %u cases (32 alignments x boundary probes + random), bad=%u\n", n_al, bad_al);
    CHECK(bad_al == 0);

    /* ---------- Part C: 64비트 주소 ---------- */
    CHECK(align_up64(0x123456789ull, 64) == 0x1234567C0ull);           /* 상위 32비트가 살아 있어야 */
    CHECK(align_up64(0x100000000ull, 4096) == 0x100000000ull);
    CHECK(align_up64(0xFFFFFFFFull, 4096) == 0x100000000ull);          /* 32비트 경계를 넘어 올림 */
    CHECK(align_up64(0xFFFF800012345001ull, 0x80000000u) == 0xFFFF800080000000ull);
    unsigned bad64a = 0;
    for (int k = 0; k < 100000; k++) {
        uint64_t addr = xorshift64() >> 1;                           /* < 2^63 → 넘침 없음 */
        uint32_t a = 1u << (xorshift32() % 32u);
        if (align_up64(addr, a) != (addr + a - 1u) / a * a) bad64a++;
    }
    CHECK(bad64a == 0);

    /* ---------- Part C: DMA 버퍼와 캐시 라인 ---------- */
    uint32_t st = 0;
    CHECK(cache_span(0x20000010u, 100, 32, &st) == 4 && st == 0x20000000u);   /* 0x...10 ~ 0x...73 */
    CHECK(cache_span(0x20000000u, 64, 32, &st) == 2 && st == 0x20000000u);
    CHECK(cache_span(0x2000001Fu, 2, 32, &st) == 2 && st == 0x20000000u);     /* 2바이트가 두 라인에 걸침 */
    CHECK(cache_span(0x20000040u, 0, 32, &st) == 0);
    CHECK(cache_span(0xFFFFFFC0u, 64, 64, &st) == 1 && st == 0xFFFFFFC0u);    /* 끝 = 2^32, 넘치면 안 됨 */
    CHECK(cache_span(0xFFFFFFF0u, 16, 64, &st) == 1 && st == 0xFFFFFFC0u);
    CHECK(dma_buf_ok(0x20000000u, 512, 32) && !dma_buf_ok(0x20000010u, 512, 32) && !dma_buf_ok(0x20000000u, 100, 32));

    /* ---------- Part C: 비트맵 할당기 (70 슬롯 = 워드 3개, 마지막 워드 6비트만 유효) ---------- */
    uint32_t map[3] = {0, 0, 0};
    bool seq = true;
    for (int s = 0; s < 70; s++) if (bm_alloc(map, 3, 70) != s) seq = false;
    CHECK(seq);                                                   /* 0, 1, ..., 69 순서대로 */
    CHECK(bm_alloc(map, 3, 70) == -1 && bm_used(map, 3) == 70);   /* 꽉 참: 패딩 비트 64..95 중 70.. 은 슬롯 아님 */
    bm_free(map, 33); bm_free(map, 5);
    CHECK(bm_alloc(map, 3, 70) == 5 && bm_alloc(map, 3, 70) == 33 && bm_alloc(map, 3, 70) == -1);
    /* 랜덤 alloc/free 를 bool 배열 브루트포스와 대조 */
    uint32_t m2[4] = {0, 0, 0, 0};
    bool used[100] = {false};
    unsigned bad_bm = 0;
    for (int k = 0; k < 20000; k++) {
        if (xorshift32() % 3u) {
            int want = -1;
            for (int s = 0; s < 100; s++) if (!used[s]) { want = s; break; }
            int got = bm_alloc(m2, 4, 100);
            if (got != want) bad_bm++;
            if (want >= 0) used[want] = true;
            if (got >= 0 && got != want) bm_free(m2, (unsigned)got);       /* 어긋나도 계속 대조할 수 있게 */
            if (want >= 0 && got != want) m2[want / 32] |= 1u << (want % 32);
        } else {
            unsigned s = xorshift32() % 100u;
            if (used[s]) { used[s] = false; bm_free(m2, s); }
        }
        unsigned cnt = 0;
        for (int s = 0; s < 100; s++) cnt += used[s];
        if (bm_used(m2, 4) != cnt) bad_bm++;
    }
    printf("bitmap: 20000 random alloc/free ops vs bool[100], bad=%u\n", bad_bm);
    CHECK(bad_bm == 0);

    /* ---------- Part C: 이벤트 그룹 ---------- */
    uint8_t ord[32];
    memset(ord, 0xFF, sizeof ord);
    CHECK(ev_drain(0x80000005u, ord) == 3 && ord[0] == 0 && ord[1] == 2 && ord[2] == 31);
    CHECK(ev_drain(0, ord) == 0);
    bool asc = ev_drain(0xFFFFFFFFu, ord) == 32;
    for (unsigned i = 0; i < 32; i++) if (ord[i] != i) asc = false;
    CHECK(asc);
    unsigned bad_ev = 0;
    for (int k = 0; k < 20000; k++) {
        uint32_t p = xorshift32() & xorshift32();                    /* 평균 8비트 */
        unsigned n = ev_drain(p, ord), j = 0;
        if (n != (unsigned)__builtin_popcount(p)) { bad_ev++; continue; }
        for (unsigned b = 0; b < 32; b++) if (p >> b & 1u) { if (ord[j++] != b) bad_ev++; }
    }
    CHECK(bad_ev == 0);
    CHECK(ev_wait_ok(0x05u, 0x04u, false) && !ev_wait_ok(0x05u, 0x0Au, false));
    CHECK(ev_wait_ok(0x07u, 0x05u, true) && !ev_wait_ok(0x05u, 0x06u, true));
    CHECK(!ev_wait_ok(0xFFFFFFFFu, 0, true) && !ev_wait_ok(0xFFFFFFFFu, 0, false));

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
