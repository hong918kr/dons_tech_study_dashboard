/* MEM-B01 DRAM 조직: channel/rank/bank/row/column — 설정 가능한 주소 매핑 디코더
 *
 *   Part A: map_validate / map_decode / map_encode   비트 필드 표 + XOR 해싱, 왕복(전단사) 테스트
 *   Part B: flat_bank / bank_histogram               연속 64 B 라인 · stride 2^k 의 뱅크 분포
 *   Part C: row_neighbor / row_lines                 크래시 덤프 PA → 이웃 행, 한 행이 덮는 PA 목록
 *
 * 이 레슨의 정의 (JESD79-4 / 데이터시트 대표값 — 실제 부품은 데이터시트 확인):
 *   DDR4 x8 8 Gb 칩: 4 BG x 4 bank, 64 K row, 1 K column.  64-bit 채널(x8 칩 8개 = 1 rank)
 *   → column 주소 1개 = 8 B (버스 1 beat), BL8 버스트 = 8 column = 64 B 캐시 라인.
 *   PA[2:0] = 8 B beat 안의 바이트 (DRAM 주소가 아님).
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o sol && ./sol
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum { F_CH, F_RANK, F_BG, F_BANK, F_ROW, F_COL, F_N };
static const char *const FNAME[F_N] = { "ch", "rank", "bg", "bank", "row", "col" };

enum { MAP_OK = 0, MAP_ERR_RANGE = -1, MAP_ERR_FIELD = -2, MAP_ERR_LAYOUT = -3,
       MAP_ERR_XOR = -4, MAP_ERR_CAP = -5 };

#define MAP_MAX_SEG        12
#define MAP_MAX_XOR        8
#define MAP_MAX_PA_BITS    48
#define MAP_MAX_FIELD_BITS 24
#define MAP_MAX_BANK_BITS  12          /* ch+rank+bg+bank 비트 합 상한 → flat bank ≤ 4096 */

/* field[f_lo+width-1 : f_lo] = PA[pa_lo+width-1 : pa_lo] */
typedef struct { uint8_t field, f_lo, pa_lo, width; } map_seg_t;
/* target 필드의 tbit 번 비트 ^= parity(src 필드 값 & src_mask).  target ∈ {CH,RANK,BG,BANK}, src ∈ {ROW,COL} */
typedef struct { uint8_t target, tbit, src; uint32_t src_mask; } map_xor_t;
typedef struct {
    const char *name;
    uint8_t   off_bits;                /* PA 하위 off_bits = 버스 beat 안의 바이트 (64-bit 버스 → 3) */
    uint8_t   nseg;
    map_seg_t seg[MAP_MAX_SEG];
    uint8_t   nxor;
    map_xor_t xr[MAP_MAX_XOR];
} addr_map_t;
typedef struct { uint8_t width[F_N]; uint8_t pa_bits; } map_info_t;   /* map_validate 가 채운다 */
typedef struct { uint32_t f[F_N]; uint32_t byte; } dram_loc_t;

#define SEG(fld, flo, palo, w)  { (fld), (flo), (palo), (w) }
#define XR(tgt, tb, src, mask)  { (tgt), (tb), (src), (mask) }

static uint32_t parity32(uint32_t x) { x ^= x >> 16; x ^= x >> 8; x ^= x >> 4; x ^= x >> 2; x ^= x >> 1; return x & 1u; }
static uint64_t mask64(unsigned w) { return w >= 64 ? ~0ull : ((1ull << w) - 1ull); }
static unsigned popcount64(uint64_t x) { unsigned c = 0; while (x) { x &= x - 1; c++; } return c; }

/* ---------------- Part A: 검증 · 디코드 · 인코드 ---------------- */

/* 매핑 표가 PA[pa_bits-1:0] 을 빈틈·겹침 없이 덮는지, 각 필드 비트가 0부터 연속인지,
 * XOR 규칙이 (해싱되지 않는 ROW/COL 만 입력으로 쓰는) 가역 형태인지 검사하고 info 를 채운다. */
static int map_validate(const addr_map_t *m, map_info_t *mi) {
    memset(mi, 0, sizeof *mi);
    (void)m; (void)popcount64;
    /* TODO: ① 각 seg 의 PA 비트·필드 비트가 겹치지 않는지 (겹치면 MAP_ERR_LAYOUT)
     *       ② PA 비트가 [0, top) 을 빈틈 없이 덮는지, 각 필드 비트가 0부터 연속인지
     *       ③ width[] · pa_bits 채우기, ROW/COL ≥ 1비트, bank 비트 합 ≤ MAP_MAX_BANK_BITS
     *       ④ XOR 규칙: target ∈ {CH..BANK}, src ∈ {ROW, COL}, tbit < width, mask ≠ 0 이고 src 폭 안 (아니면 MAP_ERR_XOR) */
    return MAP_OK;
}

static int map_decode(const addr_map_t *m, const map_info_t *mi, uint64_t pa, dram_loc_t *out) {
    memset(out, 0, sizeof *out);
    (void)m; (void)mi; (void)pa; (void)parity32;
    /* TODO: 범위 검사(MAP_ERR_RANGE) → byte = 하위 off_bits → ① seg 별로 PA 비트를 필드에 붙이기
     *       → ② XOR 규칙 적용 (target ^= parity(src & mask) << tbit) */
    return MAP_OK;
}

static int map_encode(const addr_map_t *m, const map_info_t *mi, const dram_loc_t *loc, uint64_t *pa) {
    *pa = 0;
    (void)m; (void)mi; (void)loc;
    /* TODO: 필드 넘침 검사(MAP_ERR_FIELD) → XOR 를 한 번 더 걸어 "raw" 필드 값 복원 → seg 별로 PA 자리에 놓기 */
    return MAP_OK;
}

/* ---------------- Part B: 뱅크 분포 ---------------- */

/* (ch, rank, bg, bank) 를 하나의 번호로: 비트를 이어 붙인다 (모두 2의 거듭제곱 개수) */
static uint32_t flat_bank(const map_info_t *mi, const dram_loc_t *l) {
    (void)mi; (void)l;
    return 0; /* TODO: ch | rank | bg | bank 비트를 이어 붙인 번호 */
}
static uint32_t num_banks(const map_info_t *mi) {
    return 1u << (mi->width[F_CH] + mi->width[F_RANK] + mi->width[F_BG] + mi->width[F_BANK]);
}

typedef struct { uint32_t distinct, max_load, first, hits, conflicts; } bank_stats_t;

/* PA = base + i*stride (i = 0..n-1) 를 디코드해 flat bank 히스토그램과
 * "뱅크별 마지막 row 와 같은가" 근사 분류(open-page, 요청 순서대로 — 정확한 모델은 MEM-B03)를 센다. */
static int bank_histogram(const addr_map_t *m, const map_info_t *mi, uint64_t base, uint64_t stride,
                          uint32_t n, uint32_t *hist, size_t cap, bank_stats_t *st) {
    memset(st, 0, sizeof *st);
    if (cap) memset(hist, 0, cap * sizeof *hist);
    (void)m; (void)mi; (void)base; (void)stride; (void)n; (void)num_banks; (void)flat_bank;
    /* TODO: 뱅크 수 > cap → MAP_ERR_CAP. PA = base + i*stride 를 디코드(오버플로 → MAP_ERR_RANGE),
     *       flat bank 히스토그램 + distinct / max_load / first / hits / conflicts (뱅크별 마지막 row 기준) */
    return MAP_OK;
}

/* ---------------- Part C: 크래시 덤프 / EDAC 용 도구 ---------------- */

static int loc_format(const dram_loc_t *l, char *buf, size_t cap) {
    return snprintf(buf, cap, "ch%u rank%u bg%u bank%u row 0x%04X col 0x%03X byte %u",
                    (unsigned)l->f[F_CH], (unsigned)l->f[F_RANK], (unsigned)l->f[F_BG],
                    (unsigned)l->f[F_BANK], (unsigned)l->f[F_ROW], (unsigned)l->f[F_COL],
                    (unsigned)l->byte);
}

/* 같은 ch/rank/bg/bank/col 에서 row + delta 인 PA (논리 row 번호 기준) */
static int row_neighbor(const addr_map_t *m, const map_info_t *mi, uint64_t pa, int delta, uint64_t *out) {
    *out = 0;
    (void)m; (void)mi; (void)pa; (void)delta;
    return MAP_OK; /* TODO: decode → row += delta (범위 밖이면 MAP_ERR_RANGE) → encode */
}

/* pa 가 속한 DRAM row (ch,rank,bg,bank,row 고정) 의 모든 line_bytes 버스트 시작 PA 를 col 순서로.
 * 반환: 개수 (≥0) 또는 음수 오류. line_bytes 는 2^off_bits 의 2의 거듭제곱 배. */
static int row_lines(const addr_map_t *m, const map_info_t *mi, uint64_t pa, uint32_t line_bytes,
                     uint64_t *out, size_t cap) {
    (void)m; (void)mi; (void)pa; (void)line_bytes; (void)out; (void)cap;
    return 0; /* TODO: decode → col = i * (line_bytes >> off_bits) 로 바꿔 가며 encode. 개수 반환 */
}

/* 서로 다른 page_bytes 페이지 수 (n 작음: O(n^2)) */
static uint32_t count_pages(const uint64_t *pa, int n, uint64_t page_bytes) {
    uint32_t c = 0;
    for (int i = 0; i < n; i++) {
        int seen = 0;
        for (int j = 0; j < i && !seen; j++) seen = (pa[j] / page_bytes == pa[i] / page_bytes);
        if (!seen) c++;
    }
    return c;
}

/* ---------------- 매핑 표 (DDR4-3200, 2 ch x 2 rank, x8 8 Gb → 32 GiB, PA[34:0]) ---------------- */
/* A: "RoBaRaCoCh" — Row | Bank(BG,BA) | Rank | ColHi | Ch | ColLo | byte */
static const addr_map_t MAP_A = { "A RoBaRaCoCh", 3, 7, {
    SEG(F_COL, 0, 3, 3), SEG(F_CH, 0, 6, 1), SEG(F_COL, 3, 7, 7), SEG(F_RANK, 0, 14, 1),
    SEG(F_BG, 0, 15, 2), SEG(F_BANK, 0, 17, 2), SEG(F_ROW, 0, 19, 16) }, 0, { { 0, 0, 0, 0 } } };
/* AX: A + 뱅크 XOR (permutation-based interleaving): BG[i] ^= ROW[i], BANK[i] ^= ROW[2+i] */
static const addr_map_t MAP_AX = { "AX RoBaRaCoCh+XOR", 3, 7, {
    SEG(F_COL, 0, 3, 3), SEG(F_CH, 0, 6, 1), SEG(F_COL, 3, 7, 7), SEG(F_RANK, 0, 14, 1),
    SEG(F_BG, 0, 15, 2), SEG(F_BANK, 0, 17, 2), SEG(F_ROW, 0, 19, 16) }, 4, {
    XR(F_BG, 0, F_ROW, 0x1), XR(F_BG, 1, F_ROW, 0x2), XR(F_BANK, 0, F_ROW, 0x4), XR(F_BANK, 1, F_ROW, 0x8) } };
/* B: 캐시 라인 인터리브 "RoCoRaBaBgCh" — 연속 64 B 라인이 ch → BG → bank → rank 순으로 퍼진다 */
static const addr_map_t MAP_B = { "B line-interleave", 3, 7, {
    SEG(F_COL, 0, 3, 3), SEG(F_CH, 0, 6, 1), SEG(F_BG, 0, 7, 2), SEG(F_BANK, 0, 9, 2),
    SEG(F_RANK, 0, 11, 1), SEG(F_COL, 3, 12, 7), SEG(F_ROW, 0, 19, 16) }, 0, { { 0, 0, 0, 0 } } };
/* C: B + Intel/AMD 풍 XOR 예시 (특정 CPU 아님): CH = PA6^PA12^PA13^PA19^PA20^PA21^PA22 */
static const addr_map_t MAP_C = { "C line-interleave+XOR", 3, 7, {
    SEG(F_COL, 0, 3, 3), SEG(F_CH, 0, 6, 1), SEG(F_BG, 0, 7, 2), SEG(F_BANK, 0, 9, 2),
    SEG(F_RANK, 0, 11, 1), SEG(F_COL, 3, 12, 7), SEG(F_ROW, 0, 19, 16) }, 7, {
    XR(F_CH, 0, F_COL, 0x18), XR(F_CH, 0, F_ROW, 0x0F), XR(F_BG, 0, F_ROW, 0x10), XR(F_BG, 1, F_ROW, 0x20),
    XR(F_BANK, 0, F_ROW, 0x40), XR(F_BANK, 1, F_ROW, 0x80), XR(F_RANK, 0, F_ROW, 0x100) } };

/* 전수 테스트용 작은 매핑: 15비트 PA (2 ch x 2 rank x 2 bg x 2 bank x 16 row x 16 col x 8 B) */
static const addr_map_t SMALL = { "small", 3, 7, {
    SEG(F_COL, 0, 3, 2), SEG(F_CH, 0, 5, 1), SEG(F_COL, 2, 6, 2), SEG(F_RANK, 0, 8, 1),
    SEG(F_BG, 0, 9, 1), SEG(F_BANK, 0, 10, 1), SEG(F_ROW, 0, 11, 4) }, 0, { { 0, 0, 0, 0 } } };
static const addr_map_t SMALL_X = { "small+xor", 3, 7, {
    SEG(F_COL, 0, 3, 2), SEG(F_CH, 0, 5, 1), SEG(F_COL, 2, 6, 2), SEG(F_RANK, 0, 8, 1),
    SEG(F_BG, 0, 9, 1), SEG(F_BANK, 0, 10, 1), SEG(F_ROW, 0, 11, 4) }, 4, {
    XR(F_BG, 0, F_ROW, 0x5), XR(F_BANK, 0, F_ROW, 0x2), XR(F_CH, 0, F_COL, 0x5), XR(F_RANK, 0, F_ROW, 0x8) } };

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint64_t g_rng = 0x9E3779B97F4A7C15ull;
static uint64_t xorshift64(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 7; g_rng ^= g_rng << 17; return g_rng; }

static int loc_eq(const dram_loc_t *a, const dram_loc_t *b) { return memcmp(a, b, sizeof *a) == 0; }
static dram_loc_t mkloc(uint32_t ch, uint32_t rk, uint32_t bg, uint32_t ba, uint32_t row, uint32_t col, uint32_t byte) {
    dram_loc_t l = { { ch, rk, bg, ba, row, col }, byte };
    return l;
}

/* 작은 매핑 전수: PA→loc→PA, 모든 loc→PA→loc, 그리고 PA 가 정확히 한 번씩 나오는지(전단사) */
static unsigned exhaustive_mismatch(const addr_map_t *m) {
    map_info_t mi;
    if (map_validate(m, &mi) != MAP_OK || mi.pa_bits != 15) return 1;
    static uint8_t seen[1u << 15];
    memset(seen, 0, sizeof seen);
    unsigned bad = 0;
    for (uint64_t pa = 0; pa < (1u << 15); pa++) {
        dram_loc_t l; uint64_t back;
        if (map_decode(m, &mi, pa, &l) || map_encode(m, &mi, &l, &back) || back != pa) bad++;
    }
    for (uint32_t i = 0; i < (1u << 15); i++) {          /* i 의 비트를 필드 값으로 나눠 loc 열거 */
        dram_loc_t l = mkloc(i & 1, (i >> 1) & 1, (i >> 2) & 1, (i >> 3) & 1, (i >> 4) & 15, (i >> 8) & 15, i >> 12);
        dram_loc_t l2; uint64_t pa;
        if (map_encode(m, &mi, &l, &pa) || pa >= (1u << 15)) { bad++; continue; }
        if (seen[pa]++) bad++;
        if (map_decode(m, &mi, pa, &l2) || !loc_eq(&l, &l2)) bad++;
    }
    return bad;
}

static unsigned random_mismatch(const addr_map_t *m, unsigned n) {
    map_info_t mi;
    if (map_validate(m, &mi) != MAP_OK) return n;
    unsigned bad = 0;
    for (unsigned i = 0; i < n; i++) {
        uint64_t pa = xorshift64() & mask64(mi.pa_bits), back, pa2;
        dram_loc_t l, l2;
        if (map_decode(m, &mi, pa, &l) || map_encode(m, &mi, &l, &back) || back != pa) bad++;
        uint64_t r = xorshift64();
        dram_loc_t q = mkloc((uint32_t)r & 1, (uint32_t)(r >> 1) & 1, (uint32_t)(r >> 2) & 3, (uint32_t)(r >> 4) & 3,
                             (uint32_t)(r >> 6) & 0xFFFF, (uint32_t)(r >> 22) & 0x3FF, (uint32_t)(r >> 32) & 7);
        if (map_encode(m, &mi, &q, &pa2) || map_decode(m, &mi, pa2, &l2) || !loc_eq(&q, &l2)) bad++;
    }
    return bad;
}

static int validate_rc(const addr_map_t *m) { map_info_t mi; return map_validate(m, &mi); }

int main(void) {
    map_info_t ia, iax, ib, ic;
    char buf[96];
    dram_loc_t l;
    uint64_t pa;

    /* ===== Part A ===== */
    CHECK(map_validate(&MAP_A, &ia) == MAP_OK && ia.pa_bits == 35);
    CHECK(ia.width[F_CH] == 1 && ia.width[F_RANK] == 1 && ia.width[F_BG] == 2 &&
          ia.width[F_BANK] == 2 && ia.width[F_ROW] == 16 && ia.width[F_COL] == 10);
    printf("%s: %u PA bits,", MAP_A.name, (unsigned)ia.pa_bits);
    for (int f = 0; f < F_N; f++) printf(" %s=%u", FNAME[f], (unsigned)ia.width[f]);
    printf(" bits\n");
    CHECK(map_validate(&MAP_AX, &iax) == MAP_OK && iax.pa_bits == 35);
    CHECK(map_validate(&MAP_B, &ib) == MAP_OK && ib.pa_bits == 35);
    CHECK(map_validate(&MAP_C, &ic) == MAP_OK && ic.pa_bits == 35 && ic.width[F_COL] == 10);

    /* 예: PA 0x1_2345_6740 */
    CHECK(map_decode(&MAP_A, &ia, 0x123456740ull, &l) == MAP_OK);
    CHECK(loc_eq(&l, &(dram_loc_t){ { 1, 1, 2, 2, 0x2468, 0x270 }, 0 }));
    loc_format(&l, buf, sizeof buf); printf("A : 0x1_2345_6740 -> %s\n", buf);
    CHECK(map_decode(&MAP_AX, &iax, 0x123456740ull, &l) == MAP_OK);
    CHECK(loc_eq(&l, &(dram_loc_t){ { 1, 1, 2, 0, 0x2468, 0x270 }, 0 }));
    loc_format(&l, buf, sizeof buf); printf("AX: 0x1_2345_6740 -> %s\n", buf);
    CHECK(map_decode(&MAP_B, &ib, 0x123456740ull, &l) == MAP_OK);
    CHECK(loc_eq(&l, &(dram_loc_t){ { 1, 0, 2, 3, 0x2468, 0x2B0 }, 0 }));
    loc_format(&l, buf, sizeof buf); printf("B : 0x1_2345_6740 -> %s\n", buf);
    CHECK(map_decode(&MAP_C, &ic, 0x123456740ull, &l) == MAP_OK);
    CHECK(loc_eq(&l, &(dram_loc_t){ { 1, 0, 0, 2, 0x2468, 0x2B0 }, 0 }));
    loc_format(&l, buf, sizeof buf); printf("C : 0x1_2345_6740 -> %s\n", buf);
    CHECK(map_decode(&MAP_A, &ia, 0x7FFFFFFFFull, &l) == MAP_OK &&
          loc_eq(&l, &(dram_loc_t){ { 1, 1, 3, 3, 0xFFFF, 0x3FF }, 7 }));
    CHECK(map_encode(&MAP_AX, &iax, &(dram_loc_t){ { 1, 1, 2, 0, 0x2468, 0x270 }, 0 }, &pa) == MAP_OK &&
          pa == 0x123456740ull);

    unsigned ex = exhaustive_mismatch(&SMALL), exx = exhaustive_mismatch(&SMALL_X);
    printf("exhaustive 2^15 round trip: small mismatches=%u, small+xor mismatches=%u\n", ex, exx);
    CHECK(ex == 0);
    CHECK(exx == 0);
    unsigned ra = random_mismatch(&MAP_A, 200000), rax = random_mismatch(&MAP_AX, 200000);
    unsigned rb = random_mismatch(&MAP_B, 200000), rc = random_mismatch(&MAP_C, 200000);
    printf("random 200000 x2 round trip: A=%u AX=%u B=%u C=%u mismatches\n", ra, rax, rb, rc);
    CHECK(ra == 0 && rax == 0);
    CHECK(rb == 0 && rc == 0);

    /* 범위 밖 / 필드 넘침 */
    CHECK(map_decode(&MAP_A, &ia, 1ull << 35, &l) == MAP_ERR_RANGE);
    CHECK(map_decode(&MAP_A, &ia, UINT64_MAX, &l) == MAP_ERR_RANGE);
    CHECK(map_encode(&MAP_A, &ia, &(dram_loc_t){ { 0, 0, 0, 0, 0x10000, 0 }, 0 }, &pa) == MAP_ERR_FIELD);
    CHECK(map_encode(&MAP_A, &ia, &(dram_loc_t){ { 0, 0, 4, 0, 0, 0 }, 0 }, &pa) == MAP_ERR_FIELD);
    CHECK(map_encode(&MAP_A, &ia, &(dram_loc_t){ { 2, 0, 0, 0, 0, 0 }, 0 }, &pa) == MAP_ERR_FIELD);
    CHECK(map_encode(&MAP_A, &ia, &(dram_loc_t){ { 0, 0, 0, 0, 0, 0x400 }, 0 }, &pa) == MAP_ERR_FIELD);
    CHECK(map_encode(&MAP_A, &ia, &(dram_loc_t){ { 0, 0, 0, 0, 0, 0 }, 8 }, &pa) == MAP_ERR_FIELD);

    /* 잘못된 매핑 표 */
    addr_map_t bad = MAP_A;
    bad.seg[6].width = 15; bad.seg[6].pa_lo = 20;                 /* PA[19] 빈틈 */
    CHECK(validate_rc(&bad) == MAP_ERR_LAYOUT);
    bad = MAP_A; bad.seg[3].pa_lo = 13;                           /* RANK 가 COL 비트 13 과 겹침 */
    CHECK(validate_rc(&bad) == MAP_ERR_LAYOUT);
    bad = MAP_A; bad.seg[2].f_lo = 4;                             /* COL 비트 3 빈틈 */
    CHECK(validate_rc(&bad) == MAP_ERR_LAYOUT);
    bad = MAP_A; bad.seg[2].f_lo = 2;                             /* COL 비트 2 두 번 */
    CHECK(validate_rc(&bad) == MAP_ERR_LAYOUT);
    bad = MAP_AX; bad.xr[0].target = F_ROW;                       /* ROW 를 해싱하면 역이 안 됨 */
    CHECK(validate_rc(&bad) == MAP_ERR_XOR);
    bad = MAP_AX; bad.xr[0].src = F_BANK;                         /* 해싱되는 필드를 입력으로 */
    CHECK(validate_rc(&bad) == MAP_ERR_XOR);
    bad = MAP_AX; bad.xr[1].tbit = 2;                             /* BG 는 2비트 */
    CHECK(validate_rc(&bad) == MAP_ERR_XOR);
    bad = MAP_AX; bad.xr[2].src_mask = 0x10000;                   /* ROW 는 16비트 */
    CHECK(validate_rc(&bad) == MAP_ERR_XOR);

    /* ===== Part B ===== */
    uint32_t hist[64];
    bank_stats_t st;
    CHECK(bank_histogram(&MAP_A, &ia, 0, 64, 256, hist, 64, &st) == MAP_OK &&
          st.distinct == 2 && st.max_load == 128 && hist[0] == 128 && hist[32] == 128 && st.conflicts == 0);
    CHECK(bank_histogram(&MAP_B, &ib, 0, 64, 256, hist, 64, &st) == MAP_OK &&
          st.distinct == 64 && st.max_load == 4 && st.hits == 192);
    CHECK(bank_histogram(&MAP_A, &ia, 0, 1u << 19, 64, hist, 64, &st) == MAP_OK &&
          st.distinct == 1 && st.max_load == 64 && st.conflicts == 63);
    CHECK(bank_histogram(&MAP_AX, &iax, 0, 1u << 19, 64, hist, 64, &st) == MAP_OK &&
          st.distinct == 16 && st.max_load == 4);
    CHECK(bank_histogram(&MAP_A, &ia, 0, 64, 4, hist, 16, &st) == MAP_ERR_CAP);
    CHECK(bank_histogram(&MAP_A, &ia, (1ull << 35) - 64, 64, 2, hist, 64, &st) == MAP_ERR_RANGE);

    printf("\nstride sweep, n=1024 accesses from PA 0: distinct banks(/64) / max load / conflict%%\n");
    printf("%-8s %-18s %-18s %-18s %-18s\n", "stride", MAP_A.name, MAP_AX.name, MAP_B.name, MAP_C.name);
    const addr_map_t *maps[4] = { &MAP_A, &MAP_AX, &MAP_B, &MAP_C };
    const map_info_t *infos[4] = { &ia, &iax, &ib, &ic };
    for (unsigned k = 6; k <= 21; k++) {
        char lab[16];
        snprintf(lab, sizeof lab, "%u %s", 1u << (k % 10), k < 10 ? "B" : k < 20 ? "KB" : "MB");
        printf("%-8s", lab);
        for (int j = 0; j < 4; j++) {
            bank_histogram(maps[j], infos[j], 0, 1ull << k, 1024, hist, 64, &st);
            char cell[32];
            snprintf(cell, sizeof cell, "%2u / %4u / %5.1f", st.distinct, st.max_load, 100.0 * st.conflicts / 1024);
            printf(" %-18s", cell);
        }
        printf("\n");
    }

    /* ===== Part C ===== */
    uint64_t nb;
    CHECK(row_neighbor(&MAP_A, &ia, 0x123456740ull, +1, &nb) == MAP_OK && nb == 0x123456740ull + (1u << 19));
    CHECK(row_neighbor(&MAP_A, &ia, 0x123456740ull, -1, &nb) == MAP_OK && nb == 0x123456740ull - (1u << 19));
    CHECK(row_neighbor(&MAP_AX, &iax, 0x123456740ull, +1, &nb) == MAP_OK && nb == 0x1234DE740ull);
    dram_loc_t l0, l1;
    map_decode(&MAP_AX, &iax, 0x123456740ull, &l0);
    map_decode(&MAP_AX, &iax, nb, &l1);
    CHECK(l1.f[F_ROW] == l0.f[F_ROW] + 1 && l1.f[F_BG] == l0.f[F_BG] && l1.f[F_BANK] == l0.f[F_BANK] &&
          l1.f[F_COL] == l0.f[F_COL] && l1.f[F_CH] == l0.f[F_CH] && l1.f[F_RANK] == l0.f[F_RANK]);
    printf("\nAX row+1 neighbour of 0x123456740 = 0x%llX (A would say 0x%llX)\n",
           (unsigned long long)nb, (unsigned long long)(0x123456740ull + (1u << 19)));
    CHECK(row_neighbor(&MAP_A, &ia, 0x40ull, -1, &nb) == MAP_ERR_RANGE);
    CHECK(row_neighbor(&MAP_A, &ia, 0x7FFFFFFFFull, +1, &nb) == MAP_ERR_RANGE);

    static uint64_t lines[256];
    int na = row_lines(&MAP_A, &ia, 0x123456740ull, 64, lines, 256);
    CHECK(na == 128 && lines[0] == 0x123454040ull && lines[1] == 0x1234540C0ull && lines[2] == 0x123454140ull);
    uint32_t pa_a = na > 0 ? count_pages(lines, na, 4096) : 0;
    int nbl = row_lines(&MAP_B, &ib, 0x123456740ull, 64, lines, 256);
    CHECK(nbl == 128 && lines[0] == 0x123400740ull && lines[1] == 0x123401740ull);
    uint32_t pa_b = nbl > 0 ? count_pages(lines, nbl, 4096) : 0;
    int nax = row_lines(&MAP_AX, &iax, 0x123456740ull, 64, lines, 256);
    uint32_t pa_ax = nax > 0 ? count_pages(lines, nax, 4096) : 0;
    printf("one DRAM row (8 KB) = %d lines of 64 B -> 4 KB pages touched: A=%u AX=%u B=%u\n", na, pa_a, pa_ax, pa_b);
    CHECK(pa_a == 4 && pa_ax == 4 && pa_b == 128);
    CHECK(row_lines(&MAP_A, &ia, 0, 64, lines, 64) == MAP_ERR_CAP);
    CHECK(row_lines(&MAP_A, &ia, 0, 48, lines, 256) == MAP_ERR_FIELD);

    /* EDAC: 같은 row 에서 난 CE 3개 — 매핑을 잘못 쓰면 BG/bank 를 잘못 짚는다 (PPR 대상이 틀림) */
    uint64_t ce[3];
    for (int i = 0; i < 3; i++)
        map_encode(&MAP_AX, &iax, &(dram_loc_t){ { 1, 0, 3, 1, 0x0ABC, (uint32_t)(0x40 * i + 8) }, 0 }, &ce[i]);
    dram_loc_t right, wrong;
    int same_row = 1;
    for (int i = 0; i < 3; i++) {
        map_decode(&MAP_AX, &iax, ce[i], &right);
        map_decode(&MAP_A, &ia, ce[i], &wrong);
        same_row &= (right.f[F_BG] == 3 && right.f[F_BANK] == 1 && right.f[F_ROW] == 0x0ABC);
        loc_format(&right, buf, sizeof buf);
        printf("CE pa=0x%09llX  AX: %s | A(wrong): bg%u bank%u\n", (unsigned long long)ce[i], buf,
               (unsigned)wrong.f[F_BG], (unsigned)wrong.f[F_BANK]);
    }
    CHECK(same_row);
    CHECK(wrong.f[F_BG] == 3 && wrong.f[F_BANK] == 2);      /* XOR 를 빼면 bank 1 → 2 로 오판 */

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
