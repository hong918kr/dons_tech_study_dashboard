/* MEM-B07 추가 연습 — 한 번에 검증
 *   연습 1: 전체 패리티 c7 을 빼면? SEC-only (71,64) 의 2비트 오류 → 오정정 수
 *   연습 2: 데이터 폭 k 에 대한 최소 SECDED 체크 비트 수와 오버헤드 (72/64 가 나오는 이유)
 *   연습 3: EDAC 로그의 신드롬 → 데이터/체크 비트 → x8 / x4 DRAM 칩 번호
 *   연습 4: x4 칩 하나 고장 (같은 니블 안 1~4비트) → 이 코드의 판정 분포 (chipkill 동기)
 *   연습 5: Hsiao(72,64) 홀수 무게 열 코드 만들기 → SECDED 전수 검증 + XOR 트리 폭 비교
 * 코덱은 본문 solution.c 의 위치형 SECDED(72,64) 축약판이다.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static unsigned parity64(uint64_t x) {
    x ^= x >> 32; x ^= x >> 16; x ^= x >> 8; x ^= x >> 4; x ^= x >> 2; x ^= x >> 1;
    return (unsigned)(x & 1u);
}
static unsigned popc(uint64_t x) { unsigned n = 0; for (; x; x &= x - 1) n++; return n; }
static bool is_pow2(unsigned x) { return x && !(x & (x - 1)); }

/* ---------- 본문 코덱 축약판 (MEM-B07 solution.c) ---------- */
enum { ST_NOERR, ST_CE_DATA, ST_CE_CHECK, ST_UE };
static uint8_t POS[64]; static uint64_t CMASK[7]; static uint8_t SYN_TAB[256];
static void secded_init(void) {
    int i = 0;
    for (unsigned p = 1; p < 72; p++) if (!is_pow2(p)) POS[i++] = (uint8_t)p;
    for (int j = 0; j < 7; j++) { CMASK[j] = 0; for (int k = 0; k < 64; k++) if (POS[k] >> j & 1u) CMASK[j] |= 1ull << k; }
    memset(SYN_TAB, 0xFE, sizeof SYN_TAB);
    SYN_TAB[0] = 0xFF; SYN_TAB[0x80] = 71;
    for (int j = 0; j < 7; j++) SYN_TAB[0x80 | 1u << j] = (uint8_t)(64 + j);
    for (int k = 0; k < 64; k++) SYN_TAB[0x80 | POS[k]] = (uint8_t)k;
}
/* 오류 패턴(데이터 마스크 ed, 체크 마스크 ec)만으로 신드롬 — 선형이라 데이터 불필요 */
static uint8_t err_syndrome(uint64_t ed, uint8_t ec) {
    unsigned s = 0;
    for (int j = 0; j < 7; j++) s |= parity64(ed & CMASK[j]) << j;
    s ^= ec & 0x7Fu;
    return (uint8_t)(s | (parity64(ed) ^ parity64(ec)) << 7);
}
/* 오류 패턴의 판정: 정정됨 / UE / 오정정 / 무성 */
enum { OUT_CORR, OUT_UE, OUT_MIS, OUT_SILENT };
static int classify_err(uint64_t ed, uint8_t ec) {
    uint8_t syn = err_syndrome(ed, ec);
    unsigned e = SYN_TAB[syn];
    if (e == 0xFF) return OUT_SILENT;          /* 패턴이 0 이 아닌데 신드롬 0 */
    if (e == 0xFE) return OUT_UE;
    if (e < 64) ed ^= 1ull << e; else ec ^= (uint8_t)(1u << (e - 64));
    return (ed == 0 && ec == 0) ? OUT_CORR : OUT_MIS;
}

/* ---------- 연습 1: SEC-only (71,64) ---------- */
/* 위치 1..71 만 쓰고 c7 이 없다. 신드롬 s ≠ 0 이면 "위치 s 를 정정" 하고, s > 71 이면 검출. */
static void sec_only_doubles(int *pairs, int *detected, int *miscorrected) {
    *pairs = *detected = *miscorrected = 0;
    for (unsigned a = 1; a < 72; a++)
        for (unsigned b = a + 1; b < 72; b++) {
            unsigned s = a ^ b;                 /* 0 이 될 수 없다 (a ≠ b) */
            (*pairs)++;
            if (s > 71) (*detected)++; else (*miscorrected)++;
        }
}

/* ---------- 연습 2: 최소 체크 비트 ---------- */
static unsigned sec_bits(unsigned k) { unsigned r = 0; while ((1u << r) < k + r + 1) r++; return r; }
static unsigned secded_bits(unsigned k) { return sec_bits(k) + 1; }

/* ---------- 연습 3: 신드롬 → 위치 → 칩 ---------- */
/* chip_width = 4 또는 8. bitnum 0..63 = DQ0..63, 64..71 = CB0..7 (ECC 칩이 맨 끝) 이라는 가정.
 * 반환: bitnum (0..71), UE/정상이면 -1. *chip 에 칩 번호 */
static int syn_to_chip(uint8_t syn, unsigned chip_width, int *chip) {
    unsigned e = SYN_TAB[syn];
    *chip = -1;
    if (e >= 72) return -1;
    *chip = (int)(e / chip_width);
    return (int)e;
}

/* ---------- 연습 4: x4 칩 고장 ---------- */
static void x4_chip_failures(int cnt[4][4] /* [weight-1][outcome] */) {
    memset(cnt, 0, sizeof(int) * 16);
    for (unsigned chip = 0; chip < 18; chip++)
        for (unsigned pat = 1; pat < 16; pat++) {
            uint64_t ed = 0; uint8_t ec = 0;
            for (unsigned b = 0; b < 4; b++) if (pat >> b & 1u) {
                unsigned bit = chip * 4 + b;
                if (bit < 64) ed |= 1ull << bit; else ec |= (uint8_t)(1u << (bit - 64));
            }
            int o = classify_err(ed, ec);
            cnt[popc(pat) - 1][o]++;
            if (o == OUT_SILENT) printf("    silent: chip %u (bitnum %u..%u) pattern 0x%X\n", chip, chip * 4, chip * 4 + 3, pat);
        }
}

/* ---------- 연습 5: Hsiao(72,64) ---------- */
/* H 의 열: 데이터 64개 = 무게 3 인 8비트 열 56개 전부 + 무게 5 열 8개 (0x1F 의 순환 이동),
 * 체크 8개 = 단위 열 1<<j. 모든 열이 홀수 무게이고 서로 다르다. */
static uint8_t HCOL[72];
static int HTAB[256];             /* 신드롬 → 열 번호, 없으면 -1 */
static void hsiao_init(void) {
    int n = 0;
    for (unsigned v = 0; v < 256; v++) if (popc(v) == 3) HCOL[n++] = (uint8_t)v;
    for (unsigned r = 0; r < 8; r++) HCOL[n++] = (uint8_t)((0x1Fu << r | 0x1Fu >> (8 - r)) & 0xFFu);
    for (unsigned j = 0; j < 8; j++) HCOL[n++] = (uint8_t)(1u << j);
    for (int s = 0; s < 256; s++) HTAB[s] = -1;
    for (int i = 0; i < 72; i++) HTAB[HCOL[i]] = i;
}
static uint8_t hsiao_err_syn(const unsigned *bits, unsigned k) {
    uint8_t s = 0;
    for (unsigned i = 0; i < k; i++) s ^= HCOL[bits[i]];
    return s;
}
/* Hsiao 판정: 0 → 정상, 홀수 무게 + 어떤 열과 같음 → CE, 그 외 → UE */
static int hsiao_classify(const unsigned *bits, unsigned k) {
    uint8_t s = hsiao_err_syn(bits, k);
    if (s == 0) return OUT_SILENT;
    if ((popc(s) & 1u) && HTAB[s] >= 0) {
        if (k == 1 && HTAB[s] == (int)bits[0]) return OUT_CORR;
        return OUT_MIS;
    }
    return OUT_UE;
}

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main(void) {
    secded_init();

    /* 연습 1 */
    int pairs, det, mis;
    sec_only_doubles(&pairs, &det, &mis);
    printf("ex1 SEC-only (71,64) doubles: pairs=%d detected=%d miscorrected=%d (%.1f%%)\n",
           pairs, det, mis, 100.0 * mis / pairs);
    CHECK(pairs == 2485 && det == 448 && mis == 2037);

    /* 연습 2 */
    unsigned ks[] = { 8, 16, 32, 64, 128, 256, 512 };
    unsigned want[] = { 5, 6, 7, 8, 9, 10, 11 };
    bool ok2 = true;
    printf("ex2   k  SECDED r  (n,k)        overhead\n");
    for (int i = 0; i < 7; i++) {
        unsigned r = secded_bits(ks[i]);
        printf("    %4u  %8u  (%u,%u)%*s %5.1f%%\n", ks[i], r, ks[i] + r, ks[i],
               (int)(12 - (ks[i] + r >= 100) - (ks[i] + r >= 10) - (ks[i] >= 100) - (ks[i] >= 10)), "",
               100.0 * r / ks[i]);
        if (r != want[i]) ok2 = false;
    }
    CHECK(ok2);
    CHECK(sec_bits(64) == 7 && sec_bits(57) == 6 && sec_bits(58) == 7);  /* (63,57) 은 완전 Hamming */

    /* 연습 3 */
    int chip, bit;
    bit = syn_to_chip(0xAC, 8, &chip); CHECK(bit == 37 && chip == 4);
    bit = syn_to_chip(0xAC, 4, &chip); CHECK(bit == 37 && chip == 9);
    bit = syn_to_chip(0x80, 8, &chip); CHECK(bit == 71 && chip == 8);
    bit = syn_to_chip(0x80, 4, &chip); CHECK(bit == 71 && chip == 17);
    bit = syn_to_chip(0x84, 8, &chip); CHECK(bit == 66 && chip == 8);   /* s=4 → c2 → CB2 */
    bit = syn_to_chip(0x06, 8, &chip); CHECK(bit == -1 && chip == -1);  /* p=0, s≠0 → UE */
    bit = syn_to_chip(0xC8 | 0x80, 8, &chip); CHECK(bit == -1);          /* s=72 → 없는 위치 → UE */

    /* 연습 4 */
    int c4[4][4];
    x4_chip_failures(c4);
    printf("ex4 x4 chip failure, 18 chips x 15 patterns (corr/ue/mis/silent):\n");
    for (int w = 0; w < 4; w++)
        printf("    weight %d: %3d %3d %3d %3d\n", w + 1, c4[w][0], c4[w][1], c4[w][2], c4[w][3]);
    CHECK(c4[0][OUT_CORR] == 72 && c4[0][OUT_UE] == 0);
    CHECK(c4[1][OUT_UE] == 108);
    CHECK(c4[2][OUT_CORR] + c4[2][OUT_UE] + c4[2][OUT_MIS] == 72 && c4[2][OUT_CORR] == 0);
    CHECK(c4[2][OUT_MIS] == 69 && c4[2][OUT_UE] == 3);
    CHECK(c4[3][OUT_UE] == 14 && c4[3][OUT_SILENT] == 4);

    /* 연습 5 */
    hsiao_init();
    bool distinct = true, odd = true;
    for (int i = 0; i < 72; i++) {
        if (!(popc(HCOL[i]) & 1u)) odd = false;
        for (int j = 0; j < i; j++) if (HCOL[i] == HCOL[j]) distinct = false;
    }
    CHECK(distinct && odd);
    int sc = 0, du = 0, t_ue = 0, t_mis = 0, t_n = 0;
    unsigned b[3];
    for (b[0] = 0; b[0] < 72; b[0]++) {
        sc += hsiao_classify(b, 1) == OUT_CORR;
        for (b[1] = b[0] + 1; b[1] < 72; b[1]++) {
            du += hsiao_classify(b, 2) == OUT_UE;
            for (b[2] = b[1] + 1; b[2] < 72; b[2]++) {
                int o = hsiao_classify(b, 3);
                t_n++; t_ue += o == OUT_UE; t_mis += o == OUT_MIS;
            }
        }
    }
    printf("ex5 Hsiao: single corrected=%d/72, double UE=%d/2556, triple n=%d UE=%d (%.1f%%) mis=%d\n",
           sc, du, t_n, t_ue, 100.0 * t_ue / t_n, t_mis);
    CHECK(sc == 72 && du == 2556);
    CHECK(t_n == 59640 && t_ue + t_mis == 59640);
    CHECK(t_ue > 14336);                         /* 위치형 Hamming 보다 3비트를 더 많이 UE 로 잡는다 */
    /* XOR 트리 폭: 각 체크 비트가 몇 개의 데이터 비트를 XOR 하나 */
    unsigned hrow[8] = {0}, prow[8] = {0};
    for (int i = 0; i < 64; i++)
        for (int j = 0; j < 8; j++) if (HCOL[i] >> j & 1u) hrow[j]++;
    for (int j = 0; j < 7; j++) prow[j] = popc(CMASK[j]);
    /* c7 을 데이터만으로 쓰면: c7 = parity(d) ^ parity(c0..c6) = XOR of d_i with popcount(POS[i]) 짝수 */
    for (int i = 0; i < 64; i++) prow[7] += !(popc(POS[i]) & 1u);
    printf("ex5 data inputs per check bit  Hsiao:");
    for (int j = 0; j < 8; j++) printf(" %u", hrow[j]);
    printf("  | positional Hamming c0..c7:");
    for (int j = 0; j < 8; j++) printf(" %u", prow[j]);
    printf("\n");
    bool bal = true;
    for (int j = 0; j < 8; j++) if (hrow[j] != 26) bal = false;
    CHECK(bal);
    CHECK(prow[0] == 35 && prow[3] == 31 && prow[6] == 7);
    /* 검산: c7 의 데이터 전용 식이 실제 인코더와 같은가 */
    {
        uint64_t m7 = 0, x = 0x9E3779B97F4A7C15ull; bool same = true;
        for (int i = 0; i < 64; i++) if (!(popc(POS[i]) & 1u)) m7 |= 1ull << i;
        for (int t = 0; t < 10000; t++) {
            x ^= x << 13; x ^= x >> 7; x ^= x << 17;
            unsigned c = 0;
            for (int j = 0; j < 7; j++) c |= parity64(x & CMASK[j]) << j;
            unsigned c7 = parity64(x) ^ parity64(c);
            if (c7 != parity64(x & m7)) same = false;
        }
        CHECK(same && prow[7] == popc(m7));
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
