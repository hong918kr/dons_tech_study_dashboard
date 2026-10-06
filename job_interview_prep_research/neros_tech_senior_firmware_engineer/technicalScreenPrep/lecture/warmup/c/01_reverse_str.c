/* 01 · 문자열 제자리 뒤집기
 * prompt: void reverse_str(char *s) — 추가 메모리 없이 뒤집는다. NULL · 빈 문자열도 안전하게. 테스트 함수 + main 까지 직접.
 * note: 양 끝 인덱스 i, j 를 swap 하며 가운데로. n < 2 면 할 일 없음 (n - 1 이 size_t 언더플로 나는 것도 막는다)
 * note: 테스트 포인트 — 짝수 길이, 홀수 길이, 빈 문자열, NULL
 */
// --- solution ---
#include <assert.h>
#include <stdio.h>
#include <string.h>

void reverse_str(char *s)
{
    if (s == NULL)
        return;
    size_t n = strlen(s);
    if (n < 2)
        return;
    for (size_t i = 0, j = n - 1; i < j; i++, j--) {
        char t = s[i];
        s[i] = s[j];
        s[j] = t;
    }
}

static void test_reverse_str(void)
{
    char a[] = "neros", b[] = "";
    reverse_str(a);
    reverse_str(b);
    reverse_str(NULL);
    assert(strcmp(a, "soren") == 0 && strcmp(b, "") == 0);
}

int main(void)
{
    test_reverse_str();
    puts("PASS");
    return 0;
}
// --- end ---
