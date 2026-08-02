// 03_strings.c — String Functions (문자열 함수) · Q26-40 · REFERENCE SOLUTION
// ---------------------------------------------------------------------------
// libc 문자열 함수 재구현 모음. Anduril 펌웨어 인터뷰에서 자주 나오는
// "libc 없이 직접 짜보라" 유형. 핵심은 buffer safety 와 null-termination.
//
// 빌드:  cc -std=c11 -Wall -Wextra solutions/03_strings.c -o /tmp/andb_strings
// 실행:  /tmp/andb_strings
// ---------------------------------------------------------------------------

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <limits.h>
#include <stdarg.h>
#include <string.h>   // 테스트 하네스의 검증(strcmp)에만 사용 — 구현체는 직접 작성

// ============================================================================
// 26. my_strlen — 널 종료 전까지의 길이
// ============================================================================
size_t my_strlen(const char *s) {
    const char *p = s;                 // 원본 포인터 보존, p 로 순회
    while (*p) p++;                     // '\0' 만나면 정지
    return (size_t)(p - s);            // 포인터 차 = 문자 개수
}

// ============================================================================
// 27. my_strcpy — src 를 dest 로 복사(널 포함). dest 버퍼 크기는 호출자 책임.
// ============================================================================
char *my_strcpy(char *dest, const char *src) {
    char *d = dest;
    while ((*d++ = *src++))            // '\0' 도 복사되고, 그 값(0)이 루프 종료
        ;
    return dest;                        // 관례상 dest 반환
}

// ============================================================================
// 28. my_strncpy — 최대 n 바이트 복사. libc 시맨틱 그대로:
//     - src 가 n 보다 짧으면 남는 자리를 '\0' 로 패딩
//     - src 가 n 이상이면 널 종료 안 됨(호출자가 직접 처리해야 함) ← 함정
// ============================================================================
char *my_strncpy(char *dest, const char *src, size_t n) {
    size_t i = 0;
    for (; i < n && src[i] != '\0'; i++) dest[i] = src[i];
    for (; i < n; i++) dest[i] = '\0';  // 나머지 널 패딩
    return dest;
}

// ============================================================================
// 29. my_strcat — dest 끝에 src 를 이어붙임. dest 는 충분히 커야 함.
// ============================================================================
char *my_strcat(char *dest, const char *src) {
    char *d = dest;
    while (*d) d++;                     // dest 의 '\0' 위치로 이동
    while ((*d++ = *src++))            // src 를 그 자리부터 복사(널 포함)
        ;
    return dest;
}

// ============================================================================
// 30. my_strcmp — 사전식 비교. unsigned char 로 비교해야 부호 이슈 없음.
//     반환: <0, 0, >0
// ============================================================================
int my_strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return (int)(unsigned char)*s1 - (int)(unsigned char)*s2;
}

// ============================================================================
// 31. my_strstr — haystack 안에서 needle 의 첫 등장 위치. 없으면 NULL.
//     빈 needle 은 haystack 반환(libc 시맨틱).
// ============================================================================
char *my_strstr(const char *haystack, const char *needle) {
    if (*needle == '\0') return (char *)haystack;
    for (; *haystack; haystack++) {
        const char *h = haystack, *n = needle;
        while (*h && *n && *h == *n) { h++; n++; }
        if (*n == '\0') return (char *)haystack;  // needle 끝까지 매칭됨
    }
    return NULL;
}

// ============================================================================
// 32. my_strchr — s 에서 문자 c 의 첫 등장. c 는 int 지만 char 로 취급.
//     c=='\0' 이면 종료 널의 주소를 반환(libc 시맨틱).
// ============================================================================
char *my_strchr(const char *s, int c) {
    char target = (char)c;
    while (*s) {
        if (*s == target) return (char *)s;
        s++;
    }
    return (target == '\0') ? (char *)s : NULL;  // 널 문자 탐색 지원
}

// ============================================================================
// 33. my_atoi — 앞 공백 skip, 부호, 십진 숫자 파싱. overflow 는 saturating
//     (INT_MAX/INT_MIN 로 clamp) — 임베디드에서 안전한 관례.
// ============================================================================
int my_atoi(const char *str) {
    if (!str) return 0;
    const char *p = str;
    while (*p == ' ' || *p == '\t' || *p == '\n' ||
           *p == '\r' || *p == '\v' || *p == '\f') p++;
    int sign = 1;
    if (*p == '+' || *p == '-') { if (*p == '-') sign = -1; p++; }

    long long acc = 0;                  // 반복마다 clamp 하므로 오버플로 안 남
    while (*p >= '0' && *p <= '9') {
        acc = acc * 10 + (*p - '0');
        if (sign == 1 && acc > INT_MAX) return INT_MAX;
        if (sign == -1 && -acc < INT_MIN) return INT_MIN;
        p++;
    }
    return (int)(sign * acc);
}

// ============================================================================
// 34. my_itoa — 정수를 base(2..16) 문자열로. base 10 에서만 음수 부호 처리.
//     INT_MIN 는 unsigned 로 승격해 abs 오버플로 회피.
// ============================================================================
char *my_itoa(int n, char *s, int base) {
    if (base < 2 || base > 16) { s[0] = '\0'; return s; }

    static const char digits[] = "0123456789abcdef";
    bool neg = false;
    unsigned int u;
    if (base == 10 && n < 0) {
        neg = true;
        u = (unsigned int)(-(long)n);   // -(long)INT_MIN = +2147483648, 안전
    } else {
        u = (unsigned int)n;            // 비-10진수는 비트패턴 그대로
    }

    char tmp[35];                        // 32bit 2진수(32) + 부호 + 널 여유
    int i = 0;
    if (u == 0) tmp[i++] = '0';
    while (u) { tmp[i++] = digits[u % (unsigned)base]; u /= (unsigned)base; }

    int j = 0;
    if (neg) s[j++] = '-';
    while (i > 0) s[j++] = tmp[--i];    // tmp 는 역순이라 뒤집어 복사
    s[j] = '\0';
    return s;
}

// ============================================================================
// 35. str_to_lower / str_to_upper — in-place 대소문자 변환(ASCII 기준)
// ============================================================================
void str_to_lower(char *s) {
    for (; *s; s++) if (*s >= 'A' && *s <= 'Z') *s = (char)(*s + ('a' - 'A'));
}
void str_to_upper(char *s) {
    for (; *s; s++) if (*s >= 'a' && *s <= 'z') *s = (char)(*s - ('a' - 'A'));
}

// ============================================================================
// 36. reverse_words — 단어 순서 뒤집기(in-place). "hello world" -> "world hello"
//     기법: (1) 전체 뒤집기 → (2) 각 단어를 다시 뒤집기. 공백 위치 보존.
// ============================================================================
static void reverse_range(char *s, size_t lo, size_t hi) {  // [lo, hi]
    while (lo < hi) {
        char t = s[lo]; s[lo] = s[hi]; s[hi] = t;
        lo++; hi--;
    }
}
void reverse_words(char *s) {
    size_t len = my_strlen(s);
    if (len == 0) return;
    reverse_range(s, 0, len - 1);       // 전체 뒤집기
    size_t start = 0;
    for (size_t i = 0; i <= len; i++) {
        if (i == len || s[i] == ' ') {
            if (i > start) reverse_range(s, start, i - 1);  // 단어 되뒤집기
            start = i + 1;
        }
    }
}

// ============================================================================
// 37. remove_duplicates — 이미 등장한 문자는 제거(첫 등장만 유지). in-place.
//     seen[256] 룩업으로 O(n). "banana" -> "ban"
// ============================================================================
void remove_duplicates(char *s) {
    bool seen[256] = { false };
    size_t w = 0;                        // write index (compaction)
    for (size_t r = 0; s[r]; r++) {
        unsigned char c = (unsigned char)s[r];
        if (!seen[c]) { seen[c] = true; s[w++] = s[r]; }
    }
    s[w] = '\0';
}

// ============================================================================
// 38. is_palindrome — 회문 검사(문자 그대로, 정규화 없음). 투 포인터.
// ============================================================================
bool is_palindrome(const char *s) {
    size_t len = my_strlen(s);
    if (len == 0) return true;           // 빈 문자열은 회문으로 간주
    size_t i = 0, j = len - 1;
    while (i < j) {
        if (s[i] != s[j]) return false;
        i++; j--;
    }
    return true;
}

// ============================================================================
// 39. my_strtok — 간소화 strtok. static 포인터로 상태 유지(재진입 불가!).
//     첫 호출: str 전달. 이후: NULL 전달해 이어서 토큰화. delim 은 문자 집합.
// ============================================================================
static bool in_delim(char c, const char *delim) {
    for (; *delim; delim++) if (c == *delim) return true;
    return false;
}
char *my_strtok(char *str, const char *delim) {
    static char *save = NULL;            // ← 상태 유지의 핵심(thread-unsafe)
    if (str) save = str;
    if (!save) return NULL;

    while (*save && in_delim(*save, delim)) save++;   // 앞쪽 구분자 skip
    if (*save == '\0') { save = NULL; return NULL; }  // 남은 토큰 없음

    char *token = save;
    while (*save && !in_delim(*save, delim)) save++;   // 토큰 끝까지 전진
    if (*save) { *save = '\0'; save++; }               // 구분자를 널로 잘라냄
    // (문자열 끝이면 save 는 '\0' 를 가리킨 채 유지 → 다음 호출서 NULL 반환)
    return token;
}

// ============================================================================
// 40. my_sprintf — 축소판 sprintf. 지원: %d %u %x %X %c %s %%
//     반환: 쓴 문자 수(널 제외). 버퍼 크기 검사는 생략(sprintf 시맨틱).
// ============================================================================
static int emit_uint(char *out, unsigned int v, unsigned base, bool upper) {
    char tmp[32];
    const char *digs = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    int i = 0;
    if (v == 0) tmp[i++] = '0';
    while (v) { tmp[i++] = digs[v % base]; v /= base; }
    int n = 0;
    while (i > 0) out[n++] = tmp[--i];
    return n;
}
int my_sprintf(char *str, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    char *out = str;                     // 현재 쓰기 위치

    for (const char *f = format; *f; f++) {
        if (*f != '%') { *out++ = *f; continue; }
        f++;                             // '%' 다음 지정자
        switch (*f) {
            case 'd': {
                int v = va_arg(ap, int);
                unsigned int mag;
                if (v < 0) { *out++ = '-'; mag = (unsigned int)(-(long)v); }
                else       { mag = (unsigned int)v; }
                out += emit_uint(out, mag, 10, false);
                break;
            }
            case 'u': out += emit_uint(out, va_arg(ap, unsigned int), 10, false); break;
            case 'x': out += emit_uint(out, va_arg(ap, unsigned int), 16, false); break;
            case 'X': out += emit_uint(out, va_arg(ap, unsigned int), 16, true);  break;
            case 'c': *out++ = (char)va_arg(ap, int); break;
            case 's': {
                const char *sv = va_arg(ap, const char *);
                if (!sv) sv = "(null)";
                while (*sv) *out++ = *sv++;
                break;
            }
            case '%': *out++ = '%'; break;
            case '\0': f--; break;       // 잘린 지정자: 루프에서 종료되도록
            default:  *out++ = '%'; *out++ = *f; break;  // 미지원 → 그대로 출력
        }
    }
    *out = '\0';
    va_end(ap);
    return (int)(out - str);
}

// ============================================================================
// ---- Test harness (PASS/FAIL) ----
// ============================================================================
static int g_pass = 0, g_fail = 0;

static void check_str(const char *call, const char *got, const char *want) {
    bool ok = (got && want && strcmp(got, want) == 0);
    printf("%-46s -> \"%s\"  %s", call, got ? got : "(null)",
           ok ? "[PASS]" : "[FAIL]");
    if (!ok) printf(" (expected \"%s\")", want);
    printf("\n");
    ok ? g_pass++ : g_fail++;
}
static void check_int(const char *call, long got, long want) {
    bool ok = (got == want);
    printf("%-46s -> %ld  %s", call, got, ok ? "[PASS]" : "[FAIL]");
    if (!ok) printf(" (expected %ld)", want);
    printf("\n");
    ok ? g_pass++ : g_fail++;
}
static void check_cond(const char *call, bool ok, const char *note) {
    printf("%-46s -> %s  %s\n", call, note, ok ? "[PASS]" : "[FAIL]");
    ok ? g_pass++ : g_fail++;
}

int main(void) {
    char buf[64], buf2[64];

    printf("== 26. my_strlen ==\n");
    check_int("my_strlen(\"hello\")", (long)my_strlen("hello"), 5);
    check_int("my_strlen(\"\")",      (long)my_strlen(""), 0);

    printf("\n== 27. my_strcpy ==\n");
    my_strcpy(buf, "abc");
    check_str("my_strcpy(buf,\"abc\")", buf, "abc");
    my_strcpy(buf, "");
    check_str("my_strcpy(buf,\"\")", buf, "");

    printf("\n== 28. my_strncpy ==\n");
    // src 가 n 보다 짧음 → 널 패딩(뒤 바이트가 0 이어야 함)
    for (int i = 0; i < 8; i++) buf[i] = 'X';
    my_strncpy(buf, "ab", 5);
    check_str("my_strncpy(buf,\"ab\",5)", buf, "ab");
    check_cond("  padded bytes are '\\0'", buf[2]==0 && buf[3]==0 && buf[4]==0, "buf[2..4]==0");
    // src 가 n 이상 → 널 종료 안 함(직접 종료시켜 확인)
    for (int i = 0; i < 8; i++) buf[i] = 'X';
    my_strncpy(buf, "abcdef", 3);
    buf[3] = '\0';
    check_str("my_strncpy(buf,\"abcdef\",3)+term", buf, "abc");

    printf("\n== 29. my_strcat ==\n");
    my_strcpy(buf, "hello");
    my_strcat(buf, "world");
    check_str("my_strcat(\"hello\",\"world\")", buf, "helloworld");
    my_strcpy(buf, "x");
    my_strcat(buf, "");
    check_str("my_strcat(\"x\",\"\")", buf, "x");

    printf("\n== 30. my_strcmp ==\n");
    check_cond("my_strcmp(\"abc\",\"abd\") < 0", my_strcmp("abc","abd") < 0, "negative");
    check_cond("my_strcmp(\"abd\",\"abc\") > 0", my_strcmp("abd","abc") > 0, "positive");
    check_int ("my_strcmp(\"abc\",\"abc\")", my_strcmp("abc","abc"), 0);
    check_cond("my_strcmp(\"abc\",\"ab\") > 0",  my_strcmp("abc","ab") > 0, "positive");

    printf("\n== 31. my_strstr ==\n");
    check_str ("my_strstr(\"hello world\",\"world\")", my_strstr("hello world","world"), "world");
    check_cond("my_strstr(\"abc\",\"xyz\") == NULL", my_strstr("abc","xyz")==NULL, "NULL");
    check_str ("my_strstr(\"abc\",\"\")", my_strstr("abc",""), "abc");
    check_str ("my_strstr(\"aaab\",\"aab\")", my_strstr("aaab","aab"), "aab");

    printf("\n== 32. my_strchr ==\n");
    check_str ("my_strchr(\"abc\",'b')", my_strchr("abc",'b'), "bc");
    check_cond("my_strchr(\"abc\",'z') == NULL", my_strchr("abc",'z')==NULL, "NULL");
    check_str ("my_strchr(\"abc\",'\\0')", my_strchr("abc",'\0'), "");  // 종료 널 주소

    printf("\n== 33. my_atoi ==\n");
    check_int("my_atoi(\"123\")",       my_atoi("123"), 123);
    check_int("my_atoi(\"  -42\")",     my_atoi("  -42"), -42);
    check_int("my_atoi(\"+7abc\")",     my_atoi("+7abc"), 7);
    check_int("my_atoi(\"\")",          my_atoi(""), 0);
    check_int("my_atoi(\"9999999999\")", my_atoi("9999999999"), INT_MAX);  // saturate

    printf("\n== 34. my_itoa ==\n");
    my_itoa(123, buf2, 10);   check_str("my_itoa(123,_,10)",  buf2, "123");
    my_itoa(-45, buf2, 10);   check_str("my_itoa(-45,_,10)",  buf2, "-45");
    my_itoa(0,   buf2, 10);   check_str("my_itoa(0,_,10)",    buf2, "0");
    my_itoa(255, buf2, 16);   check_str("my_itoa(255,_,16)",  buf2, "ff");
    my_itoa(6,   buf2, 2);    check_str("my_itoa(6,_,2)",     buf2, "110");
    my_itoa(INT_MIN, buf2, 10); check_str("my_itoa(INT_MIN,_,10)", buf2, "-2147483648");

    printf("\n== 35. str_to_lower / str_to_upper ==\n");
    my_strcpy(buf, "AbC1"); str_to_lower(buf); check_str("str_to_lower(\"AbC1\")", buf, "abc1");
    my_strcpy(buf, "AbC1"); str_to_upper(buf); check_str("str_to_upper(\"AbC1\")", buf, "ABC1");

    printf("\n== 36. reverse_words ==\n");
    my_strcpy(buf, "hello world"); reverse_words(buf);
    check_str("reverse_words(\"hello world\")", buf, "world hello");
    my_strcpy(buf, "a b c d"); reverse_words(buf);
    check_str("reverse_words(\"a b c d\")", buf, "d c b a");
    my_strcpy(buf, "solo"); reverse_words(buf);
    check_str("reverse_words(\"solo\")", buf, "solo");

    printf("\n== 37. remove_duplicates ==\n");
    my_strcpy(buf, "banana"); remove_duplicates(buf);
    check_str("remove_duplicates(\"banana\")", buf, "ban");
    my_strcpy(buf, "aaaa"); remove_duplicates(buf);
    check_str("remove_duplicates(\"aaaa\")", buf, "a");
    my_strcpy(buf, ""); remove_duplicates(buf);
    check_str("remove_duplicates(\"\")", buf, "");

    printf("\n== 38. is_palindrome ==\n");
    check_cond("is_palindrome(\"level\")", is_palindrome("level")==true, "true");
    check_cond("is_palindrome(\"abc\")",   is_palindrome("abc")==false, "false");
    check_cond("is_palindrome(\"\")",      is_palindrome("")==true, "true");
    check_cond("is_palindrome(\"aa\")",    is_palindrome("aa")==true, "true");

    printf("\n== 39. my_strtok ==\n");
    {
        char s[] = "a,b,c";
        const char *want[] = { "a", "b", "c" };
        int idx = 0; bool ok = true;
        for (char *t = my_strtok(s, ","); t; t = my_strtok(NULL, ",")) {
            if (idx >= 3 || strcmp(t, want[idx]) != 0) ok = false;
            idx++;
        }
        check_cond("my_strtok(\"a,b,c\",\",\") -> a,b,c", ok && idx == 3, "3 tokens");
    }
    {
        char s[] = ",,x,,yy,,";              // 앞/뒤/연속 구분자
        const char *want[] = { "x", "yy" };
        int idx = 0; bool ok = true;
        for (char *t = my_strtok(s, ","); t; t = my_strtok(NULL, ",")) {
            if (idx >= 2 || strcmp(t, want[idx]) != 0) ok = false;
            idx++;
        }
        check_cond("my_strtok(\",,x,,yy,,\",\",\") -> x,yy", ok && idx == 2, "2 tokens");
    }

    printf("\n== 40. my_sprintf ==\n");
    {
        int n = my_sprintf(buf, "num=%d", 123);
        check_str("my_sprintf(\"num=%d\",123)", buf, "num=123");
        check_int("  return value (len)", n, 7);
    }
    my_sprintf(buf, "%s=%d,%c,0x%X,%%", "hp", -5, '!', 4093);
    check_str("my_sprintf mixed specifiers", buf, "hp=-5,!,0xFFD,%");
    my_sprintf(buf, "u=%u", 4294967295u);
    check_str("my_sprintf(\"u=%u\",UINT_MAX)", buf, "u=4294967295");

    // ---- summary ----
    printf("\n==================================================\n");
    printf("RESULT: %d passed, %d failed\n", g_pass, g_fail);
    printf("==================================================\n");
    return g_fail ? 1 : 0;
}
