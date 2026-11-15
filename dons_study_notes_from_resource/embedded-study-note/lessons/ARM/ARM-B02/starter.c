/* ARM-B02 clang -S로 C→AArch64 어셈블리 읽기 — "what does this asm do?"
 *
 * shared_mystery.S = REAL clang -O2 output of 6 hidden C functions (mystery1..mystery6).
 * Read the asm, then write C functions my_mystery1..6 that behave IDENTICALLY
 * (same result for every input, including the weird ones). The harness compares
 * your C against the asm: named examples + exhaustive ranges + xorshift64 random.
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 starter.c shared_mystery.S -o /tmp/ARM-B02_v && /tmp/ARM-B02_v
 *        (solution.c 도 같은 명령, starter.c 자리만 바꾼다)
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---- the asm side (shared_mystery.S) ---- */
struct sensor_tab { uint32_t n; uint16_t id[16]; int16_t temp[16]; };
_Static_assert(offsetof(struct sensor_tab, id) == 4, "id[] at +4");
_Static_assert(offsetof(struct sensor_tab, temp) == 36, "temp[] at +36");

uint32_t mystery1(int32_t a, int32_t b);
long     mystery2(long x, long lo, long hi);
int      mystery3(uint32_t x);
size_t   mystery4(const char *s);
int32_t  mystery5(const struct sensor_tab *t, uint16_t id);
uint32_t mystery6(uint32_t head, uint32_t tail);

/* ---------------- Part A: csel 두 개 읽기 ---------------- */
static uint32_t my_mystery1(int32_t a, int32_t b) {
    (void)a; (void)b;
    return 0; /* TODO: sub / subs / csel gt */
}
static long my_mystery2(long x, long lo, long hi) {
    (void)x; (void)lo; (void)hi;
    return 0; /* TODO: cmp / csel lt / cmp / csel lt */
}

/* ---------------- Part B: 루프 + 부호 함정 ---------------- */
static int my_mystery3(uint32_t x) {
    (void)x;
    return 0; /* TODO: and #1 / add / cmp #3 / lsr #2 / b.hi */
}
static size_t my_mystery4(const char *s) {
    (void)s;
    return 0; /* TODO: ldrsb / cmp #32 / b.gt */
}

/* ---------------- Part C: 펌웨어 코드 역해석 ---------------- */
static int32_t my_mystery5(const struct sensor_tab *t, uint16_t id) {
    (void)t; (void)id;
    return 0; /* TODO: [x9,#-32] vs [x9], *5, +sign, asr #1 */
}
static uint32_t my_mystery6(uint32_t head, uint32_t tail) {
    (void)head; (void)tail;
    return 0; /* TODO: umull 0xaaaaaaab, lsr #36, msub */
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint64_t rng = 0x9E3779B97F4A7C15ull;          /* fixed seed -> deterministic */
static uint64_t xorshift64(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return rng; }

static int shown;                                     /* print only the first mismatch per group */
#define MISMATCH(...) do { if (!shown++) { printf("  first mismatch: "); printf(__VA_ARGS__); } } while (0)

static long cmp1(int32_t a, int32_t b) {
    uint32_t w = mystery1(a, b), g = my_mystery1(a, b);
    if (w != g) { MISMATCH("mystery1(%d, %d): asm=%u mine=%u\n", a, b, w, g); return 1; }
    return 0;
}
static long cmp2(long x, long lo, long hi) {
    long w = mystery2(x, lo, hi), g = my_mystery2(x, lo, hi);
    if (w != g) { MISMATCH("mystery2(%ld, %ld, %ld): asm=%ld mine=%ld\n", x, lo, hi, w, g); return 1; }
    return 0;
}
static long cmp3(uint32_t x) {
    int w = mystery3(x), g = my_mystery3(x);
    if (w != g) { MISMATCH("mystery3(0x%x): asm=%d mine=%d\n", x, w, g); return 1; }
    return 0;
}
static long cmp4(const char *s) {
    size_t w = mystery4(s), g = my_mystery4(s);
    if (w != g) { MISMATCH("mystery4(len %zu string): asm=%zu mine=%zu\n", strlen(s), w, g); return 1; }
    return 0;
}
static long cmp5(const struct sensor_tab *t, uint16_t id) {
    int32_t w = mystery5(t, id), g = my_mystery5(t, id);
    if (w != g) { MISMATCH("mystery5(n=%u, id=%u): asm=%d mine=%d\n", t->n, id, w, g); return 1; }
    return 0;
}
static long cmp6(uint32_t h, uint32_t t) {
    uint32_t w = mystery6(h, t), g = my_mystery6(h, t);
    if (w != g) { MISMATCH("mystery6(%u, %u): asm=%u mine=%u\n", h, t, w, g); return 1; }
    return 0;
}

static const int32_t E32[] = { 0, 1, -1, 2, -2, 100, -100, INT32_MAX, INT32_MIN, INT32_MAX - 1, INT32_MIN + 1 };
static const long    E64[] = { 0, 1, -1, 7, -7, 1000, INT64_MAX, INT64_MIN, INT64_MAX - 1, INT64_MIN + 1 };
#define N_OF(a) (sizeof(a) / sizeof((a)[0]))

int main(void) {
    long bad;

    /* ===== Part A ===== */
    CHECK(mystery1(7, 3) == 4u && mystery1(3, 7) == 4u);          /* asm 자체 확인 (원래 PASS) */
    CHECK(my_mystery1(7, 3) == 4u);
    CHECK(my_mystery1(3, 7) == 4u);
    CHECK(my_mystery1(-5, 5) == 10u);
    CHECK(my_mystery1(INT32_MIN, INT32_MAX) == 0xFFFFFFFFu);      /* C 로 a-b 하면 UB 인 입력 */
    shown = 0; bad = 0;
    for (int32_t a = -300; a <= 300; a++)
        for (int32_t b = -300; b <= 300; b++) bad += cmp1(a, b);
    for (size_t i = 0; i < N_OF(E32); i++)
        for (size_t j = 0; j < N_OF(E32); j++) bad += cmp1(E32[i], E32[j]);
    for (int k = 0; k < 200000; k++) { uint64_t r = xorshift64(); bad += cmp1((int32_t)(uint32_t)r, (int32_t)(uint32_t)(r >> 32)); }
    CHECK(bad == 0);

    CHECK(my_mystery2(5, 0, 10) == 5);
    CHECK(my_mystery2(-3, 0, 10) == 0);
    CHECK(my_mystery2(99, 0, 10) == 10);
    CHECK(my_mystery2(5, 10, 0) == 10);                             /* lo > hi : 누가 이기나? */
    CHECK(my_mystery2(20, 10, 0) == 0);
    shown = 0; bad = 0;
    for (long x = -8; x <= 8; x++)
        for (long lo = -8; lo <= 8; lo++)
            for (long hi = -8; hi <= 8; hi++) bad += cmp2(x, lo, hi);
    for (size_t i = 0; i < N_OF(E64); i++)
        for (size_t j = 0; j < N_OF(E64); j++)
            for (size_t k = 0; k < N_OF(E64); k++) bad += cmp2(E64[i], E64[j], E64[k]);
    CHECK(bad == 0);

    /* ===== Part B ===== */
    CHECK(my_mystery3(0x5u) == 2);
    CHECK(my_mystery3(0xAu) == 0);
    CHECK(my_mystery3(0xFFFFFFFFu) == 16);
    CHECK(my_mystery3(0x40000000u) == 1);
    shown = 0; bad = 0;
    for (uint32_t x = 0; x < (1u << 20); x++) bad += cmp3(x);
    for (int k = 0; k < 200000; k++) bad += cmp3((uint32_t)xorshift64());
    CHECK(bad == 0);

    CHECK(my_mystery4("hello world") == 5);
    CHECK(my_mystery4("") == 0);
    CHECK(my_mystery4("\tabc") == 0);
    CHECK(my_mystery4("a!~b c") == 4);
    CHECK(my_mystery4("caf\xC3\xA9 au lait") == 3);                /* UTF-8 'é' = C3 A9 */
    CHECK(my_mystery4("abc\x80" "def") == 3);
    {
        static const char alpha[] = { 'a', 'Z', '0', '!', '~', ' ', '\t', '\x7f', '\x80', '\xff', '\xc3', '\x01' };
        char buf[48];
        shown = 0; bad = 0;
        for (int k = 0; k < 100000; k++) {
            size_t len = (size_t)(xorshift64() % 41);
            for (size_t i = 0; i < len; i++) buf[i] = alpha[xorshift64() % sizeof alpha];
            buf[len] = '\0';
            bad += cmp4(buf);
        }
        CHECK(bad == 0);
    }

    /* ===== Part C ===== */
    {
        struct sensor_tab t = { 4, { 0x10, 0x22, 0x10, 0x7F, 0x55 }, { 100, -3, 555, -32768, 9 } };
        CHECK(my_mystery5(&t, 0x10) == 250);            /* 첫 번째 0x10 */
        CHECK(my_mystery5(&t, 0x22) == -7);             /* -3 * 5 / 2 */
        CHECK(my_mystery5(&t, 0x7F) == -81920);
        CHECK(my_mystery5(&t, 0x55) == INT32_MIN);      /* index 4 >= n : 안 본다 */
        t.n = 0;
        CHECK(my_mystery5(&t, 0x10) == INT32_MIN);
        shown = 0; bad = 0;
        for (int k = 0; k < 20000; k++) {
            t.n = (uint32_t)(xorshift64() % 17);
            for (int i = 0; i < 16; i++) {
                t.id[i] = (uint16_t)(xorshift64() % 8);
                t.temp[i] = (int16_t)(uint16_t)xorshift64();
            }
            for (uint16_t id = 0; id <= 8; id++) bad += cmp5(&t, id);
            bad += cmp5(&t, 0xFFFF);
        }
        CHECK(bad == 0);
    }

    CHECK(my_mystery6(5, 2) == 3u);
    CHECK(my_mystery6(2, 5) == 21u);                    /* 링 버퍼 wrap */
    CHECK(my_mystery6(23, 0) == 23u);
    CHECK(my_mystery6(0, 100) == 12u);                  /* 계약 밖 입력도 asm 과 같게 */
    shown = 0; bad = 0;
    for (uint32_t h = 0; h < 24; h++)
        for (uint32_t tl = 0; tl < 24; tl++) bad += cmp6(h, tl);
    for (int k = 0; k < 200000; k++) { uint64_t r = xorshift64(); bad += cmp6((uint32_t)r, (uint32_t)(r >> 32)); }
    for (size_t i = 0; i < N_OF(E32); i++)
        for (size_t j = 0; j < N_OF(E32); j++) bad += cmp6((uint32_t)E32[i], (uint32_t)E32[j]);
    CHECK(bad == 0);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
