/* MEM-B12 추가 연습 — 한 번에 검증
 *   연습 1: ONFI 식 row 주소 = block << page_bits | page (pack/unpack, 주소 구멍)
 *   연습 2: "지우지 않고 덮어쓸 수 있나?" — program 은 1→0 만 (YAFFS1 식 삭제 마커)
 *   연습 3: TBW / DWPD 계산 (용량 × P/E / WAF)
 *   연습 4: plane 스트라이핑의 makespan 과 쓰기 처리량
 *   연습 5: OOB 예산 — BCH(t) 패리티 바이트가 OOB 에 들어가나
 * 수치는 대표값 · 가정 — 실제 부품은 데이터시트 확인.
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c && ./a.out
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

/* ---------- 연습 1: row 주소 ---------- */
static unsigned page_bits(uint32_t ppb) { unsigned b = 0; while ((1u << b) < ppb) b++; return b; }

static int row_pack(uint32_t blk, uint32_t page, uint32_t ppb, uint32_t nblk, uint32_t *row) {
    if (!row || ppb == 0 || page >= ppb || blk >= nblk) return -1;
    *row = blk << page_bits(ppb) | page;
    return 0;
}
static int row_unpack(uint32_t row, uint32_t ppb, uint32_t nblk, uint32_t *blk, uint32_t *page) {
    if (!blk || !page || ppb == 0) return -1;
    unsigned pb = page_bits(ppb);
    uint32_t pg = row & ((1u << pb) - 1u), b = row >> pb;
    if (pg >= ppb || b >= nblk) return -1;      /* ppb 가 2의 거듭제곱이 아니면 page 필드에 "구멍" 이 생긴다 */
    *blk = b; *page = pg;
    return 0;
}

/* ---------- 연습 2: erase 없이 old → new 로 갈 수 있나 ---------- */
static bool can_program_over(const uint8_t *old, const uint8_t *nw, size_t len) {
    for (size_t i = 0; i < len; i++) if ((old[i] & nw[i]) != nw[i]) return false;   /* new 에 1 인데 old 가 0 → 불가 */
    return true;
}
static unsigned bits_to_clear(const uint8_t *old, const uint8_t *nw, size_t len) {
    unsigned c = 0;
    for (size_t i = 0; i < len; i++) { uint8_t x = (uint8_t)(old[i] & ~nw[i]); while (x) { x &= (uint8_t)(x - 1u); c++; } }
    return c;
}

/* ---------- 연습 3: TBW / DWPD ---------- */
static uint64_t tbw_bytes(uint64_t cap_bytes, uint32_t pe, uint32_t waf_x100) {
    return waf_x100 ? cap_bytes * pe * 100u / waf_x100 : 0;
}
static uint64_t dwpd_x1000(uint64_t tbw, uint64_t cap_bytes, uint32_t years) {
    return (cap_bytes && years) ? tbw * 1000u / (cap_bytes * 365u * years) : 0;
}

/* ---------- 연습 4: plane 스트라이핑 ---------- */
static uint64_t makespan_us(uint32_t npages, uint32_t planes, uint32_t t_prog_us, bool striped) {
    if (planes == 0) return 0;
    return striped ? (uint64_t)((npages + planes - 1) / planes) * t_prog_us : (uint64_t)npages * t_prog_us;
}
static uint64_t sim_makespan_us(uint32_t npages, uint32_t planes, uint32_t t_prog_us) {   /* page i → plane i%P */
    uint64_t busy[16] = { 0 }, m = 0;
    for (uint32_t i = 0; i < npages; i++) busy[i % planes] += t_prog_us;
    for (uint32_t p = 0; p < planes; p++) if (busy[p] > m) m = busy[p];
    return m;
}
static uint64_t bytes_per_s(uint64_t bytes, uint64_t us) { return us ? bytes * 1000000u / us : 0; }

/* ---------- 연습 5: OOB 예산 (BCH over GF(2^m), 패리티 = m·t 비트) ---------- */
static unsigned bch_m(uint32_t sector_bytes, unsigned t) {
    unsigned m = 2;
    while ((1ull << m) - 1u < (uint64_t)sector_bytes * 8u + (uint64_t)m * t) m++;   /* 코드워드 길이 ≤ 2^m − 1 */
    return m;
}
static unsigned bch_parity_bytes(uint32_t sector_bytes, unsigned t) { return (bch_m(sector_bytes, t) * t + 7u) / 8u; }
static bool oob_fits(uint32_t page, uint32_t oob, uint32_t sector, unsigned t, uint32_t meta) {
    return (page / sector) * bch_parity_bytes(sector, t) + meta <= oob;
}

int main(void) {
    /* 연습 1 */
    uint32_t row = 0, b = 0, p = 0;
    CHECK(page_bits(1152) == 11 && page_bits(64) == 6 && page_bits(1) == 0);
    CHECK(row_pack(0x123, 5, 1152, 4096, &row) == 0 && row == 0x91805u);
    CHECK(row_unpack(0x91805u, 1152, 4096, &b, &p) == 0 && b == 0x123 && p == 5);
    CHECK(row_unpack(0x123u << 11 | 1200u, 1152, 4096, &b, &p) == -1);   /* page 1200 ≥ 1152: 구멍 */
    CHECK(row_pack(4096, 0, 1152, 4096, &row) == -1 && row_pack(0, 1152, 1152, 4096, &row) == -1);
    unsigned rt_bad = 0, holes = 0;
    for (uint32_t r = 0; r < (16u << 4); r++) {           /* ppb = 12, nblk = 16: 모든 row 값 전수 */
        if (row_unpack(r, 12, 16, &b, &p) != 0) { holes++; continue; }
        uint32_t r2; rt_bad += row_pack(b, p, 12, 16, &r2) != 0 || r2 != r;
    }
    CHECK(rt_bad == 0 && holes == 16u * 4);                  /* 블록마다 page 12..15 네 개가 구멍 */
    CHECK((0x123u & (4u - 1u)) == 3);                       /* 4-plane: plane = 블록 주소 하위 2비트 */

    /* 연습 2 */
    uint8_t ff = 0xFF, z = 0x00, f0 = 0xF0, x30 = 0x30, x31 = 0x31;
    CHECK(can_program_over(&ff, &x31, 1) && can_program_over(&f0, &x30, 1) && can_program_over(&x30, &z, 1));
    CHECK(!can_program_over(&x30, &x31, 1) && !can_program_over(&z, &ff, 1));
    CHECK(bits_to_clear(&f0, &x30, 1) == 2 && bits_to_clear(&ff, &z, 1) == 8);
    uint8_t oob_old[4] = { 0xFF, 0xFF, 0x34, 0x12 }, oob_del[4] = { 0xFF, 0x00, 0x34, 0x12 };   /* byte1 = 삭제 마커 */
    CHECK(can_program_over(oob_old, oob_del, 4) && bits_to_clear(oob_old, oob_del, 4) == 8);
    CHECK(!can_program_over(oob_del, oob_old, 4));          /* 되살리기는 erase 없이는 불가 */

    /* 연습 3 */
    const uint64_t TB = 1000000000000ull;
    CHECK(tbw_bytes(TB, 1500, 300) == 500 * TB);            /* 1 TB TLC 1500 P/E, WAF 3.0 → 500 TBW */
    CHECK(dwpd_x1000(500 * TB, TB, 5) == 273);              /* 5 년 → 0.273 DWPD */
    CHECK(tbw_bytes(2 * TB, 500, 250) == 400 * TB);         /* 2 TB QLC 500 P/E, WAF 2.5 → 400 TBW */
    CHECK(dwpd_x1000(400 * TB, 2 * TB, 5) == 109);
    CHECK(tbw_bytes(TB, 1500, 100) == 1500 * TB && tbw_bytes(TB, 1500, 0) == 0);

    /* 연습 4 */
    CHECK(makespan_us(64, 4, 1000, false) == 64000 && makespan_us(64, 4, 1000, true) == 16000);
    CHECK(makespan_us(65, 4, 1000, true) == 17000);
    unsigned ms_bad = 0;
    for (uint32_t n = 0; n <= 100; n++) for (uint32_t P = 1; P <= 8; P++)
        ms_bad += makespan_us(n, P, 700, true) != sim_makespan_us(n, P, 700);
    CHECK(ms_bad == 0);
    CHECK(bytes_per_s(64u * 16384u, 64000) == 16384000u && bytes_per_s(64u * 16384u, 16000) == 65536000u);

    /* 연습 5 */
    CHECK(bch_m(512, 4) == 13 && bch_parity_bytes(512, 4) == 7);      /* 52 비트 → 7 B */
    CHECK(bch_parity_bytes(512, 8) == 13);                             /* 104 비트 = 13 B */
    CHECK(bch_m(1024, 40) == 14 && bch_parity_bytes(1024, 40) == 70);  /* 560 비트 = 70 B */
    CHECK(oob_fits(2048, 64, 512, 8, 2) && !oob_fits(2048, 64, 512, 16, 2));   /* 4×13+2=54 ≤ 64, 4×26+2=106 > 64 */
    CHECK(oob_fits(16384, 2048, 1024, 40, 64) && !oob_fits(16384, 2048, 1024, 72, 64));  /* 1120+64 / 2016+64 */

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
