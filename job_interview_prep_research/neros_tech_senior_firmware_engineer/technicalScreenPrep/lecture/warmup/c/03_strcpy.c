/* 03 · strcpy 직접 구현 + 크기 제한 버전
 * prompt: my_strcpy(dst, src) → dst. safe_copy(dst, cap, src) → 항상 '\0' 로 끝내고, 잘렸으면 false.
 * note: strcpy 는 dst 크기를 모른다 → 버퍼 오버플로의 단골. 실무는 크기를 받는 버전
 * note: 테스트 포인트 — 딱 맞는 길이(cap - 1 글자), 한 글자 넘침, cap == 0
 */
// --- solution ---
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

char *my_strcpy(char *dst, const char *src)
{
    char *d = dst;
    while ((*d++ = *src++) != '\0')
        ;
    return dst;
}

bool safe_copy(char *dst, size_t cap, const char *src)
{
    if (cap == 0)
        return false;
    size_t i = 0;
    for (; i + 1 < cap && src[i] != '\0'; i++)
        dst[i] = src[i];
    dst[i] = '\0';
    return src[i] == '\0';                       /* 원본을 끝까지 복사했나 */
}

static void test_copy(void)
{
    char buf[4];
    assert(strcmp(my_strcpy(buf, "arm"), "arm") == 0);
    assert(safe_copy(buf, sizeof buf, "abc") && strcmp(buf, "abc") == 0);    /* 딱 맞음 */
    assert(!safe_copy(buf, sizeof buf, "abcd") && strcmp(buf, "abc") == 0);  /* 잘림 */
}

int main(void)
{
    test_copy();
    puts("PASS");
    return 0;
}
// --- end ---
