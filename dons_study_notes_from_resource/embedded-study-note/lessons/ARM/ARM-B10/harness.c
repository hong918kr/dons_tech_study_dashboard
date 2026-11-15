/* ARM-B10 harness: tests for memcpy_byte / strlen_byte / memcpy_16 (asm in starter.S / solution.S)
 *   cc -std=c11 -Wall -Wextra -Werror -O2 harness.c solution.S -o /tmp/ARM-B10_solution && /tmp/ARM-B10_solution
 */
#define _DEFAULT_SOURCE
#define _DARWIN_C_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

/* the asm functions (prototypes the .S must satisfy) */
extern void  *memcpy_byte(void *dst, const void *src, size_t n);
extern size_t strlen_byte(const char *s);
extern void  *memcpy_16(void *dst, const void *src, size_t n);

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; \
    if (expr) { g_pass++; printf("PASS %s\n", #expr); } \
    else      { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } } while (0)

static uint64_t g_rng = 0x9E3779B97F4A7C15ull;      /* fixed seed -> deterministic */
static uint64_t xorshift64(void) {
    uint64_t x = g_rng; x ^= x << 13; x ^= x >> 7; x ^= x << 17; return g_rng = x;
}

/* C reference: volatile so clang cannot turn the loop back into a libc memcpy call */
static void ref_copy(uint8_t *dst, const uint8_t *src, size_t n) {
    volatile uint8_t *d = dst;
    for (size_t i = 0; i < n; i++) d[i] = src[i];
}
static size_t ref_strlen(const char *s) {
    const volatile char *p = s;
    size_t n = 0;
    while (p[n] != '\0') n++;
    return n;
}

typedef void *(*copy_fn)(void *, const void *, size_t);

/* ---- Part B fuzz: len 0..300 x src align 0..15 x dst align 0..15, guard bytes around dst ---- */
enum { MAXLEN = 300, GUARD = 32, CANARY = 0xA5 };
static uint8_t src_buf[16 + MAXLEN + 16];
static _Alignas(16) uint8_t dst_buf[GUARD + 16 + MAXLEN + GUARD];
static _Alignas(16) uint8_t exp_buf[GUARD + 16 + MAXLEN + GUARD];

static long fuzz_copy(copy_fn f, long *bad_ret) {
    long bad = 0; *bad_ret = 0;
    for (size_t i = 0; i < sizeof src_buf; i++) {
        uint8_t b = (uint8_t)xorshift64();
        src_buf[i] = (b == CANARY) ? 0x5A : b;      /* never equal to the canary */
    }
    for (size_t len = 0; len <= MAXLEN; len++)
        for (size_t sa = 0; sa < 16; sa++)
            for (size_t da = 0; da < 16; da++) {
                memset(dst_buf, CANARY, sizeof dst_buf);
                memset(exp_buf, CANARY, sizeof exp_buf);
                uint8_t *d = dst_buf + GUARD + da;
                ref_copy(exp_buf + GUARD + da, src_buf + sa, len);
                void *r = f(d, src_buf + sa, len);
                if (r != d) (*bad_ret)++;
                if (memcmp(dst_buf, exp_buf, sizeof dst_buf) != 0) {     /* content + both guards */
                    if (bad == 0) printf("  first mismatch: len=%zu src_align=%zu dst_align=%zu\n", len, sa, da);
                    bad++;
                }
            }
    return bad;
}

static long fuzz_strlen(void) {
    static _Alignas(16) char buf[16 + MAXLEN + 64];
    long bad = 0;
    for (size_t len = 0; len <= MAXLEN; len++)
        for (size_t a = 0; a < 16; a++) {
            for (size_t i = 0; i < sizeof buf; i++) {        /* random non-zero bytes everywhere */
                uint8_t b = (uint8_t)xorshift64();
                buf[i] = (char)(b ? b : 0x80);
            }
            buf[a + len] = '\0';                              /* only NUL; bytes after it are non-zero */
            size_t got = strlen_byte(buf + a);
            if (got != len || got != ref_strlen(buf + a)) {
                if (bad == 0) printf("  first mismatch: len=%zu align=%zu got=%zu\n", len, a, got);
                bad++;
            }
        }
    return bad;
}

/* two pages, second PROT_NONE: touching one byte past the end of page 1 -> SIGSEGV/SIGBUS */
static uint8_t *map_guarded(size_t *page) {
    *page = (size_t)getpagesize();
    uint8_t *p = mmap(NULL, 2 * *page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (p == MAP_FAILED) { perror("mmap"); exit(2); }
    if (mprotect(p + *page, *page, PROT_NONE) != 0) { perror("mprotect"); exit(2); }
    return p;
}

/* ---- Part C: firmware-style users of the asm routines ---- */
/* DMA RX ring (UART/NIC): copy len bytes starting at tail, wrapping at cap, into a linear packet buffer */
static size_t ring_read(const uint8_t *ring, size_t cap, size_t tail, size_t len, uint8_t *out) {
    size_t first = cap - tail;                     /* bytes before the wrap */
    if (first > len) first = len;
    memcpy_16(out, ring + tail, first);
    memcpy_16(out + first, ring, len - first);     /* wrapped part (maybe 0) */
    return (tail + len) % cap;                     /* new tail */
}

/* boot log: append msg, truncate to fit, always NUL-terminate; returns new length */
static size_t log_append(char *buf, size_t cap, size_t pos, const char *msg) {
    size_t n = strlen_byte(msg);
    if (pos + n > cap - 1) n = cap - 1 - pos;
    memcpy_16(buf + pos, msg, n);
    buf[pos + n] = '\0';
    return pos + n;
}

int main(void) {
    /* ---------- Part A: unit tests ---------- */
    {
        char d[8] = "xxxxxxx";
        void *r = memcpy_byte(d, "hello", 5);
        CHECK(r == d && memcmp(d, "helloxx", 8) == 0);
        uint8_t z[4] = {1, 2, 3, 4};
        CHECK(memcpy_byte(z, "AB", 0) == z && z[0] == 1 && z[1] == 2);    /* n == 0 touches nothing */
        uint8_t all[256], out[256];
        for (int i = 0; i < 256; i++) all[i] = (uint8_t)i;
        memset(out, 0xEE, sizeof out);
        memcpy_byte(out, all, 256);
        CHECK(memcmp(out, all, 256) == 0);                                  /* every byte value 0x00..0xFF */
    }
    {
        CHECK(strlen_byte("") == 0);
        CHECK(strlen_byte("a") == 1);
        CHECK(strlen_byte("hello, world") == 12);
        CHECK(strlen_byte("\x80\xff\xc3\xa9") == 4);                      /* high bytes are not NUL */
        char s[256];
        memset(s, 'x', 255); s[255] = '\0';
        CHECK(strlen_byte(s) == 255);
    }

    /* ---------- Part B: fuzz + page-end edges ---------- */
    long bad_ret;
    long bad = fuzz_copy(memcpy_byte, &bad_ret);
    CHECK(bad == 0 && bad_ret == 0);                    /* 301 x 16 x 16 = 77056 calls */
    bad = fuzz_copy(memcpy_16, &bad_ret);
    CHECK(bad == 0);
    CHECK(bad_ret == 0);                                /* memcpy_16 always returns dst */
    CHECK(fuzz_strlen() == 0);                          /* 301 x 16 = 4816 strings */
    {
        size_t pg;
        uint8_t *sp = map_guarded(&pg), *dp = map_guarded(&pg);
        long bad_pe = 0;
        for (size_t i = 0; i < pg; i++) sp[i] = (uint8_t)xorshift64();
        for (size_t n = 0; n <= MAXLEN; n++) {          /* src AND dst end exactly at the page end */
            memset(dp + pg - MAXLEN - 16, CANARY, MAXLEN + 16);
            memcpy_16(dp + pg - n, sp + pg - n, n);
            if (memcmp(dp + pg - n, sp + pg - n, n) != 0) bad_pe++;
        }
        CHECK(bad_pe == 0);                             /* (an over-read/over-write would crash instead) */
        long bad_sl = 0;
        for (size_t len = 0; len <= MAXLEN; len++) {    /* NUL is the very last byte of page 1 */
            char *s = (char *)sp + pg - 1 - len;
            memset(s, 'a' + (int)(len % 26), len);
            s[len] = '\0';
            if (strlen_byte(s) != len) bad_sl++;
        }
        CHECK(bad_sl == 0);
        munmap(sp, 2 * pg); munmap(dp, 2 * pg);
    }
    {
        enum { BIG = 65536 + 64 };
        uint8_t *s = malloc(BIG), *d = malloc(BIG), *e = malloc(BIG);
        if (!s || !d || !e) return 2;
        long bad_big = 0;
        for (int t = 0; t < 200; t++) {                 /* random big sizes and offsets */
            size_t n = (size_t)(xorshift64() % 65536), so = xorshift64() % 32, doff = xorshift64() % 32;
            for (size_t i = 0; i < BIG; i += 8) { uint64_t v = xorshift64(); memcpy(s + i, &v, 8); }
            memset(d, CANARY, BIG); memset(e, CANARY, BIG);
            ref_copy(e + doff, s + so, n);
            memcpy_16(d + doff, s + so, n);
            if (memcmp(d, e, BIG) != 0) bad_big++;
        }
        CHECK(bad_big == 0);
        free(s); free(d); free(e);
    }

    /* ---------- Part C: firmware buffers ---------- */
    {
        static _Alignas(16) uint8_t nand_page[4096], host_buf[4096 + 16];
        for (size_t i = 0; i < sizeof nand_page; i++) nand_page[i] = (uint8_t)(i * 7 + 3);
        memcpy_16(host_buf + 4, nand_page, 4096);       /* host buffer deliberately misaligned by 4 */
        CHECK(memcmp(host_buf + 4, nand_page, 4096) == 0);

        uint8_t ring[256], out[256], expect[256];
        for (int i = 0; i < 256; i++) ring[i] = (uint8_t)xorshift64();
        long bad_ring = 0;
        for (size_t tail = 0; tail < 256; tail++)
            for (size_t len = 0; len <= 256; len++) {
                for (size_t i = 0; i < len; i++) expect[i] = ring[(tail + i) % 256];
                size_t nt = ring_read(ring, 256, tail, len, out);
                if (memcmp(out, expect, len) != 0 || nt != (tail + len) % 256) bad_ring++;
            }
        CHECK(bad_ring == 0);

        char log[16];
        size_t pos = 0;
        log[0] = '\0';
        pos = log_append(log, sizeof log, pos, "BOOT:");
        pos = log_append(log, sizeof log, pos, "nand ok");
        pos = log_append(log, sizeof log, pos, " ftl ready");     /* does not fit -> truncated */
        CHECK(pos == 15 && strcmp(log, "BOOT:nand ok ft") == 0);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
