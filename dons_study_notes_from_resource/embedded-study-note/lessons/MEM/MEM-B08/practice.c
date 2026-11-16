/* MEM-B08 추가 연습 — 한 번에 검증
 *   연습 1  테스트 비용 공식: data 64 / address L^2+5L+2 / device 4n 접근, 실제 크기로 환산
 *   연습 2  device 테스트의 증가 패턴은 상위 데이터 비트 short 171쌍을 놓친다 → 곱셈 해시 패턴
 *   연습 3  8-bit 메모리: 패턴이 256 마다 반복 — Barr 의 "확인하며 반전" 은 alias 를 잡고, 분리 패스는 놓친다
 *   연습 4  2의 거듭제곱이 아닌 크기(3072 word)에서 Barr 의 addressMask 루프가 A10/A11 을 건너뛴다
 * (solution.c 의 RAM 모델 축약판을 내장 — 주소/데이터 선 결함만, 데이터 폭은 dmask 로 8/32비트)
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o /tmp/MEM-B08_pr && /tmp/MEM-B08_pr
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MAXW 4096u
typedef enum { F_NONE, F_DATA_AND, F_DATA_OR, F_ADDR_SA0, F_ADDR_SA1 } fk;
typedef struct { fk kind; unsigned a, b; } fault_t;
typedef struct { uint32_t mem[MAXW]; uint32_t n, dmask; fault_t f; uint64_t nops; } ram_t;

static uint32_t phys(const ram_t *r, uint32_t la) {
    uint32_t p = la % MAXW;                       /* 핀은 12개 (A0..A11) — n 과 무관 */
    if (r->f.kind == F_ADDR_SA0) p &= ~(1u << r->f.a);
    if (r->f.kind == F_ADDR_SA1) p |= 1u << r->f.a;
    return p;
}
static uint32_t dx(const ram_t *r, uint32_t v) {
    const uint32_t ma = 1u << r->f.a, mb = 1u << r->f.b, m = ma | mb;
    v &= r->dmask;
    if (r->f.kind == F_DATA_AND && (v & m) != m) v &= ~m;
    if (r->f.kind == F_DATA_OR && (v & m)) v |= m;
    return v;
}
static void wr(ram_t *r, uint32_t la, uint32_t v) { r->mem[phys(r, la)] = dx(r, v); r->nops++; }
static uint32_t rd(ram_t *r, uint32_t la) { r->nops++; return dx(r, r->mem[phys(r, la)]); }
static void ram_init(ram_t *r, uint32_t n, uint32_t dmask, fault_t f) {
    uint32_t s = 0x1234567u;
    r->n = n; r->dmask = dmask; r->f = f; r->nops = 0;
    for (uint32_t i = 0; i < MAXW; i++) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; r->mem[i] = s & dmask; }
}

/* Barr 3종 (축약, 0 = PASS) */
static int t_data(ram_t *r) {
    for (uint32_t p = 1; p & r->dmask; p <<= 1) { wr(r, 0, p); if (rd(r, 0) != p) return 1; }
    return 0;
}
/* use_lt = 0: Barr 원본 `(off & mask) != 0`,  1: 고친 루프 `off < n` */
static int t_addr(ram_t *r, int use_lt) {
    const uint32_t mask = r->n - 1u, P = 0xAAAAAAAAu & r->dmask, A = 0x55555555u & r->dmask;
#define MORE(o) (use_lt ? (o) < r->n : ((o) & mask) != 0)
    for (uint32_t o = 1; MORE(o); o <<= 1) wr(r, o, P);
    wr(r, 0, A);
    for (uint32_t o = 1; MORE(o); o <<= 1) if (rd(r, o) != P) return 1;
    wr(r, 0, P);
    for (uint32_t t = 1; MORE(t); t <<= 1) {
        wr(r, t, A);
        if (rd(r, 0) != P) return 1;
        for (uint32_t o = 1; MORE(o); o <<= 1) if (rd(r, o) != P && o != t) return 1;
        wr(r, t, P);
    }
#undef MORE
    return 0;
}
/* pat(i): 0 = Barr 증가 패턴 i+1,  1 = 곱셈 해시 (i+1)*0x9E3779B1 (홀수 곱 → 2^32 에서 전단사) */
static uint32_t pat(int kind, uint32_t i) { return kind ? (i + 1u) * 0x9E3779B1u : i + 1u; }
static int t_device(ram_t *r, int pk) {               /* Barr: 확인하며 반전 */
    const uint32_t m = r->dmask;
    for (uint32_t o = 0; o < r->n; o++) wr(r, o, pat(pk, o) & m);
    for (uint32_t o = 0; o < r->n; o++) { if (rd(r, o) != (pat(pk, o) & m)) return 1; wr(r, o, ~pat(pk, o) & m); }
    for (uint32_t o = 0; o < r->n; o++) if (rd(r, o) != (~pat(pk, o) & m)) return 1;
    return 0;
}
static int t_device_split(ram_t *r) {                 /* 흔한 변형: 채우고-확인, 반전 채우고-확인 (분리 패스) */
    const uint32_t m = r->dmask;
    for (uint32_t o = 0; o < r->n; o++) wr(r, o, (o + 1u) & m);
    for (uint32_t o = 0; o < r->n; o++) if (rd(r, o) != ((o + 1u) & m)) return 1;
    for (uint32_t o = 0; o < r->n; o++) wr(r, o, ~(o + 1u) & m);
    for (uint32_t o = 0; o < r->n; o++) if (rd(r, o) != (~(o + 1u) & m)) return 1;
    return 0;
}

/* 연습 1: 공식 */
static uint64_t ops_data(unsigned width) { return 2u * width; }
static uint64_t ops_addr(unsigned L) { return (uint64_t)L * L + 5u * L + 2u; }
static uint64_t ops_device(uint64_t n) { return 4u * n; }
static unsigned lg2(uint64_t n) { unsigned L = 0; while ((1ull << L) < n) L++; return L; }

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static ram_t R;

int main(void) {
    fault_t none = { F_NONE, 0, 0 };

    /* ---- 연습 1 ---- */
    for (uint32_t n = 16; n <= MAXW; n <<= 4) {
        ram_init(&R, n, 0xFFFFFFFFu, none); CHECK(t_data(&R) == 0 && R.nops == ops_data(32));
        ram_init(&R, n, 0xFFFFFFFFu, none); CHECK(t_addr(&R, 0) == 0 && R.nops == ops_addr(lg2(n)));
        ram_init(&R, n, 0xFFFFFFFFu, none); CHECK(t_device(&R, 0) == 0 && R.nops == ops_device(n));
    }
    const uint64_t words = (2ull << 30) / 8u;             /* 2 GiB, 64-bit 워드 */
    CHECK(lg2(words) == 28 && ops_addr(28) == 926u && ops_device(words) == 1073741824ull);
    printf("2 GiB / 64-bit word: L=%u  data=%llu  address=%llu  device=%llu accesses (%llu GiB moved)\n",
           lg2(words), (unsigned long long)ops_data(64), (unsigned long long)ops_addr(lg2(words)),
           (unsigned long long)ops_device(words), (unsigned long long)(ops_device(words) * 8u >> 30));

    /* ---- 연습 2: 데이터 선 short 992개에 대한 device 테스트 미검출 수 ---- */
    unsigned miss[2] = { 0, 0 }, alias_ok[2] = { 0, 0 };
    for (int pk = 0; pk < 2; pk++) {
        for (unsigned a = 0; a < 32; a++)
            for (unsigned b = a + 1; b < 32; b++)
                for (fk k = F_DATA_AND; k <= F_DATA_OR; k++) {
                    ram_init(&R, MAXW, 0xFFFFFFFFu, (fault_t){ k, a, b });
                    miss[pk] += (unsigned)(t_device(&R, pk) == 0);
                }
        for (unsigned a = 0; a < 12; a++)
            for (fk k = F_ADDR_SA0; k <= F_ADDR_SA1; k++) {
                ram_init(&R, MAXW, 0xFFFFFFFFu, (fault_t){ k, a, 0 });
                alias_ok[pk] += (unsigned)t_device(&R, pk);
            }
    }
    printf("device test, data-line shorts missed of 992: increment=%u  hash=%u ; addr SA caught of 24: %u / %u\n",
           miss[0], miss[1], alias_ok[0], alias_ok[1]);
    CHECK(miss[0] == 2u * 171u);                          /* C(19,2) 쌍 × AND/OR: 비트 13..31 은 늘 같은 값 */
    CHECK(miss[1] == 0u);
    CHECK(alias_ok[0] == 24u && alias_ok[1] == 24u);      /* 해시도 전단사라 alias 탐지는 그대로 */

    /* ---- 연습 3: 8-bit × 4096 메모리, 주소 선 SA 24개 ---- */
    unsigned inplace = 0, split = 0, split_hi_missed = 0;
    for (unsigned a = 0; a < 12; a++)
        for (fk k = F_ADDR_SA0; k <= F_ADDR_SA1; k++) {
            ram_init(&R, MAXW, 0xFFu, (fault_t){ k, a, 0 }); inplace += (unsigned)t_device(&R, 0);
            ram_init(&R, MAXW, 0xFFu, (fault_t){ k, a, 0 });
            int d = t_device_split(&R); split += (unsigned)d;
            if (!d && a >= 8) split_hi_missed++;
        }
    printf("8-bit RAM, addr SA caught of 24: in-place invert=%u  split passes=%u (missed A8..A11: %u)\n",
           inplace, split, split_hi_missed);
    CHECK(inplace == 24u);
    CHECK(split == 16u && split_hi_missed == 8u);

    /* ---- 연습 4: 3072 word (2의 거듭제곱 아님), 주소 선 SA 24개 ---- */
    unsigned orig = 0, fixed = 0, orig_missed_mask = 0;
    for (unsigned a = 0; a < 12; a++)
        for (fk k = F_ADDR_SA0; k <= F_ADDR_SA1; k++) {
            ram_init(&R, 3072, 0xFFFFFFFFu, (fault_t){ k, a, 0 });
            int d = t_addr(&R, 0); orig += (unsigned)d;
            if (!d) orig_missed_mask |= 1u << a;
            ram_init(&R, 3072, 0xFFFFFFFFu, (fault_t){ k, a, 0 }); fixed += (unsigned)t_addr(&R, 1);
        }
    printf("3072-word RAM, addr SA caught of 24: (off & mask) loop=%u  (off < n) loop=%u  missed lines mask=0x%03X\n",
           orig, fixed, orig_missed_mask);
    CHECK(orig == 20u && orig_missed_mask == 0xC00u);     /* mask 0xBFF: 1024 & mask == 0 → A10, A11 미검사 */
    CHECK(fixed == 24u);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
