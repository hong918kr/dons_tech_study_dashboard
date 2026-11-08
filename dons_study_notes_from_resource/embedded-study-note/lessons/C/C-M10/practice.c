/* C-M10 추가 연습 — 한 번에 검증 */
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PRINTF_LIKE(fmt_idx, first_arg) __attribute__((format(printf, fmt_idx, first_arg)))

/* 연습 1: 고정소수점(centi 단위) 출력 — float 없이 "-0.05" 를 정확히.
 * "%d.%02d", v / 100, abs(v % 100) 는 v = -5 에서 몫이 0 이라 부호가 사라진다 ("0.05"). */
static int fmt_centi(char *buf, size_t n, int centi) {
    unsigned m = (unsigned)centi;
    if (centi < 0) m = 0u - m;                     /* INT_MIN 도 안전한 크기 */
    return snprintf(buf, n, "%s%u.%02u", centi < 0 ? "-" : "", m / 100u, m % 100u);
}

/* 연습 2: 필요한 만큼 할당해 주는 xasprintf — vsnprintf 를 두 번 → va_copy 필수 */
static char *xasprintf(const char *fmt, ...) PRINTF_LIKE(1, 2);
static char *xasprintf(const char *fmt, ...) {
    va_list ap, ap2;
    va_start(ap, fmt);
    va_copy(ap2, ap);                              /* 1번째 vsnprintf 가 ap 를 소비한다 */
    int n = vsnprintf(NULL, 0, fmt, ap);           /* 1) 크기 재기 */
    va_end(ap);
    char *s = n >= 0 ? malloc((size_t)n + 1) : NULL;
    if (s) vsnprintf(s, (size_t)n + 1, fmt, ap2);  /* 2) 실제로 쓰기 */
    va_end(ap2);
    return s;
}

/* 연습 3: 이어 붙이기 appendf — off 가 size 를 넘지 않게 고정.
 * 순진한 off += snprintf(buf + off, size - off, ...) 는 잘린 뒤 off > size 가 되어
 * 다음 호출의 size - off 가 size_t 로 거대한 값이 된다 → 버퍼 넘침. */
static int appendf(char *buf, size_t size, size_t *off, const char *fmt, ...) PRINTF_LIKE(4, 5);
static int appendf(char *buf, size_t size, size_t *off, const char *fmt, ...) {
    if (*off >= size) return -1;                   /* 이미 꽉 참 (잘린 상태) */
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf + *off, size - *off, fmt, ap);
    va_end(ap);
    if (n < 0) return -1;
    if ((size_t)n >= size - *off) { *off = size; return -1; }   /* 잘림: off 를 끝에 고정 */
    *off += (size_t)n;
    return 0;
}
/* "00000010: 48 65 6c  |Hel|" 형태의 hexdump 한 줄. 반환 0 = 다 들어감, -1 = 잘림 */
static int hexdump_line(char *out, size_t size, uint32_t addr, const uint8_t *p, size_t len) {
    size_t off = 0;
    int r = appendf(out, size, &off, "%08x:", (unsigned)addr);
    for (size_t i = 0; i < len; i++) r |= appendf(out, size, &off, " %02x", p[i]);
    r |= appendf(out, size, &off, "  |");
    for (size_t i = 0; i < len; i++) r |= appendf(out, size, &off, "%c", p[i] >= 0x20 && p[i] < 0x7f ? p[i] : '.');
    r |= appendf(out, size, &off, "|");
    return r;
}

/* 연습 4: 나눗셈 없는 10진 변환 — Cortex-M0 에는 하드웨어 나눗셈이 없다 (/ 가 __aeabi_uidiv 호출).
 * 10의 거듭제곱을 큰 것부터 빼며 각 자리를 센다 (자리당 최대 9번 뺄셈). */
static size_t utoa10_nodiv(unsigned v, char *buf) {
    static const unsigned pow10[] = { 1000000000u, 100000000u, 10000000u, 1000000u, 100000u,
                                      10000u, 1000u, 100u, 10u, 1u };
    size_t n = 0;
    for (size_t i = 0; i < sizeof pow10 / sizeof pow10[0]; i++) {
        char d = '0';
        while (v >= pow10[i]) { v -= pow10[i]; d++; }
        if (d != '0' || n > 0 || i == 9) buf[n++] = d;   /* 앞자리 0 은 건너뛴다 (마지막 자리는 항상) */
    }
    buf[n] = '\0';
    return n;
}

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main(void) {
    char b[64];

    /* 연습 1 */
    CHECK(fmt_centi(b, sizeof b, 1234) == 5 && strcmp(b, "12.34") == 0);
    CHECK(fmt_centi(b, sizeof b, -5) == 5 && strcmp(b, "-0.05") == 0);
    CHECK(fmt_centi(b, sizeof b, 7) == 4 && strcmp(b, "0.07") == 0);
    CHECK(fmt_centi(b, sizeof b, -100) == 5 && strcmp(b, "-1.00") == 0);
    CHECK(fmt_centi(b, sizeof b, INT_MIN) == 12 && strcmp(b, "-21474836.48") == 0);
    {
        int v = -5;
        snprintf(b, sizeof b, "%d.%02d", v / 100, abs(v % 100));   /* 순진한 버전 */
        CHECK(strcmp(b, "0.05") == 0);                             /* 부호가 사라졌다 */
    }

    /* 연습 2 */
    char *s = xasprintf("id=%d name=%s crc=%04X", 7, "imu", 0xBEEFu);
    CHECK(s != NULL && strcmp(s, "id=7 name=imu crc=BEEF") == 0);
    free(s);
    s = xasprintf("%s", "");
    CHECK(s != NULL && s[0] == '\0');
    free(s);

    /* 연습 3 */
    const uint8_t data[] = { 'H', 'e', 'l', 0x00, 0xff };
    CHECK(hexdump_line(b, sizeof b, 0x10, data, 5) == 0);
    CHECK(strcmp(b, "00000010: 48 65 6c 00 ff  |Hel..|") == 0);
    {
        char small[20];
        memset(small, 0x5A, sizeof small);
        CHECK(hexdump_line(small, 16, 0x10, data, 5) == -1);       /* 잘림 보고 */
        CHECK(strcmp(small, "00000010: 48 65") == 0);              /* 15자 + NUL, 넘침 없음 */
        CHECK(small[16] == 0x5A && small[19] == 0x5A);             /* size 밖은 그대로 */
    }

    /* 연습 4 */
    CHECK(utoa10_nodiv(0, b) == 1 && strcmp(b, "0") == 0);
    CHECK(utoa10_nodiv(UINT_MAX, b) == 10 && strcmp(b, "4294967295") == 0);
    CHECK(utoa10_nodiv(1000000000u, b) == 10 && strcmp(b, "1000000000") == 0);
    {
        uint32_t x = 12345u;                                       /* xorshift32 고정 시드 */
        int bad = 0;
        char ref[16];
        for (int i = 0; i < 5000; i++) {
            x ^= x << 13; x ^= x >> 17; x ^= x << 5;
            unsigned v = (i & 1) ? x : x % 1000u;
            snprintf(ref, sizeof ref, "%u", v);
            if (utoa10_nodiv(v, b) != strlen(ref) || strcmp(b, ref) != 0) bad++;
        }
        CHECK(bad == 0);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
