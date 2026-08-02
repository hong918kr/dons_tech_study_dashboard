#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>

/*
26. size_t my_strlen(const char *s) 구현
    Example: my_strlen("hello") -> 5
*/
size_t my_strlen(const char *s) {
    // TODO: implement
    return 0;
}

/*
27. char* my_strcpy(char *dest, const char *src) 구현
    Example: my_strcpy(dest, "abc") -> dest = "abc"
*/
char* my_strcpy(char *dest, const char *src) {
    // TODO: implement
    return dest;
}

/*
28. char* my_strncpy(char *dest, const char *src, size_t n) 구현
    Example: my_strncpy(dest, "abc", 2) -> dest = "ab"
*/
char* my_strncpy(char *dest, const char *src, size_t n) {
    // TODO: implement
    return dest;
}

/*
29. char* my_strcat(char *dest, const char *src) 구현
    Example: my_strcat(dest, "world") -> dest = "helloworld"
*/
char* my_strcat(char *dest, const char *src) {
    // TODO: implement
    return dest;
}

/*
30. int my_strcmp(const char *s1, const char *s2) 구현
    Example: my_strcmp("abc", "abd") -> negative value
*/
int my_strcmp(const char *s1, const char *s2) {
    // TODO: implement
    return 0;
}

/*
31. char* my_strstr(const char *haystack, const char *needle) 구현
    Example: my_strstr("hello world", "world") -> pointer to "world"
*/
char* my_strstr(const char *haystack, const char *needle) {
    // TODO: implement
    return NULL;
}

/*
32. char* my_strchr(const char *s, int c) 구현
    Example: my_strchr("abc", 'b') -> pointer to 'b'
*/
char* my_strchr(const char *s, int c) {
    // TODO: implement
    return NULL;
}

/*
33. int my_atoi(const char *str) 구현
    Example: my_atoi("123") -> 123
*/
int my_atoi(const char *str) {
    // TODO: implement
    return 0;
}

/*
34. char* my_itoa(int n, char *s, int base) 구현
    Example: my_itoa(123, buf, 10) -> buf = "123"
*/
char* my_itoa(int n, char *s, int base) {
    // TODO: implement
    return s;
}

/*
35. 문자열을 소문자/대문자로 변환하는 함수 구현
    Example: str_to_lower("ABC") -> "abc"
             str_to_upper("abc") -> "ABC"
*/
void str_to_lower(char *s) {
    // TODO: implement
}
void str_to_upper(char *s) {
    // TODO: implement
}

/*
36. 문자열의 단어를 역순으로 배치하는 함수 구현
    Example: reverse_words("hello world") -> "world hello"
*/
void reverse_words(char *s) {
    // TODO: implement
}

/*
37. 문자열에서 중복 문자 제거
    Example: remove_duplicates("banana") -> "ban"
*/
void remove_duplicates(char *s) {
    // TODO: implement
}

/*
38. 문자열이 회문(palindrome)인지 확인
    Example: is_palindrome("level") -> true
*/
bool is_palindrome(const char *s) {
    // TODO: implement
    return false;
}

/*
39. strtok의 동작 원리 이해 및 간소화된 버전 구현
    Example: my_strtok("a,b,c", ",") -> "a", "b", "c"
*/
char* my_strtok(char *str, const char *delim) {
    // TODO: implement
    return NULL;
}

/*
40. sprintf의 간소화된 버전 구현 (정수, 문자열 포맷)
    Example: my_sprintf(buf, "num=%d", 123) -> buf = "num=123"
*/
int my_sprintf(char *str, const char *format, ...) {
    // TODO: implement
    return 0;
}

// --- Test code ---
int main(void) {
    char buf[64] = {0};
    char buf2[64] = {0};

    printf("my_strlen(\"hello\") = %zu\n", my_strlen("hello")); // 5
    my_strcpy(buf, "abc");
    printf("my_strcpy(buf, \"abc\") = %s\n", buf); // "abc"
    my_strncpy(buf, "abcdef", 3);
    printf("my_strncpy(buf, \"abcdef\", 3) = %s\n", buf); // "abc"
    my_strcpy(buf, "hello");
    my_strcat(buf, "world");
    printf("my_strcat(buf, \"world\") = %s\n", buf); // "helloworld"
    printf("my_strcmp(\"abc\", \"abd\") = %d\n", my_strcmp("abc", "abd")); // negative
    printf("my_strstr(\"hello world\", \"world\") = %s\n", my_strstr("hello world", "world")); // "world"
    printf("my_strchr(\"abc\", 'b') = %s\n", my_strchr("abc", 'b')); // "bc"
    printf("my_atoi(\"123\") = %d\n", my_atoi("123")); // 123
    my_itoa(123, buf2, 10);
    printf("my_itoa(123, buf2, 10) = %s\n", buf2); // "123"
    my_strcpy(buf, "ABC");
    str_to_lower(buf);
    printf("str_to_lower(\"ABC\") = %s\n", buf); // "abc"
    my_strcpy(buf, "abc");
    str_to_upper(buf);
    printf("str_to_upper(\"abc\") = %s\n", buf); // "ABC"
    my_strcpy(buf, "hello world");
    reverse_words(buf);
    printf("reverse_words(\"hello world\") = %s\n", buf); // "world hello"
    my_strcpy(buf, "banana");
    remove_duplicates(buf);
    printf("remove_duplicates(\"banana\") = %s\n", buf); // "ban"
    printf("is_palindrome(\"level\") = %d\n", is_palindrome("level")); // 1
    char str[] = "a,b,c";
    char *token = my_strtok(str, ",");
    while (token) {
        printf("my_strtok: %s\n", token); // "a", "b", "c"
        token = my_strtok(NULL, ",");
    }
    my_sprintf(buf, "num=%d", 123);
    printf("my_sprintf(buf, \"num=%%d\", 123) = %s\n", buf); // "num=123"

    return 0;
}

/*
------------------ Example Input/Output ------------------

my_strlen("hello") = 5
my_strcpy(buf, "abc") = abc
my_strncpy(buf, "abcdef", 3) = abc
my_strcat(buf, "world") = helloworld
my_strcmp("abc", "abd") = negative value
my_strstr("hello world", "world") = world
my_strchr("abc", 'b') = bc
my_atoi("123") = 123
my_itoa(123, buf2, 10) = 123
str_to_lower("ABC") = abc
str_to_upper("abc") = ABC
reverse_words("hello world") = world hello
remove_duplicates("banana") = ban
is_palindrome("level") = 1
my_strtok: a
my_strtok: b
my_strtok: c
my_sprintf(buf, "num=%d", 123) = num=123

----------------------------------------------------------*/