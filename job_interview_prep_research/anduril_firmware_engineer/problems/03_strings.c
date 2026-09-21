// 03_strings.c — String Functions (문자열 함수) · Q26-40 · PRACTICE STUB
// ---------------------------------------------------------------------------
// libc 문자열 함수를 직접 재구현하는 드릴. 각 함수의 // TODO 를 채우고
// 재실행해 FAIL -> PASS 로 바꿔라. 핵심: buffer safety, null-termination.
//
// 빌드:  cc -std=c11 -Wall -Wextra problems/03_strings.c -o /tmp/andb_strings
// 실행:  /tmp/andb_strings           (또는  make prob N=03_strings)
// ---------------------------------------------------------------------------

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <limits.h>
#include <stdarg.h>
#include <string.h>   // 테스트 하네스의 검증(strcmp)에만 사용 — 구현체는 직접 작성

// ============================================================================
// 26. my_strlen
//   KO: 널 종료 문자열의 길이(널 제외)를 반환하라.
//   EN: Return the length of a NUL-terminated string, excluding the NUL.
//   ex) my_strlen("hello") -> 5   |   my_strlen("") -> 0
// ============================================================================
size_t my_strlen(const char *s) {
    (void)s;
    return 0;  // TODO: implement
}

// ============================================================================
// 27. my_strcpy
//   KO: src(널 포함)를 dest 로 복사하고 dest 를 반환하라. 버퍼 크기는 호출자 책임.
//   EN: Copy src (including NUL) into dest, return dest.
//   ex) my_strcpy(buf, "abc") -> buf == "abc"
// ============================================================================
char *my_strcpy(char *dest, const char *src) {
    (void)src;
    return dest;  // TODO: implement
}

// ============================================================================
// 28. my_strncpy
//   KO: 최대 n 바이트 복사. src 가 짧으면 나머지를 '\0' 로 패딩,
//       src 가 n 이상이면 널 종료하지 않는다(libc 시맨틱, 함정!).
//   EN: Copy at most n bytes; pad with '\0' if src is shorter; NO NUL if src>=n.
//   ex) my_strncpy(buf,"ab",5) -> "ab\0\0\0"   |   my_strncpy(buf,"abcdef",3) -> "abc"(종료X)
// ============================================================================
char *my_strncpy(char *dest, const char *src, size_t n) {
    (void)src; (void)n;
    return dest;  // TODO: implement
}

// ============================================================================
// 29. my_strcat
//   KO: dest 의 끝에 src 를 이어붙이고 dest 를 반환하라.
//   EN: Append src to the end of dest, return dest.
//   ex) my_strcat("hello","world") -> "helloworld"
// ============================================================================
char *my_strcat(char *dest, const char *src) {
    (void)src;
    return dest;  // TODO: implement
}

// ============================================================================
// 30. my_strcmp
//   KO: 사전식 비교. <0 / 0 / >0 반환. (부호 이슈 피하려면 unsigned char 로 비교)
//   EN: Lexicographic compare; return <0, 0, >0.
//   ex) my_strcmp("abc","abd") -> negative
// ============================================================================
int my_strcmp(const char *s1, const char *s2) {
    (void)s1; (void)s2;
    return 0;  // TODO: implement
}

// ============================================================================
// 31. my_strstr
//   KO: haystack 에서 needle 의 첫 등장 포인터. 없으면 NULL. 빈 needle -> haystack.
//   EN: First occurrence of needle in haystack, else NULL; empty needle -> haystack.
//   ex) my_strstr("hello world","world") -> "world"
// ============================================================================
char *my_strstr(const char *haystack, const char *needle) {
    (void)haystack; (void)needle;
    return NULL;  // TODO: implement
}

// ============================================================================
// 32. my_strchr
//   KO: s 에서 문자 c 의 첫 등장 포인터. c 는 char 로 취급. c=='\0' 이면 종료 널 주소.
//   EN: First occurrence of c in s; c=='\0' returns the terminating NUL's address.
//   ex) my_strchr("abc",'b') -> "bc"
// ============================================================================
char *my_strchr(const char *s, int c) {
    (void)s; (void)c;
    return NULL;  // TODO: implement
}

// ============================================================================
// 33. my_atoi
//   KO: 앞 공백 skip, 선택적 부호, 십진 숫자 파싱. 오버플로는 INT_MAX/INT_MIN 로 saturate.
//   EN: Skip leading spaces, optional sign, parse decimal; saturate on overflow.
//   ex) my_atoi("  -42") -> -42   |   my_atoi("9999999999") -> INT_MAX
// ============================================================================
int my_atoi(const char *str) {
    (void)str;
    return 0;  // TODO: implement
}

// ============================================================================
// 34. my_itoa
//   KO: 정수 n 을 base(2..16) 문자열로 s 에 쓰고 s 반환. base 10 에서만 음수 부호.
//   EN: Convert n to a base(2..16) string in s, return s. Minus sign only for base 10.
//   ex) my_itoa(255,buf,16) -> "ff"   |   my_itoa(-45,buf,10) -> "-45"
// ============================================================================
char *my_itoa(int n, char *s, int base) {
    (void)n; (void)base;
    s[0] = '\0';  // TODO: implement  (지금은 빈 문자열 반환 → FAIL)
    return s;
}

// ============================================================================
// 35. str_to_lower / str_to_upper
//   KO: ASCII 기준 in-place 대소문자 변환.
//   EN: In-place ASCII case conversion.
//   ex) str_to_lower("ABC") -> "abc"   |   str_to_upper("abc") -> "ABC"
// ============================================================================
void str_to_lower(char *s) {
    (void)s;  // TODO: implement
}
void str_to_upper(char *s) {
    (void)s;  // TODO: implement
}

// ============================================================================
// 36. reverse_words
//   KO: 단어 순서를 뒤집어라(in-place). 힌트: 전체 뒤집기 후 각 단어 되뒤집기.
//   EN: Reverse the order of words in place.
//   ex) reverse_words("hello world") -> "world hello"
// ============================================================================
void reverse_words(char *s) {
    (void)s;  // TODO: implement
}

// ============================================================================
// 37. remove_duplicates
//   KO: 이미 등장한 문자를 제거(첫 등장만 유지)하라(in-place).
//   EN: Remove characters already seen, keeping the first occurrence (in place).
//   ex) remove_duplicates("banana") -> "ban"
// ============================================================================
void remove_duplicates(char *s) {
    (void)s;  // TODO: implement
}

// ============================================================================
// 38. is_palindrome
//   KO: 문자열이 회문인지 검사(문자 그대로). 투 포인터.
//   EN: Check whether the string is a palindrome (as-is).
//   ex) is_palindrome("level") -> true
// ============================================================================
bool is_palindrome(const char *s) {
    (void)s;
    return false;  // TODO: implement
}

// ============================================================================
// 39. my_strtok
//   KO: strtok 재구현. 첫 호출은 str, 이후는 NULL 로 이어서 토큰화. static 상태 유지!
//   EN: Reimplement strtok using static state; first call passes str, then NULL.
//   ex) "a,b,c" with "," -> "a","b","c"
// ============================================================================
char *my_strtok(char *str, const char *delim) {
    (void)str; (void)delim;
    return NULL;  // TODO: implement  (static char* 로 상태 유지)
}

// ============================================================================
// 40. my_sprintf
//   KO: 축소판 sprintf. 최소 %d %u %x %X %c %s %% 지원. 쓴 문자 수(널 제외) 반환.
//   EN: Tiny sprintf supporting %d %u %x %X %c %s %%; return count written.
//   ex) my_sprintf(buf,"num=%d",123) -> buf=="num=123", 반환 7
// ============================================================================
int my_sprintf(char *str, const char *format, ...) {
    (void)format;
    str[0] = '\0';  // TODO: implement  (va_list 사용)
    return 0;
}

// ============================================================================
// ---- Test harness (PASS/FAIL) ----  ※ 수정 불필요: 구현만 채우면 PASS 로 바뀜
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
