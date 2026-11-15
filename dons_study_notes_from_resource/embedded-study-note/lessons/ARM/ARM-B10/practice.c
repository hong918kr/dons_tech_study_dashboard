/* ARM-B10 추가 연습: tests for practice.S
 *   cc -std=c11 -Wall -Wextra -Werror -O2 practice.c practice.S -o /tmp/ARM-B10_practice && /tmp/ARM-B10_practice
 */
#define _DEFAULT_SOURCE
#define _DARWIN_C_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

extern size_t strlen_word(const char *s);
extern void  *memmove_byte(void *dst, const void *src, size_t n);
extern void  *memcpy_32(void *dst, const void *src, size_t n);
extern void  *memset_16(void *dst, int c, size_t n);

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; \
    if (expr) { g_pass++; printf("PASS %s\n", #expr); } \
    else      { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } } while (0)

static uint64_t g_rng = 0x2545F4914F6CDD1Dull;
static uint64_t xorshift64(void) {
    uint64_t x = g_rng; x ^= x << 13; x ^= x >> 7; x ^= x << 17; return g_rng = x;
}

enum { MAXLEN = 300, GUARD = 32, CANARY = 0xA5 };

/* ---- 1: strlen_word ---- */
static long test_strlen_word(void) {
    static _Alignas(16) char buf[16 + MAXLEN + 64];
    long bad = 0;
    for (size_t len = 0; len <= MAXLEN; len++)
        for (size_t a = 0; a < 16; a++) {
            for (size_t i = 0; i < sizeof buf; i++) {
                uint8_t b = (uint8_t)xorshift64();
                buf[i] = (char)(b ? b : 0x01);
            }
            for (size_t i = 0; i < a; i++) buf[i] = '\0';     /* NULs BEFORE s must be ignored */
            buf[a + len] = '\0';
            if (strlen_word(buf + a) != len) {
                if (bad == 0) printf("  first mismatch: len=%zu align=%zu\n", len, a);
                bad++;
            }
        }
    return bad;
}

/* every byte value 1..255 right before the NUL, at every position in a word: haszero false positives? */
static long test_strlen_word_bytes(void) {
    static _Alignas(16) char buf[32];
    long bad = 0;
    for (int v = 1; v < 256; v++)
        for (size_t len = 1; len <= 16; len++) {
            memset(buf, v, sizeof buf);
            buf[len] = '\0';
            buf[len + 1] = 0x01; buf[len + 2] = (char)0x80;          /* tricky bytes after NUL */
            if (strlen_word(buf) != len) bad++;
        }
    return bad;
}

static long test_page_end(void) {
    size_t pg = (size_t)getpagesize();
    uint8_t *p = mmap(NULL, 2 * pg, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (p == MAP_FAILED) return -1;
    mprotect(p + pg, pg, PROT_NONE);
    long bad = 0;
    for (size_t len = 0; len <= MAXLEN; len++) {
        char *s = (char *)p + pg - 1 - len;           /* NUL = last byte before the PROT_NONE page */
        memset(s, 'z', len); s[len] = '\0';
        if (strlen_word(s) != len) bad++;
    }
    munmap(p, 2 * pg);
    return bad;
}

/* ---- 2: memmove_byte vs a temp-buffer reference, every overlap shift ---- */
static long test_memmove(void) {
    uint8_t buf[256], ref[256], tmp[256];
    long bad = 0;
    for (int shift = -40; shift <= 40; shift++)
        for (size_t n = 0; n <= 100; n++) {
            for (int i = 0; i < 256; i++) buf[i] = (uint8_t)xorshift64();
            memcpy(ref, buf, sizeof buf);
            size_t s = 64, d = (size_t)(64 + shift);
            for (size_t i = 0; i < n; i++) tmp[i] = ref[s + i];         /* copy out, then in = memmove */
            for (size_t i = 0; i < n; i++) ref[d + i] = tmp[i];
            void *r = memmove_byte(buf + d, buf + s, n);
            if (r != buf + d || memcmp(buf, ref, sizeof buf) != 0) bad++;
        }
    return bad;
}

/* ---- 3/4: copy and set fuzz with guards ---- */
static long fuzz_memcpy_32(void) {
    static uint8_t src[16 + MAXLEN + 16];
    static _Alignas(16) uint8_t dst[GUARD + 16 + MAXLEN + GUARD], exp[GUARD + 16 + MAXLEN + GUARD];
    long bad = 0;
    for (size_t i = 0; i < sizeof src; i++) { uint8_t b = (uint8_t)xorshift64(); src[i] = b == CANARY ? 0x5A : b; }
    for (size_t len = 0; len <= MAXLEN; len++)
        for (size_t sa = 0; sa < 16; sa++)
            for (size_t da = 0; da < 16; da++) {
                memset(dst, CANARY, sizeof dst); memset(exp, CANARY, sizeof exp);
                volatile uint8_t *e = exp + GUARD + da;
                for (size_t i = 0; i < len; i++) e[i] = src[sa + i];
                void *r = memcpy_32(dst + GUARD + da, src + sa, len);
                if (r != dst + GUARD + da || memcmp(dst, exp, sizeof dst) != 0) {
                    if (bad == 0) printf("  first mismatch: len=%zu sa=%zu da=%zu\n", len, sa, da);
                    bad++;
                }
            }
    return bad;
}

static long fuzz_memset_16(void) {
    static _Alignas(16) uint8_t dst[GUARD + 16 + MAXLEN + GUARD], exp[GUARD + 16 + MAXLEN + GUARD];
    const int vals[] = {0x00, 0x5C, 0xFF, 0x1234, -1, (int)0x80000041};  /* only the low byte counts */
    long bad = 0;
    for (size_t vi = 0; vi < sizeof vals / sizeof vals[0]; vi++)
        for (size_t len = 0; len <= MAXLEN; len++)
            for (size_t da = 0; da < 16; da++) {
                memset(dst, CANARY, sizeof dst); memset(exp, CANARY, sizeof exp);
                volatile uint8_t *e = exp + GUARD + da;
                for (size_t i = 0; i < len; i++) e[i] = (uint8_t)vals[vi];
                void *r = memset_16(dst + GUARD + da, vals[vi], len);
                if (r != dst + GUARD + da || memcmp(dst, exp, sizeof dst) != 0) bad++;
            }
    return bad;
}

int main(void) {
    CHECK(strlen_word("") == 0 && strlen_word("abc") == 3 && strlen_word("12345678") == 8);
    CHECK(test_strlen_word() == 0);
    CHECK(test_strlen_word_bytes() == 0);
    CHECK(test_page_end() == 0);
    {
        char s[] = "abcdefgh";
        memmove_byte(s + 2, s, 5);                     /* overlap, dst after src -> backward */
        CHECK(strcmp(s, "ababcdeh") == 0);
        char t[] = "abcdefgh";
        memmove_byte(t, t + 2, 5);                     /* overlap, dst before src -> forward */
        CHECK(strcmp(t, "cdefgfgh") == 0);
    }
    CHECK(test_memmove() == 0);
    CHECK(fuzz_memcpy_32() == 0);
    CHECK(fuzz_memset_16() == 0);
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
