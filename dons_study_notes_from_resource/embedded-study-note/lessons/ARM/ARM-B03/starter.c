/* ARM-B03 데이터 이동과 즉시값: MOV, MOVZ/MOVK, 논리 즉시값
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 starter.c starter.S shared_ref.S -o st && ./st
 * Part A: 64비트 상수 -> MOVZ/MOVN + MOVK 계획과 기계어 인코딩 (진짜 어셈블러 출력과 대조)
 * Part B: AND/ORR/EOR 논리 즉시값(bitmask immediate) 판정 (Arm ARM DecodeBitMasks 로 전수 대조)
 * Part C: 펌웨어 마스크의 명령 개수 비용 + RCC_PLLCFGR.PLLN 패치 (asm)
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

enum { OP_MOVZ = 1, OP_MOVK = 2, OP_MOVN = 3 };
typedef struct { uint8_t op, hw; uint16_t imm16; } insn;   /* hw = 0..3 -> lsl #(16*hw) */

/* ================= Part A — MOVZ / MOVK / MOVN ================= */
/* 64-bit(sf=1) wide move: sf | opc(2) | 100101 | hw(2) | imm16 | Rd(5) */
static uint32_t enc_movz(unsigned rd, uint16_t imm16, unsigned hw) {
    (void)rd; (void)imm16; (void)hw;
    return 0;   /* TODO: 0xD2800000 | hw<<21 | imm16<<5 | rd */
}
static uint32_t enc_movk(unsigned rd, uint16_t imm16, unsigned hw) {
    (void)rd; (void)imm16; (void)hw;
    return 0;   /* TODO: opc=11 */
}
static uint32_t enc_movn(unsigned rd, uint16_t imm16, unsigned hw) {
    (void)rd; (void)imm16; (void)hw;
    return 0;   /* TODO: opc=00 */
}

/* v 를 만드는 최소 MOVZ/MOVN + MOVK 시퀀스. 반환 = 명령 개수(1..4).
 * 0xffff halfword 가 0 halfword 보다 많으면 MOVN, 아니면(동점 포함) MOVZ.
 * 첫 명령 = 가장 낮은 "손봐야 할" hw, 이후 MOVK 는 hw 오름차순. */
static int movz_movk_plan(uint64_t v, insn out[4]) {
    (void)v; (void)out;
    /* TODO: ① 0x0000 / 0xffff halfword 개수 세기 ② MOVZ 냐 MOVN 이냐 결정
     *       ③ 첫 명령 + MOVK 들을 out[] 에 채우기 ④ v == 0 / v == ~0 처리 */
    return 0;
}

/* ================= Part B — 논리 즉시값 (bitmask immediate) ================= */
static unsigned popcount64(uint64_t x) { unsigned c = 0; while (x) { x &= x - 1; c++; } return c; }

/* e 비트 원소 안에서 오른쪽 회전 (e = 2..64) */
static uint64_t ror_e(uint64_t x, unsigned r, unsigned e) {
    uint64_t m = (e == 64) ? ~0ull : ((1ull << e) - 1);
    x &= m; r %= e;
    if (r == 0) return x;
    return ((x >> r) | (x << (e - r))) & m;
}

/* v 가 AND/ORR/EOR/ANDS(64-bit) #imm 으로 인코딩되면 true + (N, immr, imms).
 * 규칙: v = (e 비트 원소)의 반복, 원소 = s 개의 연속 1 을 오른쪽으로 r 만큼 회전 (1 <= s <= e-1). */
static bool is_logical_imm64(uint64_t v, unsigned *N, unsigned *immr, unsigned *imms) {
    (void)v; (void)N; (void)immr; (void)imms; (void)popcount64;
    /* TODO: ① 0, ~0 거르기 ② 가장 작은 주기 e ③ 원소의 1 개수 s
     *       ④ ones(s) 를 회전해서 원소가 되는 r 찾기 ⑤ N / immr / imms 채우기 */
    return false;
}

/* 32-bit (w 레지스터) 판: 32비트 값을 두 번 이어 붙인 64비트가 e <= 32 로 인코딩되면 OK (N=0) */
static bool is_logical_imm32(uint32_t v, unsigned *N, unsigned *immr, unsigned *imms) {
    (void)v; (void)N; (void)immr; (void)imms;
    return false;   /* TODO: 32비트 값을 두 번 이어 붙여 64비트 판정을 재사용 (N 은 0 이어야) */
}

/* ================= Part C — 마스크 비용 ================= */
/* x & mask 를 계산하는 데 드는 명령 수 (컴파일러가 하는 상수 접기 포함):
 * mask == ~0 -> 0 (그냥 x),  mask == 0 -> 1 (mov x0,#0),  논리 즉시값 -> 1 (and #imm),
 * 그 밖 -> (상수 만들기 MOVZ/MOVN+MOVK) + 1 (and 레지스터) */
static int and_cost64(uint64_t mask) {
    (void)mask;
    return 0;   /* TODO: ~0 -> 0, 0 -> 1, 논리 즉시값 -> 1, 그 밖 -> plan 개수 + 1 */
}
static int and_cost32(uint32_t mask) {
    (void)mask;
    return 0;   /* TODO: w 레지스터 판 (상수 만들기는 최대 2개) */
}

/* ---------------- asm 함수 (solution.S / starter.S) ---------------- */
extern uint64_t k_deadbeef(void);
extern uint64_t k_cafef00d_deadbeef(void);
extern uint64_t k_ffffffff_12345678(void);
extern int64_t  k_minus2(void);
extern uint64_t k_qemu_uart(void);
extern uint64_t k_tie(void);
extern uint32_t pllcfgr_set_plln168(uint32_t r);
/* ---------------- 진짜 어셈블러가 만든 기계어 (shared_ref.S) ---------------- */
extern const uint32_t ref_movs[], ref_movs_end[], ref_logic[], ref_logic_end[];

/* ======================= 테스트 하네스 (starter 와 동일) ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint64_t g_rng = 0x9E3779B97F4A7C15ull;
static uint64_t xorshift64(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 7; g_rng ^= g_rng << 17; return g_rng; }

/* Arm ARM DecodeBitMasks() 를 C 로 옮긴 참조 구현 (wmask 만). M = 32 또는 64. 무효면 false. */
static bool decode_bit_masks(unsigned N, unsigned immr, unsigned imms, unsigned M, uint64_t *wmask) {
    unsigned x = (N << 6) | (~imms & 0x3Fu);          /* immN:NOT(imms) (7 bits) */
    int len = -1;
    for (int b = 6; b >= 0; b--) if (x >> b & 1u) { len = b; break; }   /* HighestSetBit */
    if (len < 1) return false;                        /* UNDEFINED */
    if ((1u << len) > M) return false;                /* 32비트 명령에서 N=1 등 */
    unsigned levels = (1u << len) - 1;
    if ((imms & levels) == levels) return false;      /* 전부 1 인 원소는 예약 */
    unsigned S = imms & levels, R = immr & levels, esize = 1u << len;
    uint64_t welem = (S + 1 == 64) ? ~0ull : ((1ull << (S + 1)) - 1);
    uint64_t rot = ror_e(welem, R, esize), w = 0;
    for (unsigned i = 0; i < M; i += esize) w |= rot << i;   /* Replicate */
    *wmask = (M == 32) ? (w & 0xFFFFFFFFull) : w;
    return true;
}

typedef struct { uint64_t v; unsigned N, immr, imms; } enc_t;
static enc_t g_canon64[6000], g_canon32[2000];
static uint64_t g_set64[6000], g_set32[2000];
static int g_ncanon64, g_ncanon32, g_nset64, g_nset32, g_nvalid64, g_nvalid32;

static int cmp_u64(const void *a, const void *b) {
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}
static int uniq(uint64_t *a, int n) {
    qsort(a, (size_t)n, sizeof a[0], cmp_u64);
    int k = 0;
    for (int i = 0; i < n; i++) if (k == 0 || a[k - 1] != a[i]) a[k++] = a[i];
    return k;
}
static bool in_set(const uint64_t *a, int n, uint64_t v) {
    return bsearch(&v, a, (size_t)n, sizeof v, cmp_u64) != NULL;
}
/* (N, immr, imms) 전체 2*64*64 조합을 열거 -> 유효 조합 수, 서로 다른 값 집합, 정규형(immr < esize) 목록 */
static void build_tables(void) {
    uint64_t tmp64[8192], tmp32[8192];
    int n64 = 0, n32 = 0;
    for (unsigned N = 0; N < 2; N++)
        for (unsigned immr = 0; immr < 64; immr++)
            for (unsigned imms = 0; imms < 64; imms++) {
                uint64_t w;
                unsigned x = (N << 6) | (~imms & 0x3Fu), len = 0;
                for (int b = 6; b >= 0; b--) if (x >> b & 1u) { len = (unsigned)b; break; }
                if (decode_bit_masks(N, immr, imms, 64, &w)) {
                    tmp64[n64++] = w; g_nvalid64++;
                    if (immr < (1u << len)) g_canon64[g_ncanon64++] = (enc_t){ w, N, immr, imms };
                }
                if (decode_bit_masks(N, immr, imms, 32, &w)) {
                    tmp32[n32++] = w; g_nvalid32++;
                    if (immr < (1u << len)) g_canon32[g_ncanon32++] = (enc_t){ w, N, immr, imms };
                }
            }
    g_nset64 = uniq(tmp64, n64); for (int i = 0; i < g_nset64; i++) g_set64[i] = tmp64[i];
    g_nset32 = uniq(tmp32, n32); for (int i = 0; i < g_nset32; i++) g_set32[i] = tmp32[i];
}

/* 계획을 CPU 처럼 실행 + 형식 검사 (첫 명령 MOVZ/MOVN, 나머지 MOVK, hw 서로 다르고 0..3) */
static bool exec_plan(const insn *p, int n, uint64_t *out) {
    if (n < 1 || n > 4) return false;
    uint64_t x = 0; unsigned used = 0;
    for (int i = 0; i < n; i++) {
        unsigned sh = 16u * p[i].hw;
        if (p[i].hw > 3 || (used >> p[i].hw & 1u)) return false;
        used |= 1u << p[i].hw;
        if (i == 0 && p[i].op == OP_MOVZ)      x = (uint64_t)p[i].imm16 << sh;
        else if (i == 0 && p[i].op == OP_MOVN) x = ~((uint64_t)p[i].imm16 << sh);
        else if (i > 0 && p[i].op == OP_MOVK)  x = (x & ~(0xFFFFull << sh)) | (uint64_t)p[i].imm16 << sh;
        else return false;
    }
    *out = x;
    return true;
}
static int min_count(uint64_t v) {   /* 정답 개수 = max(1, 4 - max(#0x0000, #0xffff)) */
    int z = 0, f = 0;
    for (int i = 0; i < 4; i++) { uint16_t h = (uint16_t)(v >> (16 * i)); z += (h == 0); f += (h == 0xFFFF); }
    int k = 4 - (z > f ? z : f);
    return k < 1 ? 1 : k;
}
static bool plan_ok(uint64_t v) {
    insn p[4] = {{0, 0, 0}}; uint64_t got;
    int n = movz_movk_plan(v, p);
    return exec_plan(p, n, &got) && got == v && n == min_count(v);
}
static bool plan_first_is(uint64_t v, int n_expect, int op, unsigned hw, uint16_t imm) {
    insn p[4] = {{0, 0, 0}};
    int n = movz_movk_plan(v, p);
    return n == n_expect && p[0].op == op && p[0].hw == hw && p[0].imm16 == imm;
}

/* 기계어 한 워드 -> 64-bit wide move (Rd 는 x0 이어야 함). RET 이면 0 반환 */
static int decode_wide(uint32_t w, insn *out) {
    if (w == 0xD65F03C0u) return 0;                         /* ret */
    if ((w & 0x9F800000u) != 0x92800000u || (w & 31u) != 0) return -1;   /* sf=1, 100101, Rd=0 */
    static const uint8_t opmap[4] = { OP_MOVN, 0, OP_MOVZ, OP_MOVK };
    unsigned opc = w >> 29 & 3u;
    if (opmap[opc] == 0) return -1;
    *out = (insn){ opmap[opc], (uint8_t)(w >> 21 & 3u), (uint16_t)(w >> 5) };
    return 1;
}
/* asm 함수의 기계어가 plan 과 정확히 같은가 (명령 수, 종류, hw, imm16) */
static bool code_matches_plan(uint64_t (*fn)(void), uint64_t v) {
    const uint32_t *code = (const uint32_t *)(uintptr_t)fn;  /* 코드 영역을 데이터로 읽기 */
    insn want[4] = {{0, 0, 0}}, got;
    int n = movz_movk_plan(v, want), i = 0;
    for (; i < 6; i++) {
        int r = decode_wide(code[i], &got);
        if (r == 0) break;
        if (r < 0 || i >= n) return false;
        if (got.op != want[i].op || got.hw != want[i].hw || got.imm16 != want[i].imm16) return false;
    }
    return i == n;
}

int main(void) {
    build_tables();
    printf("DecodeBitMasks: 64-bit valid=%d distinct=%d canonical=%d | 32-bit valid=%d distinct=%d canonical=%d\n",
           g_nvalid64, g_nset64, g_ncanon64, g_nvalid32, g_nset32, g_ncanon32);

    /* ---- Part A-1: 인코딩 vs 진짜 어셈블러 (shared_ref.S 순서와 같음) ---- */
    static const struct { int op; unsigned rd; uint16_t imm; unsigned hw; } mv[] = {
        {OP_MOVZ, 0, 0x1234, 0}, {OP_MOVZ, 1, 0xBEEF, 1}, {OP_MOVZ, 30, 0xFFFF, 3},
        {OP_MOVK, 2, 0xDEAD, 2}, {OP_MOVK, 9, 0x0000, 0}, {OP_MOVK, 17, 0x8000, 3},
        {OP_MOVN, 3, 0x0000, 0}, {OP_MOVN, 4, 0x0001, 1}, {OP_MOVN, 5, 0xEDCB, 2},
    };
    int nmv = (int)(sizeof mv / sizeof mv[0]);
    CHECK(ref_movs_end - ref_movs == nmv);
    for (int i = 0; i < nmv; i++) {
        uint32_t w = mv[i].op == OP_MOVZ ? enc_movz(mv[i].rd, mv[i].imm, mv[i].hw)
                   : mv[i].op == OP_MOVK ? enc_movk(mv[i].rd, mv[i].imm, mv[i].hw)
                                         : enc_movn(mv[i].rd, mv[i].imm, mv[i].hw);
        if (w != ref_movs[i]) printf("  ref_movs[%d]: assembler=%08x yours=%08x\n", i, ref_movs[i], w);
        CHECK(w == ref_movs[i]);
    }

    /* ---- Part A-2: 계획 (이름 붙은 경계값) ---- */
    CHECK(plan_first_is(0, 1, OP_MOVZ, 0, 0));                         /* movz x0, #0 */
    CHECK(plan_first_is(~0ull, 1, OP_MOVN, 0, 0));                     /* movn x0, #0 */
    CHECK(plan_first_is(0xDEADBEEFull, 2, OP_MOVZ, 0, 0xBEEF));
    CHECK(plan_first_is(0xCAFEF00DDEADBEEFull, 4, OP_MOVZ, 0, 0xBEEF));
    CHECK(plan_first_is(0xFFFFFFFF12345678ull, 2, OP_MOVN, 0, 0xA987));
    CHECK(plan_first_is((uint64_t)-2, 1, OP_MOVN, 0, 0x0001));
    CHECK(plan_first_is(0x0000000100000000ull, 1, OP_MOVZ, 2, 0x0001));
    CHECK(plan_first_is(0xFFFF00000000FFFFull, 2, OP_MOVZ, 0, 0xFFFF));   /* 동점 -> MOVZ */
    CHECK(plan_first_is(0x8000000000000000ull, 1, OP_MOVZ, 3, 0x8000));   /* INT64_MIN */
    CHECK(plan_first_is(0x7FFFFFFFFFFFFFFFull, 1, OP_MOVN, 3, 0x8000));   /* INT64_MAX */

    /* ---- Part A-3: 무작위 + halfword 구조 무작위 (실행해서 v 가 나오고 개수가 최소인가) ---- */
    int bad = 0; uint64_t first_bad = 0;
    for (int i = 0; i < 200000; i++) {
        uint64_t v = xorshift64();
        if (i & 1) {   /* 각 halfword 를 {0, 0xffff, 무작위} 중에서 -> MOVN/MOVZ 경계 자주 */
            uint64_t r = xorshift64(), t = 0;
            for (int k = 0; k < 4; k++) {
                unsigned c = (unsigned)(r >> (8 * k)) % 3u;
                uint64_t h = c == 0 ? 0 : c == 1 ? 0xFFFF : (r >> (32 + 8 * k)) & 0xFFFF;
                t |= h << (16 * k);
            }
            v = t;
        }
        if (!plan_ok(v)) { if (!bad) first_bad = v; bad++; }
    }
    if (bad) printf("  plan: %d bad, first v=0x%016llx\n", bad, (unsigned long long)first_bad);
    CHECK(bad == 0);

    /* ---- Part A-4: 손으로 쓴 asm 상수 함수: 값 + 기계어가 plan 과 똑같은지 ---- */
    CHECK(k_deadbeef() == 0xDEADBEEFull);
    CHECK(code_matches_plan(k_deadbeef, 0xDEADBEEFull));
    CHECK(k_cafef00d_deadbeef() == 0xCAFEF00DDEADBEEFull);
    CHECK(code_matches_plan(k_cafef00d_deadbeef, 0xCAFEF00DDEADBEEFull));
    CHECK(k_ffffffff_12345678() == 0xFFFFFFFF12345678ull);
    CHECK(code_matches_plan(k_ffffffff_12345678, 0xFFFFFFFF12345678ull));
    CHECK(k_minus2() == -2);
    CHECK(code_matches_plan((uint64_t (*)(void))(uintptr_t)k_minus2, (uint64_t)-2));
    CHECK(k_qemu_uart() == 0x09000000ull);
    CHECK(code_matches_plan(k_qemu_uart, 0x09000000ull));
    CHECK(k_tie() == 0xFFFF00000000FFFFull);
    CHECK(code_matches_plan(k_tie, 0xFFFF00000000FFFFull));

    /* ---- Part B-1: 이름 붙은 값 ---- */
    unsigned N = 9, r = 99, s = 99;
    CHECK(!is_logical_imm64(0, &N, &r, &s));
    CHECK(!is_logical_imm64(~0ull, &N, &r, &s));
    CHECK(!is_logical_imm64(0x1234, &N, &r, &s));
    CHECK(is_logical_imm64(0xFF00, &N, &r, &s) && N == 1 && r == 56 && s == 7);
    CHECK(is_logical_imm64(0x5555555555555555ull, &N, &r, &s) && N == 0 && r == 0 && s == 0x3C);
    CHECK(is_logical_imm64(0x00FF00FF00FF00FFull, &N, &r, &s) && N == 0 && r == 0 && s == 0x27);
    CHECK(!is_logical_imm64(0x12345678, &N, &r, &s));
    CHECK(!is_logical_imm32(0xFFFFFFFFu, &N, &r, &s) && !is_logical_imm32(0, &N, &r, &s));
    CHECK(is_logical_imm32(0x80000001u, &N, &r, &s) && N == 0 && r == 1 && s == 1);
    CHECK(!is_logical_imm32(0x00000005u, &N, &r, &s));

    /* ---- Part B-2: 진짜 어셈블러가 고른 (N, immr, imms) 와 일치 ---- */
    static const struct { unsigned sf; uint64_t v; } lg[] = {
        {1, 0xFF00}, {1, 0x00FF00FF00FF00FFull}, {1, 0x5555555555555555ull}, {1, 0x8000000000000001ull},
        {1, 0xFFFFFFFFFFFFFFFEull}, {1, 0x0000000100000001ull}, {1, 0x7FC0}, {1, 0xFFFFFFFF00000000ull},
        {1, 0x0F0F0F0F0F0F0F0Full}, {0, 0xFF00}, {0, 0x80000001u}, {0, 0xFFFF803Fu}, {0, 0x0F0F0F0Fu},
    };
    int nlg = (int)(sizeof lg / sizeof lg[0]);
    CHECK(ref_logic_end - ref_logic == nlg);
    for (int i = 0; i < nlg; i++) {
        uint32_t w = ref_logic[i];
        unsigned wN = w >> 22 & 1u, wr = w >> 16 & 63u, ws = w >> 10 & 63u, wsf = w >> 31;
        bool ok = lg[i].sf ? is_logical_imm64(lg[i].v, &N, &r, &s)
                           : is_logical_imm32((uint32_t)lg[i].v, &N, &r, &s);
        if (!ok || N != wN || r != wr || s != ws)
            printf("  ref_logic[%d] %08x: assembler N=%u immr=%u imms=%u, yours ok=%d N=%u immr=%u imms=%u\n",
                   i, w, wN, wr, ws, ok, N, r, s);
        CHECK(wsf == lg[i].sf && ok && N == wN && r == wr && s == ws);
    }

    /* ---- Part B-3: 전수 대조 (정방향) — 5334 개 정규 인코딩 각각을 정확히 되찾는가 ---- */
    CHECK(g_nset64 == 5334 && g_ncanon64 == 5334 && g_nset32 == 1302 && g_ncanon32 == 1302);
    bad = 0;
    for (int i = 0; i < g_ncanon64; i++) {
        enc_t c = g_canon64[i]; uint64_t back = 0;
        bool ok = is_logical_imm64(c.v, &N, &r, &s);
        if (!ok || N != c.N || r != c.immr || s != c.imms || !decode_bit_masks(N, r, s, 64, &back) || back != c.v) bad++;
    }
    CHECK(bad == 0);
    bad = 0;
    for (int i = 0; i < g_ncanon32; i++) {
        enc_t c = g_canon32[i];
        bool ok = is_logical_imm32((uint32_t)c.v, &N, &r, &s);
        if (!ok || N != 0 || r != c.immr || s != c.imms) bad++;
    }
    CHECK(bad == 0);

    /* ---- Part B-4: 역방향 — true 라고 답한 값은 반드시 집합 안에 (1비트 뒤집기 + 작은 값 + 무작위) ---- */
    bad = 0;
    for (int i = 0; i < g_nset64; i++)
        for (int b = 0; b < 64; b++) {
            uint64_t v = g_set64[i] ^ (1ull << b);
            if (is_logical_imm64(v, &N, &r, &s) != in_set(g_set64, g_nset64, v)) bad++;
        }
    for (uint64_t v = 0; v < 65536; v++)
        if (is_logical_imm64(v, &N, &r, &s) != in_set(g_set64, g_nset64, v)) bad++;
    for (int i = 0; i < 300000; i++) {
        uint64_t v = xorshift64();
        if (is_logical_imm64(v, &N, &r, &s) != in_set(g_set64, g_nset64, v)) bad++;
    }
    CHECK(bad == 0);
    bad = 0;
    for (int i = 0; i < g_nset32; i++)
        for (int b = 0; b < 32; b++) {
            uint32_t v = (uint32_t)g_set32[i] ^ (1u << b);
            if (is_logical_imm32(v, &N, &r, &s) != in_set(g_set32, g_nset32, v)) bad++;
        }
    for (int i = 0; i < 300000; i++) {
        uint32_t v = (uint32_t)xorshift64();
        if (is_logical_imm32(v, &N, &r, &s) != in_set(g_set32, g_nset32, v)) bad++;
    }
    CHECK(bad == 0);

    /* ---- Part C: 펌웨어 마스크 비용 (기댓값 = clang -O2 실제 출력에서 센 명령 수, demo_consts.c) ---- */
    CHECK(and_cost64(0x000000000000FF00ull) == 1);   /* and x0, x0, #0xff00              */
    CHECK(and_cost64(0x00FF00FF00FF00FFull) == 1);   /* and x0, x0, #0xff00ff00ff00ff    */
    CHECK(and_cost64(0x0000000000001234ull) == 2);   /* mov w8,#0x1234 ; and             */
    CHECK(and_cost64(0xCAFEF00DDEADBEEFull) == 5);   /* mov + 3 movk ; and               */
    CHECK(and_cost64(~0ull) == 0 && and_cost64(0) == 1);
    CHECK(and_cost32(0x12345678u) == 3);             /* mov w8 ; movk w8 ; and w0,w0,w8  */
    CHECK(and_cost32(~0x0000FF00u) == 1);            /* and w0, w0, #0xffff00ff          */
    CHECK(and_cost32(0x00007FC0u) == 1);             /* PLLN 필드 마스크                  */
    CHECK(and_cost32(0x00000005u) == 2);             /* 0b101: 1 이 떨어져 있음           */
    CHECK(and_cost32(0xA5A5A5A5u) == 3);
    CHECK(and_cost32(0xFFFF1234u) == 2);             /* MOVN 경로 1개 + and              */

    /* ---- Part C: RCC_PLLCFGR.PLLN[14:6] = 168 패치 (asm) ---- */
    CHECK(pllcfgr_set_plln168(0x24003010u) == ((0x24003010u & ~0x7FC0u) | (168u << 6)));
    CHECK(pllcfgr_set_plln168(0) == 0x2A00u);
    CHECK(pllcfgr_set_plln168(0xFFFFFFFFu) == 0xFFFFAA3Fu);
    bad = 0;
    for (int i = 0; i < 10000; i++) {
        uint32_t x = (uint32_t)xorshift64();
        if (pllcfgr_set_plln168(x) != ((x & ~0x7FC0u) | (168u << 6))) bad++;
    }
    CHECK(bad == 0);
    {   /* 첫 명령이 정말 AND(32-bit, 즉시값) 인가: sf=0 opc=00 100100 */
        const uint32_t *code = (const uint32_t *)(uintptr_t)pllcfgr_set_plln168;
        CHECK((code[0] & 0xFF800000u) == 0x12000000u);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
