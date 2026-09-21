#include <stdio.h>
#include <string.h>
#include <assert.h>

// 1) my_strcpy
char* my_strcpy(char* dst, const char* src) {
    // while ((*dst++ = *src++)) { }


    while (*dst++ = *src++);

    return dst;
}

// 2) my_strcmp
int my_strcmp(const char* a, const char* b) {
    // while (*a && *a == *b) { a++; b++; }
    // return (unsigned char)*a - (unsigned char)*b;

    
    //while(*a && *a == *b) {a++;b++;};
    while(*a && *a++ == *b++) {};
    return (unsigned char)*a - (unsigned char)*b;
}

// 3) my_strncmp
int my_strncmp(const char* a, const char* b, size_t n) {
    // for (; n && *a && (*a == *b); a++, b++, n--) {}
    // if (n == 0) return 0;
    // return (unsigned char)*a - (unsigned char)*b;
    return 0;
}

// 4) my_strlen
size_t my_strlen(const char* s) {
    // const char* p = s;
    // while (*p) p++;
    // return (size_t)(p - s);
    return 0;
}

// 5) my_strcpy_s
int my_strcpy_s(char* dst, size_t dstsz, const char* src) {
    // size_t i = 0;
    // if (!dst || !src || dstsz == 0) return -1;
    // for (; i + 1 < dstsz && src[i]; i++) dst[i] = src[i];
    // dst[i] = '\0';
    // return src[i] ? -1 : 0;
    return 0;
}

// 6) my_memcpy
void* my_memcpy(void* dst, const void* src, size_t n) {
    // uint8_t* d = dst; const uint8_t* s = src;
    // while (n--) *d++ = *s++;
    // return dst;
    return NULL;
}

// 7) my_memmove
void* my_memmove(void* dst, const void* src, size_t n) {
    // if (dst == src || n == 0) return dst;
    // if (dst < src) { // forward
    //   uint8_t* d = dst; const uint8_t* s = src; while (n--) *d++ = *s++;
    // } else { // backward
    //   uint8_t* d = (uint8_t*)dst + n; const uint8_t* s = (const uint8_t*)src + n;
    //   while (n--) *--d = *--s;
    // }
    // return dst;
    return NULL;
}

// 8) my_memcmp
int my_memcmp(const void* a, const void* b, size_t n) {
    // const uint8_t* x = a; const uint8_t* y = b;
    // for (; n; x++, y++, n--) { if (*x != *y) return *x - *y; }
    // return 0;
    return 0;
}

// 9) my_strchr
char* my_strchr(const char* s, int c) {
    // for (; *s; s++) if ((unsigned char)*s == (unsigned char)c) return (char*)s;
    // return (c == '\0') ? (char*)s : NULL;
    return NULL;
}

// 9) my_strrchr
char* my_strrchr(const char* s, int c) {
    // const char* last = NULL;
    // for (; *s; s++) if ((unsigned char)*s == (unsigned char)c) last = s;
    // return (c == '\0') ? (char*)s : (char*)last;
    return NULL;
}

// 10) my_strtok_r
char* my_strtok_r(char* str, const char* delim, char** saveptr) {
    // char* s = str ? str : *saveptr;
    // if (!s) return NULL;
    // s += strspn(s, delim);
    // if (*s == '\0') { *saveptr = NULL; return NULL; }
    // char* end = s + strcspn(s, delim);
    // if (*end) { *end = '\0'; *saveptr = end + 1; } else { *saveptr = NULL; }
    // return s;
    return NULL;
}

// --- 풍부한 테스트 코드 ---
#define TEST_PASS(name) printf("[PASS] %s\n", name)
#define TEST_FAIL(name) printf("[FAIL] %s\n", name)

int main() {
    int all_pass = 1;

    // my_strcpy
    char buf1[16] = {0};
    if (my_strcpy(buf1, "abc") != NULL && strcmp(buf1, "abc") == 0) {
        TEST_PASS("my_strcpy");
    } else {
        TEST_FAIL("my_strcpy");
        all_pass = 0;
    }

    // my_strcmp
    if (my_strcmp("abc", "abc") == 0 &&
        my_strcmp("abc", "abd") < 0 &&
        my_strcmp("abd", "abc") > 0) {
        TEST_PASS("my_strcmp");
    } else {
        TEST_FAIL("my_strcmp");
        all_pass = 0;
    }

    // my_strncmp
    if (my_strncmp("abc", "abc", 2) == 0 &&
        my_strncmp("abc", "abd", 2) == 0 &&
        my_strncmp("abc", "abd", 3) < 0) {
        TEST_PASS("my_strncmp");
    } else {
        TEST_FAIL("my_strncmp");
        all_pass = 0;
    }

    // my_strlen
    if (my_strlen("abcd") == 4) {
        TEST_PASS("my_strlen");
    } else {
        TEST_FAIL("my_strlen");
        all_pass = 0;
    }

    // my_strcpy_s
    char buf2[4] = {0};
    if (my_strcpy_s(buf2, sizeof(buf2), "abc") == 0 &&
        my_strcpy_s(buf2, sizeof(buf2), "abcdef") == -1) {
        TEST_PASS("my_strcpy_s");
    } else {
        TEST_FAIL("my_strcpy_s");
        all_pass = 0;
    }

    // my_memcpy
    char src[4] = "xyz";
    char dst[4] = {0};
    if (my_memcpy(dst, src, 4) == dst && memcmp(dst, src, 4) == 0) {
        TEST_PASS("my_memcpy");
    } else {
        TEST_FAIL("my_memcpy");
        all_pass = 0;
    }

    // my_memmove
    char overlap[8] = "abcdefg";
    if (my_memmove(overlap+2, overlap, 5) == overlap+2 /* && 실제 결과 비교 필요 */) {
        TEST_PASS("my_memmove");
    } else {
        TEST_FAIL("my_memmove");
        all_pass = 0;
    }

    // my_memcmp
    if (my_memcmp("abc", "abc", 3) == 0 &&
        my_memcmp("abc", "abd", 3) < 0) {
        TEST_PASS("my_memcmp");
    } else {
        TEST_FAIL("my_memcmp");
        all_pass = 0;
    }

    // my_strchr
    if (my_strchr("hello", 'e') != NULL &&
        my_strchr("hello", 'z') == NULL) {
        TEST_PASS("my_strchr");
    } else {
        TEST_FAIL("my_strchr");
        all_pass = 0;
    }

    // my_strrchr
    if (my_strrchr("banana", 'a') != NULL &&
        my_strrchr("banana", 'z') == NULL) {
        TEST_PASS("my_strrchr");
    } else {
        TEST_FAIL("my_strrchr");
        all_pass = 0;
    }

    // my_strtok_r
    char str[] = "a,b,c";
    char* saveptr;
    char* tok1 = my_strtok_r(str, ",", &saveptr);
    if (tok1 && strcmp(tok1, "a") == 0) {
        TEST_PASS("my_strtok_r");
    } else {
        TEST_FAIL("my_strtok_r");
        all_pass = 0;
    }

    if (all_pass) {
        printf("All pointer/string function tests passed!\n");
    } else {
        printf("Some pointer/string function tests failed!\n");
    }
    return 0;
}