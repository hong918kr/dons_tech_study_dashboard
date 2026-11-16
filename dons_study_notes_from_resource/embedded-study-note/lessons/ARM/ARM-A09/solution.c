/* ARM-A09 PIC와 리로케이션: ADRP/ADD, GOT, @PAGE — 리로케이션 적용기/디코더 + 실행 중 이미지 검사 + 미니 링커
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 starter.c  shared_pic.S -o /tmp/ARM-A09_starter  && /tmp/ARM-A09_starter
 *        cc -std=c11 -Wall -Wextra -Werror -O2 solution.c shared_pic.S -o /tmp/ARM-A09_solution && /tmp/ARM-A09_solution
 * tool : /tmp/ARM-A09_solution tool < facts.txt     (check_reloc.sh 가 실제 도구 출력으로 만든 줄을 검증)
 * Apple arm64 (Mach-O) 와 Linux aarch64 (ELF) 둘 다에서 돈다. 섹션 경계 검사만 #ifdef.
 */
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __APPLE__
#include <mach-o/getsect.h>
#include <mach-o/ldsyms.h>
#endif

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) g_pass++; \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

enum { RELOC_OK = 0, RELOC_RANGE = 1, RELOC_ALIGN = 2, RELOC_BADINSN = 3 };

/* ---- 주어진 도우미: 명령 종류 판별 (Arm ARM C4.1 인코딩 표) ---- */
static inline int is_adrp(uint32_t w)      { return (w & 0x9F000000u) == 0x90000000u; }  /* op=1 10000 */
static inline int is_adr(uint32_t w)       { return (w & 0x9F000000u) == 0x10000000u; }  /* op=0 10000 */
static inline int is_add_imm(uint32_t w)   { return (w & 0x7F800000u) == 0x11000000u; }  /* ADD (imm), sh=0, 32/64 */
static inline int is_ldst_uimm(uint32_t w) { return (w & 0x3B000000u) == 0x39000000u; }  /* LDR/STR (unsigned imm) */
static inline int is_b_or_bl(uint32_t w)   { return (w & 0x7C000000u) == 0x14000000u; }  /* B 000101 / BL 100101 */
static inline int is_ldr_x_uimm(uint32_t w){ return (w & 0xFFC00000u) == 0xF9400000u; }  /* LDR Xt, [Xn, #imm] */
static inline uint64_t page_of(uint64_t x) { return x & ~(uint64_t)0xFFF; }
/* LDR/STR (unsigned imm) 의 접근 크기(바이트) = 1 << size, 단 SIMD(V=1) 의 opc=1x 는 128비트 */
static inline unsigned ldst_size(uint32_t w) {
    unsigned size = w >> 30, v = (w >> 26) & 1, opc = (w >> 22) & 3;
    if (v && size == 0 && (opc & 2)) return 16;
    return 1u << size;
}
static inline int64_t sext(uint64_t v, unsigned bits) {               /* 하위 bits 비트를 부호 확장 */
    uint64_t m = (uint64_t)1 << (bits - 1);
    v &= ((uint64_t)1 << bits) - 1;
    return (int64_t)((v ^ m) - m);
}

/* ======================= Part A: 적용기(apply) · 디코더(decode) ======================= */

/* ADRP: imm21 = (Page(S+A) - Page(P)) >> 12, immlo = [30:29], immhi = [23:5]. 범위 ±4 GB. */
int apply_adrp(uint32_t *insn, uint64_t P, uint64_t SA) {
    if (!is_adrp(*insn)) return RELOC_BADINSN;
    int64_t pages = (int64_t)(page_of(SA) - page_of(P)) >> 12;   /* 산술 시프트 (부호 유지) */
    if (pages < -(1 << 20) || pages > (1 << 20) - 1) return RELOC_RANGE;
    uint32_t imm = (uint32_t)pages & 0x1FFFFF;
    *insn = (*insn & 0x9F00001Fu) | ((imm & 3) << 29) | ((imm >> 2) << 5);
    return RELOC_OK;
}
uint64_t decode_adrp(uint32_t insn, uint64_t P) {
    uint64_t imm = ((insn >> 29) & 3) | (((uint64_t)(insn >> 5) & 0x7FFFF) << 2);
    return page_of(P) + ((uint64_t)sext(imm, 21) << 12);
}

/* ADD Xd, Xn, #lo12 : imm12 = (S+A) & 0xFFF, 비트 [21:10]. _NC = 넘침 검사 없음. */
int apply_add_lo12(uint32_t *insn, uint64_t SA) {
    if (!is_add_imm(*insn) || (*insn & (1u << 22))) return RELOC_BADINSN;
    *insn = (*insn & ~(0xFFFu << 10)) | ((uint32_t)(SA & 0xFFF) << 10);
    return RELOC_OK;
}
/* LDR/STR [Xn, #lo12] : imm12 = ((S+A) & 0xFFF) / size  (스케일). 나머지가 있으면 인코딩 불가 → ALIGN. */
int apply_ldst_lo12(uint32_t *insn, uint64_t SA, unsigned size) {
    if (!is_ldst_uimm(*insn) || ldst_size(*insn) != size) return RELOC_BADINSN;
    uint64_t lo = SA & 0xFFF;
    if (lo % size) return RELOC_ALIGN;
    *insn = (*insn & ~(0xFFFu << 10)) | ((uint32_t)(lo / size) << 10);
    return RELOC_OK;
}
/* 하위 12비트 칸이 나타내는 "바이트 오프셋" (ADD 면 imm12, LDR/STR 이면 imm12 * size) */
uint64_t decode_lo12(uint32_t insn) {
    uint64_t imm12 = (insn >> 10) & 0xFFF;
    if (is_add_imm(insn)) return imm12;
    if (is_ldst_uimm(insn)) return imm12 * ldst_size(insn);
    return 0;
}

/* B/BL : imm26 = (S+A - P) >> 2, 범위 [-2^27, 2^27 - 4], 4 B 정렬. */
int apply_call26(uint32_t *insn, uint64_t P, uint64_t SA) {
    if (!is_b_or_bl(*insn)) return RELOC_BADINSN;
    int64_t off = (int64_t)(SA - P);
    if (off & 3) return RELOC_ALIGN;
    if (off < -(INT64_C(1) << 27) || off > (INT64_C(1) << 27) - 4) return RELOC_RANGE;
    *insn = (*insn & 0xFC000000u) | ((uint32_t)(off >> 2) & 0x03FFFFFFu);
    return RELOC_OK;
}
uint64_t decode_call26(uint32_t insn, uint64_t P) {
    return P + ((uint64_t)sext(insn & 0x03FFFFFFu, 26) << 2);
}

/* ======================= Part C: 링커 완화(relaxation) ======================= */

/* GOT 를 읽는 LDR Xt, [Xn, #got_lo12] → ADD Xt, Xn, #lo12(S)  (ld64·lld 가 같은 이미지 심볼에 하는 일) */
int relax_got_ldr_to_add(uint32_t ldr, uint64_t S, uint32_t *add_out) {
    if (!is_ldr_x_uimm(ldr)) return RELOC_BADINSN;
    uint32_t rt = ldr & 31, rn = (ldr >> 5) & 31;
    *add_out = 0x91000000u | ((uint32_t)(S & 0xFFF) << 10) | (rn << 5) | rt;
    return RELOC_OK;
}
/* lld tryRelaxAdrpAdd: ADRP Xd, S + ADD Xd, Xd, :lo12:S  →  NOP + ADR Xd, S  (S 가 ±1 MB 안일 때)
 * second = ADD 명령(이미 GOT 완화가 끝났다면 그 결과), P2 = second 의 주소 (ADR 은 두 번째 칸에 들어간다) */
int relax_adrp_to_adr(uint32_t adrp, uint32_t second, uint64_t P2, uint64_t S, uint32_t out[2]) {
    if (!is_adrp(adrp) || !is_add_imm(second) || (second >> 22) != (0x91000000u >> 22)) return RELOC_BADINSN;
    uint32_t rd = adrp & 31;
    if ((second & 31) != rd || ((second >> 5) & 31) != rd) return RELOC_BADINSN;
    int64_t val = (int64_t)(S - P2);
    if (val < -(1 << 20) || val >= (1 << 20)) return RELOC_RANGE;
    uint32_t imm = (uint32_t)val & 0x1FFFFF;
    out[0] = 0xD503201Fu;                                            /* NOP */
    out[1] = 0x10000000u | ((imm & 3) << 29) | ((imm >> 2) << 5) | rd;  /* ADR Xd, S */
    return RELOC_OK;
}

/* ---- 미니 링커: .o 의 .text 워드 + RELA 목록 + 최종 심볼 주소 → 링크된 워드 ---- */
enum { R_ADR_PREL_PG_HI21 = 275, R_ADD_ABS_LO12_NC = 277, R_LDST8 = 278, R_JUMP26 = 282, R_CALL26 = 283,
       R_LDST16 = 284, R_LDST32 = 285, R_LDST64 = 286, R_LDST128 = 299,
       R_ADR_GOT_PAGE = 311, R_LD64_GOT_LO12_NC = 312 };
typedef struct { uint32_t off; uint32_t type; uint32_t sym; int64_t addend; } rela;
typedef struct { uint64_t va; uint64_t got; int preemptible; } symval;   /* got = GOT 칸 주소 (없으면 0) */

static int apply_one(uint32_t *w, uint64_t P, uint32_t type, uint64_t SA) {
    switch (type) {
    case R_ADR_PREL_PG_HI21: case R_ADR_GOT_PAGE: return apply_adrp(w, P, SA);
    case R_ADD_ABS_LO12_NC:  return apply_add_lo12(w, SA);
    case R_LDST8:  return apply_ldst_lo12(w, SA, 1);
    case R_LDST16: return apply_ldst_lo12(w, SA, 2);
    case R_LDST32: return apply_ldst_lo12(w, SA, 4);
    case R_LDST64: case R_LD64_GOT_LO12_NC: return apply_ldst_lo12(w, SA, 8);
    case R_LDST128: return apply_ldst_lo12(w, SA, 16);
    case R_JUMP26: case R_CALL26: return apply_call26(w, P, SA);
    default: return RELOC_BADINSN;
    }
}
/* relax=0: ld.bfd / lld --no-relax 처럼 그대로. relax=1: lld 기본처럼 GOT→ADD, ADRP+ADD→NOP+ADR.
 * 반환: 0 = 성공, 아니면 (실패한 엔트리 번호 + 1) * 16 + 에러 코드 */
int link_text(uint32_t *text, size_t nwords, uint64_t base, const rela *r, size_t nr,
              const symval *sym, int relax) {
    for (size_t i = 0; i < nr; i++) {
        const rela *a = &r[i];
        if (a->off / 4 >= nwords) return (int)(i + 1) * 16 + RELOC_BADINSN;
        uint32_t *w = &text[a->off / 4];
        uint64_t P = base + a->off, S = sym[a->sym].va;
        if (relax && i + 1 < nr && r[i + 1].off == a->off + 4 && r[i + 1].sym == a->sym &&
            a->addend == 0 && r[i + 1].addend == 0 && !sym[a->sym].preemptible) {
            const rela *b = &r[i + 1];
            uint32_t w0 = *w, w1 = w[1];
            int pair = 0;
            if (a->type == R_ADR_GOT_PAGE && b->type == R_LD64_GOT_LO12_NC &&
                relax_got_ldr_to_add(w1, S, &w1) == RELOC_OK && apply_adrp(&w0, P, S) == RELOC_OK)
                pair = 1;                                     /* GOT 를 거치지 않고 ADRP+ADD 로 */
            else if (a->type == R_ADR_PREL_PG_HI21 && b->type == R_ADD_ABS_LO12_NC &&
                     apply_adrp(&w0, P, S) == RELOC_OK)
                pair = 1;
            if (pair && apply_add_lo12(&w1, S) == RELOC_OK) {
                uint32_t out[2];
                if (relax_adrp_to_adr(w0, w1, P + 4, S, out) == RELOC_OK) { w0 = out[0]; w1 = out[1]; }
                *w = w0; w[1] = w1;
                i++;                                          /* 두 엔트리를 한 번에 처리했다 */
                continue;
            }
        }
        uint64_t SA = (a->type == R_ADR_GOT_PAGE || a->type == R_LD64_GOT_LO12_NC)
                          ? sym[a->sym].got : S + (uint64_t)a->addend;
        int e = apply_one(w, P, a->type, SA);
        if (e) return (int)(i + 1) * 16 + e;
    }
    return 0;
}

/* ============================== 테스트 ============================== */
static uint64_t g_rng = 0x9E3779B97F4A7C15ull;
static uint64_t rnd(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 7; g_rng ^= g_rng << 17; return g_rng; }

typedef struct { const char *what; uint32_t type; uint64_t P, SA; uint32_t orig, linked; int err; } golden;
/* 실제 링커가 만든 워드 (demo_elf.sh / demo_mac.sh 출력에서 옮김; 주소는 파일 안 가상 주소) */
static const golden k_golden[] = {
    /* ld.bfd 2.46, PIE: use_all = 0x8a8 */
    {"bfd adrp g_val",      R_ADR_PREL_PG_HI21, 0x8b0, 0x20010, 0x90000009, 0x90000109, 0},
    {"bfd add  g_val",      R_ADD_ABS_LO12_NC,  0x8b4, 0x20010, 0x91000129, 0x91004129, 0},
    {"bfd ldr  g_tab+8",    R_LDST64,           0x8c4, 0x20020, 0xf940012a, 0xf940112a, 0},
    {"bfd ldrh g_half",     R_LDST16,           0x8d0, 0x20030, 0x7940012a, 0x7940612a, 0},
    {"bfd adrp got",        R_ADR_GOT_PAGE,     0x8d8, 0x1ffc0, 0x90000009, 0xf00000e9, 0},
    {"bfd ldr  got",        R_LD64_GOT_LO12_NC, 0x8dc, 0x1ffc0, 0xf9400129, 0xf947e129, 0},
    {"bfd bl   ext_func",   R_CALL26,           0x8e8, 0x900,   0x94000000, 0x94000006, 0},
    {"bfd b    ext_tail",   R_JUMP26,           0x8f0, 0x908,   0x14000000, 0x14000006, 0},
    /* ld.bfd static (non-PIE): use_all = 0x4007c0, GOT 칸 0x49fec0 */
    {"static adrp got",     R_ADR_GOT_PAGE,     0x4007f0, 0x49fec0, 0x90000009, 0xf00004e9, 0},
    {"static ldr  got",     R_LD64_GOT_LO12_NC, 0x4007f4, 0x49fec0, 0xf9400129, 0xf9476129, 0},
    /* demo_range.S + 링커 스크립트: 경계 */
    {"adrp +4GB-4KB",       R_ADR_PREL_PG_HI21, 0x10000, 0x10000F000, 0x90000000, 0xf07fffe0, 0},
    {"adrp +4GB",           R_ADR_PREL_PG_HI21, 0x10000, 0x100010000, 0x90000000, 0x90000000, RELOC_RANGE},
    {"bl  +128MB-0x0c",     R_CALL26,           0x10008, 0x8000ffc,   0x94000000, 0x95ffc3fd, 0},
    {"bl  +128MB",          R_CALL26,           0x10008, 0x8010008,   0x94000000, 0x94000000, RELOC_RANGE},
    /* Mach-O (ld-1266.8): demo_dylib_app.S 링크 결과 (demo_mac.txt 3절). GOT 칸 0x100004000 = _sensor_gain */
    {"macho bl stub",       R_CALL26,  0x1000005bc, 0x1000005f0, 0x94000000, 0x9400000d, 0},
    {"macho adrp got",      R_ADR_GOT_PAGE, 0x1000005c0, 0x100004000, 0x90000008, 0x90000028, 0},
    {"macho ldr got",       R_LD64_GOT_LO12_NC, 0x1000005c4, 0x100004000, 0xf9400108, 0xf9400108, 0},
    {"macho adrp page",     R_ADR_PREL_PG_HI21, 0x1000005d0, 0x100008000, 0x90000009, 0x90000049, 0},
    /* demo_misalign_mac.S (demo_mac.txt 6절): 새 ld64 는 0xf9400100 을 조용히 썼다 (4/8 → 0). 우리는 거부해야 한다 */
    {"macho misaligned",    R_LDST64,  0x100000548, 0x100008004, 0xf9400100, 0xf9400100, RELOC_ALIGN},
};

static void test_golden(void) {
    for (size_t i = 0; i < sizeof k_golden / sizeof k_golden[0]; i++) {
        const golden *g = &k_golden[i];
        uint32_t w = g->orig;
        int e = apply_one(&w, g->P, g->type, g->SA);
        int ok = (e == g->err) && (e != RELOC_OK || w == g->linked);
        if (!ok) printf("  golden[%zu] %-18s err=%d word=%08x (want err=%d word=%08x)\n",
                        i, g->what, e, w, g->err, g->linked);
        CHECK(ok);
    }
    /* 디코더가 objdump 가 찍은 대상과 같은 값을 내는가 (같은 실측 줄) */
    CHECK(decode_adrp(0xf07fffe0, 0x10000) == 0x10000F000);       /* adrp x0, 10000f000 */
    CHECK(decode_adrp(0xf00000e9, 0x8d8) == 0x1f000);              /* adrp x9, 1f000 */
    CHECK(decode_lo12(0xf947e129) == 4032);                        /* ldr x9, [x9, #4032] */
    CHECK(decode_lo12(0x7940612a) == 48);                          /* ldrh w10, [x9, #48] */
    CHECK(decode_lo12(0x91004129) == 0x10);                        /* add x9, x9, #0x10 */
    CHECK(decode_call26(0x95ffc3fd, 0x10008) == 0x8000ffc);        /* bl 8000ffc */
    CHECK(decode_call26(0x15fffffe, 0x10010) == 0x8010008);        /* lld 썽크의 b 8010008 */
    CHECK(decode_call26(0x14000000, 0x1000c) == 0x1000c);          /* b . */
}

static void test_roundtrip_and_edges(void) {
    CHECK(is_ldst_uimm(0xF9400000u) && !is_ldst_uimm(0x91000000u) && sext(0x100000, 21) == -(1 << 20));  /* 도우미 */
    int bad = 0;
    for (int i = 0; i < 20000; i++) {                               /* 무작위 왕복 */
        uint64_t P = (rnd() & 0x0000FFFFFFFFFFFCull) | 0x100000000ull;
        int64_t d = (int64_t)(rnd() % 0x1FFFFE000ull) - 0xFFFFF000ll;   /* (−4 GB+4 KB, 4 GB−4 KB) */
        uint64_t SA = P + (uint64_t)d;
        uint32_t rd = (uint32_t)(rnd() & 31), w = 0x90000000u | rd;
        if (apply_adrp(&w, P, SA) != RELOC_OK || decode_adrp(w, P) != page_of(SA) || (w & 31) != rd) bad++;
        uint32_t rr = (uint32_t)(rnd() & 0x3FF), a = 0x91000000u | rr;   /* add Xd, Xn, #0 (레지스터 무작위) */
        if (apply_add_lo12(&a, SA) != RELOC_OK || decode_lo12(a) != (SA & 0xFFF) || (a & 0x3FF) != rr) bad++;
        if (decode_adrp(w, P) + decode_lo12(a) != SA) bad++;
        unsigned sz = 1u << (rnd() % 4);
        static const uint32_t ld[4] = {0x39400000u, 0x79400000u, 0xB9400000u, 0xF9400000u};  /* ldrb/h/w/x */
        uint32_t l = ld[__builtin_ctz(sz)] | (uint32_t)(rnd() & 0x3FF);
        uint64_t al = SA & ~(uint64_t)(sz - 1);
        if (apply_ldst_lo12(&l, al, sz) != RELOC_OK || decode_lo12(l) != (al & 0xFFF)) bad++;
        int64_t bo = ((int64_t)(rnd() % (1u << 26)) - (1 << 25)) * 4;
        uint32_t b = (rnd() & 1) ? 0x94000000u : 0x14000000u;
        if (apply_call26(&b, P, P + (uint64_t)bo) != RELOC_OK || decode_call26(b, P) != P + (uint64_t)bo) bad++;
    }
    CHECK(bad == 0);
    /* 범위 경계 */
    uint32_t w = 0x90000000u;
    CHECK(apply_adrp(&w, 0x100000000ull, 0x100000000ull + 0xFFFFF000ull) == RELOC_OK);
    CHECK(apply_adrp(&w, 0x100000000ull, 0x100000000ull - 0x100000000ull) == RELOC_OK);   /* −4 GB 정확히 */
    CHECK(decode_adrp(w, 0x100000000ull) == 0);
    CHECK(apply_adrp(&w, 0x200000000ull, 0x200000000ull - 0x100001000ull) == RELOC_RANGE);
    CHECK(apply_adrp(&w, 0x10FFF, 0x10000 + 0xFFFFFFFFull) == RELOC_OK);  /* 같은 페이지 안 위치는 무관 */
    uint32_t b = 0x94000000u;
    CHECK(apply_call26(&b, 0x8000000, 0x8000000 + 0x7FFFFFC) == RELOC_OK && b == 0x95FFFFFFu);
    CHECK(apply_call26(&b, 0x8000000, 0) == RELOC_OK && b == 0x96000000u);              /* −128 MB 정확히 */
    CHECK(apply_call26(&b, 0x8000004, 0) == RELOC_RANGE);
    CHECK(apply_call26(&b, 0x1000, 0x1002) == RELOC_ALIGN);
    /* 정렬과 명령 종류 */
    uint32_t l = 0xF9400000u;
    CHECK(apply_ldst_lo12(&l, 0x1008, 8) == RELOC_OK && l == 0xF9400400u);
    CHECK(apply_ldst_lo12(&l, 0x100C, 8) == RELOC_ALIGN);
    uint32_t q = 0x3DC00000u;                                       /* ldr q0, [x0] (128비트) */
    CHECK(ldst_size(q) == 16 && apply_ldst_lo12(&q, 0xFF0, 16) == RELOC_OK && decode_lo12(q) == 0xFF0);
    uint32_t hb = 0x39400000u;                                      /* ldrb: 모든 오프셋 OK */
    CHECK(apply_ldst_lo12(&hb, 0xFFF, 1) == RELOC_OK && decode_lo12(hb) == 0xFFF);
    uint32_t notadrp = 0x10000000u;                                 /* ADR 은 ADRP 가 아니다 */
    CHECK(apply_adrp(&notadrp, 0, 0x1000) == RELOC_BADINSN);
    uint32_t add_sh = 0x91400000u;                                  /* add ..., lsl #12 → 하위 12비트용 아님 */
    CHECK(apply_add_lo12(&add_sh, 0x123) == RELOC_BADINSN);
    uint32_t ldr_w = 0xB9400000u;
    CHECK(apply_ldst_lo12(&ldr_w, 0x10, 8) == RELOC_BADINSN);      /* 타입(8 B)과 명령(4 B)이 다름 */
}

/* ---- Part B: 지금 실행 중인 이 프로그램의 명령 워드를 직접 읽는다 ---- */
extern long pic_g, pic_tab[4], *pic_ptr;
long *pic_addr_local(void);
long pic_load_tab2(void);
long *pic_addr_local_got(void);
FILE **pic_addr_ext_got(void);
int pic_call_abs(int v);
extern const uint32_t site_local[], site_tab[], site_local_got[], site_ext_got[], site_call[];
#ifdef __APPLE__
extern FILE *__stdoutp;
#define EXT_DATA_ADDR ((uintptr_t)&__stdoutp)
static int in_sect(uint64_t a, size_t len, const char *seg, const char *sect) {
    unsigned long sz = 0;
    uint8_t *p = getsectiondata(&_mh_execute_header, seg, sect, &sz);
    return p && a >= (uintptr_t)p && a + len <= (uintptr_t)p + sz;
}
#define IN_GOT(a)   in_sect((a), 8, "__DATA_CONST", "__got")
#define IN_STUBS(a) in_sect((a), 12, "__TEXT", "__stubs")
#else
#define EXT_DATA_ADDR ((uintptr_t)&stdout)
extern char __executable_start[], _end[];
static int in_image(uint64_t a, size_t len) {
    return a >= (uintptr_t)__executable_start && a + len <= (uintptr_t)_end;
}
#define IN_GOT(a)   in_image((a), 8)
#define IN_STUBS(a) in_image((a), 16)
#endif

static void test_live_image(int verbose) {
    /* 1) ADRP+ADD: 같은 이미지 안 전역 */
    uint64_t P = (uintptr_t)site_local;
    CHECK(pic_addr_local() == &pic_g);
    CHECK(is_adrp(site_local[0]) && is_add_imm(site_local[1]));
    CHECK(decode_adrp(site_local[0], P) + decode_lo12(site_local[1]) == (uintptr_t)&pic_g);
    /* 2) ADRP+LDR, 덧셈 16 → 8 로 나눈 imm12 */
    CHECK(pic_load_tab2() == 30);
    CHECK(ldst_size(site_tab[1]) == 8);
    CHECK(decode_adrp(site_tab[0], (uintptr_t)site_tab) + decode_lo12(site_tab[1]) == (uintptr_t)&pic_tab[2]);
    /* 3) 같은 이미지 심볼을 GOT 로 요청 → 링커가 바꿨나? */
    P = (uintptr_t)site_local_got;
    uint64_t page = decode_adrp(site_local_got[0], P), addr = 0;
    int relaxed = is_add_imm(site_local_got[1]);
    if (relaxed) addr = page + decode_lo12(site_local_got[1]);
    else if (is_ldr_x_uimm(site_local_got[1]) && IN_GOT(page + decode_lo12(site_local_got[1])))
        addr = *(const uint64_t *)(uintptr_t)(page + decode_lo12(site_local_got[1]));
    CHECK(addr == (uintptr_t)&pic_g && pic_addr_local_got() == &pic_g);
    if (verbose) printf("info: local symbol via GOT -> linker %s (word[1]=%08x)\n",
                        relaxed ? "relaxed LDR to ADD (no GOT slot used)" : "kept the GOT load", site_local_got[1]);
    /* 4) 다른 이미지 데이터 → GOT 칸 (완화 불가) */
    P = (uintptr_t)site_ext_got;
    uint64_t slot = decode_adrp(site_ext_got[0], P) + decode_lo12(site_ext_got[1]);
    CHECK(is_ldr_x_uimm(site_ext_got[1]));
    CHECK(IN_GOT(slot) && *(const uint64_t *)(uintptr_t)slot == EXT_DATA_ADDR);
    CHECK(pic_addr_ext_got() == (FILE **)EXT_DATA_ADDR);
    /* 5) 다른 이미지 함수 → B 는 stub/PLT 로, stub 은 GOT 칸을 읽어 점프 */
    CHECK(pic_call_abs(-5) == 5);                                   /* 먼저 한 번 불러 둔다 (Linux 지연 바인딩) */
    P = (uintptr_t)site_call;
    uint64_t stub = decode_call26(site_call[0], P);
    CHECK(is_b_or_bl(site_call[0]) && stub != (uintptr_t)&abs);
    int stub_ok = IN_STUBS(stub);                                   /* 디코딩이 틀리면 여기서 멈춘다 (역참조 금지) */
    CHECK(stub_ok);
    const uint32_t *s = stub_ok ? (const uint32_t *)(uintptr_t)stub : site_call;
    uint64_t gslot = stub_ok ? decode_adrp(s[0], stub) + decode_lo12(s[1]) : 0;
    CHECK(stub_ok && is_adrp(s[0]) && is_ldr_x_uimm(s[1]));
    CHECK(stub_ok && IN_GOT(gslot) && *(const uint64_t *)(uintptr_t)gslot == (uintptr_t)&abs);
    if (verbose && stub_ok)
        printf("info: b abs -> stub %+" PRId64 " B from site, stub reads GOT slot %+" PRId64 " B from stub\n",
               (int64_t)(stub - P), (int64_t)(gslot - stub));
    /* 6) 데이터 안의 포인터 (rebase / RELATIVE 가 로드 시 고쳐 놓음) */
    CHECK(pic_ptr == &pic_g);
}

/* ---- Part C: 미니 링커를 실제 링커 출력과 비교 (demo_elf.sh 실측) ---- */
static const uint32_t k_obj_text[19] = {   /* objdump -d demo_elf.o : 구멍은 전부 0 */
    0xa9bf7bfd, 0x910003fd, 0x90000009, 0x91000129, 0xf940012a, 0x8b0a0000, 0x90000009, 0xf940012a,
    0x8b0a0000, 0x90000009, 0x7940012a, 0x8b0a0000, 0x90000009, 0xf9400129, 0xf940012a, 0x8b0a0000,
    0x94000000, 0xa8c17bfd, 0x14000000};
enum { S_G_VAL, S_G_TAB, S_G_HALF, S_EXT_VAL, S_EXT_FUNC, S_EXT_TAIL, NSYM };
static const rela k_rela[10] = {           /* readelf -rW demo_elf.o (.rela.text) */
    {0x08, R_ADR_PREL_PG_HI21, S_G_VAL, 0},   {0x0c, R_ADD_ABS_LO12_NC, S_G_VAL, 0},
    {0x18, R_ADR_PREL_PG_HI21, S_G_TAB, 0},   {0x1c, R_LDST64, S_G_TAB, 8},
    {0x24, R_ADR_PREL_PG_HI21, S_G_HALF, 0},  {0x28, R_LDST16, S_G_HALF, 0},
    {0x30, R_ADR_GOT_PAGE, S_EXT_VAL, 0},     {0x34, R_LD64_GOT_LO12_NC, S_EXT_VAL, 0},
    {0x40, R_CALL26, S_EXT_FUNC, 0},          {0x48, R_JUMP26, S_EXT_TAIL, 0}};
typedef struct { const char *name; uint64_t base; symval sym[NSYM]; int relax; uint32_t linked[19]; } link_case;
static const link_case k_links[4] = {
    {"ld.bfd -pie", 0x8a8, {{0x20010,0,0},{0x20018,0,0},{0x20030,0,0},{0x20040,0x1ffc0,0},{0x900,0,0},{0x908,0,0}}, 0,
     {0xa9bf7bfd,0x910003fd,0x90000109,0x91004129,0xf940012a,0x8b0a0000,0x90000109,0xf940112a,0x8b0a0000,
      0x90000109,0x7940612a,0x8b0a0000,0xf00000e9,0xf947e129,0xf940012a,0x8b0a0000,0x94000006,0xa8c17bfd,0x14000006}},
    {"ld.bfd -static", 0x4007c0, {{0x4a0040,0,0},{0x4a0048,0,0},{0x4a0060,0,0},{0x4a0070,0x49fec0,0},{0x400820,0,0},{0x400828,0,0}}, 0,
     {0xa9bf7bfd,0x910003fd,0x90000509,0x91010129,0xf940012a,0x8b0a0000,0x90000509,0xf940292a,0x8b0a0000,
      0x90000509,0x7940c12a,0x8b0a0000,0xf00004e9,0xf9476129,0xf940012a,0x8b0a0000,0x94000008,0xa8c17bfd,0x14000008}},
    {"ld.lld --no-relax", 0x10974, {{0x30cd0,0,0},{0x30cd8,0,0},{0x30cf0,0,0},{0x30d00,0x20c78,0},{0x109c0,0,0},{0x109c8,0,0}}, 0,
     {0xa9bf7bfd,0x910003fd,0x90000109,0x91334129,0xf940012a,0x8b0a0000,0x90000109,0xf946712a,0x8b0a0000,
      0x90000109,0x7959e12a,0x8b0a0000,0x90000089,0xf9463d29,0xf940012a,0x8b0a0000,0x94000003,0xa8c17bfd,0x14000003}},
    {"ld.lld (relax)", 0x10974, {{0x30cd0,0,0},{0x30cd8,0,0},{0x30cf0,0,0},{0x30d00,0x20c78,0},{0x109c0,0,0},{0x109c8,0,0}}, 1,
     {0xa9bf7bfd,0x910003fd,0xd503201f,0x10101a89,0xf940012a,0x8b0a0000,0x90000109,0xf946712a,0x8b0a0000,
      0x90000109,0x7959e12a,0x8b0a0000,0xd503201f,0x10101ac9,0xf940012a,0x8b0a0000,0x94000003,0xa8c17bfd,0x14000003}},
};

static void test_mini_linker(int verbose) {
    for (int c = 0; c < 4; c++) {
        uint32_t t[19];
        memcpy(t, k_obj_text, sizeof t);
        int e = link_text(t, 19, k_links[c].base, k_rela, 10, k_links[c].sym, k_links[c].relax);
        int diff = 0;
        for (int i = 0; i < 19; i++) diff += t[i] != k_links[c].linked[i];
        if (verbose || e || diff) printf("info: mini-linker vs %-18s err=%d  words differing=%d/19\n", k_links[c].name, e, diff);
        CHECK(e == 0 && diff == 0);
    }
    /* 선점 가능(preemptible) 심볼은 relax=1 이어도 GOT 를 그대로 써야 한다 */
    symval s[NSYM];
    memcpy(s, k_links[3].sym, sizeof s);
    s[S_EXT_VAL].preemptible = 1;
    uint32_t t[19];
    memcpy(t, k_obj_text, sizeof t);
    CHECK(link_text(t, 19, 0x10974, k_rela, 10, s, 1) == 0);
    CHECK(t[12] == 0x90000089 && t[13] == 0xf9463d29 && t[2] == 0xd503201f && is_adr(t[3]));   /* GOT 유지, g_val 은 ADR */
    /* ADR 범위(±1 MB) 밖이면 ADRP+ADD 로 남는다 */
    memcpy(s, k_links[3].sym, sizeof s);
    s[S_G_VAL].va = 0x10974 + 0x200000;                              /* 2 MB 떨어진 곳 */
    memcpy(t, k_obj_text, sizeof t);
    CHECK(link_text(t, 19, 0x10974, k_rela, 10, s, 1) == 0 && is_adrp(t[2]) && is_add_imm(t[3]));
    CHECK(decode_adrp(t[2], 0x1097c) + decode_lo12(t[3]) == 0x10974 + 0x200000);
    /* 범위 에러가 엔트리 번호와 함께 보고된다: ext_func 를 200 MB 밖으로 */
    memcpy(s, k_links[0].sym, sizeof s);
    s[S_EXT_FUNC].va = 0x8a8 + 200u * 1024 * 1024;
    memcpy(t, k_obj_text, sizeof t);
    CHECK(link_text(t, 19, 0x8a8, k_rela, 10, s, 0) == 9 * 16 + RELOC_RANGE);
    /* 하나짜리 완화 함수들 */
    uint32_t add = 0, out[2] = {0, 0};
    CHECK(relax_got_ldr_to_add(0xf9400000, 0x100008000, &add) == RELOC_OK && add == 0x91000000);  /* ld64 실측 */
    CHECK(relax_got_ldr_to_add(0xb9400000, 0x1000, &add) == RELOC_BADINSN);                      /* 32비트 LDR */
    CHECK(relax_adrp_to_adr(0x90000109, 0x91334129, 0x10980, 0x30cd0, out) == RELOC_OK &&
          out[0] == 0xd503201f && out[1] == 0x10101a89);                                         /* lld 실측 */
    CHECK(relax_adrp_to_adr(0x90000109, 0x91334128, 0x10980, 0x30cd0, out) == RELOC_BADINSN);  /* Rd 다름 */
    CHECK(relax_adrp_to_adr(0x90000109, 0x91334129, 0x10980, 0x10980 + 0x100000, out) == RELOC_RANGE);
    CHECK(relax_adrp_to_adr(0x90000109, 0x91334129, 0x10980, 0x10980 - 0x100000, out) == RELOC_OK);
}

/* ---- tool 모드: check_reloc.sh 가 실제 objdump/readelf/dyld_info 에서 뽑은 줄을 검증 ---- */
static int apply_named(const char *t, uint32_t *w, uint64_t P, uint64_t SA) {
    if (!strcmp(t, "PAGE21") || !strcmp(t, "GOT_LOAD_PAGE21") || !strcmp(t, "ADR_PREL_PG_HI21") ||
        !strcmp(t, "ADR_GOT_PAGE")) return apply_adrp(w, P, SA);
    if (!strcmp(t, "ADD_ABS_LO12_NC")) return apply_add_lo12(w, SA);
    if (!strcmp(t, "PAGEOFF12"))                                   /* Mach-O: 명령을 보고 판단 */
        return is_add_imm(*w) ? apply_add_lo12(w, SA) : apply_ldst_lo12(w, SA, ldst_size(*w));
    if (!strcmp(t, "GOT_LOAD_PAGEOFF12") || !strcmp(t, "LD64_GOT_LO12_NC") || !strcmp(t, "LDST64_ABS_LO12_NC"))
        return apply_ldst_lo12(w, SA, 8);
    if (!strcmp(t, "LDST8_ABS_LO12_NC"))  return apply_ldst_lo12(w, SA, 1);
    if (!strcmp(t, "LDST16_ABS_LO12_NC")) return apply_ldst_lo12(w, SA, 2);
    if (!strcmp(t, "LDST32_ABS_LO12_NC")) return apply_ldst_lo12(w, SA, 4);
    if (!strcmp(t, "BRANCH26") || !strcmp(t, "CALL26") || !strcmp(t, "JUMP26")) return apply_call26(w, P, SA);
    if (!strcmp(t, "RELAX_ADD")) return relax_got_ldr_to_add(*w, SA, w);   /* ld64/lld 가 GOT LDR 을 ADD 로 */
    return RELOC_BADINSN;
}
static int tool_main(void) {
    char line[512], kind[8], t[40];
    int pass = 0, total = 0;
    while (fgets(line, sizeof line, stdin)) {
        uint64_t P, x, y, z, e0, e1;
        uint32_t w;
        int ok = 0;
        line[strcspn(line, "\n")] = 0;
        if (line[0] == '#' || line[0] == 0) continue;
        if (sscanf(line, "%7s", kind) != 1) continue;
        if (!strcmp(kind, "D") && sscanf(line, "D %39s %" SCNx64 " %" SCNx64 " %" SCNx64, t, &P, &x, &y) == 4) {
            w = (uint32_t)x;
            uint64_t got = !strcmp(t, "ADRP") ? decode_adrp(w, P) : !strcmp(t, "LO12") ? decode_lo12(w)
                         : !strcmp(t, "B26") ? decode_call26(w, P) : ~(uint64_t)0;
            ok = got == y;
            if (!ok) printf("  decoded 0x%" PRIx64 "\n", got);
        } else if (!strcmp(kind, "A") &&
                   sscanf(line, "A %39s %" SCNx64 " %" SCNx64 " %" SCNx64 " %" SCNx64, t, &P, &x, &y, &z) == 5) {
            w = (uint32_t)y;
            int e = apply_named(t, &w, P, x);
            ok = e == RELOC_OK && w == (uint32_t)z;
            if (!ok) printf("  applied err=%d word=%08x\n", e, w);
        } else if (!strcmp(kind, "X") && sscanf(line, "X %" SCNx64 " %" SCNx64 " %" SCNx64 " %" SCNx64 " %" SCNx64
                                                 " %" SCNx64, &P, &x, &y, &z, &e0, &e1) == 6) {
            uint32_t w0 = (uint32_t)y, w1 = (uint32_t)z, out[2] = {0, 0};   /* P = ADRP 주소, x = S */
            if (is_ldr_x_uimm(w1)) relax_got_ldr_to_add(w1, x, &w1);
            int e = apply_adrp(&w0, P, x);
            if (!e) e = apply_add_lo12(&w1, x);
            if (!e) e = relax_adrp_to_adr(w0, w1, P + 4, x, out);
            ok = e == RELOC_OK && out[0] == (uint32_t)e0 && out[1] == (uint32_t)e1;
            if (!ok) printf("  relaxed err=%d words=%08x %08x\n", e, out[0], out[1]);
        } else { printf("BAD LINE %s\n", line); total++; continue; }
        total++;
        pass += ok;
        printf("%s %s\n", ok ? "PASS" : "FAIL", line);
    }
    printf("%d/%d passed\n", pass, total);
    return pass == total && total > 0 ? 0 : 1;
}

int main(int argc, char **argv) {
    if (argc > 1 && !strcmp(argv[1], "tool")) return tool_main();
    test_golden();
    test_roundtrip_and_edges();
    test_live_image(1);
    test_mini_linker(1);
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
