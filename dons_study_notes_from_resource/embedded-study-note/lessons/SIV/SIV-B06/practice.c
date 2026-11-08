/* SIV-B06 추가 연습 — 한 번에 검증
 *
 * 본문과 같은 VDMA 기술서 + 축약 mock. 디코더 결함에 stuck1_bit(주소 비트 k 가 1 로 고정)을 더했고,
 * 버스 접근 횟수 카운터(g_ops)를 달았다.
 *   연습 1  희소 맵에서 주소 비트별 RW 짝 찾기 (coverage)
 *   연습 2  버스 에러가 난 레지스터로 결함 극성(무시=0 고정 vs 1 고정) 추정
 *   연습 3  패턴 방식별 "구별 불가" RW 쌍 세기 (원시 주소 / 해시 / 해시+보수 / 순번값 / index-bit)
 *   연습 4  방식별 버스 접근 비용 — 공식과 계측값 비교
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef enum { AT_RW, AT_RO, AT_W1C, AT_RC } acc_t;
typedef struct { const char *name; uint32_t off; acc_t acc; uint32_t mask; uint32_t reset; } reg_desc_t;
#define CH(n, b)                                                   \
    {"CH" #n "_SRC",   (b) + 0x00u, AT_RW, 0xFFFFFFFFu, 0x0u},     \
    {"CH" #n "_DST",   (b) + 0x04u, AT_RW, 0xFFFFFFFFu, 0x0u},     \
    {"CH" #n "_LEN",   (b) + 0x08u, AT_RW, 0x00FFFFFFu, 0x0u},     \
    {"CH" #n "_CCTRL", (b) + 0x0Cu, AT_RW, 0x00000003u, 0x0u},     \
    {"CH" #n "_CSTAT", (b) + 0x10u, AT_RO, 0xFFFFFFFFu, 0x1u}
static const reg_desc_t regs[] = {
    {"CTRL",    0x000u, AT_RW,  0x00000107u, 0x00000000u},
    {"SCRATCH", 0x004u, AT_RW,  0xFFFFFFFFu, 0x00000000u},
    {"INTEN",   0x008u, AT_RW,  0x0000000Fu, 0x00000000u},
    {"INTSTAT", 0x00Cu, AT_W1C, 0x0000000Fu, 0x00000000u},
    {"CFG",     0x010u, AT_RW,  0x0000FFFFu, 0x00000010u},
    {"DBG_CNT", 0x014u, AT_RC,  0x0000FFFFu, 0x00000000u},
    {"ID",      0x018u, AT_RO,  0xFFFFFFFFu, 0x56444D41u},
    {"VERSION", 0x01Cu, AT_RO,  0xFFFFFFFFu, 0x00010002u},
    {"TIMEOUT", 0x020u, AT_RW,  0x0000FFFFu, 0x00000FFFu},
    {"PRIO",    0x040u, AT_RW,  0x000000FFu, 0x00000000u},
    CH(0, 0x080u), CH(1, 0x0C0u), CH(2, 0x100u), CH(3, 0x140u),
};
#define NREGS     (sizeof regs / sizeof regs[0])
#define SLOT_SIZE 0x400u

static int desc_find(uint32_t off) {
    for (size_t i = 0; i < NREGS; i++) if (regs[i].off == off) return (int)i;
    return -1;
}

/* ---- mock: 본문 mock + stuck1_bit + 접근 카운터 ---- */
typedef struct { int ignore_bit; int stuck1_bit; } fault_t;
static fault_t  g_fault;
static uint32_t g_val[NREGS];
static unsigned g_bus_err, g_ops;

static int decode(uint32_t off) {
    if (off >= SLOT_SIZE || (off & 3u)) return -1;
    if (g_fault.ignore_bit >= 0) off &= ~(1u << g_fault.ignore_bit);
    if (g_fault.stuck1_bit >= 0) off |= 1u << g_fault.stuck1_bit;
    return desc_find(off);
}
static uint32_t reg_read32(uint32_t off) {
    g_ops++;
    int i = decode(off);
    if (i < 0) { g_bus_err++; return 0xDEADBEEFu; }
    uint32_t v = g_val[i];
    if (regs[i].acc == AT_RC) g_val[i] = 0;
    return v;
}
static void reg_write32(uint32_t off, uint32_t v) {
    g_ops++;
    int i = decode(off);
    if (i < 0) { g_bus_err++; return; }
    if (regs[i].acc == AT_RW)  g_val[i] = (g_val[i] & ~regs[i].mask) | (v & regs[i].mask);
    if (regs[i].acc == AT_W1C) g_val[i] &= ~(v & regs[i].mask);
}
static void mock_reset(int ignore_bit, int stuck1_bit) {
    g_fault.ignore_bit = ignore_bit; g_fault.stuck1_bit = stuck1_bit;
    for (size_t i = 0; i < NREGS; i++) g_val[i] = regs[i].reset;
    g_bus_err = g_ops = 0;
}

static uint32_t addr_hash(uint32_t off) {
    uint32_t x = off ^ 0x5A5A5A5Au;
    x ^= x >> 16; x *= 0x7FEB352Du;
    x ^= x >> 15; x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}
static unsigned n_of(acc_t a) { unsigned n = 0; for (size_t i = 0; i < NREGS; i++) n += regs[i].acc == a; return n; }

/* ===== 연습 1: 주소 비트 k 를 시험할 수 있는 RW 짝 (A, A|1<<k), A 의 비트 k = 0 ===== */
static int find_bit_pair(int k, uint32_t *a, uint32_t *b) {
    uint32_t bit = 1u << k;
    for (size_t i = 0; i < NREGS; i++) {
        if (regs[i].acc != AT_RW || (regs[i].off & bit)) continue;
        int j = desc_find(regs[i].off | bit);
        if (j >= 0 && regs[j].acc == AT_RW && (regs[i].mask & regs[j].mask)) {
            *a = regs[i].off; *b = regs[j].off; return 1;
        }
    }
    return 0;
}
static uint32_t pair_coverage(int lo, int hi) {
    uint32_t cov = 0, a, b;
    for (int k = lo; k <= hi; k++) if (find_bit_pair(k, &a, &b)) cov |= 1u << k;
    return cov;
}

/* ===== 연습 2: 버스 에러가 난 레지스터들의 비트 k 로 극성 추정 ===== */
/* 반환 0: A[k] 가 0 으로 보임(무시/stuck-0) — 피해자는 비트 k = 1 쪽
 *      1: A[k] 가 1 로 고정(stuck-1)       — 피해자는 비트 k = 0 쪽
 *     -1: 피해자 없음 또는 섞임 → 판단 불가 */
static int guess_polarity(int k, const uint32_t *err_offs, int n) {
    int ones = 0, zeros = 0;
    for (int i = 0; i < n; i++) { if (err_offs[i] & (1u << k)) ones++; else zeros++; }
    if (n == 0 || (ones && zeros)) return -1;
    return ones ? 0 : 1;
}
/* RW·RO 레지스터를 하나씩 읽어 버스 에러가 난 주소를 모은다 (RC/W1C 는 안 건드림) */
static int collect_bus_err_regs(uint32_t *out, int max) {
    int n = 0;
    for (size_t i = 0; i < NREGS; i++) {
        if (regs[i].acc != AT_RW && regs[i].acc != AT_RO) continue;
        unsigned e = g_bus_err;
        (void)reg_read32(regs[i].off);
        if (g_bus_err != e && n < max) out[n++] = regs[i].off;
    }
    return n;
}

/* ===== 연습 3: 모든 패스에서 공통 마스크 비트가 같은 RW 쌍 = 데이터로 구별 불가 ===== */
typedef uint32_t (*pass_fn)(uint32_t off, unsigned rank, unsigned pass);
static uint32_t p_raw(uint32_t off, unsigned r, unsigned p)   { (void)r; (void)p; return off; }
static uint32_t p_hash(uint32_t off, unsigned r, unsigned p)  { (void)r; (void)p; return addr_hash(off); }
static uint32_t p_hash2(uint32_t off, unsigned r, unsigned p) { (void)r; return addr_hash(off) ^ (p ? 0xFFFFFFFFu : 0u); }
static uint32_t p_rank(uint32_t off, unsigned r, unsigned p)  { (void)off; (void)p; return r; }
static uint32_t p_ibit(uint32_t off, unsigned r, unsigned p)  { (void)off; return ((r >> p) & 1u) ? 0xFFFFFFFFu : 0u; }

static int count_indistinct_multi(pass_fn fn, unsigned passes) {
    int n = 0;
    unsigned ri = 0;
    for (size_t i = 0; i < NREGS; i++) {
        if (regs[i].acc != AT_RW) continue;
        ri++;
        unsigned rj = ri;
        for (size_t j = i + 1; j < NREGS; j++) {
            if (regs[j].acc != AT_RW) continue;
            rj++;
            uint32_t m = regs[i].mask & regs[j].mask;
            int same = 1;
            for (unsigned p = 0; p < passes && same; p++)
                if ((fn(regs[i].off, ri, p) ^ fn(regs[j].off, rj, p)) & m) same = 0;
            n += same;
        }
    }
    return n;
}
static unsigned ceil_log2_plus1(unsigned n) { unsigned p = 0; while ((1u << p) <= n) p++; return p; }

/* ===== 연습 4: 버스 접근 비용 ===== */
static unsigned long ops_unique(unsigned n_rw, unsigned n_ro)  { return 2ul * (2ul * n_rw + n_ro); }
static unsigned long ops_indexbits(unsigned n_rw)              { return 2ul * n_rw * ceil_log2_plus1(n_rw); }
static unsigned long ops_pairwise(unsigned n_rw)               { return 2ul * n_rw * (n_rw - 1ul); }  /* 쌍마다 w,w,r,r */

static int run_unique(void) {       /* 본문 alias_unique_test 와 같은 접근 순서 */
    int fails = 0;
    for (int pass = 0; pass < 2; pass++) {
        uint32_t inv = pass ? 0xFFFFFFFFu : 0u;
        for (size_t i = 0; i < NREGS; i++)
            if (regs[i].acc == AT_RW) reg_write32(regs[i].off, addr_hash(regs[i].off) ^ inv);
        for (size_t i = 0; i < NREGS; i++) {
            uint32_t exp = regs[i].acc == AT_RW ? addr_hash(regs[i].off) ^ inv : regs[i].reset;
            if (regs[i].acc == AT_RW || regs[i].acc == AT_RO)
                fails += ((reg_read32(regs[i].off) ^ exp) & regs[i].mask) != 0;
        }
    }
    return fails;
}
static int run_indexbits(void) {
    unsigned passes = ceil_log2_plus1(n_of(AT_RW));
    int fails = 0;
    for (unsigned p = 0; p < passes; p++) {
        unsigned r = 0;
        for (size_t i = 0; i < NREGS; i++)
            if (regs[i].acc == AT_RW) { r++; reg_write32(regs[i].off, ((r >> p) & 1u) ? 0xFFFFFFFFu : 0u); }
        r = 0;
        for (size_t i = 0; i < NREGS; i++)
            if (regs[i].acc == AT_RW) {
                r++;
                fails += ((reg_read32(regs[i].off) ^ (((r >> p) & 1u) ? 0xFFFFFFFFu : 0u)) & regs[i].mask) != 0;
            }
    }
    return fails;
}

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main(void) {
    uint32_t a = 0, b = 0, errs[32];
    int n;

    /* 연습 1 */
    CHECK(find_bit_pair(5, &a, &b) && a == 0x000u && b == 0x020u);
    CHECK(find_bit_pair(7, &a, &b) && a == 0x000u && b == 0x080u);
    CHECK(!find_bit_pair(9, &a, &b));                       /* 0x200 이상엔 레지스터가 없다 → 홀 검사 몫 */
    CHECK(pair_coverage(2, 9) == 0x1FCu);
    printf("coverage A[9:2] = 0x%03X\n", pair_coverage(2, 9));

    /* 연습 2 */
    mock_reset(7, -1);  n = collect_bus_err_regs(errs, 32);
    printf("ignore A[7]: %d bus-error regs, first 0x%03X\n", n, n ? errs[0] : 0u);
    CHECK(n > 0 && guess_polarity(7, errs, n) == 0);
    mock_reset(-1, 7);  n = collect_bus_err_regs(errs, 32);
    printf("stuck1 A[7]: %d bus-error regs, first 0x%03X\n", n, n ? errs[0] : 0u);
    CHECK(n > 0 && guess_polarity(7, errs, n) == 1);
    mock_reset(5, -1);  n = collect_bus_err_regs(errs, 32);
    CHECK(n == 0 && guess_polarity(5, errs, n) == -1);     /* TIMEOUT 의 짝 CTRL 이 실재 → 에러 없음 */
    mock_reset(-1, -1); n = collect_bus_err_regs(errs, 32);
    CHECK(n == 0);

    /* 연습 3 */
    unsigned P = ceil_log2_plus1(n_of(AT_RW));
    int c_raw = count_indistinct_multi(p_raw, 1), c_hash = count_indistinct_multi(p_hash, 1);
    int c_hash2 = count_indistinct_multi(p_hash2, 2), c_rank = count_indistinct_multi(p_rank, 1);
    int c_ibit = count_indistinct_multi(p_ibit, P);
    printf("indistinct pairs: raw=%d hash=%d hash+inv=%d rank=%d indexbits(%u passes)=%d\n",
           c_raw, c_hash, c_hash2, c_rank, P, c_ibit);
    CHECK(P == 5u);
    CHECK(c_hash < c_raw && c_hash2 == c_hash);            /* 보수 패스는 구별력을 늘리지 않는다 */
    CHECK(c_ibit == 0);

    /* 연습 4 */
    unsigned nrw = n_of(AT_RW), nro = n_of(AT_RO);
    mock_reset(-1, -1);
    CHECK(run_unique() == 0 && g_ops == ops_unique(nrw, nro));
    mock_reset(-1, -1);
    CHECK(run_indexbits() == 0 && g_ops == ops_indexbits(nrw));
    printf("ops N=%u: unique=%lu indexbits=%lu pairwise=%lu\n", nrw, ops_unique(nrw, nro), ops_indexbits(nrw),
           ops_pairwise(nrw));
    printf("ops N=4096: unique=%lu indexbits=%lu pairwise=%lu\n", ops_unique(4096, 0), ops_indexbits(4096),
           ops_pairwise(4096));
    CHECK(ops_pairwise(4096) / ops_indexbits(4096) > 300ul);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
