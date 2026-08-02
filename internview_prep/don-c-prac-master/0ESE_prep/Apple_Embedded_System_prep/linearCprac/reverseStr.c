#include <stdio.h>
#include <string.h>

// 문자열을 제자리에서 뒤집는 함수
void reverse_str(char* str) {
    // TODO: 구현
    
    char* s = str;
    char* e = str;
    
    while(*s++) e++;
    e--;
    s = str;
    while (s < e)
    {
        char temp = *s;
        *s = *e;
        *e = temp;
        s++; e--;
    }
    
}

// 테스트 코드
int main() {
    char s1[] = "hello";
    char s2[] = "abcdefg";
    char s3[] = "";
    char s4[] = "a";
    char s5[] = "racecar";

    printf("Before: %s\n", s1);
    reverse_str(s1);
    printf("After:  %s\n\n", s1);

    printf("Before: %s\n", s2);
    reverse_str(s2);
    printf("After:  %s\n\n", s2);

    printf("Before: \"%s\"\n", s3);
    reverse_str(s3);
    printf("After:  \"%s\"\n\n", s3);

    printf("Before: %s\n", s4);
    reverse_str(s4);
    printf("After:  %s\n\n", s4);

    printf("Before: %s\n", s5);
    reverse_str(s5);
    printf("After:  %s\n\n", s5);

    return 0;
}