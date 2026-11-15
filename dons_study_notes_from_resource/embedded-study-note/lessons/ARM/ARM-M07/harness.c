/* ARM-M07 harness: tests for neon_memchr / checksum_u8 / minmax_u16 (asm in starter.S / solution.S)
 * plus "are the tests strong enough?" checks against the injected bugs in shared_bugs.S.
 *   cc -std=c11 -Wall -Wextra -Werror -O2 harness.c solution.S shared_bugs.S -o /tmp/ARM-M07_solution
 *   /tmp/ARM-M07_solution
 */
#define _DEFAULT_SOURCE
#define _DARWIN_C_SOURCE
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

/* ---- the asm functions (prototypes the .S must satisfy) ---- */
extern const void *neon_memchr(const void *p, int c, size_t n);
extern uint64_t    checksum_u8(const uint8_t *p, size_t n);
extern void        minmax_u16(const uint16_t *s, size_t n, uint16_t *mn, uint16_t *mx);
/* injected bugs (shared_bugs.S) */
extern const void *bug_memchr_norbit(const void *p, int c, size_t n);
extern const void *bug_memchr_nomask(const void *p, int c, size_t n);
extern const void *bug_memchr_overread(const void *p, int c, size_t n);
extern uint64_t    bug_checksum_nofold(const uint8_t *p, size_t n);
extern void        bug_minmax_signed(const uint16_t *s, size_t n, uint16_t *mn, uint16_t *mx);
extern void        bug_minmax_notail(const uint16_t *s, size_t n, uint16_t *mn, uint16_t *mx);

typedef const void *(*memchr_fn)(const void *, int, size_t);
typedef uint64_t (*sum_fn)(const uint8_t *, size_t);
typedef void (*minmax_fn)(const uint16_t *, size_t, uint16_t *, uint16_t *);

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; \
    if (expr) { g_pass++; printf("PASS %s\n", #expr); } \
    else      { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } } while (0)

static uint64_t g_rng = 0x9E3779B97F4A7C15ull;      /* fixed seed -> deterministic */
static uint64_t xorshift64(void) {
    uint64_t x = g_rng; x ^= x << 13; x ^= x >> 7; x ^= x << 17; return g_rng = x;
}

/* ---- C reference models (volatile: clang must not vectorize / call libc) ---- */
static const void *ref_memchr(const void *p, int c, size_t n) {
    const volatile uint8_t *b = p;
    for (size_t i = 0; i < n; i++) if (b[i] == (uint8_t)c) return (const uint8_t *)p + i;
    return NULL;
}
static uint64_t ref_sum(const uint8_t *p, size_t n) {
    const volatile uint8_t *b = p; uint64_t s = 0;
    for (size_t i = 0; i < n; i++) s += b[i];
    return s;
}
static void ref_minmax(const uint16_t *s, size_t n, uint16_t *mn, uint16_t *mx) {
    const volatile uint16_t *v = s; uint16_t lo = 0xFFFF, hi = 0;
    for (size_t i = 0; i < n; i++) { if (v[i] < lo) lo = v[i]; if (v[i] > hi) hi = v[i]; }
    *mn = lo; *mx = hi;
}

/* ---- fuzzers: return the number of mismatching calls ---- */
enum { MAXLEN = 300 };
static _Alignas(16) uint8_t fbuf[16 + MAXLEN + 64];

/* len 0..300 x align 0..15 x 6 match modes; bytes right after the range are ALL = c,
 * so a function that looks past p+n "finds" one there.  c is passed as c, c+256 or c-256. */
static long fuzz_memchr(memchr_fn f) {
    long bad = 0, it = 0;
    for (size_t len = 0; len <= MAXLEN; len++)
        for (size_t a = 0; a < 16; a++)
            for (int mode = 0; mode < 6; mode++, it++) {
                uint8_t c = (uint8_t)xorshift64();
                for (size_t i = 0; i < sizeof fbuf; i++) {
                    uint8_t b = (uint8_t)xorshift64();
                    fbuf[i] = (b == c) ? (uint8_t)(c ^ 0x5A) : b;      /* no accidental c */
                }
                uint8_t *p = fbuf + a;
                memset(p + len, c, 32);                                 /* trap for over-reads */
                if (len) {
                    size_t k = (size_t)(xorshift64() % len);
                    if (mode == 1) p[0] = c;
                    if (mode == 2) p[len - 1] = c;
                    if (mode == 3) memset(p, c, len);
                    if (mode == 4) p[k] = c;
                    if (mode == 5) { p[k] = c; size_t k2 = k + 1 + (size_t)(xorshift64() % 15);
                                     if (k2 < len) p[k2] = c; }          /* 2 matches, same chunk */
                }
                int ci = (int)c + 256 * (int)(it % 3) - 256;             /* c-256, c, c+256 */
                if (f(p, ci, len) != ref_memchr(p, c, len)) {
                    if (bad == 0) printf("  first mismatch: len=%zu align=%zu mode=%d c=%d\n", len, a, mode, ci);
                    bad++;
                }
            }
    return bad;
}

static long fuzz_sum(sum_fn f) {
    long bad = 0;
    for (size_t len = 0; len <= MAXLEN; len++)
        for (size_t a = 0; a < 16; a++) {
            for (size_t i = 0; i < sizeof fbuf; i++) fbuf[i] = (uint8_t)xorshift64();
            if (f(fbuf + a, len) != ref_sum(fbuf + a, len)) {
                if (bad == 0) printf("  first mismatch: len=%zu align=%zu\n", len, a);
                bad++;
            }
        }
    return bad;
}

/* len 1..300 samples x start offset 0..7 samples; neighbours are 0x0000 / 0xFFFF sentinels */
static _Alignas(16) uint16_t sbuf[8 + MAXLEN + 16];
static long fuzz_minmax(minmax_fn f, unsigned bits) {
    long bad = 0;
    uint16_t mask = (uint16_t)((1u << bits) - 1u);
    for (size_t len = 1; len <= MAXLEN; len++)
        for (size_t a = 0; a < 8; a++) {
            for (size_t i = 0; i < sizeof sbuf / 2; i++) sbuf[i] = (i & 1) ? 0xFFFF : 0x0000;
            for (size_t i = 0; i < len; i++) sbuf[a + i] = (uint16_t)(xorshift64() & mask);
            uint16_t mn = 1, mx = 1, emn, emx;
            f(sbuf + a, len, &mn, &mx);
            ref_minmax(sbuf + a, len, &emn, &emx);
            if (mn != emn || mx != emx) {
                if (bad == 0) printf("  first mismatch: len=%zu off=%zu got %u/%u want %u/%u\n",
                                     len, a, mn, mx, emn, emx);
                bad++;
            }
        }
    return bad;
}

/* ---- page guard: [PROT_NONE][RW][PROT_NONE] -> one byte outside RW = SIGSEGV/SIGBUS ---- */
static uint8_t *map_guarded(size_t *page) {
    *page = (size_t)getpagesize();
    uint8_t *m = mmap(NULL, 3 * *page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (m == MAP_FAILED) { perror("mmap"); exit(2); }
    if (mprotect(m, *page, PROT_NONE) || mprotect(m + 2 * *page, *page, PROT_NONE)) {
        perror("mprotect"); exit(2);
    }
    return m + *page;                         /* start of the RW page */
}

/* run fn in a child; returns 1 if the child died (signal or sanitizer abort) */
static int crashes_in_child(const uint8_t *p, size_t n) {
    fflush(stdout);
    pid_t pid = fork();
    if (pid == 0) {
        signal(SIGSEGV, SIG_DFL); signal(SIGBUS, SIG_DFL);
        (void)bug_memchr_overread(p, 'Z', n);
        _exit(0);
    }
    int st = 0;
    waitpid(pid, &st, 0);
    return !(WIFEXITED(st) && WEXITSTATUS(st) == 0);
}

/* ---- Part C: firmware-style users ---- */
/* UART RX stream: 0x7E | body | 0x7E | body | 0x7E ...  body = data + chk, sum(body) % 256 == 0.
 * Empty bodies (back-to-back 0x7E) are idle fill and are skipped. */
static void parse_frames(memchr_fn find, sum_fn sum, const uint8_t *buf, size_t n,
                         size_t *good, size_t *bad) {
    const uint8_t *end = buf + n, *p = find(buf, 0x7E, n);
    *good = *bad = 0;
    while (p) {
        const uint8_t *start = p + 1;
        const uint8_t *q = find(start, 0x7E, (size_t)(end - start));
        if (!q) break;                                   /* incomplete frame: wait for more */
        size_t len = (size_t)(q - start);
        if (len) { if ((sum(start, len) & 0xFF) == 0) (*good)++; else (*bad)++; }
        p = q;
    }
}

static size_t make_stream(uint8_t *out, size_t nframes, size_t *n_bad) {
    size_t n = 0; *n_bad = 0;
    out[n++] = 0x7E;
    for (size_t f = 0; f < nframes; f++) {
        size_t k = (size_t)(xorshift64() % 300), s = 0;
        uint8_t *body = out + n;
        for (size_t i = 0; i < k; i++) {
            uint8_t b = (uint8_t)xorshift64(); if (b == 0x7E) b = 0x7D;   /* (real HDLC escapes) */
            body[i] = b; s += b;
        }
        uint8_t chk = (uint8_t)(0x100 - (s & 0xFF));
        if (chk == 0x7E) { body[k++] = 0x01; chk = 0x7D; }                 /* keep chk != 0x7E */
        body[k++] = chk;
        if (f % 7 == 3) { body[0] = (uint8_t)(body[0] + (body[0] == 0x7D ? 2 : 1)); (*n_bad)++; }
        n += k;
        out[n++] = 0x7E;
        if (f % 5 == 0) out[n++] = 0x7E;                                   /* idle fill */
    }
    for (size_t i = 0; i < 40; i++) out[n++] = (uint8_t)(i == 0 ? 0x11 : 0x22); /* partial frame */
    return n;
}

/* 12-bit ADC window: triangle 2048 +/- A (clamped to 0..4095); 0xFFFF = driver's "conversion error" */
enum { WIN = 999 };                                     /* 124 x 8 + tail 7 */
static void make_window(uint16_t *w, int A, int glitch_at) {
    for (int i = 0; i < WIN; i++) {
        int t = (i % 200) - 100; if (t < 0) t = -t;                         /* 100..0..100 */
        int v = 2048 - A + (2 * A * t) / 100;
        v = v < 0 ? 0 : (v > 4095 ? 4095 : v);
        w[i] = (uint16_t)v;
    }
    if (glitch_at >= 0) w[glitch_at] = 0xFFFF;
}
typedef struct { uint16_t p2p; int clipped; int error; } adc_stat;
static adc_stat adc_window_stat(minmax_fn f, const uint16_t *w, size_t n) {
    uint16_t mn, mx; adc_stat r;
    f(w, n, &mn, &mx);
    r.error = mx > 4095;                                 /* not a 12-bit value -> sensor/driver fault */
    r.clipped = !r.error && (mn == 0 || mx == 4095);
    r.p2p = (uint16_t)(mx - mn);
    return r;
}

int main(void) {
    /* ---------- Part A: unit tests ---------- */
    {
        static const char s[] = "hello, uart\x7e\xff";                    /* 13 bytes */
        CHECK(neon_memchr(s, 'l', 13) == s + 2);
        CHECK(neon_memchr(s, 'z', 13) == NULL);
        CHECK(neon_memchr(s, 'h', 0) == NULL);
        CHECK(neon_memchr(s, 0x100 + 'u', 13) == s + 7);                  /* (unsigned char)c */
        CHECK(neon_memchr(s, -1, 13) == s + 12);                          /* -1 -> 0xFF */
        static const char t[40] = "0123456789abcdef0123456789ABCDEF#xyz";
        CHECK(neon_memchr(t, '#', 36) == t + 32);                         /* 3rd chunk, via tail */
        CHECK(neon_memchr(t, 'f', 36) == t + 15);                         /* last lane of chunk 0 */
        CHECK(neon_memchr(t, 'B', 36) == t + 27);
    }
    CHECK(fuzz_memchr(neon_memchr) == 0);
    {
        static const uint8_t d[5] = {1, 2, 3, 250, 255};
        CHECK(checksum_u8(d, 5) == 511);
        CHECK(checksum_u8(d, 0) == 0);
    }
    CHECK(fuzz_sum(checksum_u8) == 0);
    {
        static const uint16_t a[11] = {2048, 17, 4095, 3000, 900, 2, 4000, 1234, 77, 4094, 1};
        uint16_t mn = 0, mx = 0;
        minmax_u16(a, 11, &mn, &mx);           CHECK(mn == 1 && mx == 4095);
        minmax_u16(a, 0, &mn, &mx);            CHECK(mn == 0xFFFF && mx == 0);   /* empty set */
        static const uint16_t b[9] = {0x8000, 0x7FFF, 0xFFFF, 0, 1, 2, 3, 4, 5};
        minmax_u16(b, 9, &mn, &mx);            CHECK(mn == 0 && mx == 0xFFFF);
    }
    CHECK(fuzz_minmax(minmax_u16, 12) == 0);
    CHECK(fuzz_minmax(minmax_u16, 16) == 0);

    /* ---------- Part B: page guards, horizons, injected bugs ---------- */
    {
        size_t page; uint8_t *rw = map_guarded(&page), *rw_end = rw + page;
        memset(rw, 0x33, page);
        long bad = 0;
        for (size_t len = 0; len <= MAXLEN; len++) {
            const uint8_t *p = rw_end - len;                               /* ends at page end */
            if (neon_memchr(p, 0x44, len) != NULL) bad++;
            if (len && neon_memchr(p, 0x33, len) != p) bad++;
            if (checksum_u8(p, len) != 0x33u * len) bad++;
            if (neon_memchr(rw, 0x44, len) != NULL) bad++;                 /* starts at page start */
            uint16_t mn, mx;
            minmax_u16((const uint16_t *)(rw_end - 2 * len), len, &mn, &mx);
            if (len && (mn != 0x3333 || mx != 0x3333)) bad++;
        }
        rw_end[-1] = 0x44;
        if (neon_memchr(rw_end - 300, 0x44, 300) != rw_end - 1) bad++;     /* match in the last byte */
        CHECK(bad == 0);                                                   /* (and no crash) */
        CHECK(crashes_in_child(rw_end - 5, 5) == 1);                       /* the bug DOES fault */
        munmap(rw - page, 3 * page);
    }
    {
        size_t big = (size_t)1 << 20;
        uint8_t *ff = malloc(big);
        if (!ff) return 2;
        memset(ff, 0xFF, big);
        CHECK(checksum_u8(ff, 2048) == 255u * 2048);                       /* exactly the u16 horizon */
        CHECK(checksum_u8(ff, 2064) == 255u * 2064);                       /* one step past it */
        CHECK(checksum_u8(ff, big) == 255ull * big);
        CHECK(bug_checksum_nofold(ff, 2048) == 255u * 2048);               /* bug hides up to here */
        CHECK(bug_checksum_nofold(ff, 2064) != 255u * 2064);               /* ...and shows here */
        free(ff);
    }
    CHECK(fuzz_memchr(bug_memchr_norbit) > 0);
    CHECK(fuzz_memchr(bug_memchr_nomask) > 0);
    CHECK(fuzz_memchr(bug_memchr_overread) > 0);
    CHECK(fuzz_minmax(bug_minmax_signed, 12) == 0);                        /* 12-bit data hides it */
    CHECK(fuzz_minmax(bug_minmax_signed, 16) > 0);
    CHECK(fuzz_minmax(bug_minmax_notail, 12) > 0);

    /* ---------- Part C: UART frames + ADC windows ---------- */
    {
        static uint8_t stream[200 * 310 + 300];
        size_t n_bad_exp, n = make_stream(stream, 200, &n_bad_exp), good, bad, rgood, rbad;
        parse_frames(neon_memchr, checksum_u8, stream, n, &good, &bad);
        parse_frames(ref_memchr, ref_sum, stream, n, &rgood, &rbad);
        CHECK(good + bad == 200 && bad == n_bad_exp);
        CHECK(good == rgood && bad == rbad);
    }
    {
        static uint16_t w[WIN];
        int bad = 0;
        for (int A = 0; A <= 2400; A += 100) {
            make_window(w, A, -1);
            adc_stat st = adc_window_stat(minmax_u16, w, WIN);
            int lo = 2048 - A < 0 ? 0 : 2048 - A, hi = 2048 + A > 4095 ? 4095 : 2048 + A;
            if (st.p2p != hi - lo || st.clipped != (A >= 2047) || st.error) bad++;
        }
        CHECK(bad == 0);
        make_window(w, 800, 517);                                          /* one 0xFFFF sample */
        adc_stat good = adc_window_stat(minmax_u16, w, WIN);
        adc_stat miss = adc_window_stat(bug_minmax_signed, w, WIN);
        CHECK(good.error == 1);
        CHECK(miss.error == 0);                                            /* signed min/max hides 0xFFFF */
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
