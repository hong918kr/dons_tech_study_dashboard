/* ARM-B03 추가 연습 — 한 번에 검증
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c practice.S shared_ref.S -o pr && ./pr
 * 1) w 레지스터(sf=0) MOVZ/MOVK/MOVN 인코딩 + 32비트 계획   2) AND/ORR/EOR/ANDS #imm 전체 인코더
 * 3) ORR 즉시값 + MOVK 까지 고려한 상수 비용                  4) asm: mov 별칭이 ORR 로 바뀌는 것 확인
 * 5) GENMASK(h, l) 연속 마스크는 (전부 1 만 빼고) 항상 논리 즉시값
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

enum { OP_MOVZ = 1, OP_MOVK = 2, OP_MOVN = 3 };
typedef struct { uint8_t op, hw; uint16_t imm16; } insn;

/* ---------- 본문 정답 (복사) ---------- */
static unsigned popcount64(uint64_t x) { unsigned c = 0; while (x) { x &= x - 1; c++; } return c; }
static uint64_t ror_e(uint64_t x, unsigned r, unsigned e) {
    uint64_t m = (e == 64) ? ~0ull : ((1ull << e) - 1);
    x &= m; r %= e;
    return r ? (((x >> r) | (x << (e - r))) & m) : x;
}
static bool is_logical_imm64(uint64_t v, unsigned *N, unsigned *immr, unsigned *imms) {
    if (v == 0 || v == ~0ull) return false;
    unsigned e = 64;
    while (e > 2) {
        unsigned h = e / 2; uint64_t m = (1ull << h) - 1;
        if ((v & m) != ((v >> h) & m)) break;
        e = h;
    }
    uint64_t elem = (e == 64) ? v : (v & ((1ull << e) - 1));
    unsigned s = popcount64(elem);
    uint64_t run = (1ull << s) - 1;
    for (unsigned r = 0; r < e; r++)
        if (ror_e(run, r, e) == elem) {
            *N = (e == 64); *immr = r; *imms = ((~(e - 1) << 1) & 0x3Fu) | (s - 1);
            return true;
        }
    return false;
}
static bool is_logical_imm32(uint32_t v, unsigned *N, unsigned *immr, unsigned *imms) {
    return is_logical_imm64((uint64_t)v << 32 | v, N, immr, imms) && *N == 0;
}
static int movz_movk_plan(uint64_t v, insn out[4]) {
    unsigned zeros = 0, ones = 0;
    for (unsigned i = 0; i < 4; i++) { uint16_t h = (uint16_t)(v >> (16 * i)); zeros += (h == 0); ones += (h == 0xFFFF); }
    bool use_n = ones > zeros; uint16_t skip = use_n ? 0xFFFFu : 0; int n = 0;
    for (unsigned i = 0; i < 4; i++) {
        uint16_t h = (uint16_t)(v >> (16 * i));
        if (h == skip) continue;
        out[n] = n == 0 ? (insn){ use_n ? OP_MOVN : OP_MOVZ, (uint8_t)i, use_n ? (uint16_t)~h : h }
                        : (insn){ OP_MOVK, (uint8_t)i, h };
        n++;
    }
    if (n == 0) out[n++] = (insn){ use_n ? OP_MOVN : OP_MOVZ, 0, 0 };
    return n;
}

/* ---------- 연습 1: w 레지스터 wide move ---------- */
static uint32_t enc_mov_wide32(int op, unsigned rd, uint16_t imm16, unsigned hw) {
    uint32_t opc = op == OP_MOVN ? 0u : op == OP_MOVZ ? 2u : 3u;
    return opc << 29 | 0x12800000u | (hw & 1u) << 21 | (uint32_t)imm16 << 5 | (rd & 31u);  /* sf=0, hw 0..1 */
}
static int movz_movk_plan32(uint32_t v, insn out[2]) {
    uint16_t lo = (uint16_t)v, hi = (uint16_t)(v >> 16);
    if (hi == 0)       { out[0] = (insn){ OP_MOVZ, 0, lo }; return 1; }   /* v == 0 도 여기 */
    if (lo == 0)       { out[0] = (insn){ OP_MOVZ, 1, hi }; return 1; }
    if (hi == 0xFFFF)  { out[0] = (insn){ OP_MOVN, 0, (uint16_t)~lo }; return 1; }
    if (lo == 0xFFFF)  { out[0] = (insn){ OP_MOVN, 1, (uint16_t)~hi }; return 1; }
    out[0] = (insn){ OP_MOVZ, 0, lo }; out[1] = (insn){ OP_MOVK, 1, hi };
    return 2;
}
static uint32_t exec32(const insn *p, int n) {   /* w 레지스터 쓰기: 위 32비트는 0 이 되지만 여기선 32비트만 */
    uint32_t x = 0;
    for (int i = 0; i < n; i++) {
        unsigned sh = 16u * p[i].hw;
        if (p[i].op == OP_MOVZ) x = (uint32_t)p[i].imm16 << sh;
        else if (p[i].op == OP_MOVN) x = ~((uint32_t)p[i].imm16 << sh);
        else x = (x & ~(0xFFFFu << sh)) | (uint32_t)p[i].imm16 << sh;
    }
    return x;
}

/* ---------- 연습 2: 논리 즉시값 명령 전체 인코더 ---------- */
enum { L_AND = 0, L_ORR = 1, L_EOR = 2, L_ANDS = 3 };
/* sf | opc(2) | 100100 | N | immr(6) | imms(6) | Rn | Rd */
static uint32_t enc_logical_imm(int opc, unsigned sf, unsigned rd, unsigned rn, unsigned N, unsigned immr, unsigned imms) {
    return (sf & 1u) << 31 | (uint32_t)opc << 29 | 0x12000000u | N << 22 | immr << 16 | imms << 10 | rn << 5 | rd;
}
static bool assemble_logical(int opc, unsigned sf, unsigned rd, unsigned rn, uint64_t v, uint32_t *w) {
    unsigned N, r, s;
    bool ok = sf ? is_logical_imm64(v, &N, &r, &s) : is_logical_imm32((uint32_t)v, &N, &r, &s);
    if (ok) *w = enc_logical_imm(opc, sf, rd, rn, N, r, s);
    return ok;   /* false = 어셈블러가 "expected compatible register or logical immediate" 를 낼 자리 */
}

/* ---------- 연습 3: ORR 즉시값 + MOVK 까지 고려한 64비트 상수 비용 ---------- */
static uint64_t g_pat[6000]; static int g_npat;
static void build_patterns(void) {   /* 모든 (e, s, r) 로 5334 개 패턴 생성 (DecodeBitMasks 정규형과 같은 집합) */
    for (unsigned e = 2; e <= 64; e *= 2)
        for (unsigned s = 1; s < e; s++)
            for (unsigned r = 0; r < e; r++) {
                uint64_t el = ror_e((1ull << s) - 1, r, e), v = 0;
                for (unsigned i = 0; i < 64; i += e) v |= el << i;
                g_pat[g_npat++] = v;
            }
}
static int best_mov_cost64(uint64_t v) {
    unsigned N, r, s; insn p[4];
    if (is_logical_imm64(v, &N, &r, &s)) return 1;                 /* mov = orr xd, xzr, #imm */
    int best = movz_movk_plan(v, p);
    if (best <= 2) return best;
    for (int i = 0; i < g_npat; i++) {                              /* orr #pat ; movk 한 개 */
        uint64_t d = g_pat[i] ^ v; int diff = 0;
        for (int k = 0; k < 4; k++) diff += ((d >> (16 * k)) & 0xFFFF) != 0;
        if (diff == 1) return 2;
    }
    return best;
}

/* ---------- asm / 어셈블러 참조 ---------- */
extern uint64_t k_5555(void);
extern uint64_t k_00ff_1234(void);
extern const uint32_t ref_movs32[], ref_movs32_end[], ref_logic[], ref_logic_end[];

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
static uint64_t g_rng = 0x2545F4914F6CDD1Dull;
static uint64_t xorshift64(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 7; g_rng ^= g_rng << 17; return g_rng; }

int main(void) {
    unsigned N, r, s;
    build_patterns();
    CHECK(g_npat == 5334);

    /* 연습 1 */
    CHECK(ref_movs32_end - ref_movs32 == 4);
    CHECK(enc_mov_wide32(OP_MOVZ, 0, 0xBEEF, 0) == ref_movs32[0]);
    CHECK(enc_mov_wide32(OP_MOVK, 0, 0xDEAD, 1) == ref_movs32[1]);
    CHECK(enc_mov_wide32(OP_MOVN, 7, 0x5678, 0) == ref_movs32[2]);
    CHECK(enc_mov_wide32(OP_MOVZ, 29, 0x0900, 1) == ref_movs32[3]);
    int bad = 0;
    for (int i = 0; i < 100000; i++) {
        uint32_t v = (uint32_t)xorshift64();
        if (i % 4 == 1) v &= 0xFFFF0000u; else if (i % 4 == 2) v |= 0xFFFF0000u; else if (i % 4 == 3) v |= 0xFFFFu;
        insn p[2]; int n = movz_movk_plan32(v, p);
        if (exec32(p, n) != v || n != (((uint16_t)v == 0 || (uint16_t)(v >> 16) == 0 ||
                                        (uint16_t)v == 0xFFFF || (uint16_t)(v >> 16) == 0xFFFF) ? 1 : 2)) bad++;
    }
    CHECK(bad == 0);
    { insn p[2]; CHECK(movz_movk_plan32(0xFFFFFFFFu, p) == 1 && p[0].op == OP_MOVN && exec32(p, 1) == 0xFFFFFFFFu); }

    /* 연습 2: shared_ref.S 의 13개 논리 즉시값 명령을 비트 단위로 똑같이 */
    static const struct { int opc; unsigned sf, rd, rn; uint64_t v; } lg[] = {
        {L_AND, 1, 0, 1, 0xFF00}, {L_ORR, 1, 2, 3, 0x00FF00FF00FF00FFull}, {L_EOR, 1, 4, 5, 0x5555555555555555ull},
        {L_ANDS, 1, 6, 7, 0x8000000000000001ull}, {L_AND, 1, 8, 9, 0xFFFFFFFFFFFFFFFEull},
        {L_AND, 1, 10, 11, 0x0000000100000001ull}, {L_ORR, 1, 12, 13, 0x7FC0}, {L_AND, 1, 14, 15, 0xFFFFFFFF00000000ull},
        {L_AND, 1, 16, 17, 0x0F0F0F0F0F0F0F0Full}, {L_AND, 0, 0, 1, 0xFF00}, {L_ORR, 0, 2, 3, 0x80000001u},
        {L_AND, 0, 4, 5, 0xFFFF803Fu}, {L_EOR, 0, 6, 7, 0x0F0F0F0Fu},
    };
    int nlg = (int)(sizeof lg / sizeof lg[0]);
    CHECK(ref_logic_end - ref_logic == nlg);
    bad = 0;
    for (int i = 0; i < nlg; i++) {
        uint32_t w = 0;
        if (!assemble_logical(lg[i].opc, lg[i].sf, lg[i].rd, lg[i].rn, lg[i].v, &w) || w != ref_logic[i]) {
            printf("  logic[%d]: assembler=%08x yours=%08x\n", i, ref_logic[i], w); bad++;
        }
    }
    CHECK(bad == 0);
    { uint32_t w; CHECK(!assemble_logical(L_AND, 1, 0, 0, 0x1234, &w)); }   /* and x0, x0, #0x1234 -> 에러 */

    /* 연습 3: 기댓값 = clang -O2 실제 출력의 명령 수 (demo_consts.c) — 단 c_rep32 는 예외 */
    CHECK(best_mov_cost64(0x5555555555555555ull) == 1);   /* mov x0, #0x5555...  (ORR)     */
    CHECK(best_mov_cost64(0x00FF00FF00FF00FFull) == 1);
    CHECK(best_mov_cost64(0x00FF00FF00FF1234ull) == 2);   /* mov #0xff00ff00ff00ff ; movk  */
    CHECK(best_mov_cost64(0xDEADBEEFull) == 2);
    CHECK(best_mov_cost64(0xFFFFFFFF12345678ull) == 2);
    CHECK(best_mov_cost64(0xCAFEF00DDEADBEEFull) == 4);
    CHECK(best_mov_cost64(0x1234567812345678ull) == 4);   /* clang 은 3개: orr x0,x0,x0,lsl #32 트릭 (이 모델 밖) */
    bad = 0;
    for (int i = 0; i < 2000; i++) {   /* 비용은 항상 MOVZ/MOVK 계획 이하 */
        uint64_t v = xorshift64(); insn p[4];
        if (best_mov_cost64(v) > movz_movk_plan(v, p)) bad++;
    }
    CHECK(bad == 0);

    /* 연습 4: asm — 값 + 첫 워드가 ORR(64-bit, Rn=xzr) 인지 */
    const uint32_t *c1 = (const uint32_t *)(uintptr_t)k_5555, *c2 = (const uint32_t *)(uintptr_t)k_00ff_1234;
    CHECK(k_5555() == 0x5555555555555555ull);
    CHECK((c1[0] & 0xFF8003E0u) == 0xB20003E0u && c1[1] == 0xD65F03C0u);          /* orr ; ret */
    CHECK(k_00ff_1234() == 0x00FF00FF00FF1234ull);
    CHECK((c2[0] & 0xFF8003E0u) == 0xB20003E0u && c2[1] == 0xF2824680u && c2[2] == 0xD65F03C0u);   /* orr ; movk x0,#0x1234 ; ret */

    /* 연습 5: GENMASK(h, l) — 연속 마스크는 전부 1 인 경우만 빼고 논리 즉시값 */
    bad = 0; int cnt64 = 0, cnt32 = 0;
    for (unsigned l = 0; l < 64; l++)
        for (unsigned h = l; h < 64; h++) {
            uint64_t m = (h == 63 ? ~0ull : ((1ull << (h + 1)) - 1)) & ~((1ull << l) - 1);
            bool ok = is_logical_imm64(m, &N, &r, &s); cnt64++;
            if (ok != (m != ~0ull)) bad++;
            if (ok && (N != 1 || s != h - l || r != (64 - l) % 64)) bad++;    /* imms = 길이-1, immr = (64-l) mod 64 */
        }
    for (unsigned l = 0; l < 32; l++)
        for (unsigned h = l; h < 32; h++) {
            uint32_t m = (h == 31 ? 0xFFFFFFFFu : ((1u << (h + 1)) - 1)) & ~((1u << l) - 1);
            cnt32++;
            if (is_logical_imm32(m, &N, &r, &s) != (m != 0xFFFFFFFFu)) bad++;
        }
    CHECK(bad == 0 && cnt64 == 2080 && cnt32 == 528);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
