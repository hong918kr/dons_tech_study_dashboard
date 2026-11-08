/* C-B07 추가 연습 — 한 번에 검증 (libc 와 대조)
 *   cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
 *   cc -std=c11 -g -O1 -fsanitize=address,undefined practice.c -o practice_asan && ./practice_asan
 */
#include <stddef.h>
#include <stdint.h>   /* uintptr_t */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>   /* strnlen strncmp strncat strlcat strchr (strnlen/strlcat: POSIX/BSD) */

/* 연습 1: strnlen — 최대 maxlen 바이트까지만 본다 (고정 폭 필드용) */
static size_t my_strnlen(const char *s, size_t maxlen) {
    size_t n = 0;
    while (n < maxlen && s[n] != '\0') n++;
    return n;
}

/* 연습 2: strncmp — 최대 n 자, unsigned char 로 비교 (AT 명령 접두사 매칭 등) */
static int my_strncmp(const char *a, const char *b, size_t n) {
    const unsigned char *p = (const unsigned char *)a, *q = (const unsigned char *)b;
    for (; n != 0; n--, p++, q++) {
        if (*p != *q) return (int)*p - (int)*q;
        if (*p == '\0') return 0;
    }
    return 0;
}

/* 연습 3: strncat — src 에서 최대 n 자를 붙이고 "항상" NUL 을 붙인다.
 * 함정: n 은 dst 의 남은 공간이 아니다. 필요한 공간 = strlen(dst) + min(n, strlen(src)) + 1 */
static char *my_strncat(char *dst, const char *src, size_t n) {
    char *d = dst + strlen(dst);
    while (n != 0 && *src != '\0') { *d++ = *src++; n--; }
    *d = '\0';
    return dst;
}

/* 연습 4: strlcat — size 는 dst 버퍼 "전체" 크기. 반환 = 만들려던 문자열 길이.
 * dst 앞 size 바이트 안에 NUL 이 없으면 아무것도 안 쓰고 size + strlen(src) 반환. */
static size_t my_strlcat(char *dst, const char *src, size_t size) {
    size_t dlen = my_strnlen(dst, size);
    size_t slen = strlen(src);
    if (dlen == size) return size + slen;
    size_t room = size - dlen - 1;
    size_t k = (slen < room) ? slen : room;
    memcpy(dst + dlen, src, k);
    dst[dlen + k] = '\0';
    return dlen + slen;
}

/* 연습 5: strchr — c 를 char 로 바꿔 찾는다. c == '\0' 이면 종료 NUL 의 주소. */
static char *my_strchr(const char *s, int c) {
    const char ch = (char)c;
    for (;; s++) {
        if (*s == ch) return (char *)(uintptr_t)s;   /* 표준 시그니처의 const 제거 */
        if (*s == '\0') return NULL;
    }
}

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
static int sgn(int v) { return (v > 0) - (v < 0); }

int main(void) {
    /* 연습 1: NUL 없는 4바이트 배열도 안전 (strlen 이었다면 넘어 읽음) */
    const char field[4] = { 'A', 'B', 'C', 'D' };
    CHECK(my_strnlen(field, sizeof field) == 4);
    CHECK(my_strnlen("ab", 8) == strnlen("ab", 8) && my_strnlen("abc", 0) == 0);

    /* 연습 2 */
    static const struct { const char *a, *b; size_t n; } nc[] = {
        { "AT+NAME=x", "AT+NAME=", 8 }, { "AT+RST", "AT+NAME=", 8 }, { "ab", "abc", 3 },
        { "ab", "abc", 2 }, { "\x80", "a", 1 }, { "x", "y", 0 },
    };
    for (size_t i = 0; i < sizeof nc / sizeof nc[0]; i++)
        CHECK(sgn(my_strncmp(nc[i].a, nc[i].b, nc[i].n)) == sgn(strncmp(nc[i].a, nc[i].b, nc[i].n)));

    /* 연습 3: 딱 필요한 크기 = 3("dev") + 2 + 1 = 6 바이트 힙 버퍼 */
    {
        char *a = malloc(6), *b = malloc(6);
        if (!a || !b) return 2;
        memcpy(a, "dev", 4); memcpy(b, "dev", 4);
        my_strncat(a, "-01xyz", 2);
        strncat(b, "-01xyz", 2);
        CHECK(memcmp(a, b, 6) == 0 && strcmp(a, "dev-0") == 0);
        free(a); free(b);
    }

    /* 연습 4 */
    static const struct { const char *d, *s; size_t size; } lc[] = {
        { "dev", "-01", 8 }, { "dev", "-0123", 8 }, { "1234567", "x", 8 }, { "", "abc", 4 },
    };
    for (size_t i = 0; i < sizeof lc / sizeof lc[0]; i++) {
        char *a = malloc(lc[i].size), *b = malloc(lc[i].size);
        if (!a || !b) return 2;
        memset(a, 0xAA, lc[i].size); memset(b, 0xAA, lc[i].size);
        strcpy(a, lc[i].d); strcpy(b, lc[i].d);
        size_t ra = my_strlcat(a, lc[i].s, lc[i].size);
        size_t rb = strlcat(b, lc[i].s, lc[i].size);
        CHECK(ra == rb && memcmp(a, b, lc[i].size) == 0);
        free(a); free(b);
    }
    {
        char full[4] = { 'w', 'x', 'y', 'z' };          /* NUL 없음 → 건드리지 않음 */
        CHECK(my_strlcat(full, "abc", sizeof full) == 4 + 3 && full[3] == 'z');
    }

    /* 연습 5 */
    const char *cmd = "NAME=sensor";
    CHECK(my_strchr(cmd, '=') == strchr(cmd, '=') && my_strchr(cmd, '=') == cmd + 4);
    CHECK(my_strchr(cmd, 'Q') == NULL);
    CHECK(my_strchr(cmd, '\0') == cmd + 11);                  /* 종료 NUL 도 찾는다 */
    CHECK(my_strchr("a\xe9", 0xE9) == strchr("a\xe9", 0xE9));  /* int → char 변환 */

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
