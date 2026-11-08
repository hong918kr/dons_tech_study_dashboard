/* SIV-M14 추가 연습 — 한 번에 검증
 *
 * 연습 1  3비트 오류 전수 조사: SECDED 가 "보장하지 않는" 영역에서 실제로 무슨 일이 일어나나
 * 연습 2  주소 비트 커버리지: 어떤 테스트 주소 집합이 addr_drop 결함을 놓치나
 * 연습 3  CE 트리아지: soft error 인가 hard error(stuck cell) 인가
 * 연습 4  부분 쓰기(RMW)에서 UE 를 세탁(launder)하지 않기 — poison 전파
 *
 * 코덱은 MEM-B07 의 SECDED(72,64) 축약(solution.c 와 같은 H 행렬), 메모리 컨트롤러 mock 은 레지스터 없이
 * 함수 수준으로 축약했다 (solution.c 의 DMC0 와 같은 판정 규칙).
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---------------- 코덱 (solution.c 와 동일) ---------------- */
static uint8_t POS[64];
static int8_t  DBIT[128];
static void codec_init(void) {
    int i = 0;
    for (int p = 0; p < 128; p++) DBIT[p] = -1;
    for (int p = 3; p <= 71; p++)
        if (p & (p - 1)) { POS[i] = (uint8_t)p; DBIT[p] = (int8_t)i; i++; }
}
static unsigned parity64(uint64_t x) {
    x ^= x >> 32; x ^= x >> 16; x ^= x >> 8; x ^= x >> 4; x ^= x >> 2; x ^= x >> 1;
    return (unsigned)(x & 1);
}
static unsigned ctz64(uint64_t x) { unsigned n = 0; while (x && !(x & 1)) { x >>= 1; n++; } return n; }
static uint8_t hsyn64(uint64_t d) {
    uint8_t s = 0;
    for (int i = 0; i < 64; i++) if (d >> i & 1) s ^= POS[i];
    return s;
}
static uint8_t secded_encode(uint64_t d) {
    uint8_t c = hsyn64(d);
    return (uint8_t)(c | (parity64(d) ^ parity64(c)) << 7);
}
static uint8_t secded_syndrome(uint64_t d, uint8_t c) {
    uint8_t s = (uint8_t)(hsyn64(d) ^ (c & 0x7F));
    return (uint8_t)(s | (parity64(d) ^ parity64(c)) << 7);
}
enum { ECC_OK, ECC_CE, ECC_UE };
static int secded_classify(uint8_t syn, unsigned *bitnum) {
    unsigned s = syn & 0x7Fu, p = syn >> 7;
    if (!p) return s ? ECC_UE : ECC_OK;
    if (s == 0) { *bitnum = 71; return ECC_CE; }
    if ((s & (s - 1)) == 0) { *bitnum = 64 + ctz64(s); return ECC_CE; }
    if (DBIT[s] >= 0) { *bitnum = (unsigned)DBIT[s]; return ECC_CE; }
    return ECC_UE;
}
/* 코드워드 비트 b (0..63 데이터, 64..71 체크)를 뒤집은 오류 패턴 */
static void flip(unsigned b, uint64_t *md, uint8_t *mc) {
    if (b < 64) *md ^= 1ull << b; else *mc ^= (uint8_t)(1u << (b - 64));
}

/* ---------------- 함수 수준 메모리 컨트롤러 mock ---------------- */
#define WORDS 64u
enum { RESP_OKAY = 0, RESP_SLVERR = 2 };
#define POISON_CHK 0x03u     /* c0,c1 을 뒤집음 → s=3, p=0 → 어떤 데이터든 반드시 UE */
typedef struct {
    uint32_t addr_drop;                                  /* 캡처 주소에서 빠지는 비트 */
    bool     launder;                                    /* RMW 가 UE 워드를 깨끗한 ECC 로 다시 씀 */
    bool     stuck; uint32_t stuck_addr; unsigned stuck_bit; unsigned stuck_val;   /* 셀 고장 */
} pfault_t;
static struct {
    uint64_t data[WORDS]; uint8_t chk[WORDS];
    unsigned ce_cnt, ue_cnt; bool ce_valid; uint32_t ce_addr;
    pfault_t f;
} P;
static void pm_init(const pfault_t *f) { memset(&P, 0, sizeof P); if (f) P.f = *f; }
static uint64_t cell_read(unsigned i) {                 /* 셀 고장은 저장 후 읽을 때 드러난다 */
    uint64_t d = P.data[i];
    if (P.f.stuck && P.f.stuck_addr == i * 8u)
        d = (d & ~(1ull << P.f.stuck_bit)) | (uint64_t)P.f.stuck_val << P.f.stuck_bit;
    return d;
}
static void pm_write64(uint32_t addr, uint64_t d) { P.data[addr >> 3] = d; P.chk[addr >> 3] = secded_encode(d); }
static void pm_inject(uint32_t addr, uint64_t d, uint64_t md, uint8_t mc) {
    P.data[addr >> 3] = d ^ md; P.chk[addr >> 3] = (uint8_t)(secded_encode(d) ^ mc);
}
static int pm_decode(uint32_t addr, uint64_t *out) {   /* 판정 + 보고 */
    unsigned i = addr >> 3, bit = 0;
    uint64_t d = cell_read(i);
    int cls = secded_classify(secded_syndrome(d, P.chk[i]), &bit);
    if (cls == ECC_CE) {
        if (bit < 64) d ^= 1ull << bit;
        if (!P.ce_valid) { P.ce_valid = true; P.ce_addr = addr & ~P.f.addr_drop; }
        P.ce_cnt++;
    } else if (cls == ECC_UE) P.ue_cnt++;
    *out = d;
    return cls;
}
static int pm_read64(uint32_t addr, uint64_t *out) { return pm_decode(addr, out) == ECC_UE ? RESP_SLVERR : RESP_OKAY; }
/* 바이트 쓰기 = read-modify-write. 옛 워드가 UE 면 병합 후 poison 을 남겨야 한다 (세탁 금지) */
static void pm_write8(uint32_t byte_addr, uint8_t v) {
    uint32_t addr = byte_addr & ~7u;
    unsigned sh = (byte_addr & 7u) * 8u;
    uint64_t old;
    int cls = pm_decode(addr, &old);                     /* CE 면 정정된 값, UE 면 원시값 */
    uint64_t merged = (old & ~(0xFFull << sh)) | (uint64_t)v << sh;
    pm_write64(addr, merged);
    if (cls == ECC_UE && !P.f.launder) P.chk[addr >> 3] ^= POISON_CHK;
}

/* ---------------- 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

/* ===== 연습 1: 1·2·3비트 오류 전수 조사 ===== */
typedef struct { unsigned single_ok, double_ue, triple_total, triple_ue, triple_mis_data, triple_mis_chk; } survey_t;
static void survey(survey_t *s) {
    memset(s, 0, sizeof *s);
    unsigned bn;
    for (unsigned a = 0; a < 72; a++) {
        uint64_t md = 0; uint8_t mc = 0;
        flip(a, &md, &mc);
        s->single_ok += secded_classify(secded_syndrome(md, mc), &bn) == ECC_CE && bn == a;
        for (unsigned b = a + 1; b < 72; b++) {
            uint64_t md2 = md; uint8_t mc2 = mc;
            flip(b, &md2, &mc2);
            s->double_ue += secded_classify(secded_syndrome(md2, mc2), &bn) == ECC_UE;
            for (unsigned c = b + 1; c < 72; c++) {
                uint64_t md3 = md2; uint8_t mc3 = mc2;
                flip(c, &md3, &mc3);
                int cls = secded_classify(secded_syndrome(md3, mc3), &bn);
                s->triple_total++;
                if (cls == ECC_UE) s->triple_ue++;
                else if (bn < 64) s->triple_mis_data++;          /* 네 번째 비트를 뒤집는다: 조용한 오염 */
                else s->triple_mis_chk++;                        /* 데이터 3비트 오염된 채 "정정됨" */
            }
        }
    }
}
/* 선형성 확인: 0 데이터에서 계산한 신드롬 = 임의 데이터에서 계산한 신드롬 */
static bool syndrome_is_data_independent(void) {
    uint64_t x = 0x9E3779B97F4A7C15ull;
    for (int t = 0; t < 1000; t++) {
        x ^= x << 13; x ^= x >> 7; x ^= x << 17;
        uint64_t md = 1ull << (x % 64) | 1ull << ((x >> 8) % 64);
        uint8_t mc = (uint8_t)(x >> 16);
        uint64_t d = x * 0x2545F4914F6CDD1Dull;
        if (secded_syndrome(d ^ md, (uint8_t)(secded_encode(d) ^ mc)) != secded_syndrome(md, mc)) return false;
    }
    return true;
}

/* ===== 연습 2: 주소 비트 커버리지 ===== */
/* 집합 안에서 0 과 1 을 모두 가져 본 주소 비트 (width_mask 안에서) */
static uint32_t addr_bits_covered(const uint32_t *a, int n, uint32_t width_mask) {
    uint32_t seen1 = 0, seen0 = 0;
    for (int i = 0; i < n; i++) { seen1 |= a[i]; seen0 |= ~a[i]; }
    return seen1 & seen0 & width_mask;
}
/* 주소마다 데이터 비트 0 CE 를 심고 CE 캡처 주소가 맞는지 → 틀린 주소 수 */
static int capture_mismatches(const uint32_t *a, int n, const pfault_t *f) {
    int bad = 0;
    for (int i = 0; i < n; i++) {
        uint64_t d;
        pm_init(f);
        pm_inject(a[i], 0x1234, 1, 0);
        (void)pm_read64(a[i], &d);
        bad += !(P.ce_valid && P.ce_addr == a[i]);
    }
    return bad;
}

/* ===== 연습 3: CE 트리아지 ===== */
enum { TRI_NONE, TRI_SOFT, TRI_HARD };
static const char *const TRI_NAME[] = {"NONE", "SOFT", "HARD"};
/* CE 가 보고된 주소: 읽기(정정값 d) → scrub(d 재기록) → 읽기: CE 면 HARD
 * → 보수 패턴 ~d 쓰고 읽기: CE 면 HARD → d 복원 → SOFT. 처음부터 CE 가 없으면 NONE */
static int triage_ce(uint32_t addr) {
    uint64_t d, t;
    unsigned c0 = P.ce_cnt;
    (void)pm_read64(addr, &d);
    if (P.ce_cnt == c0) return TRI_NONE;
    pm_write64(addr, d);
    unsigned c1 = P.ce_cnt;
    (void)pm_read64(addr, &t);
    if (P.ce_cnt != c1) return TRI_HARD;
    pm_write64(addr, ~d);
    (void)pm_read64(addr, &t);
    int v = P.ce_cnt != c1 ? TRI_HARD : TRI_SOFT;
    pm_write64(addr, d);
    return v;
}

int main(void) {
    codec_init();

    /* 연습 1 */
    survey_t s;
    survey(&s);
    printf("[ex1] single: %u/72 corrected, double: %u/2556 UE, triple: %u total, UE=%u, miscorrect data=%u, miscorrect chk=%u\n",
           s.single_ok, s.double_ue, s.triple_total, s.triple_ue, s.triple_mis_data, s.triple_mis_chk);
    CHECK(s.single_ok == 72 && s.double_ue == 2556);
    CHECK(s.triple_total == 59640 && s.triple_ue + s.triple_mis_data + s.triple_mis_chk == 59640);
    CHECK(s.triple_ue > 0 && s.triple_mis_data > 0 && s.triple_mis_chk > 0);   /* 3비트는 스펙 밖: 섞여 나온다 */
    CHECK(syndrome_is_data_independent());

    /* 연습 2 */
    const uint32_t full[8] = {0x000, 0x008, 0x010, 0x020, 0x040, 0x080, 0x100, 0x1F8};
    const uint32_t few[6]  = {0x000, 0x008, 0x010, 0x020, 0x040, 0x080};
    const uint32_t ones[2] = {0x000, 0x1F8};
    CHECK(addr_bits_covered(full, 8, 0x1F8) == 0x1F8);
    CHECK(addr_bits_covered(few, 6, 0x1F8) == 0x0F8);          /* bit 8 은 한 번도 1 이 아님 */
    CHECK(addr_bits_covered(ones, 2, 0x1F8) == 0x1F8);         /* 모든 비트를 토글하지만 ... */
    pfault_t fd = {.addr_drop = 0x100};
    CHECK(capture_mismatches(few, 6, &fd) == 0);               /* 결함이 숨는다 */
    CHECK(capture_mismatches(full, 8, &fd) == 2);              /* 0x100, 0x1F8 에서 드러남 */
    pfault_t fs = {.addr_drop = 0x018};                        /* 두 비트가 같이 빠지는 결함 */
    CHECK(capture_mismatches(ones, 2, &fs) == 1);              /* {0, 0x1F8} 도 잡긴 하지만 어느 비트인지 모른다 */

    /* 연습 3 */
    pm_init(NULL);
    pm_inject(0x080, 0xF0F0F0F0F0F0F0F0ull, 1ull << 4, 0);    /* 일회성 뒤집힘 (soft) */
    int v1 = triage_ce(0x080);
    pfault_t fst = {.stuck = true, .stuck_addr = 0x0C0, .stuck_bit = 7, .stuck_val = 0};
    pm_init(&fst);
    pm_write64(0x0C0, 0xFFull);                                /* bit7 에 1 을 썼지만 셀은 0 에 고정 */
    int v2 = triage_ce(0x0C0);
    pm_init(NULL);
    pm_write64(0x040, 0x55);
    int v3 = triage_ce(0x040);
    printf("[ex3] soft flip -> %s, stuck-at-0 -> %s, clean -> %s\n", TRI_NAME[v1], TRI_NAME[v2], TRI_NAME[v3]);
    CHECK(v1 == TRI_SOFT && v2 == TRI_HARD && v3 == TRI_NONE);

    /* 연습 4 */
    uint64_t d;
    pm_init(NULL);
    pm_inject(0x100, 0x1122334455667788ull, 3ull << 8, 0);    /* UE 워드 */
    pm_write8(0x100 + 0, 0xAB);                                /* 바이트 0 부분 쓰기 */
    unsigned ue_after_rmw = P.ue_cnt;
    int r = pm_read64(0x100, &d);
    printf("[ex4] good : RMW on UE word -> ue_cnt=%u, next read %s\n", ue_after_rmw, r == RESP_SLVERR ? "SLVERR" : "OKAY");
    CHECK(ue_after_rmw == 1 && r == RESP_SLVERR);              /* poison 이 살아 있다 */
    pfault_t fl = {.launder = true};
    pm_init(&fl);
    pm_inject(0x100, 0x1122334455667788ull, 3ull << 8, 0);
    pm_write8(0x100 + 0, 0xAB);
    r = pm_read64(0x100, &d);
    printf("[ex4] launder: next read %s data=0x%016llX (bits 8,9 still flipped)\n", r == RESP_SLVERR ? "SLVERR" : "OKAY",
           (unsigned long long)d);
    CHECK(r == RESP_OKAY && d == ((0x1122334455667788ull ^ (3ull << 8)) & ~0xFFull) + 0xAB);   /* 조용한 오염 */
    pm_init(NULL);
    pm_inject(0x0C8, 0xCAFEF00DCAFEF00Dull, 1ull << 40, 0);   /* CE 워드에 부분 쓰기: 정정 후 병합 */
    pm_write8(0x0C8 + 7, 0x00);
    unsigned ce_rmw = P.ce_cnt;
    r = pm_read64(0x0C8, &d);
    CHECK(ce_rmw == 1 && r == RESP_OKAY && d == 0x00FEF00DCAFEF00Dull && P.ce_cnt == 1);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
