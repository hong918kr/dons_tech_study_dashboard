/* ARM-A09 추가 연습 정답 — ADR / CONDBR19 / TSTBR14 / MOVW_UABS / 베니어(veneer) / Mach-O chained fixup
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c shared_pic.S -o /tmp/ARM-A09_practice && /tmp/ARM-A09_practice
 * 정답 워드는 전부 실제 링커 출력: demo_practice.txt (ld.bfd 2.46), demo_elf.txt (ld.bfd / ld.lld), demo_mac.txt (ld64 + dyld_info).
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) g_pass++; \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
enum { RELOC_OK = 0, RELOC_RANGE = 1, RELOC_ALIGN = 2, RELOC_BADINSN = 3 };

static int64_t sext(uint64_t v, unsigned bits) {
    uint64_t m = (uint64_t)1 << (bits - 1);
    v &= ((uint64_t)1 << bits) - 1;
    return (int64_t)((v ^ m) - m);
}
/* 범위 [-2^(bits-1), 2^(bits-1)-1] × scale 검사 + 정렬 검사 → imm 필드 값 */
static int pcrel_field(int64_t off, unsigned bits, unsigned scale, uint32_t *field) {
    if (off % (int64_t)scale) return RELOC_ALIGN;
    int64_t q = off / (int64_t)scale;
    if (q < -(INT64_C(1) << (bits - 1)) || q > (INT64_C(1) << (bits - 1)) - 1) return RELOC_RANGE;
    *field = (uint32_t)q & ((1u << bits) - 1);
    return RELOC_OK;
}

/* ---- 연습 1: ADR (R_AARCH64_ADR_PREL_LO21) — 바이트 단위 ±1 MB, immlo [30:29] + immhi [23:5] ---- */
static int apply_adr(uint32_t *insn, uint64_t P, uint64_t SA) {
    if ((*insn & 0x9F000000u) != 0x10000000u) return RELOC_BADINSN;
    uint32_t f;
    int e = pcrel_field((int64_t)(SA - P), 21, 1, &f);
    if (e) return e;
    *insn = (*insn & 0x9F00001Fu) | ((f & 3) << 29) | ((f >> 2) << 5);
    return RELOC_OK;
}
static uint64_t decode_adr(uint32_t insn, uint64_t P) {
    return P + (uint64_t)sext(((insn >> 29) & 3) | (((insn >> 5) & 0x7FFFF) << 2), 21);
}

/* ---- 연습 2: B.cond / CBZ / CBNZ (CONDBR19, imm19 [23:5], ±1 MB) · TBZ / TBNZ (TSTBR14, imm14 [18:5], ±32 KB) ---- */
static int apply_condbr19(uint32_t *insn, uint64_t P, uint64_t SA) {
    int bcond = (*insn & 0xFF000010u) == 0x54000000u, cbz = (*insn & 0x7E000000u) == 0x34000000u;
    if (!bcond && !cbz) return RELOC_BADINSN;
    uint32_t f;
    int e = pcrel_field((int64_t)(SA - P), 19, 4, &f);
    if (e) return e;
    *insn = (*insn & ~(0x7FFFFu << 5)) | (f << 5);
    return RELOC_OK;
}
static int apply_tstbr14(uint32_t *insn, uint64_t P, uint64_t SA) {
    if ((*insn & 0x7E000000u) != 0x36000000u) return RELOC_BADINSN;
    uint32_t f;
    int e = pcrel_field((int64_t)(SA - P), 14, 4, &f);
    if (e) return e;
    *insn = (*insn & ~(0x3FFFu << 5)) | (f << 5);
    return RELOC_OK;
}
static uint64_t decode_condbr19(uint32_t insn, uint64_t P) { return P + ((uint64_t)sext(insn >> 5, 19) << 2); }
static uint64_t decode_tstbr14(uint32_t insn, uint64_t P)  { return P + ((uint64_t)sext(insn >> 5, 14) << 2); }

/* ---- 연습 3: MOVZ/MOVK 4개로 64비트 절대 주소 (MOVW_UABS_G3, G2_NC, G1_NC, G0_NC) — non-PIC, -mcmodel=large ---- */
static int apply_movw_uabs(uint32_t insn[4], uint64_t SA) {
    for (int i = 0; i < 4; i++) {
        unsigned hw = (insn[i] >> 21) & 3;                   /* 명령에 이미 들어 있는 lsl #16*hw */
        if ((insn[i] & 0x1F800000u) != 0x12800000u || hw != (unsigned)(3 - i)) return RELOC_BADINSN;
        insn[i] = (insn[i] & ~(0xFFFFu << 5)) | ((uint32_t)(SA >> (16 * hw)) & 0xFFFF) << 5;
    }
    return RELOC_OK;
}

/* ---- 연습 4: BL 이 ±128 MB 를 넘을 때 ld.bfd 식 베니어: ADRP x16 / ADD x16 / BR x16 ---- */
static int make_veneer(uint64_t P_bl, uint32_t *bl, uint64_t V, uint64_t target, uint32_t out[3]) {
    int64_t pages = (int64_t)((target & ~0xFFFull) - (V & ~0xFFFull)) >> 12;
    if (pages < -(1 << 20) || pages > (1 << 20) - 1) return RELOC_RANGE;      /* ADRP 도 ±4 GB 한계 */
    uint32_t imm = (uint32_t)pages & 0x1FFFFF, f;
    out[0] = 0x90000010u | ((imm & 3) << 29) | ((imm >> 2) << 5);              /* adrp x16, target */
    out[1] = 0x91000210u | ((uint32_t)(target & 0xFFF) << 10);                 /* add  x16, x16, :lo12:target */
    out[2] = 0xD61F0200u;                                                       /* br   x16 */
    int e = pcrel_field((int64_t)(V - P_bl), 26, 4, &f);                       /* bl 은 이제 베니어로 */
    if (e) return e;
    *bl = (*bl & 0xFC000000u) | f;
    return RELOC_OK;
}

/* ---- 연습 5: Mach-O chained fixup (DYLD_CHAINED_PTR_64_OFFSET) 포인터 해독과 체인 걷기 ----
 * bind  : bit63=1, ordinal[23:0], addend[31:24], next[62:51]
 * rebase: bit63=0, target[35:0] (= mach header 기준 vm offset), high8[43:36], next[62:51]
 * next 단위 = 4 B (stride). next == 0 이면 체인 끝. */
typedef struct { int bind; uint32_t ordinal; uint8_t addend; uint64_t target; uint32_t next; } chained;
static chained decode_chained64(uint64_t raw) {
    chained c;
    memset(&c, 0, sizeof c);
    c.bind = (int)(raw >> 63);
    c.next = (uint32_t)(raw >> 51) & 0xFFF;
    if (c.bind) { c.ordinal = (uint32_t)raw & 0xFFFFFF; c.addend = (uint8_t)(raw >> 24); }
    else c.target = (raw & 0xFFFFFFFFFull) | ((raw >> 36) & 0xFF) << 56;
    return c;
}
/* page = 그 페이지의 바이트, start = 첫 픽스업 오프셋. 찾은 오프셋들을 off[] 에, 개수를 반환 */
static int walk_chain(const uint8_t *page, size_t len, uint32_t start, uint32_t off[], chained fx[], int max) {
    int n = 0;
    uint32_t o = start;
    while (n < max && o + 8 <= len) {
        uint64_t raw;
        memcpy(&raw, page + o, 8);
        fx[n] = decode_chained64(raw);
        off[n++] = o;
        if (fx[n - 1].next == 0) break;
        o += fx[n - 1].next * 4;
    }
    return n;
}

int main(void) {
    /* 1) ADR: demo_practice.txt (ld.bfd), demo_elf.txt (ld.lld 완화 결과), demo_elf.txt range (lld) */
    uint32_t w = 0x10000004u;                                 /* adr x4, #0 */
    CHECK(apply_adr(&w, 0x10010, 0x18004) == RELOC_OK && w == 0x1003ffa4u);
    w = 0x10000009u;
    CHECK(apply_adr(&w, 0x10980, 0x30cd0) == RELOC_OK && w == 0x10101a89u);
    w = 0x10000000u;
    CHECK(apply_adr(&w, 0x10004, 0x30000) == RELOC_OK && w == 0x100fffe0u);
    CHECK(decode_adr(0x10101ac9u, 0x109a8) == 0x30d00);
    w = 0x10000000u;
    CHECK(apply_adr(&w, 0x100000, 0x100000 + 0xFFFFF) == RELOC_OK && decode_adr(w, 0x100000) == 0x1FFFFF);
    CHECK(apply_adr(&w, 0x100000, 0x200000) == RELOC_RANGE);   /* +1 MB 정확히 = 밖 */
    CHECK(apply_adr(&w, 0x100000, 0) == RELOC_OK && decode_adr(w, 0x100000) == 0);   /* −1 MB 정확히 = 안 */
    w = 0x90000000u;
    CHECK(apply_adr(&w, 0, 0) == RELOC_BADINSN);               /* ADRP 는 ADR 이 아님 */

    /* 2) CONDBR19 / TSTBR14: demo_practice.txt 경계 실측 */
    uint32_t bne = 0x54000001u, cbz = 0xb4000000u, tbz = 0x36180001u, tbnz = 0x37f80001u;
    CHECK(apply_condbr19(&bne, 0x10000, 0x10fffc) == RELOC_OK && bne == 0x547fffe1u);
    CHECK(apply_condbr19(&cbz, 0x10004, 0x10fffc) == RELOC_OK && cbz == 0xb47fffc0u);
    CHECK(apply_tstbr14(&tbz, 0x10008, 0x18004) == RELOC_OK && tbz == 0x361bffe1u);
    CHECK(apply_tstbr14(&tbnz, 0x1000c, 0x800c) == RELOC_OK && tbnz == 0x37fc0001u);
    CHECK(decode_condbr19(0x547fffe1u, 0x10000) == 0x10fffc && decode_tstbr14(0x37fc0001u, 0x1000c) == 0x800c);
    CHECK(apply_condbr19(&bne, 0x10000, 0x110000) == RELOC_RANGE);   /* ld.bfd: truncated to fit CONDBR19 */
    CHECK(apply_tstbr14(&tbz, 0x10008, 0x18008) == RELOC_RANGE);     /* +0x8000 */
    CHECK(apply_tstbr14(&tbnz, 0x1000c, 0x8008) == RELOC_RANGE);     /* −0x8004 */
    CHECK(apply_condbr19(&bne, 0x10000, 0x10002) == RELOC_ALIGN);
    CHECK(apply_tstbr14(&cbz, 0x10000, 0x10004) == RELOC_BADINSN);   /* CBZ 는 TBZ 가 아님 */

    /* 3) MOVW_UABS: far_data = 0x123456789ab0 */
    uint32_t mv[4] = {0xd2e00002u, 0xf2c00002u, 0xf2a00002u, 0xf2800002u};
    CHECK(apply_movw_uabs(mv, 0x123456789ab0ull) == RELOC_OK);
    CHECK(mv[0] == 0xd2e00002u && mv[1] == 0xf2c24682u && mv[2] == 0xf2aacf02u && mv[3] == 0xf2935602u);
    uint32_t mvbad[4] = {0xd2e00002u, 0xf2a00002u, 0xf2c00002u, 0xf2800002u};   /* hw 순서가 틀림 */
    CHECK(apply_movw_uabs(mvbad, 1) == RELOC_BADINSN);

    /* 4) 베니어: demo_elf.txt — bl@0x10008 → far_func 0x8010008 (128 MB 정확히 = 밖) */
    uint32_t bl = 0x94000000u, ven[3];
    CHECK(make_veneer(0x10008, &bl, 0x10018, 0x8010008, ven) == RELOC_OK);
    CHECK(bl == 0x94000004u && ven[0] == 0x90040010u && ven[1] == 0x91002210u && ven[2] == 0xd61f0200u);

    /* 5) chained fixups: demo_mac.txt (otool -s 원본 바이트, dyld_info -fixup_chain_details 와 비교) */
    static const uint64_t got_page[3] = {0x8010000000000000ull, 0x8010000000000001ull, 0x8000000000000002ull};
    static const uint64_t data_page[3] = {100, 0x0010000000008000ull, 0x8000000000000000ull};
    uint32_t off[8];
    chained fx[8];
    int n = walk_chain((const uint8_t *)got_page, sizeof got_page, 0x0000, off, fx, 8);
    CHECK(n == 3 && off[0] == 0 && off[1] == 8 && off[2] == 16);
    CHECK(fx[0].bind && fx[0].ordinal == 0 && fx[1].ordinal == 1 && fx[2].ordinal == 2 && fx[2].next == 0);
    n = walk_chain((const uint8_t *)data_page, sizeof data_page, 0x0008, off, fx, 8);   /* start[0] = 0x0008 */
    CHECK(n == 2 && off[0] == 8 && off[1] == 16);
    CHECK(!fx[0].bind && fx[0].target == 0x8000 && fx[0].next == 2);       /* rebase → 0x100000000 + 0x8000 */
    CHECK(fx[1].bind && fx[1].ordinal == 0 && fx[1].next == 0);           /* bind → import 0 = _sensor_gain */
    chained hi = decode_chained64(0x00000ab000001234ull);                  /* 합성: high8 = 0xab */
    CHECK(!hi.bind && hi.target == (0xabull << 56 | 0x1234));

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
