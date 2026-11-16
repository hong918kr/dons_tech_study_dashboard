/* MEM-B01 추가 연습 — 한 번에 검증 (MEM-B01 solution.c 의 디코더를 그대로 포함: 자기완결)
 *
 *   연습 1: 용량 · rank row(page) 크기 · 칩 page 크기 (DDR4 x8 16 Gb 매핑)
 *   연습 2: DDR5 서브채널 매핑 (32-bit subchannel, BL16 = 64 B)
 *   연습 3: DRAMA 식 역공학 — "같은 뱅크인가?" 오라클만으로 XOR 뱅크 함수 복원
 *   연습 4: XOR 매핑에도 있는 최악 stride 찾기
 *   연습 5: OS 페이지 하나가 건드리는 DRAM row 수 (bank coloring 가능 여부)
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum { F_CH, F_RANK, F_BG, F_BANK, F_ROW, F_COL, F_N };

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
    if (m->off_bits > 8 || m->nseg == 0 || m->nseg > MAP_MAX_SEG || m->nxor > MAP_MAX_XOR)
        return MAP_ERR_LAYOUT;
    uint64_t pa_used = mask64(m->off_bits);
    uint32_t f_used[F_N] = { 0 };
    unsigned top = m->off_bits;
    for (unsigned i = 0; i < m->nseg; i++) {
        const map_seg_t *s = &m->seg[i];
        if (s->field >= F_N || s->width == 0 || s->pa_lo + s->width > MAP_MAX_PA_BITS ||
            s->f_lo + s->width > MAP_MAX_FIELD_BITS)
            return MAP_ERR_LAYOUT;
        uint64_t pm = mask64(s->width) << s->pa_lo;
        uint32_t fm = (uint32_t)(mask64(s->width) << s->f_lo);
        if ((pa_used & pm) || (f_used[s->field] & fm)) return MAP_ERR_LAYOUT;   /* 겹침 */
        pa_used |= pm;
        f_used[s->field] |= fm;
        if (s->pa_lo + s->width > top) top = s->pa_lo + s->width;
    }
    if (pa_used != mask64(top)) return MAP_ERR_LAYOUT;                        /* PA 빈틈 */
    unsigned bank_bits = 0;
    for (unsigned f = 0; f < F_N; f++) {
        if (f_used[f] & (f_used[f] + 1u)) return MAP_ERR_LAYOUT;                /* 필드 비트 빈틈 */
        mi->width[f] = (uint8_t)popcount64(f_used[f]);
        if (f < F_ROW) bank_bits += mi->width[f];
    }
    if (mi->width[F_ROW] == 0 || mi->width[F_COL] == 0 || bank_bits > MAP_MAX_BANK_BITS)
        return MAP_ERR_LAYOUT;
    for (unsigned i = 0; i < m->nxor; i++) {
        const map_xor_t *x = &m->xr[i];
        if (x->target > F_BANK || (x->src != F_ROW && x->src != F_COL) ||
            x->tbit >= mi->width[x->target] || x->src_mask == 0 ||
            (x->src_mask >> mi->width[x->src]) != 0)
            return MAP_ERR_XOR;
    }
    mi->pa_bits = (uint8_t)top;
    return MAP_OK;
}

static int map_decode(const addr_map_t *m, const map_info_t *mi, uint64_t pa, dram_loc_t *out) {
    memset(out, 0, sizeof *out);
    if (mi->pa_bits == 0) return MAP_ERR_LAYOUT;
    if (pa >> mi->pa_bits) return MAP_ERR_RANGE;
    out->byte = (uint32_t)(pa & mask64(m->off_bits));
    for (unsigned i = 0; i < m->nseg; i++) {                  /* ① 비트 필드 잘라 붙이기 */
        const map_seg_t *s = &m->seg[i];
        out->f[s->field] |= (uint32_t)((pa >> s->pa_lo) & mask64(s->width)) << s->f_lo;
    }
    for (unsigned i = 0; i < m->nxor; i++) {                  /* ② XOR 해싱 (src 는 해싱 안 됨) */
        const map_xor_t *x = &m->xr[i];
        out->f[x->target] ^= parity32(out->f[x->src] & x->src_mask) << x->tbit;
    }
    return MAP_OK;
}

static int map_encode(const addr_map_t *m, const map_info_t *mi, const dram_loc_t *loc, uint64_t *pa) {
    *pa = 0;
    if (mi->pa_bits == 0) return MAP_ERR_LAYOUT;
    for (unsigned f = 0; f < F_N; f++)
        if (loc->f[f] >> mi->width[f]) return MAP_ERR_FIELD;
    if (loc->byte >> m->off_bits) return MAP_ERR_FIELD;
    uint32_t raw[F_N];
    memcpy(raw, loc->f, sizeof raw);
    for (unsigned i = 0; i < m->nxor; i++) {                  /* ② 의 역: 같은 XOR 를 한 번 더 */
        const map_xor_t *x = &m->xr[i];
        raw[x->target] ^= parity32(loc->f[x->src] & x->src_mask) << x->tbit;
    }
    uint64_t p = loc->byte;
    for (unsigned i = 0; i < m->nseg; i++) {                  /* ① 의 역: 필드 비트를 PA 자리로 */
        const map_seg_t *s = &m->seg[i];
        p |= ((uint64_t)(raw[s->field] >> s->f_lo) & mask64(s->width)) << s->pa_lo;
    }
    *pa = p;
    return MAP_OK;
}

/* ---------------- Part B: 뱅크 분포 ---------------- */

/* (ch, rank, bg, bank) 를 하나의 번호로: 비트를 이어 붙인다 (모두 2의 거듭제곱 개수) */
static uint32_t flat_bank(const map_info_t *mi, const dram_loc_t *l) {
    uint32_t b = l->f[F_CH];
    b = (b << mi->width[F_RANK]) | l->f[F_RANK];
    b = (b << mi->width[F_BG]) | l->f[F_BG];
    b = (b << mi->width[F_BANK]) | l->f[F_BANK];
    return b;
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
    if (mi->pa_bits == 0) return MAP_ERR_LAYOUT;
    uint32_t nb = num_banks(mi);
    if (nb > cap) return MAP_ERR_CAP;
    memset(hist, 0, nb * sizeof *hist);
    uint32_t last_row[1u << MAP_MAX_BANK_BITS];
    for (uint32_t i = 0; i < n; i++) {
        if (stride && i > (UINT64_MAX - base) / stride) return MAP_ERR_RANGE;
        dram_loc_t l;
        int rc = map_decode(m, mi, base + (uint64_t)i * stride, &l);
        if (rc) return rc;
        uint32_t b = flat_bank(mi, &l);
        if (hist[b]++ == 0) { st->distinct++; st->first++; }
        else if (last_row[b] == l.f[F_ROW]) st->hits++;
        else st->conflicts++;
        last_row[b] = l.f[F_ROW];
        if (hist[b] > st->max_load) st->max_load = hist[b];
    }
    return MAP_OK;
}

/* ---------------- Part C: 크래시 덤프 / EDAC 용 도구 ---------------- */

/* pa 가 속한 DRAM row (ch,rank,bg,bank,row 고정) 의 모든 line_bytes 버스트 시작 PA 를 col 순서로.
 * 반환: 개수 (≥0) 또는 음수 오류. line_bytes 는 2^off_bits 의 2의 거듭제곱 배. */
static int row_lines(const addr_map_t *m, const map_info_t *mi, uint64_t pa, uint32_t line_bytes,
                     uint64_t *out, size_t cap) {
    dram_loc_t l;
    int rc = map_decode(m, mi, pa, &l);
    if (rc) return rc;
    uint32_t cols_per_line = line_bytes >> m->off_bits;
    if (cols_per_line == 0 || (cols_per_line & (cols_per_line - 1)) ||
        (line_bytes & (uint32_t)mask64(m->off_bits)) || cols_per_line > (1u << mi->width[F_COL]))
        return MAP_ERR_FIELD;
    uint32_t n = (1u << mi->width[F_COL]) / cols_per_line;
    if (n > cap) return MAP_ERR_CAP;
    l.byte = 0;
    for (uint32_t i = 0; i < n; i++) {
        l.f[F_COL] = i * cols_per_line;
        rc = map_encode(m, mi, &l, &out[i]);
        if (rc) return rc;
    }
    return (int)n;
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

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint64_t g_rng = 0x2545F4914F6CDD1Dull;
static uint64_t xorshift64(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 7; g_rng ^= g_rng << 17; return g_rng; }

/* ===== 연습 1: 용량과 page 크기 ===== */
/* DDR4 x8 16 Gb (대표값: 4 BG x 4 bank, 128 K row, 1 K col), 1 ch x 1 rank, 64-bit → 16 GiB */
static const addr_map_t MAP_16G = { "ddr4-x8-16Gb 1R", 3, 5, {
    SEG(F_COL, 0, 3, 3), SEG(F_COL, 3, 6, 7), SEG(F_BG, 0, 13, 2), SEG(F_BANK, 0, 15, 2),
    SEG(F_ROW, 0, 17, 17) }, 0, { { 0, 0, 0, 0 } } };
static uint64_t map_bytes(const map_info_t *mi) { return 1ull << mi->pa_bits; }
static uint32_t rank_row_bytes(const addr_map_t *m, const map_info_t *mi) { return 1u << (mi->width[F_COL] + m->off_bits); }
static uint32_t chip_page_bytes(unsigned col_bits, unsigned dq_width) { return (1u << col_bits) * dq_width / 8u; }

/* ===== 연습 2: DDR5 서브채널 (대표값: x8 16 Gb = 8 BG x 4 bank, 64 K row, 1 K col) ===== */
/* 1 rank DIMM = 서브채널 2개 x 32-bit. "ch" 필드를 서브채널로 쓴다. PA[1:0] = 4 B beat 안 바이트 */
static const addr_map_t MAP_DDR5 = { "ddr5 2x32b subch", 2, 6, {
    SEG(F_COL, 0, 2, 4), SEG(F_CH, 0, 6, 1), SEG(F_BG, 0, 7, 3), SEG(F_BANK, 0, 10, 2),
    SEG(F_COL, 4, 12, 6), SEG(F_ROW, 0, 18, 16) }, 0, { { 0, 0, 0, 0 } } };

/* ===== 연습 3: DRAMA 식 뱅크 함수 복원 ===== */
static uint32_t oracle_bank(const addr_map_t *m, const map_info_t *mi, uint64_t pa) {
    dram_loc_t l;
    map_decode(m, mi, pa, &l);
    return flat_bank(mi, &l);           /* 공격자는 이 값을 못 보고 "같은가"만 안다 (타이밍) */
}
static int uf_find(int *p, int x) { while (p[x] != x) x = p[x] = p[p[x]]; return x; }

/* PA 비트 [lo, hi) 중 뱅크를 바꾸는 비트를 찾고, 둘을 같이 뒤집으면 같은 뱅크로 돌아오는 쌍을 묶는다.
 * 반환: 함수 개수, fn[k] = 그 함수에 들어가는 PA 비트 마스크 (가장 낮은 비트 순) */
static int recover_bank_functions(const addr_map_t *m, const map_info_t *mi, unsigned lo, unsigned hi,
                                  uint64_t *fn, int cap) {
    int parent[64], rel[64] = { 0 };
    uint32_t b0 = oracle_bank(m, mi, 0);
    for (unsigned i = lo; i < hi; i++) { parent[i] = (int)i; rel[i] = oracle_bank(m, mi, 1ull << i) != b0; }
    for (unsigned i = lo; i < hi; i++)
        for (unsigned j = i + 1; j < hi; j++)
            if (rel[i] && rel[j] && oracle_bank(m, mi, (1ull << i) | (1ull << j)) == b0)
                parent[uf_find(parent, (int)j)] = uf_find(parent, (int)i);
    int n = 0;
    for (unsigned i = lo; i < hi; i++) {
        if (!rel[i] || uf_find(parent, (int)i) != (int)i) continue;   /* 대표(가장 낮은 비트)만 */
        if (n == cap) return MAP_ERR_CAP;
        uint64_t mask = 0;
        for (unsigned j = i; j < hi; j++) if (rel[j] && uf_find(parent, (int)j) == (int)i) mask |= 1ull << j;
        fn[n++] = mask;
    }
    return n;
}

/* ===== 연습 4: 최악 stride ===== */
typedef struct { uint64_t stride; uint32_t max_load, distinct; } worst_t;
static worst_t worst_stride(const addr_map_t *m, const map_info_t *mi, unsigned lo, unsigned hi, uint32_t n) {
    worst_t w = { 0, 0, 0 };
    static uint32_t hist[1u << MAP_MAX_BANK_BITS];
    for (unsigned a = lo; a <= hi; a++)
        for (unsigned b = a; b <= hi; b++) {                 /* b == a → 2^a 하나, 아니면 2^a + 2^b */
            uint64_t s = (1ull << a) + (b == a ? 0 : (1ull << b));
            bank_stats_t st;
            if (bank_histogram(m, mi, 0, s, n, hist, 1u << MAP_MAX_BANK_BITS, &st)) continue;
            if (st.max_load > w.max_load) { w.stride = s; w.max_load = st.max_load; w.distinct = st.distinct; }
        }
    return w;
}

/* ===== 연습 5: OS 페이지 하나가 건드리는 (bank, row) 수 ===== */
static uint32_t page_rows(const addr_map_t *m, const map_info_t *mi, uint64_t page_pa, uint32_t page_bytes) {
    static uint64_t key[4096];
    uint32_t n = 0, lines = page_bytes / 64u;
    for (uint32_t i = 0; i < lines && i < 4096; i++) {
        dram_loc_t l;
        if (map_decode(m, mi, page_pa + 64ull * i, &l)) return 0;
        uint64_t k = ((uint64_t)flat_bank(mi, &l) << 32) | l.f[F_ROW];
        int seen = 0;
        for (uint32_t j = 0; j < n && !seen; j++) seen = key[j] == k;
        if (!seen) key[n++] = k;
    }
    return n;
}

static void print_fns(const char *name, const uint64_t *fn, int n) {
    printf("%-22s:", name);
    for (int k = 0; k < n; k++) {
        printf(" {");
        int first = 1;
        for (unsigned b = 0; b < 64; b++) if (fn[k] >> b & 1) { printf(first ? "%u" : ",%u", b); first = 0; }
        printf("}");
    }
    printf("\n");
}

int main(void) {
    map_info_t ia, iax, ib, ic, i16, i5;
    CHECK(map_validate(&MAP_A, &ia) == MAP_OK && map_validate(&MAP_AX, &iax) == MAP_OK);
    CHECK(map_validate(&MAP_B, &ib) == MAP_OK && map_validate(&MAP_C, &ic) == MAP_OK);

    /* 연습 1 */
    CHECK(map_validate(&MAP_16G, &i16) == MAP_OK && i16.pa_bits == 34 && i16.width[F_ROW] == 17);
    CHECK(map_bytes(&i16) == 16ull << 30 && map_bytes(&ia) == 32ull << 30);
    CHECK(rank_row_bytes(&MAP_16G, &i16) == 8192 && rank_row_bytes(&MAP_A, &ia) == 8192);
    CHECK(chip_page_bytes(10, 8) == 1024 && chip_page_bytes(10, 4) == 512 && chip_page_bytes(10, 16) == 2048);
    printf("ex1: %s = %llu GiB, rank row %u B, chip page x4/x8/x16 = %u/%u/%u B\n", MAP_16G.name,
           (unsigned long long)(map_bytes(&i16) >> 30), rank_row_bytes(&MAP_16G, &i16),
           chip_page_bytes(10, 4), chip_page_bytes(10, 8), chip_page_bytes(10, 16));

    /* 연습 2 */
    CHECK(map_validate(&MAP_DDR5, &i5) == MAP_OK && i5.pa_bits == 34 && i5.width[F_BG] == 3);
    CHECK((1u << 4) * 4u == 64u);                       /* BL16 x 4 B = 64 B: 라인 하나 = 버스트 하나 */
    static uint64_t lines[256];
    CHECK(row_lines(&MAP_DDR5, &i5, 0, 64, lines, 256) == 64 && lines[1] - lines[0] == 4096);
    CHECK(rank_row_bytes(&MAP_DDR5, &i5) == 4096);
    unsigned bad5 = 0;
    for (int i = 0; i < 100000; i++) {
        uint64_t pa = xorshift64() & ((1ull << 34) - 1), back;
        dram_loc_t l;
        if (map_decode(&MAP_DDR5, &i5, pa, &l) || map_encode(&MAP_DDR5, &i5, &l, &back) || back != pa) bad5++;
    }
    CHECK(bad5 == 0);
    uint32_t h5[64];
    bank_stats_t s5;
    CHECK(bank_histogram(&MAP_DDR5, &i5, 0, 64, 64, h5, 64, &s5) == MAP_OK && s5.distinct == 64 && s5.max_load == 1);
    printf("ex2: DDR5 map %u PA bits, 64 consecutive lines -> %u distinct (subch,bg,bank), row = %u B per subchannel\n",
           (unsigned)i5.pa_bits, s5.distinct, rank_row_bytes(&MAP_DDR5, &i5));

    /* 연습 3 */
    uint64_t fn[16];
    int nf = recover_bank_functions(&MAP_AX, &iax, 3, 35, fn, 16);
    print_fns("ex3: AX recovered", fn, nf);
    CHECK(nf == 6 && fn[0] == (1ull << 6) && fn[1] == (1ull << 14) && fn[2] == ((1ull << 15) | (1ull << 19)) &&
          fn[3] == ((1ull << 16) | (1ull << 20)) && fn[4] == ((1ull << 17) | (1ull << 21)) &&
          fn[5] == ((1ull << 18) | (1ull << 22)));
    nf = recover_bank_functions(&MAP_C, &ic, 3, 35, fn, 16);
    print_fns("ex3: C recovered", fn, nf);
    CHECK(nf == 6 && fn[0] == 0x783040ull && fn[1] == ((1ull << 7) | (1ull << 23)) &&
          fn[5] == ((1ull << 11) | (1ull << 27)));
    nf = recover_bank_functions(&MAP_A, &ia, 3, 35, fn, 16);
    CHECK(nf == 6 && fn[0] == (1ull << 6) && fn[5] == (1ull << 18));

    /* 연습 4 */
    worst_t wa = worst_stride(&MAP_A, &ia, 6, 26, 256), wx = worst_stride(&MAP_AX, &iax, 6, 26, 256);
    worst_t wc = worst_stride(&MAP_C, &ic, 6, 26, 256);
    printf("ex4: worst stride (n=256) A: 0x%llX max_load %u distinct %u | AX: 0x%llX max_load %u distinct %u"
           " | C: 0x%llX max_load %u distinct %u\n",
           (unsigned long long)wa.stride, wa.max_load, wa.distinct,
           (unsigned long long)wx.stride, wx.max_load, wx.distinct,
           (unsigned long long)wc.stride, wc.max_load, wc.distinct);
    CHECK(wa.stride == (1ull << 19) && wa.max_load == 256 && wa.distinct == 1);
    bank_stats_t sx;
    uint32_t hx[64];
    CHECK(bank_histogram(&MAP_AX, &iax, 0, wx.stride, 256, hx, 64, &sx) == MAP_OK && sx.max_load == wx.max_load);
    CHECK(wx.stride == (1ull << 23) && wx.distinct == 1);   /* 해시에 안 쓰인 row 비트 위의 stride → 그대로 캠핑 */
    CHECK(wc.distinct <= 2 && wc.stride % 4096 == 0);        /* C: 4 KB 배수 stride 는 8 MB 안에서 ch 만 바뀜 */

    /* 연습 5 */
    uint32_t ra4 = page_rows(&MAP_A, &ia, 0x123450000ull, 4096), ra16 = page_rows(&MAP_A, &ia, 0x123450000ull, 16384);
    uint32_t rb4 = page_rows(&MAP_B, &ib, 0x123450000ull, 4096), rb16 = page_rows(&MAP_B, &ib, 0x123450000ull, 16384);
    printf("ex5: (bank,row) per OS page  A: 4KB=%u 16KB=%u | B: 4KB=%u 16KB=%u\n", ra4, ra16, rb4, rb16);
    CHECK(ra4 == 2 && ra16 == 2 && rb4 == 64 && rb16 == 64);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
