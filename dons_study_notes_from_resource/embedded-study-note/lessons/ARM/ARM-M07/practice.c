/* ARM-M07 practice: count_byte / strlen_neon / memchr2_neon (practice.S) + hist_u8_4way (C)
 *   cc -std=c11 -Wall -Wextra -Werror -O2 practice.c practice.S shared_bugs.S -o /tmp/ARM-M07_practice
 *   /tmp/ARM-M07_practice
 */
#define _DEFAULT_SOURCE
#define _DARWIN_C_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

extern size_t      count_byte(const void *p, int c, size_t n);
extern size_t      strlen_neon(const char *s);
extern const void *memchr2_neon(const void *p, int c1, int c2, size_t n);

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; \
    if (expr) { g_pass++; printf("PASS %s\n", #expr); } \
    else      { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } } while (0)

static uint64_t g_rng = 0x2545F4914F6CDD1Dull;
static uint64_t xorshift64(void) {
    uint64_t x = g_rng; x ^= x << 13; x ^= x >> 7; x ^= x << 17; return g_rng = x;
}

/* ---- 연습 4: byte histogram. SIMD has no scatter-increment, so the classic speedup is
 * 4 sub-histograms: consecutive equal bytes no longer hit the SAME counter back-to-back
 * (store -> load dependency through memory), then merge. ---- */
static void hist_u8_4way(const uint8_t *p, size_t n, uint32_t hist[256]) {
    uint32_t h[4][256];
    memset(h, 0, sizeof h);
    size_t i = 0;
    for (; i + 4 <= n; i += 4) { h[0][p[i]]++; h[1][p[i + 1]]++; h[2][p[i + 2]]++; h[3][p[i + 3]]++; }
    for (; i < n; i++) h[0][p[i]]++;
    for (int b = 0; b < 256; b++) hist[b] = h[0][b] + h[1][b] + h[2][b] + h[3][b];
}

/* references */
static size_t ref_count(const uint8_t *p, uint8_t c, size_t n) {
    const volatile uint8_t *v = p; size_t k = 0;
    for (size_t i = 0; i < n; i++) k += (v[i] == c);
    return k;
}
static const void *ref_memchr2(const uint8_t *p, uint8_t a, uint8_t b, size_t n) {
    const volatile uint8_t *v = p;
    for (size_t i = 0; i < n; i++) if (v[i] == a || v[i] == b) return p + i;
    return NULL;
}

static uint8_t *map_guarded(size_t *page) {           /* [NONE][RW][NONE] -> returns RW */
    *page = (size_t)getpagesize();
    uint8_t *m = mmap(NULL, 3 * *page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (m == MAP_FAILED) { perror("mmap"); exit(2); }
    if (mprotect(m, *page, PROT_NONE) || mprotect(m + 2 * *page, *page, PROT_NONE)) { perror("mprotect"); exit(2); }
    return m + *page;
}

int main(void) {
    static _Alignas(16) uint8_t buf[16 + 300 + 64];

    /* 연습 1: count_byte */
    {
        long bad = 0;
        for (size_t len = 0; len <= 300; len++)
            for (size_t a = 0; a < 16; a++) {
                uint8_t c = (uint8_t)(xorshift64() & 7);                 /* small alphabet: many hits */
                for (size_t i = 0; i < sizeof buf; i++) buf[i] = (uint8_t)(xorshift64() & 7);
                if (count_byte(buf + a, (int)c - 256 * (int)(len & 1), len) != ref_count(buf + a, c, len)) bad++;
            }
        CHECK(bad == 0);
        size_t big = (size_t)1 << 20;
        uint8_t *m = malloc(big);
        if (!m) return 2;
        memset(m, 0xA5, big);
        CHECK(count_byte(m, 0xA5, 255 * 16) == 255 * 16);                /* exactly the u8 horizon */
        CHECK(count_byte(m, 0xA5, 256 * 16) == 256 * 16);                /* one step past it */
        CHECK(count_byte(m, 0xA5, big) == big && count_byte(m, 0x5A, big) == 0);
        free(m);
    }

    /* 연습 2: strlen_neon -- every length x alignment; zeros BEFORE s inside the first chunk */
    {
        long bad = 0;
        for (size_t len = 0; len <= 300; len++)
            for (size_t a = 0; a < 16; a++) {
                for (size_t i = 0; i < sizeof buf; i++) { uint8_t b = (uint8_t)xorshift64(); buf[i] = b ? b : 1; }
                memset(buf, 0, a);                                       /* NULs just before s */
                buf[a + len] = 0;
                if (strlen_neon((const char *)buf + a) != len) bad++;
            }
        CHECK(bad == 0);
        size_t page; uint8_t *rw = map_guarded(&page), *end = rw + page;
        memset(rw, 'x', page);
        long pbad = 0;
        for (size_t len = 0; len <= 300; len++) {                        /* NUL = last byte of the page */
            end[-1] = 0;
            if (strlen_neon((const char *)end - 1 - len) != len) pbad++;
        }
        for (size_t len = 0; len <= 300; len++) {                        /* s = first byte of the page */
            memset(rw, 'y', 400); rw[len] = 0;
            if (strlen_neon((const char *)rw) != len) pbad++;
        }
        CHECK(pbad == 0);
        munmap(rw - page, 3 * page);
    }

    /* 연습 3: memchr2_neon -- fuzz + AT-command line end */
    {
        long bad = 0;
        for (size_t len = 0; len <= 300; len++)
            for (size_t a = 0; a < 16; a++) {
                for (size_t i = 0; i < sizeof buf; i++) buf[i] = (uint8_t)(xorshift64() % 120 + 20);
                if (len && (xorshift64() & 1)) buf[a + xorshift64() % len] = '\r';
                if (len && (xorshift64() & 1)) buf[a + xorshift64() % len] = '\n';
                memset(buf + a + len, '\n', 16);                         /* over-read trap */
                if (memchr2_neon(buf + a, '\r', '\n' + 256, len) != ref_memchr2(buf + a, '\r', '\n', len)) bad++;
            }
        CHECK(bad == 0);
        static const char at[] = "+CSQ: 23,99\r\nOK\r\n";
        const char *eol = memchr2_neon(at, '\r', '\n', sizeof at - 1);
        CHECK(eol == at + 11);
    }

    /* 연습 4: hist_u8_4way */
    {
        size_t n = 1000003;
        uint8_t *m = malloc(n);
        if (!m) return 2;
        for (size_t i = 0; i < n; i++) m[i] = (uint8_t)(xorshift64() >> 56);
        uint32_t h[256], ref[256] = {0};
        hist_u8_4way(m, n, h);
        for (size_t i = 0; i < n; i++) ref[m[i]]++;
        uint64_t tot = 0; for (int b = 0; b < 256; b++) tot += h[b];
        CHECK(memcmp(h, ref, sizeof h) == 0 && tot == n);
        CHECK(h[0x7E] == count_byte(m, 0x7E, n));                       /* one bin = count_byte */
        free(m);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
