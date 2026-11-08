/* C-M08 추가 연습 — 한 번에 검증 (libc / 단순 루프와 대조, xorshift32 고정 시드)
 *   cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
 *   cc -std=c11 -g -O1 -fsanitize=address,undefined practice.c -o practice_san && ./practice_san
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint32_t __attribute__((may_alias)) word_t;
typedef uint32_t __attribute__((may_alias, aligned(1))) uword_t;
#define WMASK ((uintptr_t)3u)

static size_t head_bytes(uintptr_t a) { return (size_t)(-a & WMASK); }

/* 연습 1: memcmp — 워드로 같은 구간을 빨리 건너뛰고, 다르면 바이트로 "첫 번째 다른 바이트" 를 찾는다.
 * 부호는 unsigned char 로 본 차이 (7.24.4.1). 워드 값끼리 빼면 리틀 엔디안에서 순서가 틀린다. */
static int my_memcmp(const void *a, const void *b, size_t n) {
    const unsigned char *p = a, *q = b;
    while (n >= 4 && *(const uword_t *)p == *(const uword_t *)q) { p += 4; q += 4; n -= 4; }
    for (; n; n--, p++, q++)
        if (*p != *q) return (int)*p - (int)*q;
    return 0;
}

/* 연습 2: 비정렬 로드가 없는 코어(Cortex-M0)용 memcpy — 정렬된 로드 두 개를 시프트로 합친다 (shift-merge).
 * 리틀 엔디안 전용. src 객체 밖은 1바이트도 읽지 않는다 (첫 부분 워드는 바이트로 조립). */
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
static void *memcpy_shift(void *restrict dst, const void *restrict src, size_t n) {
    unsigned char *d = dst;
    const unsigned char *s = src;
    size_t h = head_bytes((uintptr_t)d);
    if (h > n) h = n;
    n -= h;
    while (h--) *d++ = *s++;                               /* dst 정렬 */
    unsigned k = (unsigned)((uintptr_t)s & WMASK);         /* src 의 정렬 어긋남 0..3 */
    if (k == 0) {
        for (; n >= 4; n -= 4, d += 4, s += 4) *(word_t *)d = *(const word_t *)s;
    } else if (n >= 8 - k) {
        unsigned sh = 8u * k;
        word_t carry = 0;                                  /* s[0..4-k) 를 "s-k 의 정렬 워드" 위치에 조립 */
        for (unsigned j = 0; j < 4 - k; j++) carry |= (word_t)s[j] << (8u * (k + j));
        const word_t *ws = (const word_t *)(s + (4 - k));  /* 다음 정렬 워드 (객체 안) */
        while (n >= 8 - k) {                               /* ws[0] 의 끝 s+8-k 가 범위 안일 때만 */
            word_t next = *ws++;
            *(word_t *)d = (carry >> sh) | (next << (32u - sh));
            carry = next; d += 4; s += 4; n -= 4;
        }
    }
    while (n--) *d++ = *s++;                               /* 꼬리 */
    return dst;
}
#else
#error "memcpy_shift: little-endian only"
#endif

/* 연습 3: RGB565 LCD 색 채우기 — 픽셀 2개를 워드 하나로 (v | v << 16) */
static void fill_u16(uint16_t *p, uint16_t v, size_t n) {
    if (n && ((uintptr_t)p & 2u)) { *p++ = v; n--; }       /* 2바이트 정렬 → 4바이트 정렬 */
    word_t w = (word_t)v | ((word_t)v << 16);
    for (; n >= 2; n -= 2, p += 2) *(word_t *)p = w;
    if (n) *p = v;
}

/* 연습 4: 지워지지 않는 0 채우기 (키·비밀번호 버퍼 정리) — volatile 접근은 생략될 수 없다 */
static void secure_zero(void *p, size_t n) {
    volatile unsigned char *v = p;
    while (n--) *v++ = 0;
}

/* ---------------- 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t g_rng = 0x1234ABCDu;
static uint32_t xorshift32(void) { uint32_t x = g_rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return g_rng = x; }
static uint32_t rnd(uint32_t n) { return xorshift32() % n; }
static int sgn(int x) { return (x > 0) - (x < 0); }

static _Alignas(16) unsigned char A[512], B[512], D1[512], D2[512];

int main(void) {
    /* 연습 1 */
    CHECK(my_memcmp("abcdefgh", "abcdefgh", 8) == 0);
    CHECK(my_memcmp("abcdefgX", "abcdefgh", 8) < 0);           /* 'X'(0x58) < 'h'(0x68) */
    CHECK(my_memcmp("\x80", "\x01", 1) > 0);                   /* unsigned 비교: 0x80 > 0x01 */
    CHECK(my_memcmp("\x01\x00\x00\x00", "\x00\x01\x00\x00", 4) > 0); /* 워드 값 비교(LE)라면 반대 부호 */
    {
        int bad = 0;
        for (int i = 0; i < 3000; i++) {
            size_t n = rnd(200), oa = rnd(8), ob = rnd(8);
            for (size_t j = 0; j < n; j++) A[oa + j] = B[ob + j] = (unsigned char)xorshift32();
            if (n && rnd(4)) B[ob + rnd((uint32_t)n)] = (unsigned char)xorshift32();  /* 대부분 한 바이트 다르게 */
            if (sgn(my_memcmp(A + oa, B + ob, n)) != sgn(memcmp(A + oa, B + ob, n))) bad++;
        }
        CHECK(bad == 0);
    }
    /* 연습 2: 길이 0..40 × 오프셋 전수 + 퍼즈 */
    {
        int bad = 0;
        for (size_t len = 0; len <= 40; len++)
            for (size_t so = 0; so < 8; so++)
                for (size_t dof = 0; dof < 8; dof++) {
                    for (size_t j = 0; j < 64; j++) { A[j] = (unsigned char)xorshift32(); D1[j] = D2[j] = (unsigned char)j; }
                    memcpy_shift(D1 + dof, A + so, len); memcpy(D2 + dof, A + so, len);
                    bad += memcmp(D1, D2, 64) != 0;
                }
        CHECK(bad == 0);
        bad = 0;
        for (int i = 0; i < 3000; i++) {
            size_t len = rnd(301), so = rnd(16), dof = rnd(16);
            for (size_t j = 0; j < 512; j++) { A[j] = (unsigned char)xorshift32(); D1[j] = D2[j] = (unsigned char)~j; }
            void *r = memcpy_shift(D1 + dof, A + so, len); memcpy(D2 + dof, A + so, len);
            bad += r != D1 + dof || memcmp(D1, D2, 512) != 0;
        }
        CHECK(bad == 0);
        bad = 0;              /* src = 정확히 len 바이트짜리 힙 블록: 1바이트라도 넘어 읽으면 ASan 이 잡는다 */
        for (size_t len = 1; len <= 64; len++)
            for (size_t dof = 0; dof < 4; dof++) {
                unsigned char *src = malloc(len);
                if (!src) return 1;
                for (size_t j = 0; j < len; j++) src[j] = (unsigned char)xorshift32();
                memcpy_shift(D1 + dof, src, len); memcpy(D2 + dof, src, len);
                bad += memcmp(D1, D2, 512) != 0;
                free(src);
            }
        CHECK(bad == 0);
    }
    /* 연습 3 */
    {
        static _Alignas(4) uint16_t fb[64], ref[64];
        int bad = 0;
        for (size_t off = 0; off < 4; off++)
            for (size_t n = 0; n <= 40; n++) {
                for (size_t j = 0; j < 64; j++) fb[j] = ref[j] = 0xAAAA;
                fill_u16(fb + off, 0xF800, n);                         /* 빨강 (RGB565) */
                for (size_t j = 0; j < n; j++) ref[off + j] = 0xF800;
                bad += memcmp(fb, ref, sizeof fb) != 0;
            }
        CHECK(bad == 0);
    }
    /* 연습 4 */
    {
        unsigned char key[32];
        memset(key, 0x5A, sizeof key);
        secure_zero(key, sizeof key);
        int nz = 0;
        for (size_t j = 0; j < sizeof key; j++) nz += key[j] != 0;
        CHECK(nz == 0);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
