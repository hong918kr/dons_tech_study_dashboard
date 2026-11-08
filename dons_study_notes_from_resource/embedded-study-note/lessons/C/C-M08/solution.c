/* C-M08 memcpy / memmove / memset 직접 구현
 *
 * Part A  바이트 버전           my_memcpy_b / my_memmove_b / my_memset_b
 * Part B  워드 단위(word-at-a-time) 버전  my_memcpy / my_memmove / my_memset
 *         = dst 머리(head)를 바이트로 정렬 → 4바이트 워드 루프 → 꼬리(tail) 바이트
 * Part C  펌웨어 장면: UART 수신 버퍼 앞부분 버리기 / 중간에 끼워 넣기, LCD 프레임버퍼 스크롤
 *
 * 테스트는 libc memcpy/memmove/memset 을 "정답" 으로 삼아 xorshift32(고정 시드) 퍼즈로 수천 케이스를 대조한다.
 * demo_bench.c 는 이 파일을 MEMLAB_NO_MAIN 으로 #include 해서 같은 구현으로 속도를 잰다.
 *
 * 왜 may_alias 인가: unsigned char 배열을 uint32_t lvalue 로 읽고 쓰는 것은 ISO C 의 유효 타입(effective type)
 * 규칙(C11 6.5p7) 위반이다. GCC/clang 확장 __attribute__((may_alias)) 를 붙인 타입으로 접근하면 컴파일러가
 * 그 접근을 char 접근처럼 "무엇이든 가리킬 수 있다" 로 취급한다 (musl, newlib 이 쓰는 방법).
 * 정렬은 별개 문제다: word_t 는 4바이트 정렬 주소에서만, 비정렬 src 는 aligned(1) 을 붙인 uword_t 로 읽는다.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef uint32_t __attribute__((may_alias)) word_t;              /* 정렬된 주소 전용 워드     */
typedef uint32_t __attribute__((may_alias, aligned(1))) uword_t; /* 비정렬 주소도 허용하는 워드 */
#define WSIZE sizeof(word_t)
#define WMASK ((uintptr_t)WSIZE - 1u)

/* ---------------- Part A: 바이트 버전 ---------------- */
void *my_memcpy_b(void *restrict dst, const void *restrict src, size_t n) {
    unsigned char *d = dst;
    const unsigned char *s = src;
    while (n--) *d++ = *s++;
    return dst;
}

/* 겹칠 수 있다. 방향 판단은 (uintptr_t) 로: 서로 다른 객체의 포인터를 < 로 비교하면 UB (C11 6.5.8p5) */
void *my_memmove_b(void *dst, const void *src, size_t n) {
    unsigned char *d = dst;
    const unsigned char *s = src;
    if ((uintptr_t)d - (uintptr_t)s >= (uintptr_t)n) {   /* d < s(랩어라운드) 또는 안 겹침 → 앞에서부터 */
        while (n--) *d++ = *s++;
    } else {                                             /* s < d < s+n → 뒤에서부터 */
        d += n; s += n;
        while (n--) *--d = *--s;
    }
    return dst;
}

void *my_memset_b(void *dst, int c, size_t n) {
    unsigned char *d = dst;
    while (n--) *d++ = (unsigned char)c;                 /* c 는 unsigned char 로 변환 (7.24.6.1) */
    return dst;
}

/* ---------------- Part B: 워드 단위 버전 ---------------- */
/* addr 에서 다음 WSIZE 정렬 경계까지 남은 바이트 수 (이미 정렬이면 0) */
size_t head_bytes(uintptr_t addr) {
    return (size_t)(-addr & WMASK);
}

/* 앞에서부터 복사하는 공통 엔진 (restrict 없음 → memmove 의 d < s 겹침에도 쓸 수 있다) */
static void copy_fwd(unsigned char *d, const unsigned char *s, size_t n) {
    size_t h = head_bytes((uintptr_t)d);
    if (h > n) h = n;
    n -= h;
    while (h--) *d++ = *s++;                             /* ① dst 정렬 */
    if (((uintptr_t)s & WMASK) == 0) {                   /* ② src 도 정렬됨 */
        for (; n >= WSIZE; n -= WSIZE, d += WSIZE, s += WSIZE)
            *(word_t *)d = *(const word_t *)s;
    } else {                                             /* ② src 만 비정렬 → 비정렬 로드 */
        for (; n >= WSIZE; n -= WSIZE, d += WSIZE, s += WSIZE)
            *(word_t *)d = *(const uword_t *)s;
    }
    while (n--) *d++ = *s++;                             /* ③ 꼬리 */
}

void *my_memcpy(void *restrict dst, const void *restrict src, size_t n) {
    copy_fwd(dst, src, n);
    return dst;
}

void *my_memmove(void *dst, const void *src, size_t n) {
    unsigned char *d = dst;
    const unsigned char *s = src;
    if ((uintptr_t)d - (uintptr_t)s >= (uintptr_t)n) {
        copy_fwd(d, s, n);
    } else {                                             /* 뒤에서부터: dst 끝을 정렬 → 워드 → 머리 */
        d += n; s += n;
        size_t t = (size_t)((uintptr_t)d & WMASK);
        if (t > n) t = n;
        n -= t;
        while (t--) *--d = *--s;
        for (; n >= WSIZE; n -= WSIZE) {
            d -= WSIZE; s -= WSIZE;
            *(word_t *)d = *(const uword_t *)s;          /* 읽고 나서 쓴다: 겹쳐도 안전 */
        }
        while (n--) *--d = *--s;
    }
    return dst;
}

void *my_memset(void *dst, int c, size_t n) {
    unsigned char *d = dst;
    unsigned char b = (unsigned char)c;
    word_t w = (word_t)b * 0x01010101u;                  /* 0xAB → 0xABABABAB */
    size_t h = head_bytes((uintptr_t)d);
    if (h > n) h = n;
    n -= h;
    while (h--) *d++ = b;
    for (; n >= WSIZE; n -= WSIZE, d += WSIZE)
        *(word_t *)d = w;
    while (n--) *d++ = b;
    return dst;
}

/* ---------------- Part C: 펌웨어 장면 ---------------- */
#define RX_CAP 64
typedef struct {
    uint8_t buf[RX_CAP];
    size_t  len;                                         /* 유효 바이트 수 */
} rxbuf_t;

/* 앞에서 n 바이트(프레임 하나)를 버리고 나머지를 앞으로 당긴다. 반환: 남은 길이 */
size_t rx_consume(rxbuf_t *rb, size_t n) {
    if (n >= rb->len) { rb->len = 0; return 0; }
    my_memmove(rb->buf, rb->buf + n, rb->len - n);       /* dst < src, 겹침 */
    rb->len -= n;
    return rb->len;
}

/* pos 위치에 p[0..k) 를 끼워 넣는다 (예: 프레임 앞에 헤더 삽입). 공간이 없거나 pos > len 이면 -1 */
int rx_insert(rxbuf_t *rb, size_t pos, const uint8_t *p, size_t k) {
    if (pos > rb->len || k > RX_CAP - rb->len) return -1;
    my_memmove(rb->buf + pos + k, rb->buf + pos, rb->len - pos);  /* dst > src, 겹침 */
    my_memcpy(rb->buf + pos, p, k);
    rb->len += k;
    return 0;
}

/* rows×cols 바이트 프레임버퍼(1바이트/픽셀 또는 문자 셀)를 k 줄 위로 올리고, 아래 k 줄을 bg 로 지운다 */
void fb_scroll_up(uint8_t *fb, size_t rows, size_t cols, size_t k, uint8_t bg) {
    if (k > rows) k = rows;
    my_memmove(fb, fb + k * cols, (rows - k) * cols);
    my_memset(fb + (rows - k) * cols, bg, k * cols);
}

#ifndef MEMLAB_NO_MAIN
/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t g_rng = 0xC0FFEE11u;                    /* 고정 시드 → 매번 같은 케이스 */
static uint32_t xorshift32(void) {
    uint32_t x = g_rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return g_rng = x;
}
static uint32_t rnd(uint32_t n) { return xorshift32() % n; }  /* 0..n-1 (약간의 편향은 무시) */

#define BUF 512
static _Alignas(16) unsigned char S[BUF], D1[BUF], D2[BUF];
static void fill_rand(unsigned char *p, size_t n) { for (size_t i = 0; i < n; i++) p[i] = (unsigned char)xorshift32(); }

typedef void *(*cpy_fn)(void *restrict, const void *restrict, size_t);
typedef void *(*mov_fn)(void *, const void *, size_t);
typedef void *(*set_fn)(void *, int, size_t);

/* 서로 다른 버퍼 사이 복사: 길이 0..300, src/dst 오프셋 0..15 (모든 정렬 조합). 틀린 케이스 수 반환 */
static int fuzz_cpy(cpy_fn f, int iters) {
    int bad = 0;
    for (int i = 0; i < iters; i++) {
        size_t len = rnd(301), so = rnd(16), dof = rnd(16);
        fill_rand(S, BUF); fill_rand(D1, BUF); memcpy(D2, D1, BUF);
        void *r = f(D1 + dof, S + so, len);
        memcpy(D2 + dof, S + so, len);
        if (r != D1 + dof || memcmp(D1, D2, BUF) != 0) bad++;   /* 버퍼 전체 비교 = 범위 밖 쓰기도 잡는다 */
    }
    return bad;
}

/* 같은 버퍼 안에서 이동: 절반은 |dst-src| < 16 인 심한 겹침. 방향별 케이스 수도 센다 */
static int g_fwd_ovl, g_bwd_ovl, g_disjoint;
static int fuzz_mov(mov_fn f, int iters) {
    int bad = 0;
    g_fwd_ovl = g_bwd_ovl = g_disjoint = 0;
    for (int i = 0; i < iters; i++) {
        size_t so = rnd(256), dof, len;
        if (rnd(2)) dof = so + rnd(31) - 15;     /* 근처 (so-15 .. so+15), size_t 랩은 아래서 처리 */
        else        dof = rnd(256);
        if (dof >= 256) dof = rnd(256);                          /* 랩/범위 밖이면 다시 뽑기 */
        len = rnd(257);
        fill_rand(D1, BUF); memcpy(D2, D1, BUF);
        void *r = f(D1 + dof, D1 + so, len);
        memmove(D2 + dof, D2 + so, len);
        if (r != D1 + dof || memcmp(D1, D2, BUF) != 0) bad++;
        if (dof < so && so < dof + len) g_fwd_ovl++;
        else if (so < dof && dof < so + len) g_bwd_ovl++;
        else g_disjoint++;
    }
    return bad;
}

static int fuzz_set(set_fn f, int iters) {
    int bad = 0;
    for (int i = 0; i < iters; i++) {
        size_t len = rnd(301), dof = rnd(16);
        int c = (int)rnd(1024) - 512;                            /* 음수·255 초과도 → unsigned char 로 변환 */
        fill_rand(D1, BUF); memcpy(D2, D1, BUF);
        void *r = f(D1 + dof, c, len);
        memset(D2 + dof, c, len);
        if (r != D1 + dof || memcmp(D1, D2, BUF) != 0) bad++;
    }
    return bad;
}

static int bytes_eq(const void *p, const char *s) { return memcmp(p, s, strlen(s)) == 0; }

int main(void) {
    /* Part A — 바이트 버전: 고정 케이스 */
    {
        char b[16] = "abcdef";
        CHECK(my_memcpy_b(b + 8, b, 6) == b + 8 && bytes_eq(b + 8, "abcdef"));
        char m[16] = "abcdefgh";
        my_memmove_b(m + 2, m, 6);                               /* 뒤로 밀기 (dst > src) */
        CHECK(bytes_eq(m, "ababcdef"));
        char m2[16] = "abcdefgh";
        my_memmove_b(m2, m2 + 2, 6);                             /* 앞으로 당기기 (dst < src) */
        CHECK(bytes_eq(m2, "cdefghgh"));
        char z[8] = "xxxxxxx";
        CHECK(my_memset_b(z + 1, 0x141, 3) == z + 1 && bytes_eq(z, "xAAAxxx"));  /* 0x141 → 0x41 */
        CHECK(my_memcpy_b(z, "q", 0) == z && z[0] == 'x');      /* n == 0: 아무것도 안 함 */
    }
    g_rng = 0xC0FFEE11u;
    CHECK(fuzz_cpy(my_memcpy_b, 2000) == 0);
    CHECK(fuzz_mov(my_memmove_b, 2000) == 0);
    CHECK(fuzz_set(my_memset_b, 2000) == 0);

    /* Part B — head_bytes 표 */
    {
        static const struct { uintptr_t a; size_t want; } t[] = {
            { 0x20000000u, 0 }, { 0x20000001u, 3 }, { 0x20000002u, 2 }, { 0x20000003u, 1 },
            { 0x20000004u, 0 }, { 0x2000FFFFu, 1 }, { (uintptr_t)-1, 1 }, { 0, 0 },
        };
        int ok = 1;
        for (size_t i = 0; i < sizeof t / sizeof t[0]; i++)
            if (head_bytes(t[i].a) != t[i].want) { ok = 0; printf("  head_bytes(%#lx)=%zu want %zu\n",
                (unsigned long)t[i].a, head_bytes(t[i].a), t[i].want); }
        CHECK(ok);
    }
    /* Part B — 경계 케이스: 길이 0..40 × src/dst 오프셋 0..7 전수 (워드 경계 바로 앞뒤) */
    {
        int bad_c = 0, bad_m = 0, bad_s = 0;
        for (size_t len = 0; len <= 40; len++)
            for (size_t so = 0; so < 8; so++)
                for (size_t dof = 0; dof < 8; dof++) {
                    fill_rand(S, 64); fill_rand(D1, 64); memcpy(D2, D1, 64);
                    my_memcpy(D1 + dof, S + so, len); memcpy(D2 + dof, S + so, len);
                    bad_c += memcmp(D1, D2, 64) != 0;
                    fill_rand(D1, 64); memcpy(D2, D1, 64);
                    my_memmove(D1 + dof, D1 + so, len); memmove(D2 + dof, D2 + so, len);
                    bad_m += memcmp(D1, D2, 64) != 0;
                    my_memset(D1 + dof, (int)so * 37, len); memset(D2 + dof, (int)so * 37, len);
                    bad_s += memcmp(D1, D2, 64) != 0;
                }
        CHECK(bad_c == 0);
        CHECK(bad_m == 0);
        CHECK(bad_s == 0);
    }
    /* Part B — 퍼즈: libc 와 대조 (고정 시드) */
    g_rng = 0xC0FFEE11u;
    CHECK(fuzz_cpy(my_memcpy, 5000) == 0);
    CHECK(fuzz_mov(my_memmove, 5000) == 0);
    CHECK(g_fwd_ovl > 500 && g_bwd_ovl > 500 && g_disjoint > 500);   /* 세 방향 모두 충분히 나왔나 */
    printf("  memmove fuzz: fwd-overlap %d, bwd-overlap %d, disjoint %d\n", g_fwd_ovl, g_bwd_ovl, g_disjoint);
    CHECK(fuzz_set(my_memset, 5000) == 0);

    /* Part C — UART 수신 버퍼 */
    {
        rxbuf_t rb = { .len = 0 };
        memcpy(rb.buf, "~AB~~CDE~", 9); rb.len = 9;             /* '~' = 프레임 구분자, 프레임 2개 */
        CHECK(rx_consume(&rb, 4) == 5 && bytes_eq(rb.buf, "~CDE~"));    /* 첫 프레임 "~AB~" 처리 끝 */
        CHECK(rx_consume(&rb, 99) == 0 && rb.len == 0);
        memcpy(rb.buf, "HELLO", 5); rb.len = 5;
        CHECK(rx_insert(&rb, 0, (const uint8_t *)"[1]", 3) == 0 && rb.len == 8 && bytes_eq(rb.buf, "[1]HELLO"));
        CHECK(rx_insert(&rb, 8, (const uint8_t *)"\r\n", 2) == 0 && bytes_eq(rb.buf, "[1]HELLO\r\n"));
        CHECK(rx_insert(&rb, 4, (const uint8_t *)"--", 2) == 0 && bytes_eq(rb.buf, "[1]H--ELLO\r\n"));
        CHECK(rx_insert(&rb, 13, (const uint8_t *)"x", 1) == -1);           /* pos > len */
        rb.len = RX_CAP - 1;
        CHECK(rx_insert(&rb, 0, (const uint8_t *)"xy", 2) == -1 && rb.len == RX_CAP - 1);  /* 넘침 거부 */
    }
    /* Part C — 프레임버퍼 스크롤 (8줄 × 20칸 문자 LCD) */
    {
        enum { R = 8, C = 20 };
        uint8_t fb[R * C], ref[R * C];
        for (size_t i = 0; i < sizeof fb; i++) fb[i] = (uint8_t)('a' + i / C);   /* 줄마다 'a','b',... */
        memcpy(ref, fb + 3 * C, 5 * C); memset(ref + 5 * C, ' ', 3 * C);
        fb_scroll_up(fb, R, C, 3, ' ');
        CHECK(memcmp(fb, ref, sizeof fb) == 0);
        CHECK(fb[0] == 'd' && fb[4 * C + 19] == 'h' && fb[5 * C] == ' ');
        fb_scroll_up(fb, R, C, 100, 0);                                       /* k > rows: 전부 지움 */
        CHECK(fb[0] == 0 && fb[R * C - 1] == 0);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
#endif
