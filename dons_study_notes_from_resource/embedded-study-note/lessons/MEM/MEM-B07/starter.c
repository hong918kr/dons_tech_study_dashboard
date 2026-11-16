/* MEM-B07 SECDED Hamming(72,64) 구현
 *
 * 64비트 데이터 + 8비트 체크 = 72비트 코드워드. 이 레슨의 레이아웃 (SIV-M14 가 축약해 쓰는 것과 같다):
 *   코드워드 "위치" 0..71 은 수학용 번호다.
 *     위치 2^j (1,2,4,8,16,32,64) = Hamming 체크비트 c0..c6
 *     위치 0                      = c7 (72비트 전체 짝수 패리티, overall parity)
 *     나머지 64 위치 3,5,6,7,9,...,71 = 데이터 d0..d63 (작은 위치부터 차례로)
 *   저장 형태는 DIMM 처럼 "데이터 64비트(DQ[63:0]) + 체크 바이트(CB[7:0])": check 바이트의 bit j = c_j.
 *   신드롬 바이트 syn = s | p<<7
 *     s = 값이 1 인 비트들의 위치 XOR (7비트), p = 72비트 전체 패리티 (정상 코드워드면 둘 다 0)
 *   비트 번호(bitnum, 오류 주입·보고용): 데이터 0..63, 체크 c0..c7 = 64..71.
 *
 * Part A: 테이블 초기화(데이터 위치, 체크 마스크, 신드롬→비트 표) + encoder + syndrome + decoder
 * Part B: 오류 주입 캠페인 — 단일 전수, 2비트 전수 + 무작위 1,000,000회, 3비트 전수(정직한 한계)
 * Part C: 메모리 컨트롤러 읽기 경로 — ecc_write / ecc_read, CE/UE 카운터, scrub-on-CE
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---------------- 주어진 도구 ---------------- */
static unsigned parity64(uint64_t x) {
    x ^= x >> 32; x ^= x >> 16; x ^= x >> 8; x ^= x >> 4; x ^= x >> 2; x ^= x >> 1;
    return (unsigned)(x & 1u);
}
static uint64_t xs64(uint64_t *s) {                  /* xorshift64, 고정 시드 → 결정적 */
    uint64_t x = *s; x ^= x << 13; x ^= x >> 7; x ^= x << 17; return *s = x;
}
static bool is_pow2(unsigned x) { return x && !(x & (x - 1)); }

typedef enum { ECC_NO_ERR = 0, ECC_CE_DATA, ECC_CE_CHECK, ECC_UE } ecc_status_t;
typedef struct {
    ecc_status_t st;
    int          bit;   /* CE_DATA: 데이터 비트 0..63, CE_CHECK: 체크 비트 0..7, 그 외 -1 */
    uint8_t      syn;   /* 신드롬 바이트 s | p<<7 (EDAC 로그에 남기는 값) */
} ecc_result_t;

#define SYN_NOERR 0xFFu /* SYN_TAB 값: 오류 없음 */
#define SYN_UE    0xFEu /* SYN_TAB 값: 정정 불가 */
static uint8_t  POS[64];      /* 데이터 비트 i → 코드워드 위치 (3..71)              */
static uint64_t CMASK[7];     /* 체크비트 c_j 에 들어가는 데이터 비트 마스크         */
static uint8_t  SYN_TAB[256]; /* 신드롬 바이트 → bitnum 0..71 / SYN_UE / SYN_NOERR */

/* 느린 기준 구현 (주어짐): 72칸 배열에 위치대로 놓고 정의대로 XOR. 테이블과 무관한 교차 검증용. */
static uint8_t ref_encode(uint64_t d) {
    uint8_t cw[72] = {0};
    int i = 0;
    for (int p = 1; p < 72; p++)
        if (p & (p - 1)) cw[p] = (uint8_t)(d >> i++ & 1u);
    unsigned c = 0, all = 0;
    for (int j = 0; j < 7; j++) {
        unsigned x = 0;
        for (int p = 1; p < 72; p++) if (p >> j & 1) x ^= cw[p];
        cw[1 << j] = (uint8_t)x;
        c |= x << j;
    }
    for (int p = 1; p < 72; p++) all ^= cw[p];
    return (uint8_t)(c | all << 7);
}

/* ---------------- Part A: 코덱 ---------------- */
/* POS / CMASK / SYN_TAB 를 만든다. SYN_TAB 는 256 엔트리 × 1 B. */
static void secded_init(void) {
    (void)parity64; (void)CMASK; (void)is_pow2;
    /* TODO: POS[64] (위치 1..71 중 2의 거듭제곱이 아닌 곳), CMASK[7], SYN_TAB[256] 만들기 */
}

/* 체크 바이트: c_j = parity(d & CMASK[j]) (j<7), c7 = 나머지 71비트의 패리티 */
static uint8_t secded_encode(uint64_t d) {
    (void)d;
    return 0; /* TODO: c_j = parity(d & CMASK[j]), c7 = 나머지 71비트 패리티 */
}

/* 신드롬 바이트 s | p<<7 */
static uint8_t secded_syndrome(uint64_t d, uint8_t c) {
    (void)d; (void)c;
    return 0; /* TODO: s | p<<7 */
}

/* 디코드: CE 면 *d 또는 *c 를 제자리에서 고친다. UE 면 아무것도 바꾸지 않는다. */
static ecc_result_t secded_decode(uint64_t *d, uint8_t *c) {
    ecc_result_t r = { ECC_NO_ERR, -1, 0 };
    (void)d; (void)c;
    return r; /* TODO: 신드롬 → SYN_TAB → 제자리 정정 / UE */
}

/* ---------------- Part B: 오류 주입 캠페인 ---------------- */
typedef struct {
    uint32_t n;         /* 시도 수                                         */
    uint32_t corrected; /* CE 로 보고 + (d,c) 원래대로 복구                  */
    uint32_t ue;        /* UE 로 보고 (데이터 불변)                         */
    uint32_t mis_data;  /* CE_DATA 로 보고했는데 결과가 원본과 다름 = 오정정   */
    uint32_t mis_check; /* CE_CHECK 로 보고했는데 결과가 원본과 다름 = 오정정  */
    uint32_t silent;    /* NO_ERR 로 보고했는데 원본과 다름 = 무성 오염(SDC)   */
} camp_t;

static void flip72(uint64_t *d, uint8_t *c, unsigned bit) {   /* 주어짐: bitnum 하나 뒤집기 */
    if (bit < 64) *d ^= 1ull << bit;
    else *c ^= (uint8_t)(1u << (bit - 64));
}

/* 시도 1회: 원본 d0 을 인코드 → bits[0..k-1] 뒤집기 → 디코드 → 진실과 비교해 분류 */
static void camp_trial(camp_t *t, uint64_t d0, const unsigned *bits, unsigned k) {
    (void)t; (void)d0; (void)bits; (void)k;
    /* TODO: 인코드 → 뒤집기 → 디코드 → corrected/ue/mis_data/mis_check/silent 중 하나 + n++ */
}

/* 0..71 에서 서로 다른 k 개를 뽑는다 (같은 비트를 두 번 뒤집으면 오류가 사라진다) */
static void pick_distinct(uint64_t *rng, unsigned k, unsigned *out) {
    (void)rng;
    for (unsigned i = 0; i < k; i++) out[i] = i; /* TODO: 무작위 + 중복 없이 */
}

/* 주어짐: 무작위 k비트 캠페인 (데이터도 매번 무작위) */
static camp_t run_random(unsigned k, uint32_t trials, uint64_t seed) {
    camp_t t = {0};
    unsigned bits[8];
    uint64_t rng = seed;
    for (uint32_t i = 0; i < trials; i++) {
        uint64_t d0 = xs64(&rng);
        pick_distinct(&rng, k, bits);
        camp_trial(&t, d0, bits, k);
    }
    return t;
}
/* 주어짐: k = 1..3 전수 (조합 a<b<c), 데이터 d0 고정 */
static camp_t run_exhaustive(unsigned k, uint64_t d0) {
    camp_t t = {0};
    unsigned b[3];
    for (b[0] = 0; b[0] < 72; b[0]++) {
        if (k == 1) { camp_trial(&t, d0, b, 1); continue; }
        for (b[1] = b[0] + 1; b[1] < 72; b[1]++) {
            if (k == 2) { camp_trial(&t, d0, b, 2); continue; }
            for (b[2] = b[1] + 1; b[2] < 72; b[2]++) camp_trial(&t, d0, b, 3);
        }
    }
    return t;
}

/* ---------------- Part C: 메모리 컨트롤러 읽기 경로 ---------------- */
#define MC_WORDS 256u
typedef struct {
    uint64_t data[MC_WORDS];   /* DRAM 배열: DQ[63:0] */
    uint8_t  chk[MC_WORDS];    /* DRAM 배열: CB[7:0]  */
    uint32_t ce_cnt, ue_cnt;
    int32_t  last_ce_addr, last_ue_addr;
    uint8_t  last_ce_syn, last_ue_syn;
    bool     scrub_on_ce;      /* CE 를 고친 코드워드를 배열에 다시 쓴다 (demand scrub) */
} mc_t;

static void mc_init(mc_t *m, bool scrub_on_ce) {          /* 주어짐 */
    memset(m, 0, sizeof *m);                               /* enc(0) = 0x00 → 0 으로 채우면 유효 */
    m->last_ce_addr = m->last_ue_addr = -1;
    m->scrub_on_ce = scrub_on_ce;
}
static void mc_inject(mc_t *m, uint32_t addr, unsigned bit) { /* 주어짐: 배열의 비트를 직접 뒤집는 테스트 훅 */
    flip72(&m->data[addr % MC_WORDS], &m->chk[addr % MC_WORDS], bit);
}

/* 쓰기: 데이터와 체크 바이트를 함께 저장. 범위 밖이면 -1 */
static int ecc_write(mc_t *m, uint32_t addr, uint64_t d) {
    (void)m; (void)addr; (void)d;
    return 0; /* TODO: 범위 검사, data + chk 저장 */
}

/* 읽기: 디코드 → *out 에 (정정된) 데이터, *res 에 상태. CE/UE 카운터와 마지막 주소·신드롬 기록.
 * CE + scrub_on_ce 면 고친 코드워드를 다시 쓴다. UE 면 원시 데이터를 돌려주고 배열은 그대로 둔다
 * (호출자가 poison 처리). 범위 밖이면 -1 */
static int ecc_read(mc_t *m, uint32_t addr, uint64_t *out, ecc_result_t *res) {
    (void)m; (void)addr;
    *out = 0;
    res->st = ECC_NO_ERR; res->bit = -1; res->syn = 0;
    return 0; /* TODO: 범위 검사, 디코드, 카운터, scrub-on-CE */
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static void print_camp(const char *name, camp_t t) {
    printf("%-22s n=%-8u corrected=%-6u ue=%-8u mis_data=%-6u mis_check=%-5u silent=%u\n",
           name, t.n, t.corrected, t.ue, t.mis_data, t.mis_check, t.silent);
}

int main(void) {
    secded_init();

    /* ---- Part A: 테이블 ---- */
    CHECK(POS[0] == 3 && POS[1] == 5 && POS[4] == 9 && POS[37] == 44 && POS[63] == 71);
    {
        bool ok = true; uint8_t seen[72] = {0};
        for (int i = 0; i < 64; i++) {
            if (POS[i] < 3 || POS[i] > 71 || is_pow2(POS[i]) || seen[POS[i]]) ok = false;
            else seen[POS[i]] = 1;
            if (i && POS[i] <= POS[i - 1]) ok = false;
        }
        CHECK(ok);
    }
    {
        int ce = 0, ue = 0, noerr = 0;
        for (int s = 0; s < 256; s++) {
            if (SYN_TAB[s] == SYN_NOERR) noerr++;
            else if (SYN_TAB[s] == SYN_UE) ue++;
            else ce++;
        }
        printf("SYN_TAB: %zu bytes, CE entries=%d, UE entries=%d, NO_ERR entries=%d\n",
               sizeof SYN_TAB, ce, ue, noerr);
        CHECK(ce == 72 && ue == 183 && noerr == 1);
        CHECK(SYN_TAB[0x80] == 71 && SYN_TAB[0x81] == 64 && SYN_TAB[0xC0] == 70 && SYN_TAB[0x83] == 0);
    }

    /* ---- Part A: 알려진 벡터 + 기준 구현 대조 ---- */
    CHECK(secded_encode(0) == 0x00);
    CHECK(secded_encode(1) == 0x83);                       /* d0 @ 위치 3 → c0=c1=1, 전체 패리티 c7=1 */
    CHECK(secded_encode(1ull << 63) == 0xC7);              /* d63 @ 위치 71 = 1000111b */
    CHECK(secded_encode(0x0123456789ABCDEFull) == 0x9C);
    CHECK(secded_encode(~0ull) == 0xFF);
    {
        uint64_t rng = 0x9E3779B97F4A7C15ull; int bad = 0, nz = 0;
        for (int i = 0; i < 100000; i++) {
            uint64_t d = xs64(&rng);
            uint8_t c = secded_encode(d);
            if (c != ref_encode(d)) bad++;
            if (secded_syndrome(d, c) != 0) nz++;
        }
        printf("encode vs ref_encode: 100000 random words, mismatches=%d, nonzero clean syndromes=%d\n", bad, nz);
        CHECK(bad == 0);
        CHECK(nz == 0);
    }
    /* 예제: 0x0123456789ABCDEF 의 데이터 비트 37 → 신드롬 0x80|44 = 0xAC, CE_DATA 37 */
    {
        uint64_t d = 0x0123456789ABCDEFull ^ (1ull << 37); uint8_t c = 0x9C;
        ecc_result_t r = secded_decode(&d, &c);
        CHECK(r.st == ECC_CE_DATA && r.bit == 37 && r.syn == 0xAC && d == 0x0123456789ABCDEFull && c == 0x9C);
        d = 0x0123456789ABCDEFull; c = 0x9C ^ 0x80;        /* c7 하나 */
        r = secded_decode(&d, &c);
        CHECK(r.st == ECC_CE_CHECK && r.bit == 7 && r.syn == 0x80 && c == 0x9C);
        d = 0x0123456789ABCDEFull ^ (1ull << 0) ^ (1ull << 1); c = 0x9C;   /* 위치 3,5 → s=6, p=0 */
        r = secded_decode(&d, &c);
        CHECK(r.st == ECC_UE && r.syn == 0x06 && d == (0x0123456789ABCDEFull ^ 3ull) && c == 0x9C);
    }

    /* ---- Part A: 단일 비트 전수 (72 위치 × 64 데이터 워드), 보고된 비트 번호까지 ---- */
    {
        uint64_t words[64] = { 0, ~0ull, 0xAAAAAAAAAAAAAAAAull, 0x5555555555555555ull,
                               0x0123456789ABCDEFull, 1, 1ull << 63 };
        uint64_t rng = 12345;
        for (int i = 7; i < 64; i++) words[i] = xs64(&rng);
        camp_t t = {0}; int badbit = 0;
        for (int w = 0; w < 64; w++)
            for (unsigned b = 0; b < 72; b++) {
                camp_trial(&t, words[w], &b, 1);
                uint64_t d = words[w]; uint8_t c = secded_encode(d);
                flip72(&d, &c, b);
                ecc_result_t r = secded_decode(&d, &c);
                int want = b < 64 ? (int)b : (int)b - 64;
                if (r.bit != want || r.st != (b < 64 ? ECC_CE_DATA : ECC_CE_CHECK)) badbit++;
            }
        print_camp("single, exhaustive", t);
        CHECK(t.n == 4608 && t.corrected == 4608);
        CHECK(badbit == 0);
    }

    /* ---- Part B: 2비트 전수 (C(72,2)=2556) × 4 데이터 워드 ---- */
    {
        camp_t a = run_exhaustive(2, 0), b = run_exhaustive(2, ~0ull),
               c = run_exhaustive(2, 0x0123456789ABCDEFull), d = run_exhaustive(2, 0xDEADBEEFCAFEF00Dull);
        print_camp("double, exhaustive", a);
        CHECK(a.n == 2556 && a.ue == 2556);
        CHECK(b.ue == 2556 && c.ue == 2556 && d.ue == 2556);
    }
    /* ---- Part B: 무작위 2비트 1,000,000회 → 전부 UE, 오정정 0 ---- */
    {
        camp_t t = run_random(2, 1000000u, 0x2545F4914F6CDD1Dull);
        print_camp("double, random 1M", t);
        CHECK(t.n == 1000000u && t.ue == 1000000u);
        CHECK(t.mis_data == 0 && t.mis_check == 0 && t.silent == 0 && t.corrected == 0);
    }
    /* ---- Part B: 3비트 전수 (C(72,3)=59640) — SECDED 의 보장 밖. 정직하게 센다 ---- */
    {
        camp_t a = run_exhaustive(3, 0), b = run_exhaustive(3, 0xDEADBEEFCAFEF00Dull);
        print_camp("triple, exhaustive", a);
        CHECK(a.n == 59640 && a.ue == 14336 && a.mis_data == 40320 && a.mis_check == 4984);
        CHECK(a.silent == 0 && a.corrected == 0);
        CHECK(memcmp(&a, &b, sizeof a) == 0);              /* 선형 → 결과가 데이터와 무관 */
        camp_t r = run_random(3, 100000u, 777);
        print_camp("triple, random 100k", r);
        CHECK(r.ue > 23000 && r.ue < 25000);               /* 14336/59640 = 24.04 % 근처 */
    }

    /* ---- Part C: 메모리 컨트롤러 읽기 경로 ---- */
    {
        static mc_t m;                                     /* 정적: 스택 대신 (펌웨어 스타일) */
        uint64_t v; ecc_result_t r; bool ok = true;
        mc_init(&m, false);
        for (uint32_t a = 0; a < MC_WORDS; a++) ok &= ecc_write(&m, a, 0x1111111111111111ull * (a & 0xF) ^ a) == 0;
        for (uint32_t a = 0; a < MC_WORDS; a++) {
            ecc_read(&m, a, &v, &r);
            if (r.st != ECC_NO_ERR || v != (0x1111111111111111ull * (a & 0xF) ^ a)) ok = false;
        }
        CHECK(ok && m.ce_cnt == 0 && m.ue_cnt == 0);

        uint64_t want = 0x0123456789ABCDEFull;
        ecc_write(&m, 10, want);
        mc_inject(&m, 10, 37);                             /* 셀 하나 뒤집힘 */
        CHECK(ecc_read(&m, 10, &v, &r) == 0 && v == want && r.st == ECC_CE_DATA && r.bit == 37);
        CHECK(m.ce_cnt == 1 && m.last_ce_addr == 10 && m.last_ce_syn == 0xAC);
        ecc_read(&m, 10, &v, &r);                          /* scrub 없음 → 같은 오류를 또 센다 */
        CHECK(r.st == ECC_CE_DATA && m.ce_cnt == 2 && m.data[10] != want);
        mc_inject(&m, 10, 5);                              /* 두 번째 셀 → 누적 = 2비트 */
        uint64_t raw = m.data[10];
        CHECK(ecc_read(&m, 10, &v, &r) == 0 && r.st == ECC_UE && v == raw && m.data[10] == raw);
        CHECK(m.ue_cnt == 1 && m.last_ue_addr == 10 && m.ce_cnt == 2);

        mc_init(&m, true);                                 /* scrub-on-CE 켬 */
        ecc_write(&m, 20, want);
        mc_inject(&m, 20, 37);
        ecc_read(&m, 20, &v, &r);
        CHECK(r.st == ECC_CE_DATA && v == want && m.data[20] == want && m.ce_cnt == 1);
        ecc_read(&m, 20, &v, &r);
        CHECK(r.st == ECC_NO_ERR && m.ce_cnt == 1);        /* 고쳐 썼으니 깨끗 */
        mc_inject(&m, 20, 5);                              /* 같은 워드의 다른 셀: 누적 안 됨 → 다시 CE */
        ecc_read(&m, 20, &v, &r);
        CHECK(r.st == ECC_CE_DATA && r.bit == 5 && v == want && m.ue_cnt == 0);

        mc_inject(&m, 30, 71);                             /* c7 만: 데이터는 멀쩡, CE_CHECK 7 */
        ecc_read(&m, 30, &v, &r);
        CHECK(r.st == ECC_CE_CHECK && r.bit == 7 && v == 0 && m.chk[30] == 0);
        mc_inject(&m, 40, 2); mc_inject(&m, 40, 66);       /* 2비트 → 다시 쓰면(write) 깨끗해진다 */
        ecc_write(&m, 40, 7);
        ecc_read(&m, 40, &v, &r);
        CHECK(r.st == ECC_NO_ERR && v == 7);
        CHECK(ecc_read(&m, MC_WORDS, &v, &r) == -1 && ecc_write(&m, MC_WORDS, 1) == -1);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
