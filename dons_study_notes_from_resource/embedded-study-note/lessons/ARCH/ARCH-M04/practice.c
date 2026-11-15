/* ARCH-M04 추가 연습 — 한 번에 검증 (본문 solution.c 의 판정 로직을 축소해 넣었다)
 *
 *   연습 1  c[i] = a[i] + b[i], 세 배열이 C 간격: 패딩을 어디에 몇 라인 넣어야 하나
 *   연습 2  블랙박스 캐시의 라인 크기와 용량을 hit/miss 만 보고 알아내기 (실리콘 검증식 directed test)
 *   연습 3  tag 비트 수와 tag RAM 오버헤드 (valid + dirty 포함)
 *   연습 4  열 하나가 쓰는 set 수의 닫힌 식: min(R, S / gcd(p, S))  (p = pitch 의 라인 수)
 *   연습 5  64×64 전치 (src 행 순회 + dst 열 순회): pitch 패딩 전/후
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr && ./pr
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; printf("PASS  %s\n", #expr); } \
                         else printf("FAIL  %s  (line %d)\n", #expr, __LINE__); } while (0)

/* ---- 축소 직접 사상 캐시 (본문 dm_access 와 같은 판정) ---- */
#define MAX_SETS 4096u
typedef struct { uint32_t B, S, ob; uint8_t v[MAX_SETS]; uint32_t tag[MAX_SETS]; uint64_t acc, miss; } dm_t;
static void dm_init(dm_t *c, uint32_t C, uint32_t B) {
    memset(c, 0, sizeof *c);
    c->B = B; c->S = C / B;
    while ((1u << c->ob) < B) c->ob++;
}
static int dm_access(dm_t *c, uint32_t a) {          /* 1 = hit, 0 = miss */
    uint32_t line = a >> c->ob, idx = line & (c->S - 1), tag = line / c->S;
    c->acc++;
    if (c->v[idx] && c->tag[idx] == tag) return 1;
    c->miss++; c->v[idx] = 1; c->tag[idx] = tag;
    return 0;
}
static dm_t g_c;

/* ---------- 연습 1 ---------- */
static uint64_t three_arrays(uint32_t pad_b, uint32_t pad_c) {
    const uint32_t C = 4096, base = 0x20000000u;
    dm_init(&g_c, C, 32);
    for (uint32_t i = 0; i < 256; i++) {
        dm_access(&g_c, base + 4 * i);                     /* a[i] 읽기 */
        dm_access(&g_c, base + C + pad_b + 4 * i);         /* b[i] 읽기 */
        dm_access(&g_c, base + 2 * C + pad_c + 4 * i);     /* c[i] 쓰기 (write-allocate 가정) */
    }
    return g_c.miss;
}

/* ---------- 연습 2: 블랙박스 ---------- */
/* 숨은 캐시: 테스트가 형상을 정하고, 탐색 함수는 bb_flush / bb_access 만 쓴다 */
static dm_t g_box;
static uint32_t g_box_C, g_box_B;
static void bb_flush(void) { dm_init(&g_box, g_box_C, g_box_B); }
static int bb_access(uint32_t a) { return dm_access(&g_box, a); }

/* 라인 크기: 비운 뒤 0 을 읽고, 1, 2, 4, ... 바이트 떨어진 곳을 읽어 처음으로 미스가 나는 거리 = B */
static uint32_t probe_line_size(void) {
    for (uint32_t d = 1; d != 0; d <<= 1) {
        bb_flush();
        bb_access(0);
        if (!bb_access(d)) return d;
    }
    return 0;
}
/* 용량 (직접 사상 가정): 0 → X → 0 에서 마지막 0 이 미스하면 X 가 0 과 같은 set (X ≡ 0 mod C).
 * X = B, 2B, 4B, ... 중 처음으로 0 을 쫓아낸 X = C. */
static uint32_t probe_capacity(uint32_t B) {
    for (uint32_t x = B; x != 0; x <<= 1) {
        bb_flush();
        bb_access(0); bb_access(x);
        if (!bb_access(0)) return x;
    }
    return 0;
}

/* ---------- 연습 3 ---------- */
static unsigned log2u(uint32_t x) { unsigned n = 0; while (x >>= 1) n++; return n; }
static unsigned tag_bits(unsigned addr_bits, uint32_t C, uint32_t B) {
    return addr_bits - log2u(B) - log2u(C / B);          /* offset + index = log2 C (W = 1 일 때) */
}
static uint32_t tag_ram_bits(unsigned addr_bits, uint32_t C, uint32_t B, unsigned extra) {
    return (C / B) * (tag_bits(addr_bits, C, B) + extra);
}

/* ---------- 연습 4 ---------- */
static uint32_t gcd_u(uint32_t a, uint32_t b) { while (b) { uint32_t t = a % b; a = b; b = t; } return a; }
static uint32_t column_sets_formula(uint32_t rows, uint32_t pitch_lines, uint32_t S) {
    uint32_t period = S / gcd_u(pitch_lines % S ? pitch_lines % S : S, S);
    return rows < period ? rows : period;
}
static uint32_t column_sets_sim(uint32_t rows, uint32_t pitch, uint32_t B, uint32_t S) {
    static uint8_t m[MAX_SETS]; memset(m, 0, sizeof m);
    uint32_t k = 0;
    for (uint32_t i = 0; i < rows; i++) {
        uint32_t idx = ((i * pitch) / B) % S;
        if (!m[idx]) { m[idx] = 1; k++; }
    }
    return k;
}

/* ---------- 연습 5 ---------- */
static uint64_t transpose_misses(uint32_t pitch, uint32_t dst_off) {
    const uint32_t src = 0x20000000u, dst = 0x20000000u + dst_off;
    dm_init(&g_c, 4096, 32);
    for (uint32_t i = 0; i < 64; i++)
        for (uint32_t j = 0; j < 64; j++) {
            dm_access(&g_c, src + i * pitch + 4 * j);      /* src[i][j] 읽기 */
            dm_access(&g_c, dst + j * pitch + 4 * i);      /* dst[j][i] 쓰기 */
        }
    return g_c.miss;
}

int main(void) {
    printf("-- 1: three arrays C apart --\n");
    uint64_t m0 = three_arrays(0, 0), m1 = three_arrays(32, 0), m2 = three_arrays(32, 64), m3 = three_arrays(32, 32);
    printf("      misses: no pad %llu, pad b only %llu, pad b=32 c=64 %llu, pad b=32 c=32 %llu (of 768)\n",
           (unsigned long long)m0, (unsigned long long)m1, (unsigned long long)m2, (unsigned long long)m3);
    CHECK(m0 == 768);                    /* 세 개가 한 set 을 돌려 쓴다: 전부 미스 */
    CHECK(m1 == 544);                    /* b 는 살아났지만 a 와 c 가 여전히 2C 간격 */
    CHECK(m2 == 96);                     /* 3 배열 × 32 라인 = compulsory 만 */
    CHECK(m3 == 544);                    /* b 와 c 가 같은 패딩 → 이번엔 b 와 c 가 충돌 */

    printf("-- 2: black-box geometry --\n");
    static const uint32_t geo[][2] = {{1024, 16}, {4096, 32}, {16384, 64}, {32768, 128}, {256, 4}};
    int ok = 1;
    for (unsigned g = 0; g < sizeof geo / sizeof geo[0]; g++) {
        g_box_C = geo[g][0]; g_box_B = geo[g][1];
        uint32_t B = probe_line_size(), C = probe_capacity(B);
        printf("      hidden C=%u B=%u -> probed C=%u B=%u\n", g_box_C, g_box_B, C, B);
        if (B != g_box_B || C != g_box_C) ok = 0;
    }
    CHECK(ok);

    printf("-- 3: tag RAM overhead --\n");
    CHECK(tag_bits(32, 16384, 32) == 18);
    CHECK(tag_ram_bits(32, 16384, 32, 2) == 10240);          /* 512 라인 × (18 + valid + dirty) */
    CHECK(tag_ram_bits(32, 16384, 64, 2) == 5120);           /* 라인을 키우면 오버헤드 절반 */
    CHECK(tag_bits(32, 1024, 16) == 22);                     /* 1 KB 플래시 캐시 */

    printf("-- 4: sets used by one column --\n");
    ok = 1;
    for (uint32_t pl = 1; pl <= 40; pl++)
        for (uint32_t rows = 1; rows <= 160; rows += 13)
            if (column_sets_formula(rows, pl, 128) != column_sets_sim(rows, pl * 32, 32, 128)) ok = 0;
    CHECK(ok);
    CHECK(column_sets_formula(64, 8, 128) == 16);            /* pitch 256 B */
    CHECK(column_sets_formula(64, 9, 128) == 64);            /* pitch 288 B */
    CHECK(column_sets_formula(64, 12, 128) == 32);           /* pitch 384 B: 4 라인 패딩은 오히려 나쁨 */
    CHECK(column_sets_formula(64, 10, 128) == 64);           /* pitch 320 B: gcd 2 → 64 set, 딱 맞음 */

    printf("-- 5: 64x64 transpose --\n");
    uint64_t t0 = transpose_misses(256, 0x4000), t1 = transpose_misses(288, 0x4800), t2 = transpose_misses(288, 0x4820);
    printf("      misses (of 8192): pitch 256 %llu, pitch 288 %llu, pitch 288 + dst shifted 1 line %llu\n",
           (unsigned long long)t0, (unsigned long long)t1, (unsigned long long)t2);
    CHECK(t0 == 4664);                   /* dst 열 쓰기가 거의 전부 미스 + src 도 얻어맞는다 */
    CHECK(t1 == 1296 && t2 == 1268);     /* 하한 = compulsory 1024 (src 512 + dst 512 라인) */

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
