/* ARM-B06 추가 연습 — 하네스 (답은 practice.S)
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c practice.S shared_bugs.S -o /tmp/ARM-B06_p && /tmp/ARM-B06_p
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#ifdef __APPLE__
#include <sys/sysctl.h>
#endif

int      ctz64(uint64_t x);
int      ctz64_cssc(uint64_t x);
uint64_t field_insert_var(uint64_t reg, unsigned lsb, unsigned width, uint64_t val);
uint32_t rotl32(uint32_t x, unsigned n);
uint64_t nibrev64(uint64_t x);
int      cls32(int32_t x);
uint32_t log_pack(uint32_t word, uint32_t stat);
uint32_t irq_mask_all_except(uint32_t imr, uint32_t keep);

/* ---------------- C 기준 구현 ---------------- */
static int ref_ctz(uint64_t x) { for (int i = 0; i < 64; i++) if ((x >> i) & 1u) return i; return 64; }
static uint64_t ref_insert(uint64_t r, unsigned lsb, unsigned w, uint64_t v) {
    uint64_t m = (w >= 64) ? ~UINT64_C(0) : ((UINT64_C(1) << w) - 1u);
    return (r & ~(m << lsb)) | ((v & m) << lsb);
}
static uint32_t ref_rotl(uint32_t x, unsigned n) {
    n &= 31u;
    return n ? (x << n) | (x >> (32u - n)) : x;            /* n==0 에서 x>>32 (UB) 를 피한다 */
}
static uint64_t ref_nibrev64(uint64_t x) {
    uint64_t r = 0;
    for (unsigned i = 0; i < 16; i++) r |= ((x >> (4u * i)) & 0xFu) << (4u * (15u - i));
    return r;
}
static int ref_cls32(int32_t x) {
    uint32_t u = (uint32_t)x, s = u >> 31;
    int n = 0;
    for (int b = 30; b >= 0 && ((u >> b) & 1u) == s; b--) n++;
    return n;
}

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; printf("PASS %s\n", #expr); } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint64_t g_rng = 0x9E3779B97F4A7C15ull;
static uint64_t rnd(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 7; g_rng ^= g_rng << 17; return g_rng; }

static bool have_cssc(void) {
#ifdef __APPLE__
    int v = 0; size_t n = sizeof v;
    return sysctlbyname("hw.optional.arm.FEAT_CSSC", &v, &n, NULL, 0) == 0 && v == 1;
#else
    return false;                                          /* Linux: getauxval(AT_HWCAP2) 로 확인 가능 — 여기선 생략 */
#endif
}
static bool random_ctz(bool cssc) {
    for (int i = 0; i < 100000; i++) {
        uint64_t x = rnd() << (rnd() & 63);
        if (ctz64(x) != ref_ctz(x)) return false;
        if (cssc && ctz64_cssc(x) != ref_ctz(x)) return false;
    }
    return true;
}
static bool exhaustive_insert(void) {
    for (unsigned lsb = 0; lsb < 64; lsb++)
        for (unsigned w = 1; lsb + w <= 64; w++)
            for (int k = 0; k < 8; k++) {
                uint64_t r = rnd(), v = rnd();
                if (field_insert_var(r, lsb, w, v) != ref_insert(r, lsb, w, v)) return false;
            }
    return true;
}
static bool all_rot(void) {
    for (unsigned n = 0; n < 64; n++)
        for (int k = 0; k < 1000; k++) {
            uint32_t x = (uint32_t)rnd();
            if (rotl32(x, n) != ref_rotl(x, n)) return false;
        }
    return true;
}
static bool random_misc(void) {
    for (int i = 0; i < 100000; i++) {
        uint64_t x = rnd();
        int32_t s = (int32_t)(uint32_t)(x >> (rnd() & 31));     /* uint32→int32 변환은 구현 정의 (clang: 비트 그대로) */
        uint32_t w = (uint32_t)rnd(), st = (uint32_t)rnd();
        if (nibrev64(x) != ref_nibrev64(x)) return false;
        if (cls32(s) != ref_cls32(s)) return false;
        if (log_pack(w, st) != ((w & ~0xFFu) | ((st >> 8) & 0xFFu))) return false;
        if (irq_mask_all_except(w, st) != (w | ~st)) return false;
    }
    return true;
}

int main(void) {
    bool cssc = have_cssc();
    /* 연습 1 */
    CHECK(ctz64(0) == 64 && ctz64(1) == 0 && ctz64(0x8000000000000000ull) == 63 && ctz64(0x50) == 4);
    if (cssc) CHECK(ctz64_cssc(0) == 64 && ctz64_cssc(0x50) == 4);
    else printf("SKIP ctz64_cssc (FEAT_CSSC 없음)\n");
    CHECK(random_ctz(cssc));
    /* 연습 2 */
    CHECK(field_insert_var(0xFFFFFFFFFFFFFFFFull, 4, 4, 0x0) == 0xFFFFFFFFFFFFFF0Full);
    CHECK(field_insert_var(0, 60, 4, 0x1F) == 0xF000000000000000ull);
    CHECK(field_insert_var(0x1234, 0, 64, 0xCAFEBABEDEADBEEFull) == 0xCAFEBABEDEADBEEFull);
    CHECK(exhaustive_insert());
    /* 연습 3 */
    CHECK(rotl32(0x80000001u, 1) == 0x00000003u && rotl32(0x12345678u, 0) == 0x12345678u);
    CHECK(rotl32(0x12345678u, 8) == 0x34567812u && rotl32(0x12345678u, 40) == 0x34567812u);
    CHECK(all_rot());
    /* 연습 4 */
    CHECK(nibrev64(0x0123456789ABCDEFull) == 0xFEDCBA9876543210ull);
    /* 연습 5 */
    CHECK(cls32(0) == 31 && cls32(-1) == 31 && cls32(1) == 30 && cls32(INT32_MIN) == 0 && cls32(INT32_MAX) == 0);
    CHECK(cls32(0x00012345) == 14 && cls32(-0x00012345) == 14);
    /* 연습 6 */
    CHECK(log_pack(0xAABBCCDDu, 0x30E507A1u) == 0xAABBCC07u);
    CHECK(irq_mask_all_except(0x00000000u, 0x00000024u) == 0xFFFFFFDBu);
    CHECK(random_misc());

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
